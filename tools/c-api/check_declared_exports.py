#!/usr/bin/env python3
"""Hold the C API's *declared* routes against the ones the library actually exports.

The ABI baseline already compares the export list with a recorded baseline, so export drift is
caught. It cannot catch a different failure: a header that declares a route the library does not
export. Both halves stay self-consistent -- the baseline matches itself, the export count matches
the prose -- while a consumer written correctly against the published header fails at the call
site, not at load, with the dynamic loader unable to find the entry point.

A count cannot see this. 2,855 declared and 2,855 exported are equally satisfied by a header
declaring one route the library lacks while the library exports one the header omits. The check
that catches it is a set difference in both directions, which is what this tool runs.

Reported by the C# binding, which caught the shape by running exactly this comparison on its own
side every tick.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

HEADER_ROOT = Path(__file__).resolve().parents[2] / "modules" / "c-api" / "include" / "CNA" / "C"

# A declaration is `CNA_C_API <return type> cna_name(`, with the return type on the same line or
# wrapped onto the next. Matching the macro rather than the name is what keeps a route mentioned in
# a doc comment from being read as a declaration.
DECLARATION = re.compile(r"CNA_C_API\s+[A-Za-z_][\w\s*]*?\b(cna_\w+)\s*\(", re.MULTILINE)


def declared_routes(header_root: Path) -> set[str]:
    if not header_root.is_dir():
        raise SystemExit(f"No C API headers at {header_root}")
    names: set[str] = set()
    for header in sorted(header_root.glob("*.h")):
        names.update(DECLARATION.findall(header.read_text(encoding="utf-8")))
    if not names:
        raise SystemExit(f"No declarations found under {header_root}; the pattern is stale.")
    return names


def exported_routes(library: Path) -> set[str]:
    if shutil.which("nm") is None:
        raise SystemExit("nm is required to read the library's dynamic exports.")
    completed = subprocess.run(
        ["nm", "-D", "--defined-only", str(library)],
        capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise SystemExit(f"Reading dynamic exports failed:\n{completed.stderr.strip()}")
    names: set[str] = set()
    for line in completed.stdout.splitlines():
        fields = line.split()
        if not fields:
            continue
        symbol = re.sub(r"@@?.*", "", fields[-1])
        if symbol.startswith("cna_"):
            names.add(symbol)
    if not names:
        raise SystemExit(f"{library} exports no cna_* symbols; it is not the C API library.")
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--library", required=True, type=Path,
                        help="the built libcna_c_api shared object")
    parser.add_argument("--headers", type=Path, default=HEADER_ROOT,
                        help="directory of public C API headers")
    args = parser.parse_args()

    declared = declared_routes(args.headers)
    exported = exported_routes(args.library)

    missing = sorted(declared - exported)
    extra = sorted(exported - declared)

    for name in missing:
        print(f"  DECLARED, NOT EXPORTED  {name}")
    for name in extra:
        print(f"  EXPORTED, NOT DECLARED  {name}")

    if missing or extra:
        print()
        if missing:
            print("A declared route the library does not export fails at the consumer's call site "
                  "rather than at load: define it, or remove the declaration.")
        if extra:
            print("An exported route no header declares is unreachable API surface: declare it, or "
                  "stop exporting it.")
        return 1

    print(f"declared and exported agree exactly: {len(declared)} routes.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
