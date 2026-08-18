#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""
PLAT-8 / PLAT-121 - ratchet gate on CNA's remaining SDL coupling.

Purpose
-------
plan_platform.md migrates ~260 production files off SDL across nine phases. Two failure modes
matter over that span, and neither is caught by a check that only runs at the end:

  * silent backsliding -- a new SDL call site added to an already-migrated module;
  * a stalled migration that still looks finished because nobody is counting.

So the count is recorded in a checked-in budget file and enforced continuously. The budget may go
down and never up. Each migration task lowers it; nothing is allowed to raise it without an
explicit, deliberate override that shows up in review as a diff to the budget file.

Warning first, error later
--------------------------
PLAT-8 runs this in *warning* mode: exceeding the budget prints a warning but does not fail the
build, because at that point essentially the whole tree is over budget by design and a hard error
would simply block all work. PLAT-121 flips it to --strict once the count reaches the allowlist
floor. The point of running it from the very start, warning-only, is that the trend is visible
from day one instead of discovered at the end.

The allowlist
-------------
Established by PLAT-3 (docs/platform-renderer-sdl-audit.md) and design decision 6:

  * modules/platform/          -- the platform module itself; SDL3 lives here by definition
  * modules/renderers/sdl-renderer, sdl-gpu   -- SDL by identity
  * modules/renderers/fna3d, freedirect       -- SDL by upstream dependency
  * the SDL3 audio implementation             -- separate contract, PLAT-91..99

Anything else counted here is coupling still to remove.

Usage
-----
    python3 tools/platform/sdl_ratchet.py                 # report current vs budget
    python3 tools/platform/sdl_ratchet.py --check         # warn if over budget (exit 0)
    python3 tools/platform/sdl_ratchet.py --check --strict # fail if over budget (PLAT-121)
    python3 tools/platform/sdl_ratchet.py --update        # lower the budget to current
    python3 tools/platform/sdl_ratchet.py --update --allow-increase   # deliberate override
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sdl_inventory import scan  # noqa: E402

BUDGET_FILE = Path("tools/platform/sdl_budget.json")

# Path prefixes (repo-relative, POSIX separators) exempt from the ratchet.
ALLOWLIST_PREFIXES: tuple[str, ...] = (
    "modules/platform/",
    "modules/renderers/sdl-renderer/",
    "modules/renderers/sdl-gpu/",
    "modules/renderers/fna3d/",
    "modules/renderers/freedirect/",
    "modules/audio/src/Platform/Sdl3/",  # SDL3 playback and recording implementations
    "modules/audio/src/Platform/Sdl2/",  # SDL2 playback implementation
    "modules/audio/src/Backend/Sdl3Mixer/",  # memory mixer implementation
)


def is_allowlisted(path: str) -> bool:
    return path.startswith(ALLOWLIST_PREFIXES)


def measure(repo_root: Path) -> dict[str, object]:
    inv = scan(repo_root)
    counted = [
        f for f in inv.production_files if not is_allowlisted(f.path.as_posix())
    ]
    by_module: dict[str, int] = {}
    for record in counted:
        key = ("renderers/" if record.is_renderer else "") + record.module
        by_module[key] = by_module.get(key, 0) + 1

    return {
        "files": len(counted),
        "references": sum(f.reference_count for f in counted),
        "by_module": dict(sorted(by_module.items(), key=lambda kv: (-kv[1], kv[0]))),
    }


def load_budget(repo_root: Path) -> dict[str, object] | None:
    path = repo_root / BUDGET_FILE
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def load_floor(budget: dict[str, object]) -> tuple[int, int] | None:
    """The lowest budget ever recorded, if PLAT-121's floor has been reached.

    PLAT-121 calls 0/0 an *irreversible* floor, and until this existed that was only prose: the
    budget was raised back to 4 files / 14 references by one `--update --allow-increase` when a
    renderer arrived still coupled to SDL, and every gate went on passing against the raised
    number. The floor is stored as data so the claim is enforced rather than asserted, and so
    undoing it is a visible edit to a checked-in file rather than a command-line flag.
    """
    floor = budget.get("floor")
    if not isinstance(floor, dict):
        return None
    return int(floor["max_files"]), int(floor["max_references"])


