#!/usr/bin/env python3
"""Link-closure gate for the CNA module probes (plans/MODULARIZATION_PLAN.md §4).

Reads the generated link line of a probe executable (the Makefiles generator's
CMakeFiles/<target>.dir/link.txt, or under Ninja the target's final command as
`ninja -t commands -s` prints it) and fails if any token matches a forbidden
pattern. This turns each module's *real* dependency closure into a permanent,
mechanically checked contract instead of an assumption.

Usage:
  check_module_link_closure.py --build-dir <dir> --target <name> \
      --forbid <python-regex> [--require <python-regex> ...]

Exit codes: 0 ok, 1 violation, 77 (ctest SKIP) when neither generator's link
line can be read (another generator, or a multi-config build).
"""
import argparse
import os
import re
import subprocess
import sys


def cache_value(build_dir, name):
    """One entry of the build tree's CMakeCache.txt, or None."""
    try:
        with open(os.path.join(build_dir, "CMakeCache.txt"), encoding="utf-8") as cache:
            for line in cache:
                key, _, value = line.rstrip("\n").partition("=")
                if key.split(":", 1)[0] == name:
                    return value
    except OSError:
        pass
    return None


def ninja_link_line(build_dir, target):
    """plans/plan_apple_m4.md AM4-167: the Ninja generator writes no link.txt, so every probe
    skipped in a Ninja tree. Ninja itself prints a target's final command."""
    if cache_value(build_dir, "CMAKE_GENERATOR") != "Ninja":
        return None
    ninja = cache_value(build_dir, "CMAKE_MAKE_PROGRAM")
    if not ninja:
        return None
    completed = subprocess.run([ninja, "-C", build_dir, "-t", "commands", "-s", target],
                               capture_output=True, text=True, check=False)
    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if completed.returncode != 0 or not lines:
        return None
    return lines[-1]


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
    if os.path.exists(link_txt):
        tokens = open(link_txt, encoding="utf-8").read().split()
    else:
        line = ninja_link_line(a.build_dir, a.target)
        if line is None:
            print(f"SKIP: no link line for {a.target}: {link_txt} not found and not a "
                  "single-config Ninja tree")
            return 77
        tokens = line.split()
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
