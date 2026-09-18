#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Guard the renderer-curation API decisions: what was removed stays removed, what was kept stays used.

`plans/plan_renderer_cleanup.md` `RRC-009` removed `DrawMeshEXT` -- the SpriteBatch 2D triangle-mesh
entry point -- from the C++ API and the C ABI, because the 25-renderer curation left it with no
implementer at all. `RRC-010` examined `needsSurfacePresenter` the same way and reached the opposite
verdict: no retained renderer *sets* it, but a retained, tested consumer (`TerminalSurfacePresenter`,
reached through `IPlatformSurfacePresenter`) is waiting on it, so it stays.

Those two decisions fail in opposite directions, and this gate holds both:

* A reintroduced `DrawMeshEXT` would be an entry point every renderer refuses -- exactly the dead
  ABI branch that was deleted. Re-adding one needs a renderer that implements it, so the gate names
  that condition rather than forbidding the name forever.
* A deleted `needsSurfacePresenter` read site, or a deleted terminal presenter, would quietly strand
  the `TERMINAL` platform with no way back. `RRC-010` kept the field *because* of that code, so the
  justification is checked rather than asserted in prose alone.

Deliberately narrow. This is not a general dead-API scanner: it pins the two specific decisions that
the curation forced, in the files that decide them, so a later session cannot undo either by
accident. Historical records are not searched -- `plans/`, `misc/`, `audit/` and the dated evidence
trees legitimately record that `DrawMeshEXT` once existed, and rewriting history to erase it would
be a defect, not a fix.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

# The removed name, and the C ABI surface that carried it.
REMOVED_CPP_SYMBOL = "DrawMeshEXT"
REMOVED_C_ROUTE = "cna_sprite_batch_draw_mesh_ext"
REMOVED_C_STRUCT = "CNA_SpriteMeshEXT"

# Active trees only. A match anywhere here is a live claim, not a historical one.
ACTIVE_CODE_DIRS = ("modules", "scripts", "cmake", "tools")
ACTIVE_DOC_DIRS = ("docs",)

# Files whose whole purpose is to record the removal, so they must name it.
REMOVAL_RECORD_FILES = {
    "docs/c-api/ABI_VERSIONING.md",      # the 0.29.0 release notes
    "docs/c-api/CABI_BLOCKER_HANDOFF.md",  # a dated 0.9.0-era note, past tense
    "CHANGELOG.md",
}

# `RRC-010`'s justification for keeping needsSurfacePresenter: the read site and the consumer.
PRESENTER_FIELD = "needsSurfacePresenter"
PRESENTER_DECLARATION = (
    "modules/graphics/include/CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp")
PRESENTER_READ_SITE = "modules/graphics/src/Xna/GraphicsDevice.cpp"
PRESENTER_CONTRACT = "modules/platform/include/CNA/Platform/IPlatformSurfacePresenter.hpp"
PRESENTER_CONSUMER = "modules/platform/src/Terminal/TerminalSurfacePresenter.cpp"

SOURCE_SUFFIXES = {".hpp", ".h", ".cpp", ".c", ".cc", ".py", ".cmake", ".txt", ".json", ".csv"}


def iter_files(directories: tuple[str, ...], suffixes: set[str] | None) -> list[Path]:
    found: list[Path] = []
    for directory in directories:
        root = REPO_ROOT / directory
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            if "third_party" in path.parts or "build-probe" in path.parts:
                continue
            if suffixes is not None and path.suffix not in suffixes:
                continue
            found.append(path)
    return found


