#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Checks that plans/plan_xnapipeline.md's stated task totals match its own task table.

plans/plan_xnapipeline.md XNAP-9B, extended by XNAP-95. The plan's summary paragraph and its task
table drifted apart once (the summary said "69 tasks: 57 done, 8 partial, 2 blocked, 1 open" -- a
total that neither matched the table nor added up to itself), which is the kind of error a reader
has no way to catch and a five-line script catches every time. This is deliberately not a
documentation framework: it counts checkbox markers, compares them with every counter sentence in
the same file, and says which number is wrong.

**Why there are two accepted phrasings.** The first version of this script recognized only the
form "N tasks: D done, P partial, B blocked, O open", and that turned out to be a hole rather than
a simplification: the final-reconciliation row, XNAP-95, carried its own count in a *different*
shape -- "Of 69 tasks: 57 `[x]`, 8 `[~]`, 2 `[!]`, 1 open" -- and went on contradicting the table
for as long as it existed while this script stayed green. A gate that only sees one way of writing
a number is a gate that teaches people the other way. Both forms are parsed now, and
`--self-test` (which runs on every invocation) feeds the exact stale sentence back in and fails if
it is not caught.

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
# The canonical phrasing, which --fix writes.
SUMMARY = re.compile(
    r"(?P<total>\d+)\s+tasks:\s+(?P<done>\d+)\s+done,\s+(?P<partial>\d+)\s+partial,"
    r"\s+(?P<blocked>\d+)\s+blocked,\s+(?P<open>\d+)\s+open"
)

# The marker-symbol phrasing, which XNAP-95's own row used and which this script did not see for
# as long as that row existed. Accepted rather than banned: it is a perfectly clear way to write
# the same claim, and a gate that rejects clear prose gets worked around instead of obeyed.
MARKER_SUMMARY = re.compile(
    r"[Oo]f\s+(?P<total>\d+)\s+tasks:\s+(?P<done>\d+)\s+`?\[x\]`?[^.]*?,"
    r"\s*(?P<partial>\d+)\s+`?\[~\]`?[^.]*?,"
    r"\s*(?P<blocked>\d+)\s+`?\[!\]`?[^.]*?,"
    r"\s*(?P<open>\d+)\s+open"
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
    return _check_text(path, path.read_text(encoding="utf-8"))


def _check_text(path: Path, text: str) -> list[str]:
    counts, _, row_problems = count_tasks(text)
    problems = [f"{path}:{problem}" for problem in row_problems]

    total = sum(counts.values())
    if total == 0:
        problems.append(f"{path}: no XNAP task rows found; the table's shape must have changed.")
        return problems

    # Normalize the emphasis markers so "**62 done**" and "62 done" read the same.
    plain = text.replace("**", "")
    summaries = list(SUMMARY.finditer(plain)) + list(MARKER_SUMMARY.finditer(plain))
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


# The exact sentence that escaped, and what the table it contradicted actually held. Kept as a
# literal so this script's own blind spot cannot come back quietly.
SELF_TEST_STALE = (
    "| `XNAP-95` | Final reconciliation. | [~] Reconciled. Of 69 tasks: 57 `[x]`, 8 `[~]` with "
    "the remaining scope named in the row itself, 2 `[!]` blocked with the blocker named, 1 open "
    "owner decision. |\n"
)


def self_test() -> list[str]:
    """Feeds the stale sentence that escaped back in, and reports if it is still not caught.

    A one-row table with a stale "Of 69 tasks" counter beside it: the counter must be seen, and
    must be reported as disagreeing with the table. Runs on every invocation, because a gate whose
    own falsification is optional is a gate nobody re-runs.
    """
    document = "# self-test\n\n" + SELF_TEST_STALE
    problems = []
    counts, _seen, _row = count_tasks(document)
    if sum(counts.values()) != 1:
        problems.append("self-test: the one-row fixture no longer parses as one task.")
    plain = document.replace("**", "")
    if not MARKER_SUMMARY.search(plain):
        problems.append(
            "self-test: the marker-symbol counter phrasing 'Of 69 tasks: 57 [x], 8 [~], 2 [!], "
            "1 open' is not recognized. That is the exact shape that contradicted the task table "
            "for as long as XNAP-95 existed while this script reported success.")
    found = [problem for problem in _check_text(Path("<self-test>"), document)
             if "disagrees with the task table" in problem]
    if not found:
        problems.append(
            "self-test: a stale counter that disagrees with its own table was not reported.")
    return problems


def main(argv: list[str]) -> int:
    arguments = argv[1:]
    should_fix = "--fix" in arguments
    arguments = [argument for argument in arguments if argument != "--fix"]
    paths = [Path(argument) for argument in arguments]
    if not paths:
        paths = [Path(__file__).resolve().parents[2] / "plans" / "plan_xnapipeline.md"]

    problems: list[str] = self_test()
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
