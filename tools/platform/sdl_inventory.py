#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""
PLAT-1 - machine-readable inventory of CNA's direct SDL coupling.

Purpose
-------
plan_platform.md separates CNA from SDL3 behind a CNA-owned platform contract. That work needs a
number it can drive to zero, per module and per subsystem, rather than a hand-counted table that
goes stale after the first migration commit. This script is that number.

It reports, over the module tree:

  * how many distinct SDL_* identifiers CNA references;
  * how many files reference SDL, split into production (src/, include/) and test/example;
  * the same split per module, and per renderer family;
  * which SDL_PROP_WINDOW_* native-handle properties are consumed (these define the minimum
    NativeWindowHandle surface, so the struct is derived from measured need, not guessed);
  * which renderer families reach for SDL_GL_* directly (they are freed as a group by one
    IPlatformGlContext);
  * optionally, per-symbol and per-file frequency listings.

What counts as "a reference"
----------------------------
A textual occurrence of an SDL identifier (`SDL_...`, `SDL3_...`) or an `SDL3/`-rooted include
path. This is deliberately the widest reasonable definition, and it is *not* a bug that it counts
comments and forward declarations:

  * `struct SDL_Window;` in a CNA header is exactly the coupling this plan removes -- it is why
    GameWindow.hpp, GraphicsDeviceManager.hpp and IGraphicsRenderer.hpp are on the list;
  * a Doxygen comment describing behaviour in terms of `SDL_GetWindowSize()` is a contract stated
    in SDL's vocabulary, and PLAT-66 exists to restate it in platform terms.

Both must reach zero for the plan's definition of done, so both are counted. The trade-off is that
prose mentions of SDL in an explanatory comment also count; that is the conservative direction.

What deliberately does *not* count -- two classes of CNA-owned spelling that merely look like SDL:

  1. Identifiers containing "SDL_" as an infix, notably the renderer-selection macros
     `CNA_RENDERER_SDL_RENDERER` and `CNA_RENDERER_SDL_GPU`. The leading `\b` in the patterns
     below excludes them.
  2. The bare tokens `SDL_GPU` and `SDL_RENDERER` (see EXCLUDED_EXACT below). These are CNA's own
     `CNA_GRAPHICS_RENDERER` identity names, written in prose and in enum-to-string tables.
     Neither is an SDL3 API symbol -- the real API spells them `SDL_GPUDevice`, `SDL_GPUTexture`,
     `SDL_CreateRenderer` and so on, all of which still count.

Both exclusions are load-bearing rather than incidental: those spellings name CNA build options
and renderer identities, they survive the plan by design (the SDL_RENDERER, SDL_GPU and FNA3D
renderers are allowlisted), and counting them would make the plan's definition of done unreachable
by construction. Together they are worth 78 files -- a naive `grep SDL_` over the tree reports
four extra files for the first class and 74 for the second.

Usage
-----
    python3 tools/platform/sdl_inventory.py                     # markdown summary (plan §2 block)
    python3 tools/platform/sdl_inventory.py --format json
    python3 tools/platform/sdl_inventory.py --format csv --level file
    python3 tools/platform/sdl_inventory.py --symbols 40        # top-N symbol frequencies
    python3 tools/platform/sdl_inventory.py --update            # rewrite plan_platform.md §2
    python3 tools/platform/sdl_inventory.py --check             # fail if plan §2 is stale

--check is what keeps §2 generated rather than hand-maintained; --update regenerates it in place
between the BEGIN/END markers.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

# --- what the scan covers -------------------------------------------------------------------

SOURCE_SUFFIXES = {".cpp", ".hpp", ".h", ".cc", ".c", ".mm", ".m", ".inl"}

# `SDL_Foo` covers the overwhelming majority; `SDL3_image`/`SDL3_mixer` appear in include paths
# and would otherwise be missed, since they carry no `SDL_` prefix of their own.
SDL_IDENTIFIER_RE = re.compile(r"\bSDL_[A-Za-z0-9_]+\b")
SDL_ANY_RE = re.compile(r"\bSDL_[A-Za-z0-9_]+\b|\bSDL3(?:_[A-Za-z0-9_]+)?/")
WINDOW_PROPERTY_RE = re.compile(r"\bSDL_PROP_WINDOW_[A-Z0-9_]+\b")
GL_RE = re.compile(r"\bSDL_GL_[A-Za-z0-9_]+\b")

