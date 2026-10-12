#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Symbol-level link-footprint gate (docs/link-size-architecture.md).

The module probes (check_module_link_closure.py) check which *archives* a consumer's link line
names. That cannot see the failure this gate exists for: a consumer links the right archives, but
one reference inside one of them extracts a member that drags a whole unrelated subsystem along
(Guide.IsTrialMode once linked the 30 MB avatar catalog through the gamer-services archive the
game legitimately needed). This reads the finished executable's symbol table instead.

Usage:
  check_link_footprint.py --binary <exe> [--nm <tool>] [--forbid REGEX ...] [--require REGEX ...]

A forbidden pattern fails when any symbol -- defined, or imported from a shared library -- matches
it, demangled. A required pattern fails when none does; requirements keep the consumer honest, so
that a probe which stopped using an API cannot pass vacuously.

Absolute byte limits are deliberately not checked: toolchains and build types differ by more than
the regressions this guards against. The sizes are printed for the log.

--nm names the symbol lister of the toolchain that built the binary (CMake passes CMAKE_NM), so a
cross-compiled executable is read by a tool that understands it; a binary it cannot read is a
failure. Without --nm the host's nm or llvm-nm is used, and finding neither is a skip.

Exit codes: 0 ok, 1 violation or unreadable binary, 77 (ctest SKIP) when no --nm was given and
the host has no nm-compatible tool.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys


_NM_LINE = re.compile(r"^\s*(?:[0-9a-fA-F]+\s+)?[A-Za-z?-]\s+(.+)$")


def _names(text):
    """Symbol names from nm output: everything after the type letter (demangled names hold spaces)."""
    return [match.group(1) for match in map(_NM_LINE.match, text.splitlines()) if match]


def symbols(binary, tools):
    """Demangled symbol names of the binary, or None when none of the tools can list them."""
    for path in tools:
        listed = subprocess.run([path, "-C", binary], capture_output=True, text=True, check=False)
        if listed.returncode == 0 and listed.stdout:
            return _names(listed.stdout)
        # Apple's nm has no -C: demangle separately.
        raw = subprocess.run([path, binary], capture_output=True, text=True, check=False)
        demangler = shutil.which("c++filt") or shutil.which("llvm-cxxfilt")
        if raw.returncode == 0 and raw.stdout and demangler:
            names = _names(raw.stdout)
            demangled = subprocess.run([demangler], input="\n".join(names), capture_output=True,
                                       text=True, check=False)
            if demangled.returncode == 0:
                return demangled.stdout.splitlines()
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--nm", help="the toolchain's nm (CMAKE_NM)")
    parser.add_argument("--forbid", action="append", default=[], metavar="REGEX")
    parser.add_argument("--require", action="append", default=[], metavar="REGEX")
    args = parser.parse_args()

    if not os.path.isfile(args.binary):
        print(f"link footprint: {args.binary} does not exist", file=sys.stderr)
        return 1
    if args.nm:
        tools = [args.nm]
    else:
        tools = [path for path in map(shutil.which, ("nm", "llvm-nm")) if path]
        if not tools:
            print("link footprint: no --nm given and the host has no nm or llvm-nm; skipping")
            return 77
    names = symbols(args.binary, tools)
    if names is None:
        print(f"link footprint: {', '.join(tools)} cannot read {args.binary}", file=sys.stderr)
        return 1

    print(f"link footprint: {args.binary}: {os.path.getsize(args.binary):,} bytes, "
          f"{len(names):,} symbols")
    # The size tool beside the nm used: llvm-nm -> llvm-size, x86_64-w64-mingw32-nm -> ...-size.
    nm_dir, nm_name = os.path.split(tools[0])
    size_tool = (shutil.which(nm_name[:-2] + "size", path=nm_dir or None)
                 if nm_name.endswith("nm") else None)
    if size_tool:
        sized = subprocess.run([size_tool, args.binary], capture_output=True, text=True, check=False)
        if sized.returncode == 0:
            print(sized.stdout.rstrip())

    failed = False
    for pattern in args.forbid:
        regex = re.compile(pattern)
        hits = [name for name in names if regex.search(name)]
        if hits:
            failed = True
            print(f"FORBIDDEN '{pattern}': {len(hits)} symbol(s), e.g.")
            for name in hits[:5]:
                print(f"    {name}")
    for pattern in args.require:
        regex = re.compile(pattern)
        if not any(regex.search(name) for name in names):
            failed = True
            print(f"MISSING '{pattern}': no symbol matches")
    if failed:
        print("link footprint: FAILED -- ask the linker that produced it why a member was linked: "
              "mold -Wl,--print-dependencies, lld -Wl,--why-extract=FILE, GNU ld -Wl,-Map=FILE "
              "with tools/link_audit/why_linked.py (docs/link-size-architecture.md, 'Checking a "
              "binary'); another linker may not reproduce the extraction")
        return 1
    print(f"link footprint: ok ({len(args.forbid)} forbidden families absent, "
          f"{len(args.require)} required present)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
