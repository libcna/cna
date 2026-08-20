#!/usr/bin/env python3
"""Link-closure gate for the CNA module probes (plans/MODULARIZATION_PLAN.md §4).

Reads the generated link line of a probe executable (the Makefiles generator's
CMakeFiles/<target>.dir/link.txt) and fails if any token matches a forbidden
pattern. This turns each module's *real* dependency closure into a permanent,
mechanically checked contract instead of an assumption.

Usage:
  check_module_link_closure.py --build-dir <dir> --target <name> \
      --forbid <python-regex> [--require <python-regex> ...]

Exit codes: 0 ok, 1 violation, 77 (ctest SKIP) when the generator produced no
link.txt (non-Makefiles generators).
"""
import argparse
import os
import re
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--target", required=True)
    ap.add_argument("--forbid", required=True,
                    help="regex; any matching link-line token fails the gate")
    ap.add_argument("--require", action="append", default=[],
                    help="regex; at least one link-line token must match each")
    a = ap.parse_args()

    link_txt = os.path.join(a.build_dir, "CMakeFiles", a.target + ".dir", "link.txt")
    if not os.path.exists(link_txt):
        print(f"SKIP: {link_txt} not found (non-Makefiles generator?)")
        return 77

    tokens = open(link_txt, encoding="utf-8").read().split()
    forbid = re.compile(a.forbid)
    bad = sorted({t for t in tokens if forbid.search(t)})
    if bad:
        print(f"FORBIDDEN link inputs for {a.target} (pattern: {a.forbid}):")
        for t in bad:
            print(f"  {t}")
        return 1

    for req in a.require:
        rex = re.compile(req)
        if not any(rex.search(t) for t in tokens):
            print(f"MISSING required link input for {a.target}: no token matches {req}")
            return 1

    print(f"OK: {a.target} link closure clean ({len(tokens)} tokens)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