# CNA's own CNA_GRAPHICS_RENDERER identity names, which collide with SDL's namespace but are not
# SDL API. Exact-token only: SDL_GPUDevice, SDL_GPUTexture, SDL_CreateRenderer and every other
# real symbol is unaffected, as is the lowercase header name SDL_gpu (from <SDL3/SDL_gpu.h>).
EXCLUDED_EXACT = frozenset({"SDL_GPU", "SDL_RENDERER"})

PRODUCTION_AREAS = ("src", "include")
NON_PRODUCTION_AREAS = ("tests", "examples")

BEGIN_MARKER = "<!-- BEGIN GENERATED: tools/platform/sdl_inventory.py -->"
END_MARKER = "<!-- END GENERATED: tools/platform/sdl_inventory.py -->"

# Dominant concern per module. Descriptive only -- it is the human-readable half of the table and
# has no effect on any count. PLAT-2 produces the authoritative per-symbol classification.
MODULE_CONCERNS = {
    "input": "keyboard, mouse, gamepad, joystick, haptic, sensor, touch, text input",
    "devices-ext": "clipboard, message box, file dialog, tray, camera, locale, power, display, URL",
    "devices": "`Microsoft::Devices` sensors + vibrate, SDL subsystem refcounting",
    "graphics": "`GraphicsDevice`, common renderer contracts, `ImageLoader`",
    "audio": "audio device/stream, mixer, microphone",
    "runtime": "`Game` loop, `GameWindow`, `GraphicsDeviceManager`",
    "media": "`MediaPlayer`, `VideoPlayer`, library paths",
    "core": "`Logger`, `Entrypoint` (`SDL_main`), `GraphicsRendererType`",
    "content": "`SDL_IOStream`-based readers, glTF import",
    "gamer-services": "`Guide` overlay, local store",
    "storage": "`SDL_GetPrefPath`",
    "graphics-ext": "ASCII post-process effect",
    "net": "-",
    "math": "-",
}


@dataclass
class FileRecord:
    """One scanned source file that references SDL."""

    path: Path
    module: str
    is_renderer: bool
    area: str
    reference_count: int
    symbols: Counter = field(default_factory=Counter)

    @property
    def is_production(self) -> bool:
        return self.area in PRODUCTION_AREAS


@dataclass
class Inventory:
    files: list[FileRecord]
    scanned_file_count: int

    # -- headline metrics ---------------------------------------------------------------------

    @property
    def distinct_symbols(self) -> set[str]:
        out: set[str] = set()
        for record in self.files:
            out.update(record.symbols)
        return out

    @property
    def production_files(self) -> list[FileRecord]:
        return [f for f in self.files if f.is_production]

    @property
    def renderer_production_files(self) -> list[FileRecord]:
        return [f for f in self.production_files if f.is_renderer]

    @property
    def non_production_files(self) -> list[FileRecord]:
        return [f for f in self.files if not f.is_production]

    @property
    def window_properties(self) -> set[str]:
        out: set[str] = set()
        for record in self.files:
            out.update(s for s in record.symbols if WINDOW_PROPERTY_RE.fullmatch(s))
        return out

    @property
    def gl_renderer_families(self) -> set[str]:
        return {
            f.module
            for f in self.files
            if f.is_renderer and f.is_production and any(GL_RE.fullmatch(s) for s in f.symbols)
        }

    def symbol_frequencies(self) -> Counter:
        total: Counter = Counter()
        for record in self.files:
            total.update(record.symbols)
        return total

    def production_files_by_module(self) -> dict[tuple[bool, str], int]:
        counts: dict[tuple[bool, str], int] = defaultdict(int)
        for record in self.production_files:
            counts[(record.is_renderer, record.module)] += 1
        return dict(counts)


def classify(path: Path, modules_root: Path) -> tuple[str, bool, str] | None:
    """Return (module, is_renderer, area) for a path under modules/, or None if unattributable.

    modules/<name>/<area>/...              -> (<name>, False, <area>)
    modules/renderers/<family>/<area>/...  -> (<family>, True,  <area>)
    """
    try:
        parts = path.relative_to(modules_root).parts
    except ValueError:
        return None

    if len(parts) < 2:
        return None

    if parts[0] == "renderers":
        if len(parts) < 3:
            return None
        return parts[1], True, parts[2]

    return parts[0], False, parts[1]


