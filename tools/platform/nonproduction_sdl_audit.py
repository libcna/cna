#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""PLAT-123: classify and ratchet SDL use in tests and examples.

Production has a zero-outside-the-allowlist rule. Non-production code needs a different rule:
an integration executable may legitimately create a native window, while a public-header compile
test may deliberately spell an SDL guard to prove that no such type leaked. This audit makes
those exceptions explicit and finite instead of exempting every tests/ or examples/ directory.

Every surviving file must match one narrow category below and must have a checked-in per-file
reference ceiling. A new file, an unclassified location, or an increased count fails. Removing a
dependency is always allowed and ``--update`` tightens the manifest to the new floor.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sdl_inventory import FileRecord, scan  # noqa: E402


MANIFEST = Path("tools/platform/nonproduction_sdl_budget.json")

CATEGORY_RATIONALES = {
    "platform-sdl3-integration": (
        "Exercises the selected SDL3 platform implementation at its native edge; these tests "
        "live in modules/platform/tests and are excluded from non-SDL platform builds."
    ),
    "platform-sdl2-integration": (
        "Exercises the selected SDL2 platform implementation at its native edge; these tests "
        "live in modules/platform/tests and are excluded from non-SDL2 platform builds."
    ),
    "audio-native-integration": (
        "Exercises the separately selected SDL3/SDL_mixer audio implementation or mixer ABI; "
        "the audio boundary is independent of CNA's window/input platform."
    ),
    "audio-native-fixture": (
        "Selects the native dummy audio driver for a higher-layer content/media integration "
        "test; it contains no native type or call."
    ),
    "renderer-integration-executable": (
        "Non-shipping executable that supplies a real host window/context to exercise renderer "
        "interop. Production renderer coupling is governed separately by renderer_sdl_audit.py."
    ),
    "containment-assertion": (
        "Deliberately spells native header guards or API names to prove they are absent from a "
        "public/neutral surface; it does not call or include the native API."
    ),
}


def classify(record: FileRecord) -> str | None:
    path = record.path.as_posix()

    if path.startswith("modules/platform/tests/CNA/Platform/Sdl3"):
        return "platform-sdl3-integration"
    if path.startswith("modules/platform/tests/CNA/Platform/Sdl2"):
        return "platform-sdl2-integration"

    if path.startswith("modules/audio/tests/"):
        return "audio-native-integration"

    if path in {
        "modules/content/tests/Microsoft/Xna/Framework/Content/CnjCapabilityMatrixTests.cpp",
        "modules/media/tests/Microsoft/Xna/Framework/Media/MediaPlayerTests.cpp",
    }:
        return "audio-native-fixture"

    if path.startswith("modules/renderers/") and "/examples/" in path:
        return "renderer-integration-executable"
    if path.startswith("modules/graphics/examples/"):
        return "renderer-integration-executable"

    if path in {
        "modules/input/tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp",
        "modules/input/tests/Microsoft/Xna/Framework/Input/PublicApiInputSignatureFreezeTests.cpp",
        "modules/platform/tests/CNA/Platform/ContractIsSdlFreeTests.cpp",
        "modules/platform/tests/CNA/Platform/IPlatformTests.cpp",
        "modules/platform/tests/CNA/Platform/TerminalPresenterTests.cpp",
    }:
        return "containment-assertion"

    return None


def current_entries(repo_root: Path) -> tuple[dict[str, dict[str, object]], list[str]]:
    entries: dict[str, dict[str, object]] = {}
    errors: list[str] = []
    for record in scan(repo_root).non_production_files:
        path = record.path.as_posix()
        category = classify(record)
        if category is None:
            errors.append(f"unclassified SDL use: {path} ({record.reference_count} references)")
            continue
        entries[path] = {
            "category": category,
            "max_references": record.reference_count,
        }
    return entries, errors


def load_manifest(repo_root: Path) -> dict[str, object] | None:
    path = repo_root / MANIFEST
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def write_manifest(repo_root: Path, entries: dict[str, dict[str, object]]) -> None:
    payload = {
        "_comment": (
            "PLAT-123 per-file ceilings for justified SDL use in tests/examples. Entries may "
            "be removed or lowered without an override; additions/increases require "
            "nonproduction_sdl_audit.py --update --allow-increase."
        ),
        "entries": dict(sorted(entries.items())),
    }
    (repo_root / MANIFEST).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def print_report(entries: dict[str, dict[str, object]]) -> None:
    files: Counter[str] = Counter()
    references: Counter[str] = Counter()
    for value in entries.values():
        category = str(value["category"])
        files[category] += 1
        references[category] += int(value["max_references"])

    print(
        f"non-production SDL audit: {len(entries)} justified files, "
        f"{sum(references.values())} references"
    )
    for category in CATEGORY_RATIONALES:
        if files[category]:
            print(f"  {files[category]:4d} files / {references[category]:4d} refs  {category}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--check", action="store_true", help="enforce the checked-in manifest")
    parser.add_argument("--update", action="store_true", help="tighten the manifest to current")
    parser.add_argument(
        "--allow-increase", action="store_true",
        help="permit new files or raised per-file ceilings during an explicit update")
    args = parser.parse_args(argv)

    repo_root = args.repo.resolve()
    current, classification_errors = current_entries(repo_root)
    if classification_errors:
        print("\n".join(classification_errors), file=sys.stderr)
        print(
            "Move the native test to its owning edge, migrate it to the platform contract, or "
            "add a narrowly justified category before updating the manifest.",
            file=sys.stderr,
        )
        return 1

    manifest = load_manifest(repo_root)
    recorded = {} if manifest is None else dict(manifest.get("entries", {}))

    regressions: list[str] = []
    for path, value in current.items():
        previous = recorded.get(path)
        if previous is None:
            regressions.append(f"new SDL-referencing test/example: {path}")
            continue
        if previous.get("category") != value["category"]:
            regressions.append(
                f"category changed for {path}: {previous.get('category')} -> {value['category']}")
        before = int(previous.get("max_references", 0))
        after = int(value["max_references"])
        if after > before:
            regressions.append(f"SDL references increased in {path}: {before} -> {after}")

    if args.update:
        if manifest is not None and regressions and not args.allow_increase:
            print("refusing to raise the non-production SDL manifest:", file=sys.stderr)
            print("\n".join(f"  {item}" for item in regressions), file=sys.stderr)
            print("Re-run with --allow-increase only for a reviewed, justified exception.", file=sys.stderr)
            return 1
        write_manifest(repo_root, current)
        print_report(current)
        print(f"manifest updated: {MANIFEST}")
        return 0

    if manifest is None:
        print(f"no manifest recorded; create it with: {Path(__file__).name} --update", file=sys.stderr)
        return 1 if args.check else 0

    if regressions:
        print("non-production SDL audit failed:", file=sys.stderr)
        print("\n".join(f"  {item}" for item in regressions), file=sys.stderr)
        return 1

    print_report(current)
    removed = len(recorded) - len(current)
    if removed > 0:
        print(f"  below manifest by {removed} files; tighten with --update")
    else:
        print("  at per-file manifest ceilings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
