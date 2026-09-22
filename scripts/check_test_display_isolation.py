#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_gpu_test_isolation.md GTI-0006: no registered test may reach the live desktop.

Reads a configured build tree's real test registrations (``ctest --show-only=json-v1``) and its
CMakeCache.txt, and fails when any test would open a window on the owner's desktop without the
explicit opt-in:

* a test ENVIRONMENT forcing DISPLAY to display 0 while CNA_TEST_ALLOW_LIVE_DISPLAY is OFF;
* a test ENVIRONMENT naming the live Wayland socket (``wayland-0``);
* on a non-Windows tree, a test without the Wayland guard (``WAYLAND_DISPLAY=string_append:``),
  so that run with WAYLAND_DISPLAY unset it would fall back to ``$XDG_RUNTIME_DIR/wayland-0``;
* with CNA_TEST_DISPLAY empty, a leftover empty ``DISPLAY=`` entry, which gives a test no display
  instead of the caller's.

It checks what ctest would actually run, not the CMake that produced it, so it catches a
registration that bypasses cmake/TestDisplayPolicy.cmake as well as a regression inside it.

    check_test_display_isolation.py --ctest <ctest> --build-dir <dir> [--wayland-guard-applies ON|OFF]
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

GUARD = "WAYLAND_DISPLAY=string_append:"
LIVE_X = re.compile(r"^(unix|localhost|127\.0\.0\.1)?:0(\.[0-9]+)?$|^/tmp/\.X11-unix/X0(\.[0-9]+)?$")
LIVE_WAYLAND = re.compile(r"^(.*/)?wayland-0$")


def read_cache(build_dir: Path) -> dict:
    cache = {}
    for line in (build_dir / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"^([A-Za-z0-9_]+):[A-Z]+=(.*)$", line)
        if match:
            cache[match.group(1)] = match.group(2)
    return cache


def truthy(value: str) -> bool:
    return value.upper() in ("1", "ON", "YES", "TRUE", "Y")


def properties(test: dict) -> dict:
    return {p["name"]: p["value"] for p in test.get("properties", [])}


def as_list(value) -> list:
    if value is None:
        return []
    return value if isinstance(value, list) else [value]


def violations(tests: list, cache: dict, guard_applies: bool) -> list:
    allow_live = truthy(cache.get("CNA_TEST_ALLOW_LIVE_DISPLAY", "OFF"))
    display_empty = cache.get("CNA_TEST_DISPLAY", "") == ""
    problems = []
    for test in tests:
        name = test.get("name", "?")
        # gtest_discover_tests' stand-in for a test executable that is not built yet: it runs
        # nothing and only reports that. The cases discovered once the binary exists carry the
        # guard through gtest_discover_tests(PROPERTIES ...).
        if name.endswith("_NOT_BUILT"):
            continue
        props = properties(test)
        environment = as_list(props.get("ENVIRONMENT"))
        modification = as_list(props.get("ENVIRONMENT_MODIFICATION"))
        for entry in environment:
            key, _, value = entry.partition("=")
            if key == "DISPLAY" and LIVE_X.match(value) and not allow_live:
                problems.append(f"{name}: forces DISPLAY={value} (the live desktop) without "
                                "CNA_TEST_ALLOW_LIVE_DISPLAY=ON")
            if key == "DISPLAY" and value == "" and display_empty:
                problems.append(f"{name}: carries an empty DISPLAY= entry; it should inherit the caller's")
            if key == "WAYLAND_DISPLAY" and LIVE_WAYLAND.match(value):
                problems.append(f"{name}: forces WAYLAND_DISPLAY={value} (the live compositor)")
        if guard_applies and GUARD not in modification:
            problems.append(f"{name}: has no {GUARD} guard; run with WAYLAND_DISPLAY unset it "
                            "would connect to $XDG_RUNTIME_DIR/wayland-0")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ctest", required=True)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--wayland-guard-applies", default="ON",
                        help="OFF where the tree has no X/Wayland (Windows); the guard is not required there")
    args = parser.parse_args()

    cache = read_cache(args.build_dir)
    listing = subprocess.run([args.ctest, "--test-dir", str(args.build_dir), "--show-only=json-v1"],
                             check=True, capture_output=True, text=True)
    tests = json.loads(listing.stdout).get("tests", [])
    if not tests:
        print("check_test_display_isolation: the tree registers no tests; nothing was checked",
              file=sys.stderr)
        return 1

    problems = violations(tests, cache, truthy(args.wayland_guard_applies))
    if problems:
        for problem in problems[:50]:
            print(f"LIVE-DESKTOP RISK  {problem}")
        if len(problems) > 50:
            print(f"... and {len(problems) - 50} more")
        print(f"check_test_display_isolation: {len(problems)} problem(s) in {len(tests)} tests")
        return 1
    print(f"check_test_display_isolation: {len(tests)} tests checked; none can reach the live desktop "
          f"(CNA_TEST_DISPLAY='{cache.get('CNA_TEST_DISPLAY', '')}', "
          f"live opt-in {cache.get('CNA_TEST_ALLOW_LIVE_DISPLAY', 'OFF')})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