def scan(repo_root: Path) -> Inventory:
    modules_root = repo_root / "modules"
    if not modules_root.is_dir():
        raise SystemExit(f"error: {modules_root} not found -- run this from the CNA repository")

    records: list[FileRecord] = []
    scanned = 0

    for path in sorted(modules_root.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue

        classified = classify(path, modules_root)
        if classified is None:
            continue
        module, is_renderer, area = classified
        scanned += 1

        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:  # unreadable file is a scan defect, not something to skip quietly
            raise SystemExit(f"error: cannot read {path}: {exc}") from exc

        matches = [m for m in SDL_ANY_RE.findall(text) if m not in EXCLUDED_EXACT]
        if not matches:
            continue

        symbols = Counter(s for s in SDL_IDENTIFIER_RE.findall(text) if s not in EXCLUDED_EXACT)

        records.append(
            FileRecord(
                path=path.relative_to(repo_root),
                module=module,
                is_renderer=is_renderer,
                area=area,
                reference_count=len(matches),
                symbols=symbols,
            )
        )

    return Inventory(files=records, scanned_file_count=scanned)


# --- rendering ------------------------------------------------------------------------------


def render_markdown(inv: Inventory) -> str:
    out = io.StringIO()

    out.write("| Metric | Value |\n|---|---|\n")
    out.write(f"| Distinct `SDL_*` identifiers referenced anywhere under `modules/` | **{len(inv.distinct_symbols)}** |\n")
    out.write(f"| Files referencing SDL (all) | **{len(inv.files)}** |\n")
    out.write(f"| Production files (`src/` + `include/`) referencing SDL | **{len(inv.production_files)}** |\n")
    out.write(f"| …of which are renderer production files | **{len(inv.renderer_production_files)}** |\n")
    out.write(f"| Test/example files referencing SDL | **{len(inv.non_production_files)}** |\n")
    out.write(f"| Distinct `SDL_PROP_WINDOW_*` native-handle properties read | **{len(inv.window_properties)}** |\n")
    out.write(f"| Renderer families reaching for `SDL_GL_*` directly | **{len(inv.gl_renderer_families)}** |\n")

    out.write("\nProduction SDL surface per module (`src/` + `include/` only):\n\n")
    out.write("| Module | Files | Dominant concern |\n|---|---:|---|\n")

    counts = inv.production_files_by_module()
    non_renderer = sorted(
        ((m, n) for (is_r, m), n in counts.items() if not is_r),
        key=lambda kv: (-kv[1], kv[0]),
    )
    for module, count in non_renderer:
        concern = MODULE_CONCERNS.get(module, "-")
        out.write(f"| `modules/{module}` | {count} | {concern} |\n")

    renderer_total = len(inv.renderer_production_files)
    renderer_families = sum(1 for (is_r, _), _ in counts.items() if is_r)
    out.write(
        f"| `modules/renderers/*` | {renderer_total} | native window handle, GL context, "
        f"Vulkan surface, SDL renderer/GPU ({renderer_families} families) |\n"
    )

    out.write(
        "\nThe native-window properties actually consumed today — these define the minimum\n"
        "`NativeWindowHandle` surface, so the struct is derived from measured need, not guessed:\n\n"
    )
    out.write("```text\n")
    for prop in sorted(inv.window_properties):
        out.write(f"{prop}\n")
    out.write("```\n")

    out.write("\nRenderer families calling `SDL_GL_*` directly, freed as a group by one `IPlatformGlContext`:\n\n")
    out.write("```text\n")
    out.write(" ".join(sorted(inv.gl_renderer_families)) + "\n")
    out.write("```\n")

    return out.getvalue()


def render_csv(inv: Inventory, level: str) -> str:
    out = io.StringIO()
    writer = csv.writer(out, lineterminator="\n")

    if level == "file":
        writer.writerow(["path", "module", "is_renderer", "area", "is_production", "references", "distinct_symbols"])
        for record in sorted(inv.files, key=lambda r: str(r.path)):
            writer.writerow(
                [
                    record.path,
                    record.module,
                    int(record.is_renderer),
                    record.area,
                    int(record.is_production),
                    record.reference_count,
                    len(record.symbols),
                ]
            )
    elif level == "symbol":
        writer.writerow(["symbol", "occurrences", "files"])
        file_counts: Counter = Counter()
        for record in inv.files:
            file_counts.update(record.symbols.keys())
        for symbol, occurrences in inv.symbol_frequencies().most_common():
            writer.writerow([symbol, occurrences, file_counts[symbol]])
    else:  # module
        writer.writerow(["module", "is_renderer", "production_files", "non_production_files", "references"])
        production: Counter = Counter()
        non_production: Counter = Counter()
        references: Counter = Counter()
        for record in inv.files:
            key = (record.is_renderer, record.module)
            if record.is_production:
                production[key] += 1
            else:
                non_production[key] += 1
            references[key] += record.reference_count
        for (is_renderer, module) in sorted(set(production) | set(non_production), key=lambda k: (k[0], k[1])):
            key = (is_renderer, module)
            writer.writerow([module, int(is_renderer), production[key], non_production[key], references[key]])

    return out.getvalue()


def render_json(inv: Inventory) -> str:
    counts = inv.production_files_by_module()
    return json.dumps(
        {
            "scanned_files": inv.scanned_file_count,
            "distinct_symbols": len(inv.distinct_symbols),
            "files_all": len(inv.files),
            "files_production": len(inv.production_files),
            "files_production_renderers": len(inv.renderer_production_files),
            "files_non_production": len(inv.non_production_files),
            "window_properties": sorted(inv.window_properties),
            "gl_renderer_families": sorted(inv.gl_renderer_families),
            "production_by_module": {
                ("renderers/" if is_renderer else "") + module: count
                for (is_renderer, module), count in sorted(counts.items(), key=lambda kv: (kv[0][0], kv[0][1]))
            },
        },
        indent=2,
    )


# --- plan §2 synchronisation ----------------------------------------------------------------


def splice(plan_text: str, generated: str) -> str:
    begin = plan_text.find(BEGIN_MARKER)
    end = plan_text.find(END_MARKER)
    if begin == -1 or end == -1 or end < begin:
        raise SystemExit(
            f"error: plan_platform.md is missing the generated-block markers\n"
            f"  expected {BEGIN_MARKER!r} … {END_MARKER!r}"
        )
    head = plan_text[: begin + len(BEGIN_MARKER)]
    tail = plan_text[end:]
    return f"{head}\n\n{generated}\n{tail}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2], help="repository root")
    parser.add_argument("--format", choices=("markdown", "csv", "json"), default="markdown")
    parser.add_argument("--level", choices=("module", "file", "symbol"), default="module", help="CSV granularity")
    parser.add_argument("--symbols", type=int, metavar="N", help="also print the N most frequent SDL symbols")
    parser.add_argument("--update", action="store_true", help="rewrite plan_platform.md §2 in place")
    parser.add_argument("--check", action="store_true", help="exit non-zero if plan_platform.md §2 is stale")
    args = parser.parse_args(argv)

    repo_root: Path = args.repo.resolve()
    inv = scan(repo_root)

    if args.update or args.check:
        plan_path = repo_root / "plan_platform.md"
        plan_text = plan_path.read_text(encoding="utf-8")
        updated = splice(plan_text, render_markdown(inv))

        if args.check:
            if updated != plan_text:
                print(
                    "plan_platform.md §2 is out of date with the measured inventory.\n"
                    "Regenerate it with: python3 tools/platform/sdl_inventory.py --update",
                    file=sys.stderr,
                )
                return 1
            print("plan_platform.md §2 matches the measured inventory.")
            return 0

        if updated != plan_text:
            plan_path.write_text(updated, encoding="utf-8")
            print(f"updated {plan_path.relative_to(repo_root)} §2")
        else:
            print(f"{plan_path.relative_to(repo_root)} §2 already up to date")
        return 0

    if args.format == "markdown":
        sys.stdout.write(render_markdown(inv))
    elif args.format == "csv":
        sys.stdout.write(render_csv(inv, args.level))
    else:
        sys.stdout.write(render_json(inv) + "\n")

    if args.symbols:
        print(f"\nTop {args.symbols} SDL symbols by occurrence:")
        for symbol, count in inv.symbol_frequencies().most_common(args.symbols):
            print(f"  {count:6d}  {symbol}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
