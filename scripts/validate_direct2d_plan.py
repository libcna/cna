#!/usr/bin/env python3
"""plan_direct2d.md D2D-132: keep the Direct2D plan's statuses and its evidence consistent.

A plan row is only worth reading if its status can be trusted. This checker fails when

  * a row uses a status outside the documented set, or a task id repeats,
  * a `✅` row cites no evidence at all (no test, script, document, or source file),
  * a row cites a repository path, a CTest name, or a `CNA_DIRECT2D_*` variable that does not
    exist,
  * a row cross-references a `D2D-<n>` task that is not in the table,
  * an incomplete row is missing from the plan's own closing classification table, or
  * the maintained status marker no longer matches the real row counts.

Usage: validate_direct2d_plan.py [<repository-root>]
"""

from __future__ import annotations

import pathlib
import re
import sys


STATUS_MARKER_RE = re.compile(
    r"<!--\s*direct2d-plan-status:\s*total=(?P<total>\d+)\s+done=(?P<done>\d+)\s+"
    r"partial=(?P<partial>\d+)\s+open=(?P<open>\d+)\s*-->"
)
ROW_RE = re.compile(r"^\|\s*(D2D-\d+)\s*\|(?P<title>[^|]*)\|\s*(?P<status>[^|\s]+)\s*\|(?P<evidence>.*)\|\s*$")
PATH_RE = re.compile(
    r"`((?:scripts|docs|modules|cmake|tests|integration|examples|\.github)/[A-Za-z0-9_./*-]+)`"
)
CTEST_RE = re.compile(r"`?(Direct2D_[A-Za-z0-9]+)`?")
ENV_RE = re.compile(r"`?(CNA_DIRECT2D_[A-Z0-9_]+)`?")
TASK_REF_RE = re.compile(r"\bD2D-(\d+)\b")

COMPLETE = "✅"
PARTIAL = "🟨"
OPEN = "⬜"
SPIKE = "🔬"
VALID_STATUSES = {COMPLETE, PARTIAL, OPEN, SPIKE}

# Evidence a completed row must point at. Prose alone ("this is fine now") is not evidence, but a
# row may delegate to another task's evidence, and this plan is written in Czech, so the verbs that
# actually assert a run are listed alongside the English nouns.
EVIDENCE_WORDS = (
    "test", "Test", "oracle", "unit", "Unit", "gate", "fixture", "CTest", "workflow",
    "benchmark", "probe", "assert",
    "ověř", "Ověř", "prošel", "prošla", "prošly", "otestov", "testován", "odmít", "capability",
)
# A cross-reference stands in for evidence only when the referenced row carries its own.
DELEGATION_RE = re.compile(r"\bD2D-\d+\b")
# "D2D-106 až D2D-112" in the closing classification table covers every task in between.
RANGE_RE = re.compile(r"D2D-(\d+)\s*(?:až|-|–|to)\s*D2D-(\d+)")


def load(root: pathlib.Path, relative: str) -> str:
    return (root / relative).read_text(encoding="utf-8")


