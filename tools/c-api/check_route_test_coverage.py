#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Measure how much of the C ABI any test actually calls, and ratchet it.

The coverage matrix answers a different question from this one, and the difference is where a
claim can hide. That matrix maps every public **C++** symbol to a C route and to a *rule's* test
description -- so when a rule covers twenty symbols and its test exercises twelve, all twenty are
credited. `CBIND-052A` found one such overclaim by accident (a renderer identity recorded as
implemented, with test evidence, whose C constant did not exist at all).

This asks the mechanical question instead: **which exported `cna_*` functions does no test source
mention?** It cannot prove a test is meaningful, only that one exists at all -- but "no test names
this route" is a fact, and it was 78 routes when this gate was written.

The budget is a ratchet, not a target. It may fall and must never rise, which is what stops the
next family of routes from arriving untested behind a matrix row that says otherwise.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
HEADER_DIR = REPO_ROOT / "modules" / "c-api" / "include" / "CNA" / "C"
TEST_DIRS = (
    REPO_ROOT / "modules" / "c-api" / "tests",
    REPO_ROOT / "modules" / "c-api" / "examples",
)
BUDGET_PATH = Path(__file__).resolve().parent / "route_test_coverage_budget.json"
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}


def declared_routes() -> dict[str, str]:
    """Every `CNA_C_API` function the public headers declare, mapped to its header."""
    routes: dict[str, str] = {}
    for path in sorted(HEADER_DIR.glob("*.h")):
        text = re.sub(r"/\*.*?\*/", "", path.read_text(encoding="utf-8"), flags=re.S)
        for match in re.finditer(
            r"CNA_C_API\s+[A-Za-z_][A-Za-z0-9_ *]*?\s+(cna_[A-Za-z0-9_]+)\s*\(", text
        ):
            routes.setdefault(match.group(1), path.name)
    if not routes:
        raise SystemExit(f"No CNA_C_API declarations found under {HEADER_DIR}.")
    return routes


def routes_named_by_tests() -> set[str]:
    names: set[str] = set()
    for directory in TEST_DIRS:
        for path in directory.rglob("*"):
            if path.suffix in SOURCE_SUFFIXES:
                names.update(
                    re.findall(r"\b(cna_[A-Za-z0-9_]+)\b", path.read_text(errors="ignore")))
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="fail when the uncovered set grew")
    mode.add_argument("--update", action="store_true", help="record the current uncovered set")
    mode.add_argument("--list", action="store_true", help="print every uncovered route")
    args = parser.parse_args()

    routes = declared_routes()
    named = routes_named_by_tests()
    uncovered = sorted(set(routes) - named)

    if args.list:
        for route in uncovered:
            print(f"{routes[route]}\t{route}")
        return 0

    if args.update:
        BUDGET_PATH.write_text(
            json.dumps({"uncovered_route_budget": len(uncovered),
                        "uncovered_routes": uncovered}, indent=2) + "\n",
            encoding="utf-8")
        print(f"recorded budget: {len(uncovered)} uncovered of {len(routes)} declared routes")
        return 0

    if not BUDGET_PATH.exists():
        print(f"{BUDGET_PATH.name} is missing; record it with --update.")
        return 1
    budget = json.loads(BUDGET_PATH.read_text(encoding="utf-8"))
    allowed = budget["uncovered_route_budget"]
    previous = set(budget.get("uncovered_routes", ()))

    covered = len(routes) - len(uncovered)
    if len(uncovered) > allowed:
        arrived = sorted(set(uncovered) - previous)
        print(
            f"C ABI route test coverage REGRESSED: {len(uncovered)} routes are named by no test, "
            f"budget is {allowed}.")
        if arrived:
            print("\nRoutes that no test names, and did not before:")
            for route in arrived:
                print(f"  {route}   ({routes[route]})")
        print(
            "\nAdd a test that calls them, or -- if that is genuinely impossible -- raise the "
            "budget deliberately with --update and say why in plans/plan_binding.md. A matrix row "
            "saying 'implemented' is not evidence that anything calls the route.")
        return 1

    if len(uncovered) < allowed:
        print(
            f"C ABI route test coverage IMPROVED: {len(uncovered)} uncovered, budget still "
            f"{allowed}. Lower it with --update so it cannot drift back.")
        return 1

    print(
        f"{covered}/{len(routes)} declared routes are named by a test "
        f"({100 * covered / len(routes):.1f}%); {len(uncovered)} uncovered, at budget.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
