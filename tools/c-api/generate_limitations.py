#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Publish what the C ABI does **not** do, from the record that already says so.

``COVERAGE.md`` is a 6,415-row inventory: complete, mechanical, and useless to somebody deciding
whether this ABI can carry their program. This tool reads the same record and answers the other
question — *what is missing, and what do I do about it* — by collapsing the partially mapped and
unmapped symbols into the reasons behind them.

Three rules keep the result honest:

* **Every unmapped reason must fall under a declared theme.** A reason nobody classified fails the
  run rather than landing in a silent "other" bucket, which is how a limitations document rots.
* **A deferral may not name a closed task as its owner.** "Planned in CBIND-035" stops being a plan
  the moment CBIND-035 closes; from then on it is an omission wearing a plan's clothes.
* **The counts come from the inventory, not from prose.** A limitation nobody re-counted is a
  limitation nobody re-checked.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
COVERAGE_PATH = REPO_ROOT / "docs" / "c-api" / "COVERAGE.md"
PLAN_PATH = REPO_ROOT / "plan_binding.md"
DECLARATION_PATH = Path(__file__).resolve().parent / "limitations.json"
COMPATIBILITY_PATH = Path(__file__).resolve().parent / "compatibility_matrix.json"
DOC_PATH = REPO_ROOT / "docs" / "c-api" / "LIMITATIONS.md"


def read_rows() -> list[dict]:
    """Return the inventory rows that record a limitation."""
    if not COVERAGE_PATH.exists():
        raise SystemExit(f"{COVERAGE_PATH.relative_to(REPO_ROOT)} does not exist.")
    rows = []
    for line in COVERAGE_PATH.read_text(encoding="utf-8").splitlines():
        if not line.startswith("| `CPP-"):
            continue
        cells = [cell.strip() for cell in line.split(" | ")]
        if len(cells) < 7:
            continue
        status = cells[-1]
        if "partial" in status:
            kind = "partial"
        elif "not applicable" in status:
            kind = "not-applicable"
        else:
            continue
        task = ""
        task_match = re.search(r"`(CBIND-[0-9A-Za-z]+)`", status)
        if task_match:
            task = task_match.group(1)
        rows.append({
            "kind": kind,
            "symbol": cells[3],
            "mapping": cells[4],
            "tests": cells[5],
            "task": task,
        })
    if not rows:
        raise SystemExit("No partial or not-applicable rows were found; the inventory looks wrong.")
    return rows


def open_tasks() -> set[str]:
    """Task ids the plan still records as unfinished."""
    text = PLAN_PATH.read_text(encoding="utf-8")
    return set(re.findall(r"\|\s*(CBIND-[0-9A-Za-z]+)\s*\|[^|]*\|\s*⬜", text))


def classify(mapping: str, themes: list[dict]) -> str | None:
    for theme in themes:
        for needle in theme["match"]:
            if needle.lower() in mapping.lower():
                return theme["id"]
    return None


def group(rows: list[dict], kind: str) -> list[dict]:
    grouped: dict[str, dict] = {}
    for row in rows:
        if row["kind"] != kind:
            continue
        entry = grouped.setdefault(row["mapping"], {
            "mapping": row["mapping"],
            "symbols": [],
            "tasks": set(),
        })
        entry["symbols"].append(row["symbol"])
        if row["task"]:
            entry["tasks"].add(row["task"])
    return sorted(grouped.values(), key=lambda entry: (-len(entry["symbols"]), entry["mapping"]))


def short_symbol(symbol: str) -> str:
    """The last two namespace components, which is what identifies a symbol to a reader."""
    text = symbol.strip("`")
    text = re.sub(r"\(.*", "", text)
    parts = text.split("::")
    return "::".join(parts[-2:]) if len(parts) > 1 else text


def analyze() -> dict:
    declaration = json.loads(DECLARATION_PATH.read_text(encoding="utf-8"))
    rows = read_rows()
    partial = group(rows, "partial")
    unmapped = group(rows, "not-applicable")

    problems: list[str] = []
    live = open_tasks()

    # A deferral is only a deferral while somebody owns it.
    for entry in partial:
        for task in sorted(set(re.findall(r"CBIND-[0-9A-Za-z]+", entry["mapping"]))):
            if task not in live:
                problems.append(
                    f"a partial mapping still defers to {task}, which the plan records as finished: "
                    f"{entry['mapping'][:90]}")

    # The same rule one level up: an open decision whose owner is a finished task is nobody's.
    # A decision with no live owner is not deferred, it is dropped.
    for entry in declaration["environment"]:
        owner = entry.get("owner", "")
        if re.fullmatch(r"CBIND-[0-9A-Za-z]+", owner) and owner not in live:
            problems.append(
                f"the limitation \"{entry['subject']}\" is owned by {owner}, "
                "which the plan records as finished")

    for entry in unmapped:
        entry["theme"] = classify(entry["mapping"], declaration["themes"])
        if entry["theme"] is None:
            problems.append(
                f"{len(entry['symbols'])} unmapped symbol(s) have a reason no declared theme covers: "
                f"{entry['mapping'][:110]}")

    implemented = 0
    snapshot = re.search(r"\*\*([0-9,]+) implemented\*\*", COVERAGE_PATH.read_text(encoding="utf-8"))
    if snapshot is None:
        problems.append("the inventory snapshot line does not report an implemented count")
    else:
        implemented = int(snapshot.group(1).replace(",", ""))

    return {
        "declaration": declaration,
        "implemented_symbols": implemented,
        "partial": partial,
        "unmapped": unmapped,
        "problems": problems,
        "partial_symbols": sum(len(entry["symbols"]) for entry in partial),
        "unmapped_symbols": sum(len(entry["symbols"]) for entry in unmapped),
    }


