#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Checks that plans/plan_xnapipeline.md's stated task totals match its own task table.

plans/plan_xnapipeline.md XNAP-9B. The plan's summary paragraph and its task table drifted apart
once (the summary said "69 tasks: 57 done, 8 partial, 2 blocked, 1 open" -- a total that neither
matched the table nor added up to itself), which is the kind of error a reader has no way to catch
and a five-line script catches every time. This is deliberately not a documentation framework: it
counts checkbox markers, compares them with every "N tasks: ..." sentence in the same file, and
says which number is wrong.

Usage:
    python3 tools/xnb/check_plan_status.py [--fix] [plan.md ...]

`--fix` rewrites every stated total to what the table actually says. The table is the source of
truth and the summary is derived from it, so that direction is the only correct one.

Exit code 0 when every stated total agrees with the table, 1 otherwise.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# One task row: "| `XNAP-42` | description | [x] optional commentary |"
TASK_ROW = re.compile(r"^\|\s*`(?P<id>XNAP-[0-9A-Za-z]+)`\s*\|(?P<body>.*)$")
STATUS_MARKER = re.compile(r"\|\s*\[(?P<mark>[x~! ])\](?P<rest>[^|]*)\s*(?:\||$)")
SUMMARY = re.compile(
    r"(?P<total>\d+)\s+tasks:\s+(?P<done>\d+)\s+done,\s+(?P<partial>\d+)\s+partial,"
    r"\s+(?P<blocked>\d+)\s+blocked,\s+(?P<open>\d+)\s+open"
)

MARK_NAMES = {"x": "done", "~": "partial", "!": "blocked", " ": "open"}


def count_tasks(text: str) -> tuple[dict[str, int], dict[str, int], list[str]]:
    """Counts task rows by status marker, returning (counts, first line per id, problems)."""
    problems: list[str] = []
    counts = {name: 0 for name in MARK_NAMES.values()}
    seen: dict[str, int] = {}
    for number, line in enumerate(text.splitlines(), start=1):
        row = TASK_ROW.match(line)
        if row is None:
            continue
        task = row.group("id")
        if task in seen:
            problems.append(
                f"{number}: task id {task} is reused; it was already defined on line "
                f"{seen[task]}. The plan's own rule is never to reuse an id."
            )
        else:
            seen[task] = number
        status = STATUS_MARKER.search("|" + row.group("body"))
        if status is None:
            problems.append(f"{number}: task {task} has no [x]/[~]/[!]/[ ] status marker.")
            continue
        mark = status.group("mark")
        counts[MARK_NAMES[mark]] += 1
        if mark in "~!" and not status.group("rest").strip():
            problems.append(
                f"{number}: task {task} is [{mark}] with no explanation. A partial or "
                f"blocked task must name the remaining scope or the blocker in its own row."
            )
    return counts, seen, problems


def check(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    counts, _, row_problems = count_tasks(text)
    problems = [f"{path}:{problem}" for problem in row_problems]

    total = sum(counts.values())
    if total == 0:
        problems.append(f"{path}: no XNAP task rows found; the table's shape must have changed.")
        return problems

    # Normalize the emphasis markers so "**62 done**" and "62 done" read the same.
    summaries = list(SUMMARY.finditer(text.replace("**", "")))
    if not summaries:
        problems.append(
            f"{path}: no summary sentence of the form "
            f"'N tasks: D done, P partial, B blocked, O open' found. The table has "
            f"{total} tasks: {counts['done']} done, {counts['partial']} partial, "
            f"{counts['blocked']} blocked, {counts['open']} open."
        )
    for summary in summaries:
        stated = {name: int(summary.group(name)) for name in MARK_NAMES.values()}
        stated_total = int(summary.group("total"))
        if stated_total != sum(stated.values()):
            problems.append(
                f"{path}: the summary '{summary.group(0)}' does not add up: "
                f"{' + '.join(str(stated[n]) for n in MARK_NAMES.values())} = "
                f"{sum(stated.values())}, not {stated_total}."
            )
        if stated_total != total or stated != counts:
            problems.append(
                f"{path}: the summary '{summary.group(0)}' disagrees with the task table, "
                f"which has {total} tasks: {counts['done']} done, {counts['partial']} partial, "
                f"{counts['blocked']} blocked, {counts['open']} open."
            )
    return problems


def fix(path: Path) -> bool:
    """Rewrites every stated total in @p path to match its task table. Returns True if changed."""
    text = path.read_text(encoding="utf-8")
    counts, _, _ = count_tasks(text)
    total = sum(counts.values())
    if total == 0:
        return False
    replacement = (
        f"{total} tasks: {counts['done']} done, {counts['partial']} partial, "
        f"{counts['blocked']} blocked, {counts['open']} open"
    )
    # The emphasis markers sit inside the sentence, so rewrite on the de-emphasized text and put
    # the plan's own bolding back around the whole sentence where it had it.
    updated = SUMMARY.sub(replacement, text.replace("**", "\x00"))
    updated = updated.replace("\x00", "**")
    if updated == text:
        return False
    path.write_text(updated, encoding="utf-8")
    return True


def main(argv: list[str]) -> int:
    arguments = argv[1:]
    should_fix = "--fix" in arguments
    arguments = [argument for argument in arguments if argument != "--fix"]
    paths = [Path(argument) for argument in arguments]
    if not paths:
        paths = [Path(__file__).resolve().parents[2] / "plans" / "plan_xnapipeline.md"]

    problems: list[str] = []
    for path in paths:
        if not path.is_file():
            problems.append(f"{path}: not a file.")
            continue
        if should_fix and fix(path):
            print(f"{path}: rewrote the stated task totals from the task table.")
        problems.extend(check(path))

    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        print(f"{len(problems)} plan-status problem(s).", file=sys.stderr)
        return 1
    for path in paths:
        print(f"{path}: task totals agree with the task table.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