def relative(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def check_removed_api_absent() -> list[str]:
    """RRC-009: no active source, tool or generated-metadata file names the removed surface."""
    failures: list[str] = []
    names = (REMOVED_CPP_SYMBOL, REMOVED_C_ROUTE, REMOVED_C_STRUCT)

    this_file = relative(Path(__file__).resolve())
    for path in iter_files(ACTIVE_CODE_DIRS, SOURCE_SUFFIXES):
        name = relative(path)
        # This gate must spell out what it forbids, and the baseline is checked separately below
        # with a message that says how to regenerate it rather than how to edit it.
        if name in REMOVAL_RECORD_FILES or name == this_file:
            continue
        if name == "tools/c-api/abi_baseline.json":
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        for needle in names:
            if needle not in text:
                continue
            # One allowance, and only one: the shared sort-mode test explains in a comment which
            # instrument its leg B2 replaced. That is the removal's own rationale, not a use.
            if (name == "modules/graphics/examples/spritebatch_sort_mode_semantics_test.cpp"
                    and needle == REMOVED_CPP_SYMBOL
                    and f"batch.{REMOVED_CPP_SYMBOL}(" not in text
                    and f"virtual void {REMOVED_CPP_SYMBOL}" not in text):
                continue
            lines = [n for n, line in enumerate(text.splitlines(), 1) if needle in line]
            failures.append(
                f"{name}:{lines[0]}: names the removed `{needle}`. "
                "RRC-009 removed it because no renderer implemented it; re-adding the surface "
                "requires a renderer that does, plus an ABI minor bump and release notes "
                "(docs/c-api/ABI_VERSIONING.md).")
    return failures


def check_removed_api_not_documented_as_available() -> list[str]:
    """RRC-009: active documentation must not offer the removed API to an application."""
    failures: list[str] = []
    for path in iter_files(ACTIVE_DOC_DIRS, {".md"}):
        name = relative(path)
        if name in REMOVAL_RECORD_FILES:
            continue
        text = path.read_text(encoding="utf-8")
        for needle in (REMOVED_CPP_SYMBOL, REMOVED_C_ROUTE, REMOVED_C_STRUCT):
            if needle in text:
                line = next(n for n, l in enumerate(text.splitlines(), 1) if needle in l)
                failures.append(
                    f"{name}:{line}: active documentation still names `{needle}`, which no "
                    "longer exists. Describe it only as removed, and only where the removal "
                    "itself is the subject.")
    return failures


def check_abi_baseline_clean() -> list[str]:
    """RRC-009: the recorded ABI baseline is generated, so it must have been regenerated."""
    baseline = REPO_ROOT / "tools/c-api/abi_baseline.json"
    if not baseline.is_file():
        return [f"{relative(baseline)}: missing; the ABI gate cannot run without it."]
    text = baseline.read_text(encoding="utf-8")
    failures = []
    for needle in (REMOVED_C_ROUTE, REMOVED_C_STRUCT):
        if needle in text:
            failures.append(
                f"{relative(baseline)}: still records `{needle}`. Regenerate it with "
                "`tools/c-api/generate_abi_baseline.py --write --library <built cna_c_api>` "
                "rather than editing it by hand.")
    return failures


def check_presenter_justification_intact() -> list[str]:
    """RRC-010: needsSurfacePresenter was kept for concrete code. Check that code still exists."""
    failures: list[str] = []

    declaration = REPO_ROOT / PRESENTER_DECLARATION
    if not declaration.is_file() or PRESENTER_FIELD not in declaration.read_text(encoding="utf-8"):
        # Removed deliberately? Then the whole justification must go with it, which is a decision,
        # not a drift -- so say exactly that instead of demanding the field back.
        return [
            f"{PRESENTER_DECLARATION}: `{PRESENTER_FIELD}` is gone. RRC-010 kept it because "
            f"`{PRESENTER_CONSUMER}` is a working, tested consumer with no other route to a frame. "
            "Removing it is an owner decision that must also retire the TERMINAL presentation "
            "path and update plans/plan_renderer_cleanup.md RRC-010."]

    read_site = REPO_ROOT / PRESENTER_READ_SITE
    if not read_site.is_file() or PRESENTER_FIELD not in read_site.read_text(encoding="utf-8"):
        failures.append(
            f"{PRESENTER_READ_SITE}: no longer reads `{PRESENTER_FIELD}`. That read is the only "
            "thing that creates a platform surface presenter; without it the field is inert and "
            "the TERMINAL output path is unreachable by construction.")

    for required, why in ((PRESENTER_CONTRACT, "the CPU-frame presentation contract"),
                          (PRESENTER_CONSUMER, "the terminal presenter that consumes it")):
        if not (REPO_ROOT / required).is_file():
            failures.append(
                f"{required}: missing -- {why}. RRC-010's reason for keeping "
                f"`{PRESENTER_FIELD}` was that this code exists and is tested.")
    return failures


def main() -> int:
    groups = (
        ("RRC-009 removed C++/C ABI surface is absent from active code", check_removed_api_absent),
        ("RRC-009 active documentation does not offer it", check_removed_api_not_documented_as_available),
        ("RRC-009 generated ABI baseline was regenerated", check_abi_baseline_clean),
        ("RRC-010 needsSurfacePresenter's justification still holds", check_presenter_justification_intact),
    )

    failures: list[str] = []
    for label, check in groups:
        found = check()
        print(f"{'FAIL' if found else 'ok  '}  {label}")
        failures.extend(found)

    if failures:
        print(f"\n{len(failures)} problem(s):", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("\nOK: the renderer-curation API decisions (RRC-009, RRC-010) are intact.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
