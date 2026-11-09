#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Run a check as an advisory check: a failure is reported, not fatal.

Since 2026-11-09 the owner has the checks that guard only metadata, generated reports or the
AI-era tooling warn instead of block (the CMake files mark each one where it is registered).
The wrapped check runs unchanged. A pass, or the check's own skip (exit 77), goes through.
Any other failure becomes exit 77, which those tests' SKIP_RETURN_CODE reports as a skip,
with a message naming the check. The check's own output above that message says what is wrong.

Usage: run_advisory.py [--hint TEXT] <script> [arguments...]
       (interpreter options such as -B may precede <script>)
"""

import subprocess
import sys

SKIP = 77


def main(argv: list[str]) -> int:
    args = argv[1:]
    hint = ""
    if len(args) >= 2 and args[0] == "--hint":
        hint, args = args[1], args[2:]
    if not args:
        print("usage: run_advisory.py [--hint TEXT] <script> [arguments...]", file=sys.stderr)
        return 2
    result = subprocess.run([sys.executable, *args], check=False)
    if result.returncode in (0, SKIP):
        return result.returncode
    message = (f"ADVISORY: {' '.join(args)} exited {result.returncode}. This check is advisory, "
               "so it is reported as a skip, not a failure.")
    if hint:
        message += f" {hint}"
    print(message, file=sys.stderr)
    return SKIP


if __name__ == "__main__":
    sys.exit(main(sys.argv))