def render(analysis: dict) -> str:
    declaration = analysis["declaration"]
    lines: list[str] = []
    add = lines.append

    add("# What the CNA C API does not do")
    add("")
    add("This file is generated by `tools/c-api/generate_limitations.py` from the same inventory")
    add("that produces [`COVERAGE.md`](COVERAGE.md), plus the declared non-symbol limitations in")
    add("`tools/c-api/limitations.json`. It is not a summary of that inventory: it answers the other")
    add("question. `COVERAGE.md` records every one of the 6,415 public C++ declarations and what")
    add("became of it. This records the ones that did **not** become a callable C route, grouped by")
    add("the reason, so a consumer can decide whether any of it stands in their way.")
    add("")
    add("A limitation here is never an apology. Most of these are statements the canonical C++ makes")
    add("first -- a deleted operator, a protected hook, a template over a type C cannot name -- and")
    add("the C ABI is only repeating them.")
    add("")

    add("## The shape of it")
    add("")
    add("| | Symbols | What it means for a caller |")
    add("|---|---:|---|")
    add(f"| Fully mapped | {analysis['implemented_symbols']:,} | A C route exists and is tested. |")
    add(f"| **Partially mapped** | {analysis['partial_symbols']} | "
        "A route exists but covers a stated subset. Read the next section before relying on one. |")
    add(f"| **No C form** | {analysis['unmapped_symbols']} | "
        "Nothing callable was omitted; see the reasons below. |")
    add("")

    add("## Partially mapped: a route exists, and it does less than the C++ does")
    add("")
    add("These are the only entries in this document that can surprise a working program: something")
    add("does exist under the name you expect, and it does less than the C++ of the same name. Each")
    add("row says what you get and, in the same breath, what you do not.")
    add("")
    add("| Symbols | What exists, and what it leaves out |")
    add("|---|---|")
    for entry in analysis["partial"]:
        names = collections.Counter(short_symbol(symbol) for symbol in entry["symbols"])
        subjects = "<br/>".join(
            f"`{name}`" + (f" ×{count}" if count > 1 else "")
            for name, count in sorted(names.items()))
        mapping = entry["mapping"].replace("|", "\\|")
        add(f"| {subjects} | {mapping} |")
    add("")

    add("## No C form: the reasons, by theme")
    add("")
    add("Every unmapped symbol falls under one of these. The generator fails if one does not, which")
    add("is what stops this list from acquiring a silent \"other\" category.")
    add("")
    by_theme: dict[str, list[dict]] = collections.defaultdict(list)
    for entry in analysis["unmapped"]:
        by_theme[entry["theme"]].append(entry)
    for theme in declaration["themes"]:
        entries = by_theme.get(theme["id"], [])
        if not entries:
            continue
        count = sum(len(entry["symbols"]) for entry in entries)
        add(f"### {theme['title']} — {count} symbols")
        add("")
        add(theme["note"])
        add("")

    add("## Limitations that are not about symbols")
    add("")
    add("An inventory cannot record these, because they are properties of the package and the")
    add("promise rather than of any declaration.")
    add("")
    add("| Subject | What it means | Status |")
    add("|---|---|---|")
    for entry in declaration["environment"]:
        impact = entry["impact"].replace("|", "\\|")
        add(f"| **{entry['subject']}** | {impact} | {entry['status']} ({entry['owner']}) |")
    add("")

    compatibility = json.loads(COMPATIBILITY_PATH.read_text(encoding="utf-8"))
    add("## Not covered by any test here")
    add("")
    add("Taken from `tools/c-api/compatibility_matrix.json`, so the two cannot disagree:")
    add("")
    for entry in compatibility["unsupported"]:
        note = entry["note"]
        add(f"- **{entry['subject']}.** {note}")
    add("")
    add("Regenerate with:")
    add("")
    add("```bash")
    add("python3 tools/c-api/generate_limitations.py --write")
    add("```")
    add("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group_argument = parser.add_mutually_exclusive_group(required=True)
    group_argument.add_argument("--write", action="store_true", help="regenerate the document")
    group_argument.add_argument("--check", action="store_true", help="verify it is current and sound")
    arguments = parser.parse_args()

    analysis = analyze()
    rendered = render(analysis)

    if analysis["problems"]:
        print(f"{len(analysis['problems'])} problem(s) with the recorded limitations:", file=sys.stderr)
        for problem in analysis["problems"]:
            print(f"  {problem}", file=sys.stderr)
        return 1

    summary = (
        f"{analysis['partial_symbols']} partially mapped symbols in {len(analysis['partial'])} groups, "
        f"{analysis['unmapped_symbols']} unmapped in {len(analysis['unmapped'])} groups")

    if arguments.write:
        DOC_PATH.write_text(rendered, encoding="utf-8")
        print(f"wrote {DOC_PATH.relative_to(REPO_ROOT)}: {summary}")
        return 0

    current = DOC_PATH.read_text(encoding="utf-8") if DOC_PATH.exists() else ""
    if current != rendered:
        print(
            f"{DOC_PATH.relative_to(REPO_ROOT)} is out of date; "
            "run generate_limitations.py --write",
            file=sys.stderr)
        return 1
    print(f"{DOC_PATH.relative_to(REPO_ROOT)} is current: {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
