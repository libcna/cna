#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""
PLAT-3 / PLAT-76 - audit which renderers legitimately require SDL3, and which only look like they do.

Purpose
-------
plan_platform.md allows exactly one class of exception to "no SDL outside modules/platform":
renderers whose identity *is* an SDL3 API. PLAT-3 establishes that allowlist by auditing all 46
renderer identities; PLAT-76 re-runs the same audit at the end of Phase 4 to prove nothing else
survived. Same tool, two moments -- which is why this is a script with a --check mode rather than
a paragraph in a document.

Verdicts
--------
Every renderer family is placed in exactly one bucket, from measured usage:

  sdl-native        Its identity is an SDL3 API. Calls SDL_GPU*/SDL_Render* as its actual
                    rendering path. Allowlisted permanently.
  sdl-upstream      Its own sources are SDL-free (or nearly), but the third-party library it wraps
                    links SDL3 itself. Allowlisted for a dependency reason, not a source reason --
                    a distinction worth keeping, because it cannot be fixed by migrating CNA code.
  cpu-presentation  Rasterises on the CPU and uses SDL_Renderer purely to get finished pixels onto
                    the window. NOT an SDL renderer by identity. Needs a platform presentation
                    service; see the note this tool prints.
  interface-leak    Names only a bare SDL_Window type inherited from the common creation or
                    registry boundary, without using a window service. This bucket is expected to
                    stay empty after PLAT-59; PLAT-58/PLAT-61 remove the remaining common types.
  migratable        Uses only platform services (native handle, GL context, Vulkan surface,
                    window, events). Freed by its Phase 4 task.

The interface-leak bucket is the reason this audit exists: counted naively, *every* renderer looks
SDL-coupled, including STUB and HEADLESS, which do no rendering at all.

Usage
-----
    python3 tools/platform/renderer_sdl_audit.py                 # markdown report
    python3 tools/platform/renderer_sdl_audit.py --format csv
    python3 tools/platform/renderer_sdl_audit.py --out docs/platform-renderer-sdl-audit.md
    python3 tools/platform/renderer_sdl_audit.py --check         # PLAT-76 gate
