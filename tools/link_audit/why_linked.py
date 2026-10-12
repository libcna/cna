#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Explain a static link from a GNU ld map file (-Wl,-Map=<file>, BFD linker).

GNU ld records, for every archive member it extracted, the object and the symbol whose undefined
reference made it do so. That is the whole dependency story of a static link: an archive member is
in the executable because a chain of such references leads to it from an object named on the
command line. This tool reads that record and the memory map that follows it.

    why_linked.py MAP --top 40              # archive members by runtime (non-debug) bytes
    why_linked.py MAP --why EmbeddedAvatar  # reference chain from the program to each match
    why_linked.py MAP --modules             # runtime bytes per archive

mold and lld do not write the "Archive member included" section; relink with -fuse-ld=bfd to get
it (docs/link-size-architecture.md, "Inspecting a link").
"""

import argparse
import collections
import re
import sys

_MEMBER_RE = re.compile(r"^(\S+\.a)\((\S+)\)$")
_REF_RE = re.compile(r"^\s+(\S+)\s+\((.*)\)$")
_SECTION_RE = re.compile(r"^ (\.\S+)?\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S+)$")
_CONT_RE = re.compile(r"^\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S+)$")


def parse(path):
    """Return (reasons, sizes): member -> (referrer, symbol); object -> {section: bytes}."""
    reasons = {}
    sizes = collections.defaultdict(lambda: collections.Counter())
    with open(path, encoding="utf-8", errors="replace") as handle:
        lines = iter(handle)
        for line in lines:
            if line.startswith("Archive member included"):
                break
        pending = None
        for line in lines:
            if line.startswith(("Discarded input sections", "Memory Configuration",
                                "Allocating common symbols")):
                break
            line = line.rstrip("\n")
            if not line.strip():
                continue
            if not line.startswith(" "):
                # A member name may be followed on the same line by its reason when short.
                parts = line.split(None, 1)
                pending = parts[0]
                if len(parts) == 2:
                    match = re.match(r"^(\S+)\s+\((.*)\)$", parts[1])
                    if match:
                        reasons[pending] = (match.group(1), match.group(2))
                        pending = None
                continue
            match = _REF_RE.match(line)
            if match and pending:
                reasons[pending] = (match.group(1), match.group(2))
                pending = None
        for line in lines:
            if line.startswith("Linker script and memory map"):
                break
        section = None
        for line in lines:
            line = line.rstrip("\n")
            if line.startswith(" .") and len(line.split()) == 1:
                section = line.split()[0]
                continue
            match = _SECTION_RE.match(line)
            if match:
                name = match.group(1) or section
                if match.group(1):
                    section = match.group(1)
                sizes[match.group(3)][_section_class(name)] += int(match.group(2), 16)
                continue
            match = _CONT_RE.match(line)
            if match and section:
                sizes[match.group(2)][_section_class(section)] += int(match.group(1), 16)
            elif line and not line.startswith(" "):
                section = None
    return reasons, sizes


def _section_class(name):
    if not name:
        return "other"
    if name.startswith(".debug"):
        return "debug"
    for key in (".text", ".rodata", ".data.rel.ro", ".data", ".bss", ".eh_frame",
                ".gcc_except_table", ".tbss", ".tdata"):
        if name == key or name.startswith(key + "."):
            return key
    return "other"


def runtime_bytes(counter):
    return sum(value for key, value in counter.items() if key not in ("debug", ".bss", ".tbss"))


def chain(member, reasons, limit=64):
    steps = []
    seen = set()
    current = member
    while current in reasons and current not in seen and len(steps) < limit:
        seen.add(current)
        referrer, symbol = reasons[current]
        steps.append((current, referrer, symbol))
        current = referrer
    return steps


def short(name):
    match = _MEMBER_RE.match(name)
    if match:
        return f"{match.group(1).rsplit('/', 1)[-1]}({match.group(2)})"
    return name.rsplit("/", 1)[-1] if "/" in name else name


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("map", help="GNU ld map file")
    parser.add_argument("--why", action="append", default=[], metavar="REGEX",
                        help="print the reference chain for each archive member matching REGEX")
    parser.add_argument("--top", type=int, metavar="N",
                        help="list the N largest linked objects by runtime bytes")
    parser.add_argument("--modules", action="store_true", help="runtime bytes per archive")
    args = parser.parse_args(argv)

    reasons, sizes = parse(args.map)

    if args.top:
        ranked = sorted(sizes.items(), key=lambda item: runtime_bytes(item[1]), reverse=True)
        for name, counter in ranked[:args.top]:
            print(f"{runtime_bytes(counter):>12,}  text={counter['.text']:>10,}  "
                  f"rodata={counter['.rodata']:>11,}  {short(name)}")
    if args.modules:
        per_archive = collections.Counter()
        for name, counter in sizes.items():
            match = _MEMBER_RE.match(name)
            per_archive[match.group(1) if match else "(objects and shared libraries)"] += \
                runtime_bytes(counter)
        for name, total in per_archive.most_common():
            print(f"{total:>12,}  {name}")
    for pattern in args.why:
        regex = re.compile(pattern)
        matches = [member for member in reasons if regex.search(member)]
        if not matches:
            print(f"{pattern}: no linked archive member matches")
        for member in matches:
            print(f"== {short(member)}  ({runtime_bytes(sizes.get(member, {})):,} runtime bytes)")
            for current, referrer, symbol in chain(member, reasons):
                print(f"   {short(current)}\n      <- {short(referrer)}  [{symbol}]")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
