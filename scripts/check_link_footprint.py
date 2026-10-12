#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Symbol-level link-footprint gate (docs/link-size-architecture.md).

The module probes (check_module_link_closure.py) check which *archives* a consumer's link line
names. That cannot see the failure this gate exists for: a consumer links the right archives, but
one reference inside one of them extracts a member that drags a whole unrelated subsystem along
(Guide.IsTrialMode once linked the 30 MB avatar catalog through the gamer-services archive the
game legitimately needed). This reads the finished executable's symbol table instead.

Usage:
  check_link_footprint.py --binary <exe> [--forbid REGEX ...] [--require REGEX ...]

A forbidden pattern fails when any symbol -- defined, or imported from a shared library -- matches
it, demangled. A required pattern fails when none does; requirements keep the consumer honest, so
that a probe which stopped using an API cannot pass vacuously.

Absolute byte limits are deliberately not checked: toolchains and build types differ by more than
the regressions this guards against. The sizes are printed for the log.

Exit codes: 0 ok, 1 violation, 77 (ctest SKIP) when no nm-compatible tool can read the binary.
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


def symbols(binary):
    """Demangled symbol names of the binary, or None when no tool can list them."""
    for tool in ("nm", "llvm-nm"):
        path = shutil.which(tool)
        if not path:
            continue
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
    parser.add_argument("--forbid", action="append", default=[], metavar="REGEX")
    parser.add_argument("--require", action="append", default=[], metavar="REGEX")
    args = parser.parse_args()

    if not os.path.isfile(args.binary):
        print(f"link footprint: {args.binary} does not exist", file=sys.stderr)
        return 1
    names = symbols(args.binary)
    if names is None:
        print("link footprint: no nm-compatible tool can read the binary; skipping")
        return 77

    print(f"link footprint: {args.binary}: {os.path.getsize(args.binary):,} bytes, "
          f"{len(names):,} symbols")
    size_tool = shutil.which("size")
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
        print("link footprint: FAILED -- tools/link_audit/why_linked.py explains which reference "
              "pulled a forbidden object in (relink with -fuse-ld=bfd -Wl,-Map=...)")
        return 1
    print(f"link footprint: ok ({len(args.forbid)} forbidden families absent, "
          f"{len(args.require)} required present)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