"""

from __future__ import annotations

import argparse
import csv
import io
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sdl_classify import classify_symbol  # noqa: E402
from sdl_inventory import scan  # noqa: E402

# Renderers whose identity is an SDL3 API. This is the allowlist plan_platform.md's design
# decision 6 permits; PLAT-76's --check asserts nothing else has joined it.
SDL_NATIVE = frozenset({"sdl-renderer", "sdl-gpu"})

# Renderers allowlisted because the third-party library they wrap links SDL3 itself. CNA's own
# sources here contain only small integration seams. Free-direct creates and owns an internal
# SDL_Renderer that CNA never sees (its CMakeLists resolves SDL3::SDL3 from CNA's vendored targets),
# and FNA3D links SDL3 upstream the same way. Migrating CNA code cannot remove either dependency,
# which is precisely why they are a separate verdict from sdl-native.
SDL_UPSTREAM = frozenset({"fna3d", "freedirect"})

# SDL_Renderer calls that mean "put my finished CPU pixels on the screen" rather than "render with
# SDL". A family that calls any of these, and is not sdl-native, is doing CPU presentation.
PRESENTATION_CALLS = frozenset(
    {
        "SDL_CreateRenderer",
        "SDL_DestroyRenderer",
        "SDL_CreateTexture",
        "SDL_DestroyTexture",
        "SDL_UpdateTexture",
        "SDL_LockTexture",
        "SDL_UnlockTexture",
        "SDL_RenderTexture",
        "SDL_RenderPresent",
        "SDL_RenderClear",
        "SDL_SetRenderLogicalPresentation",
        "SDL_GetRenderLogicalPresentationRect",
        "SDL_SetRenderVSync",
        "SDL_GetRenderOutputSize",
        "SDL_SetRenderClipRect",
        "SDL_SetRenderDrawColor",
        "SDL_SetRenderTarget",
        "SDL_RenderReadPixels",
        "SDL_SetTextureBlendMode",
        "SDL_SetTextureScaleMode",
    }
)

# The one SDL type still present in common renderer creation/registry plumbing after PLAT-59/60.
INTERFACE_LEAK_TYPES = frozenset({"SDL_Window"})

# PLAT-76 uses one canonical custom target property as machine-readable CMake metadata. Keeping
# this syntax deliberately narrow makes a missing declaration or an accidental fifth exception a
# gate failure instead of accepting an arbitrary mention in a comment.
REQUIRES_SDL3_CMAKE = re.compile(
    r"set_property\s*\(\s*TARGET\s+\$\{RENDERER_TARGET\}\s+"
    r"PROPERTY\s+REQUIRES_PLATFORM\s+SDL3\s*\)",
    re.MULTILINE,
)

VERDICT_ORDER = ["sdl-native", "sdl-upstream", "cpu-presentation", "interface-leak", "migratable", "sdl-free"]


_COMMENT_OR_STRING = re.compile(
    r'"(?:\\.|[^"\\])*"'  # string literal
    r"|'(?:\\.|[^'\\])*'"  # char literal
    r"|//[^\n]*"  # line comment
    r"|/\*.*?\*/",  # block comment
    re.DOTALL,
)


def strip_comments_and_strings(text: str) -> str:
    """Blank out comments and literals, keeping offsets irrelevant but line content intact.

    The inventory deliberately counts SDL named in comments -- a doc comment phrased in SDL's
    vocabulary is coupling the plan removes. A *verdict*, though, must not be decided by prose:
    html-dom's only "presentation call" is the words `SDL_SetRenderClipRect` inside a comment
    explaining what this renderer does NOT do, and freedirect's SDL_Renderer mentions are all
    commentary about the library's internals. Classifying either as a CPU presenter on that basis
    would be wrong, so the verdict path reads code only.
    """
    return _COMMENT_OR_STRING.sub(" ", text)


def parse_identity_map(repo_root: Path) -> dict[str, str]:
    """Map each CNA_GRAPHICS_RENDERER identity to its module directory, from CMake.

    Parsed from cmake/RendererSelection.cmake rather than hardcoded, so a renderer added or
    re-homed later is picked up here without this tool needing an edit. One directory may serve
    several identities (EasyGL serves five GL profiles).
    """
    selection = (repo_root / "cmake" / "RendererSelection.cmake").read_text(encoding="utf-8")

    identities: dict[str, str] = {}
    pending: list[str] = []
    for line in selection.splitlines():
        names = re.findall(r'CNA_GRAPHICS_RENDERER STREQUAL "([A-Z0-9_]+)"', line)
        if names:
            if re.match(r"\s*(el)?if\(", line):
                pending = list(names)
            else:
                pending.extend(names)
            continue
        match = re.search(r'set\(RENDERER_DIR "modules/renderers/([A-Za-z0-9_-]+)"', line)
        if match and pending:
            for name in pending:
                identities[name] = match.group(1)
            pending = []

    declared = set(re.findall(r"^option\(CNA_RENDERER_([A-Z0-9_]+)", selection, re.MULTILINE))
    missing = declared - identities.keys()
    if missing:
        raise SystemExit(
            "error: these renderer identities are declared as options but have no RENDERER_DIR "
            f"mapping this parser could find: {', '.join(sorted(missing))}"
        )
    return identities


def audit(repo_root: Path) -> tuple[list[dict[str, object]], dict[str, str]]:
    identities = parse_identity_map(repo_root)
    families_to_identities: dict[str, list[str]] = defaultdict(list)
    for identity, family in sorted(identities.items()):
        families_to_identities[family].append(identity)

    inv = scan(repo_root)
    per_family_symbols: dict[str, Counter] = defaultdict(Counter)
    per_family_code_symbols: dict[str, Counter] = defaultdict(Counter)
    for record in inv.files:
        if not (record.is_renderer and record.is_production):
            continue
        per_family_symbols[record.module].update(record.symbols)
        code = strip_comments_and_strings((repo_root / record.path).read_text(encoding="utf-8", errors="replace"))
        per_family_code_symbols[record.module].update(re.findall(r"\bSDL_[A-Za-z0-9_]+\b", code))

    rows: list[dict[str, object]] = []
    for family in sorted(families_to_identities):
        symbols = per_family_symbols.get(family, Counter())
        areas: Counter = Counter()
        for symbol, count in symbols.items():
            areas[classify_symbol(symbol)[0]] += count

        code_symbols = per_family_code_symbols.get(family, Counter())
        code_sdl_specific = {s for s in code_symbols if classify_symbol(s)[0] == "sdl-renderer-specific"}
        presentation = code_sdl_specific & PRESENTATION_CALLS

        # A real service need is any call into a platform area. SDL_Window named as a bare type does
        # not count while the common creation and registry plumbing still carries it; service calls
        # distinguish a real window dependency from a temporary common-boundary leak.
        service_areas = {"native-handle", "gl-vulkan-interop", "event", "display", "filesystem", "timing", "window"}
        real_service_symbols = {
            s for s in code_symbols
            if classify_symbol(s)[0] in service_areas and s not in INTERFACE_LEAK_TYPES
        }

        if family in SDL_NATIVE:
            verdict = "sdl-native"
        elif family in SDL_UPSTREAM:
            verdict = "sdl-upstream"
        elif not symbols:
            verdict = "sdl-free"
        elif presentation:
            verdict = "cpu-presentation"
        elif code_sdl_specific <= INTERFACE_LEAK_TYPES and not real_service_symbols:
            verdict = "interface-leak"
        else:
            verdict = "migratable"

        service_area_counts: Counter = Counter()
        for symbol in real_service_symbols:
            service_area_counts[classify_symbol(symbol)[0]] += code_symbols[symbol]
        needs = [a for a in ("native-handle", "gl-vulkan-interop", "event", "window", "display", "filesystem") if service_area_counts.get(a)]

        rows.append(
            {
                "family": family,
                "identities": " ".join(families_to_identities[family]),
                "identity_count": len(families_to_identities[family]),
                "verdict": verdict,
                "sdl_refs": sum(symbols.values()),
                "sdl_code_refs": sum(code_symbols.values()),
                "presentation_calls": " ".join(sorted(presentation)),
                "platform_services": " ".join(needs),
            }
        )

    rows.sort(key=lambda r: (VERDICT_ORDER.index(str(r["verdict"])), -int(r["sdl_refs"]), r["family"]))
    return rows, identities

def render_markdown(rows: list[dict[str, object]], identities: dict[str, str]) -> str:
    out = io.StringIO()
    by_verdict: Counter = Counter(str(r["verdict"]) for r in rows)

    out.write("# Renderer SDL audit (PLAT-3)\n\n")
    out.write(
        "**Generated** by `tools/platform/renderer_sdl_audit.py`. Regenerate with `--out`, gate with\n"
        "`--check` (PLAT-76). Do not hand-edit.\n\n"
        f"{len(identities)} renderer identities over {len(rows)} module families.\n\n"
    )

    out.write("| Verdict | Families | Meaning |\n|---|---:|---|\n")
    meanings = {
        "sdl-native": "Identity **is** an SDL3 API. Permanently allowlisted.",
        "sdl-upstream": "Own sources are effectively SDL-free; the wrapped third-party library links SDL3. Allowlisted for a dependency reason.",
        "cpu-presentation": "CPU rasteriser using SDL_Renderer only to present finished pixels. Needs a platform presentation service.",
        "interface-leak": "Names only the common boundary's bare SDL_Window type. Freed by PLAT-58/PLAT-61.",
        "migratable": "Uses platform services only (native handle, GL/Vulkan, window, events).",
        "sdl-free": "No SDL references at all.",
    }
    for verdict in VERDICT_ORDER:
        if by_verdict.get(verdict):
            out.write(f"| `{verdict}` | {by_verdict[verdict]} | {meanings[verdict]} |\n")

    out.write("\n## Per-family detail\n\n")
    out.write("| Family | Identities | Verdict | SDL refs (all / in code) | Platform services needed | Presentation calls |\n")
    out.write("|---|---|---|---:|---|---|\n")
    for row in rows:
        presentation = f"`{row['presentation_calls'].replace(' ', '`, `')}`" if row["presentation_calls"] else "—"
        services = f"`{str(row['platform_services']).replace(' ', '`, `')}`" if row["platform_services"] else "—"
        out.write(
            f"| `{row['family']}` | {row['identities']} | `{row['verdict']}` | "
            f"{row['sdl_refs']} / {row['sdl_code_refs']} | {services} | {presentation} |\n"
        )

    allowlist = sorted(str(r["family"]) for r in rows if r["verdict"] in ("sdl-native", "sdl-upstream"))
    presenters = sorted(str(r["family"]) for r in rows if r["verdict"] == "cpu-presentation")
    leaks = sorted(str(r["family"]) for r in rows if r["verdict"] == "interface-leak")

    out.write("\n## Findings\n\n")
    out.write(
        f"1. **The allowlist is {len(allowlist)}, not three:** "
        + ", ".join(f"`{f}`" for f in allowlist)
        + ". `sdl-renderer` and `sdl-gpu` are SDL3 APIs by identity. `fna3d` and `freedirect` wrap\n"
        "   third-party libraries that link SDL3 themselves — free-direct creates and owns an\n"
        "   internal `SDL_Renderer` CNA never sees — so no amount of migrating CNA code removes\n"
        "   their dependency. That is a different kind of exception and is recorded as one.\n\n"
    )
    if presenters:
        out.write(
            f"2. **{len(presenters)} CPU rasterisers present through `SDL_Renderer`:** "
            + ", ".join(f"`{f}`" for f in presenters)
            + ".\n   They are not SDL renderers by identity — they rasterise on the CPU and then use\n"
            "   `SDL_CreateTexture`/`SDL_UpdateTexture`/`SDL_RenderTexture`/`SDL_RenderPresent` purely\n"
            "   to get finished pixels onto the window, with letterbox scaling and vsync. The platform\n"
            "   contract as originally drafted had nowhere for this to go, which would have left them\n"
            "   permanently allowlisted for want of an interface. **Present a CPU pixel buffer to a\n"
            "   window** is a genuine platform capability (SDL2 has it, SDL 1.2 has it as a software\n"
            "   surface, Win32 has `StretchDIBits`), so it belongs in the contract.\n\n"
        )
    else:
        out.write(
            "2. **No CPU rasteriser still presents through `SDL_Renderer`.** CPU renderers hand one\n"
            "   finished frame to `IPlatformSurfacePresenter`; native upload, scaling and vsync now\n"
            "   remain behind the selected platform implementation.\n\n"
        )
    if leaks:
        out.write(
            f"3. **{len(leaks)} renderers are coupled only by the remaining common boundary:** "
            + ", ".join(f"`{f}`" for f in leaks)
            + ".\n   They name only the bare `SDL_Window` type still carried by renderer creation or\n"
            "   the registry, without calling a window service. PLAT-58/PLAT-61 remove that last\n"
            "   common-boundary type.\n"
        )
    else:
        out.write(
            "3. **PLAT-59 closed the interface-only renderer coupling.** `HEADLESS`, `PORTABLEGL`,\n"
            "   `SOFTWARE` and `STUB` are now SDL-free. `IGraphicsRenderer` exposes neither native\n"
            "   window/renderer getter and no longer names `SDL_Renderer`; PLAT-60 had already\n"
            "   removed its `SDL_Texture*` method.\n"
        )

    return out.getvalue()

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--format", choices=("markdown", "csv"), default="markdown")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--check", action="store_true", help="PLAT-76 gate: assert the allowlist is exactly the expected set")
    args = parser.parse_args(argv)

    repo_root: Path = args.repo.resolve()
    rows, identities = audit(repo_root)

    if args.check:
        allowlisted = {str(r["family"]) for r in rows if r["verdict"] in ("sdl-native", "sdl-upstream")}
        expected = SDL_NATIVE | SDL_UPSTREAM
        still_coupled = {str(r["family"]) for r in rows if r["verdict"] in ("cpu-presentation", "interface-leak", "migratable")}
        renderer_root = repo_root / "modules" / "renderers"
        declared = {
            family_dir.name
            for family_dir in renderer_root.iterdir()
            if family_dir.is_dir()
            and (cmake_file := family_dir / "CMakeLists.txt").is_file()
            and REQUIRES_SDL3_CMAKE.search(cmake_file.read_text(encoding="utf-8"))
        }

        if allowlisted != expected:
            print(f"allowlist drift: expected {sorted(expected)}, found {sorted(allowlisted)}", file=sys.stderr)
            return 1
        if still_coupled:
            print(
                f"{len(still_coupled)} renderer families still reference SDL outside the allowlist:\n  "
                + ", ".join(sorted(still_coupled)),
                file=sys.stderr,
            )
            return 1
        if declared != expected:
            print(
                "REQUIRES_PLATFORM SDL3 drift: "
                f"expected {sorted(expected)}, found {sorted(declared)}",
                file=sys.stderr,
            )
            return 1
        print(
            f"allowlist and REQUIRES_PLATFORM SDL3 declarations are exactly {sorted(expected)}; "
            "no other renderer family references SDL."
        )
        return 0

    if args.format == "csv":
        buffer = io.StringIO()
        writer = csv.DictWriter(buffer, fieldnames=list(rows[0].keys()), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
        text = buffer.getvalue()
    else:
        text = render_markdown(rows, identities)

    if args.out:
        out_path = args.out if args.out.is_absolute() else (Path.cwd() / args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(text, encoding="utf-8")
        print(f"wrote {len(rows)} renderer families to {out_path}")
    else:
        sys.stdout.write(text)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
