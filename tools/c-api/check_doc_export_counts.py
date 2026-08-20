#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Hold the prose export counts against the measured one.

``abi_baseline.json`` records how many `cna_*` symbols the library actually exports, and a gate
already fails when that number changes without review. Three *sentences* repeat the same number in
`docs/c-api/` and in `limitations.json`, and no gate read them: they said **2,720** for months while
the baseline measured 2,838, were corrected by hand on 2026-08-17, and went stale again at the very
next slice that added exports. `plans/plan_binding.md` recorded that as "nothing prevents it happening
again". This is the thing that prevents it.

The rule is deliberately narrow. A four-digit-or-more number counts as an export claim only when its
immediate neighbourhood also talks about exporting, or names the `cna_*` symbol set -- so
`COVERAGE.md`'s symbol and header totals, which are different measurements with their own generator,
are not swept in.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
BASELINE_PATH = Path(__file__).resolve().parent / "abi_baseline.json"

# Files whose prose repeats the count. Generated files are included on purpose: a generator that
# copies a stale number out of its own source is exactly as wrong as a hand-written one.
SEARCH_PATHS = (
    "docs/c-api/ABI_VERSIONING.md",
    "docs/c-api/CONSUMING.md",
    "docs/c-api/LIMITATIONS.md",
    "docs/c-api/RELEASE_GATE.md",
    "tools/c-api/limitations.json",
)

NUMBER = re.compile(r"\b(\d{1,3}(?:,\d{3})+|\d{4,})\b")
WINDOW = 90
EXPORT_WORDS = re.compile(r"export", re.IGNORECASE)
SYMBOL_SET = re.compile(r"`cna_\*`")


def measured_export_count() -> int:
    baseline = json.loads(BASELINE_PATH.read_text(encoding="utf-8"))
    exports = baseline.get("exports")
    if not isinstance(exports, list):
        raise SystemExit(
            f"{BASELINE_PATH} has no export list; regenerate it with "
            "generate_abi_baseline.py --write --library <libcna_c_api.so>."
        )
    return len(exports)


def claims(text: str) -> list[tuple[int, str]]:
    """Every number in `text` whose neighbourhood claims it is the export count."""
    found = []
    for match in NUMBER.finditer(text):
        start = max(0, match.start() - WINDOW)
        window = text[start:match.end() + WINDOW]
        if EXPORT_WORDS.search(window) or (SYMBOL_SET.search(window) and "names" in window):
            found.append((int(match.group(1).replace(",", "")), match.group(1)))
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true",
        help="fail when a prose export count disagrees with the measured one")
    parser.parse_args()

    expected = measured_export_count()
    wrong: list[str] = []
    checked = 0
    for relative in SEARCH_PATHS:
        path = REPO_ROOT / relative
        if not path.exists():
            wrong.append(f"{relative}: missing")
            continue
        text = path.read_text(encoding="utf-8")
        for value, written in claims(text):
            checked += 1
            if value != expected:
                line = text[:text.find(written)].count("\n") + 1
                wrong.append(
                    f"{relative}:{line}: claims {written} exported symbols, "
                    f"but abi_baseline.json measures {expected}")

    if wrong:
        print("Prose export counts disagree with the measured ABI:")
        for entry in wrong:
            print(f"  {entry}")
        print(
            "\nUpdate the sentence, not this check. The measured value comes from "
            "tools/c-api/abi_baseline.json; regenerate that first if it is the stale one.")
        return 1
    if checked == 0:
        print(
            "No export-count claim was found in any searched document. That is a failure, not a "
            "pass: this gate exists because those sentences go stale, and finding none means the "
            "sentences moved and the gate is now watching nothing.")
        return 1
    print(f"{checked} prose export count(s) agree with the measured {expected}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