def main(argv: list[str]) -> int:
    root = pathlib.Path(argv[1] if len(argv) > 1 else pathlib.Path(__file__).resolve().parents[1])
    root = root.resolve()
    try:
        plan = load(root, "plan_direct2d.md")
    except OSError as error:
        print(f"Direct2D plan checker failed to read the plan: {error}", file=sys.stderr)
        return 1

    # Every place a Direct2D CTest name or environment variable can legitimately be defined.
    registration_sources = ""
    for candidate in (
        "modules/renderers/direct2d/examples/CMakeLists.txt",
        "cmake/UnitTests.cmake",
        "modules/renderers/direct2d/src/Direct2DRenderer.cpp",
        "modules/renderers/direct2d/examples/direct2d_2d_parity_test.cpp",
        "modules/renderers/direct2d/examples/direct2d_lifetime_test.cpp",
        "modules/renderers/direct2d/examples/direct2d_smoke_test.cpp",
        "scripts/run-wine-direct2d.sh",
        "scripts/run-proton-direct2d.sh",
        "scripts/run-direct2d-virtual-display.sh",
        "scripts/run-direct2d-fresh-wine-suite.sh",
        ".github/workflows/d3d-windows-ci.yml",
    ):
        try:
            registration_sources += load(root, candidate)
        except OSError:
            print(f"Direct2D plan checker: missing expected input {candidate}", file=sys.stderr)
            return 1

    errors: list[str] = []
    statuses: dict[str, str] = {}
    order: list[str] = []

    for lineno, line in enumerate(plan.splitlines(), start=1):
        match = ROW_RE.match(line)
        if match is None:
            continue
        task = match.group(1)
        status = match.group("status")
        evidence = match.group("evidence")
        if task in statuses:
            errors.append(f"line {lineno}: duplicate row for {task}")
            continue
        if status not in VALID_STATUSES:
            errors.append(f"line {lineno}: {task} has unknown status '{status}'")
            continue
        statuses[task] = status
        order.append(task)

        cites_evidence = (
            any(word in evidence for word in EVIDENCE_WORDS)
            or bool(PATH_RE.search(evidence))
            or bool(CTEST_RE.search(evidence))
            or bool(DELEGATION_RE.search(evidence))
        )
        if status == COMPLETE and not cites_evidence:
            errors.append(
                f"line {lineno}: {task} is marked complete but its acceptance cites no test, "
                "gate, document, source, or delegated task evidence"
            )

        for path in PATH_RE.findall(evidence):
            if "*" in path:
                if not list(root.glob(path)):
                    errors.append(f"line {lineno}: {task} cites '{path}', which matches nothing")
            elif not (root / path).exists():
                errors.append(f"line {lineno}: {task} cites '{path}', which does not exist")

        for ctest in set(CTEST_RE.findall(evidence)):
            if ctest not in registration_sources:
                errors.append(
                    f"line {lineno}: {task} cites CTest '{ctest}', which no registration defines"
                )

        for variable in set(ENV_RE.findall(evidence)):
            if variable not in registration_sources:
                errors.append(
                    f"line {lineno}: {task} cites '{variable}', which nothing reads or sets"
                )

    if not statuses:
        errors.append("no D2D rows were parsed at all -- the table format changed")

    for number in sorted({int(n) for n in TASK_REF_RE.findall(plan)}):
        if f"D2D-{number}" not in statuses:
            errors.append(f"plan references D2D-{number}, which has no row in the table")

    # The closing classification table must account for exactly the incomplete rows.
    classification = plan.split("## Klasifikace zbývajících položek")[-1]
    classified = {f"D2D-{n}" for n in TASK_REF_RE.findall(classification)}
    for first, last in RANGE_RE.findall(classification):
        for number in range(int(first), int(last) + 1):
            classified.add(f"D2D-{number}")
    incomplete = {task for task, status in statuses.items() if status in {PARTIAL, OPEN, SPIKE}}
    for task in sorted(incomplete - classified, key=lambda item: int(item.split("-")[1])):
        errors.append(f"{task} is incomplete but the closing classification table does not list it")

    marker = STATUS_MARKER_RE.search(plan)
    done = sum(1 for status in statuses.values() if status == COMPLETE)
    partial = sum(1 for status in statuses.values() if status == PARTIAL)
    still_open = sum(1 for status in statuses.values() if status in {OPEN, SPIKE})
    if marker is None:
        errors.append(
            "plan_direct2d.md has no '<!-- direct2d-plan-status: total=N done=N partial=N "
            "open=N -->' marker, so nothing pins its own summary against the rows"
        )
    else:
        expected = (len(statuses), done, partial, still_open)
        actual = (
            int(marker.group("total")), int(marker.group("done")),
            int(marker.group("partial")), int(marker.group("open")),
        )
        if expected != actual:
            errors.append(
                "status marker is stale: it claims "
                f"total={actual[0]} done={actual[1]} partial={actual[2]} open={actual[3]}, "
                f"but the table has total={expected[0]} done={expected[1]} "
                f"partial={expected[2]} open={expected[3]}"
            )

    for error in errors:
        print(f"Direct2D plan checker: {error}", file=sys.stderr)
    if errors:
        return 1
    print(
        f"Direct2D plan checker: {len(statuses)} rows consistent "
        f"({done} complete, {partial} partial, {still_open} open)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