def write_budget(repo_root: Path, current: dict[str, object], floor: tuple[int, int] | None) -> None:
    path = repo_root / BUDGET_FILE
    payload: dict[str, object] = {
        "_comment": (
            "PLAT-8 ratchet budget. Maximum SDL-referencing production files (and total "
            "references) allowed outside the PLAT-3 allowlist. This may go DOWN and never UP: "
            "each migration task lowers it. Regenerate with "
            "'python3 tools/platform/sdl_ratchet.py --update'. See plan_platform.md."
        ),
        "max_files": current["files"],
        "max_references": current["references"],
        "by_module_at_last_update": current["by_module"],
    }

    # The floor only ever moves down, and once recorded it outlives every later --update.
    lowest = (int(current["files"]), int(current["references"]))
    if floor is not None:
        lowest = (min(floor[0], lowest[0]), min(floor[1], lowest[1]))
    payload["floor"] = {
        "_comment": (
            "PLAT-121's irreversible floor: the lowest budget this repository has ever reached. "
            "--update refuses to write a budget above it, with or without --allow-increase, and "
            "--check fails when the checked-in budget exceeds it. New SDL coupling outside the "
            "PLAT-3 allowlist is fixed at the call site, never absorbed here."
        ),
        "max_files": lowest[0],
        "max_references": lowest[1],
    }

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--check", action="store_true", help="compare against the budget")
    parser.add_argument("--strict", action="store_true", help="with --check, exit non-zero when over budget (PLAT-121)")
    parser.add_argument("--update", action="store_true", help="record the current measurement as the budget")
    parser.add_argument("--allow-increase", action="store_true", help="with --update, permit raising the budget")
    args = parser.parse_args(argv)

    repo_root: Path = args.repo.resolve()
    current = measure(repo_root)
    budget = load_budget(repo_root)
    floor = load_floor(budget) if budget is not None else None

    if args.update:
        # The floor outranks --allow-increase. That flag exists for a migration still in progress;
        # once the floor is recorded the migration is finished, and the answer to new coupling is
        # to remove it, not to buy room for it here.
        if floor is not None and (int(current["files"]) > floor[0] or int(current["references"]) > floor[1]):
            print(
                "refusing to raise the ratchet budget above PLAT-121's recorded floor.\n"
                f"  floor:      {floor[0]} files, {floor[1]} references\n"
                f"  measured:   {current['files']} files, {current['references']} references\n"
                "--allow-increase does NOT override this. Fix the new SDL coupling instead:\n"
                "run 'python3 tools/platform/sdl_ratchet.py' to see which module it is in, and move\n"
                "it behind CNA::Platform (see plan_platform.md and docs/platform-abstraction.md).",
                file=sys.stderr,
            )
            return 1
        if budget is not None and not args.allow_increase:
            if int(current["files"]) > int(budget["max_files"]) or int(current["references"]) > int(budget["max_references"]):
                print(
                    "refusing to raise the ratchet budget.\n"
                    f"  files:      {budget['max_files']} -> {current['files']}\n"
                    f"  references: {budget['max_references']} -> {current['references']}\n"
                    "The budget ratchets down only. If this increase is genuinely intended, re-run\n"
                    "with --allow-increase so the raised budget shows up as a deliberate diff.",
                    file=sys.stderr,
                )
                return 1
        write_budget(repo_root, current, floor)
        print(f"budget set: {current['files']} files, {current['references']} references")
        return 0

    if budget is None:
        print(f"no budget recorded yet; current: {current['files']} files, {current['references']} references")
        print(f"record it with: python3 {BUDGET_FILE.parent}/sdl_ratchet.py --update")
        return 0

    over_files = int(current["files"]) - int(budget["max_files"])
    over_refs = int(current["references"]) - int(budget["max_references"])

    # Checked before the measurement, because a budget above the floor makes every later
    # comparison meaningless: it is the state in which real new coupling passes as "at budget".
    if args.check and floor is not None and (
        int(budget["max_files"]) > floor[0] or int(budget["max_references"]) > floor[1]
    ):
        print(
            "CNA platform ratchet (PLAT-121): the checked-in budget is ABOVE its recorded floor.\n"
            f"  budget: {budget['max_files']} files, {budget['max_references']} references\n"
            f"  floor:  {floor[0]} files, {floor[1]} references\n"
            "The floor is the lowest budget this repository has ever reached and it does not move\n"
            "back up. Restore the budget and remove the coupling that motivated raising it.",
            file=sys.stderr,
        )
        return 1 if args.strict else 0

    if not args.check:
        print(f"SDL coupling outside the allowlist: {current['files']} files, {current['references']} references")
        print(f"budget:                             {budget['max_files']} files, {budget['max_references']} references")
        print()
        print("Remaining by module:")
        for module, count in current["by_module"].items():  # type: ignore[union-attr]
            print(f"  {count:4d}  {module}")
        return 0

    if over_files > 0 or over_refs > 0:
        message = (
            f"CNA platform ratchet (PLAT-8): SDL coupling INCREASED outside the allowlist.\n"
            f"  files:      {current['files']} (budget {budget['max_files']}, +{max(0, over_files)})\n"
            f"  references: {current['references']} (budget {budget['max_references']}, +{max(0, over_refs)})\n"
            f"New SDL usage belongs behind the platform contract -- see plan_platform.md.\n"
            f"If this increase is genuinely intended, run:\n"
            f"  python3 tools/platform/sdl_ratchet.py --update --allow-increase"
        )
        print(message, file=sys.stderr)
        return 1 if args.strict else 0

    if over_files < 0 or over_refs < 0:
        print(
            f"ratchet: below budget by {-over_files} files / {-over_refs} references. "
            f"Lower it with: python3 tools/platform/sdl_ratchet.py --update"
        )
    else:
        print(f"ratchet: at budget ({current['files']} files, {current['references']} references).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
