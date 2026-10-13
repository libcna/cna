#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Model a static link's archive extraction from symbol tables, with every reason, not only the first.

A GNU ld map (why_linked.py) records one reference per extracted archive member: the first one the
linker happened to resolve. Cutting that reference often just exposes the next one. This tool
re-derives the extraction set from `nm` over the same inputs the link used and keeps *every*
reference between extracted objects, so a cut can be planned against all of them at once.

    link_closure.py --objects a.o b.o --archives libx.a liby.a --who-needs AvatarCatalog
    link_closure.py --objects ... --archives ... --list cna_audio

The model treats the archives as one group (what CMake's repeated archives on a cyclic graph amount
to) and resolves strong and weak definitions alike, as GNU ld, lld and mold all extract a member for
an undefined reference that only a weak definition satisfies.
"""

import argparse
import collections
import re
import subprocess
import sys


def nm_table(paths):
    """Return {object: (defined set, undefined set)} for objects and archive members."""
    table = collections.defaultdict(lambda: (set(), set()))
    for path in paths:
        output = subprocess.run(["nm", "-A", "-P", "--no-sort", path], capture_output=True,
                                text=True, check=False).stdout
        for line in output.splitlines():
            # "lib.a[member.o]: sym T addr size" (POSIX format with -A)
            head, _, rest = line.partition(": ")
            fields = rest.split()
            if len(fields) < 2:
                continue
            symbol, kind = fields[0], fields[1]
            obj = head
            if kind == "U":
                table[obj][1].add(symbol)
            elif kind in ("T", "D", "B", "R", "V", "W", "G", "S", "C", "u", "i"):
                table[obj][0].add(symbol)
    return table


def closure(objects, archives):
    roots = nm_table(objects)
    members = nm_table(archives)
    definer = {}
    for obj, (defined, _) in members.items():
        for symbol in defined:
            definer.setdefault(symbol, obj)
    included = set(roots)
    defined_now = set()
    for obj in roots:
        defined_now |= roots[obj][0]
    edges = collections.defaultdict(set)  # target member -> {(referrer, symbol)}
    pending = list(roots)
    tables = dict(roots)
    tables.update(members)
    while pending:
        obj = pending.pop()
        for symbol in tables[obj][1]:
            target = definer.get(symbol)
            if target is None or target == obj:
                continue
            if symbol in roots.get(obj, (set(), set()))[0]:
                continue
            edges[target].add((obj, symbol))
            if target not in included:
                included.add(target)
                pending.append(target)
    return included, edges


def demangle(names):
    if not names:
        return []
    output = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True, text=True,
                            check=False).stdout
    return output.splitlines()


def short(name):
    match = re.match(r"^(.*/)?([^/\[]+)\[(.+)\]$", name)
    if match:
        return f"{match.group(2)}({match.group(3)})"
    return name.rsplit("/", 1)[-1]


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--objects", nargs="+", required=True, help="objects named on the link line")
    parser.add_argument("--archives", nargs="+", required=True, help="static archives on the link line")
    parser.add_argument("--who-needs", action="append", default=[], metavar="REGEX",
                        help="every extracted object referencing a member matching REGEX, and the symbols")
    parser.add_argument("--list", action="append", default=[], metavar="REGEX",
                        help="extracted members whose name matches REGEX")
    parser.add_argument("--refs-of", action="append", default=[], metavar="REGEX",
                        help="every extracted member that a member matching REGEX references")
    args = parser.parse_args(argv)

    included, edges = closure(args.objects, args.archives)
    for pattern in args.list:
        regex = re.compile(pattern)
        for obj in sorted(o for o in included if regex.search(o)):
            print(short(obj))
    for pattern in args.refs_of:
        regex = re.compile(pattern)
        outgoing = collections.defaultdict(list)
        for target, references in edges.items():
            for referrer, symbol in references:
                if regex.search(referrer):
                    outgoing[(referrer, target)].append(symbol)
        for (referrer, target) in sorted(outgoing):
            symbols = sorted(outgoing[(referrer, target)])
            shown = demangle(symbols[:3])
            more = f" (+{len(symbols) - 3} more)" if len(symbols) > 3 else ""
            print(f"{short(referrer)} -> {short(target)}: {'; '.join(shown)}{more}")
    for pattern in args.who_needs:
        regex = re.compile(pattern)
        for target in sorted(t for t in included if regex.search(t) and t in edges):
            print(f"== {short(target)}")
            by_referrer = collections.defaultdict(list)
            for referrer, symbol in edges[target]:
                by_referrer[referrer].append(symbol)
            for referrer in sorted(by_referrer):
                symbols = sorted(by_referrer[referrer])
                shown = demangle(symbols[:4])
                more = f" (+{len(symbols) - 4} more)" if len(symbols) > 4 else ""
                print(f"   <- {short(referrer)}: {'; '.join(shown)}{more}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
