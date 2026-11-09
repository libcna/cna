#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Run a C API inventory check as an advisory check.

The generated C API inventories (docs/c-api/COVERAGE.md, LIMITATIONS.md and RELEASE_GATE.md) go
stale on any public-header edit, a comment included. Since 2026-11-09 the owner has them warn
instead of block. CTest reports a failed check as skipped (exit 77, the tests' SKIP_RETURN_CODE)
and the check's own output says what changed.

Usage: run_advisory.py <check script> [arguments...]
"""

import subprocess
import sys

SKIP = 77

REGENERATE = (
    "python3 tools/c-api/generate_coverage_inventory.py --write\n"
    "python3 tools/c-api/generate_limitations.py --write\n"
    "python3 tools/c-api/check_release_gate.py --write"
)


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: run_advisory.py <check script> [arguments...]", file=sys.stderr)
        return 2
    result = subprocess.run([sys.executable, *argv[1:]], check=False)
    if result.returncode in (0, SKIP):
        return result.returncode
    print(
        f"ADVISORY: {' '.join(argv[1:])} exited {result.returncode}. The C API inventories are "
        "advisory, so this is reported as a skip, not a failure. To bring them up to date, run "
        f"from the repository root:\n{REGENERATE}",
        file=sys.stderr,
    )
    return SKIP


if __name__ == "__main__":
    sys.exit(main(sys.argv))
