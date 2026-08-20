#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Renderer-descriptor integrity gate.

Every renderer family owns one `GraphicsRendererDescriptor` translation unit -- the
pre-construction contract `GraphicsDevice` consults before a window or a renderer exists
(plan_runtimerenderer.md design decision 2). `scripts/check_runtime_renderer_discipline.py`
already checks that each family HAS one and that its factory is family-namespaced, but it does
that with regular expressions: it never parses the file, let alone compiles it.

That gap was not theoretical. `BgfxRendererDescriptor.cpp`, `LlglRendererDescriptor.cpp` and
`OpenGL1RendererDescriptor.cpp` sat on `next` with unbalanced braces -- one of them containing a
literal `return 0;` at namespace scope -- so `-DCNA_GRAPHICS_RENDERER=BGFX|LLGL|OPENGL1` could not
compile at all, while the discipline gate reported "OK: 45 renderer families". A gate that cannot
see a syntax error is not a gate over syntax.

The build system now closes the hole where it belongs: `cmake/RendererDescriptorGate.cmake`
defines `cna_renderer_descriptor_gate`, an OBJECT library that compiles -- with the project's real
flags -- every registered family's descriptor that this configuration would not otherwise touch,
and fails to configure if a registered identity has no descriptor in the matching namespace. That
target is part of `all`, so an ordinary build of ANY renderer compiles all 45.

This script is the part of the gate that runs without configuring a build, and the part that sees
what a compiler cannot:

  1. COMPILE (fast pre-check). Each descriptor is compiled with `-fsyntax-only`, the framework
     include roots and that family's own identity define -- the same inputs its real build gives
     it. It needs no toolchain setup and no third-party SDK, so it can run as the first step of a
     CI job, before anything is configured. It also covers the one family the CMake gate defers
     (the configuration's OWN selected family already compiles its descriptor for real, but only
     in that configuration).

  2. STRUCTURE. Brace balance and "no statement directly inside a namespace body", for EVERY
     descriptor including the ones layer 1 cannot reach.

Layer 2 is not redundant with a compile. A stray closing brace mid-file closes the family
namespace early and leaves `GetDescriptor` at global scope: that still COMPILES, and only the
generated registry's `Bgfx::GetDescriptor()` call fails, at link time, in the one configuration
that selects BGFX. That is precisely the bgfx defect, and it is the reason a structural rule about
where a descriptor's declarations live is worth stating separately from "it parses".

Layer 2 also covers the two descriptors that legitimately cannot be compiled on an arbitrary host:
`bgfx` includes `bgfx/bgfx.h` and `directx9` reaches `d3d9.h` through its capability headers. That
set is DISCOVERED, never declared -- a family drops to structure-only when, and only when, the
compiler reports a missing include naming a header outside this repository. Any other compiler
error is a hard failure. So a new renderer is covered the day it lands, and a family cannot be
quietly excused by editing a list.

Usage:  python3 scripts/check_renderer_descriptors.py [--cxx g++] [--verbose]
Exit:   0 when every descriptor is intact, 1 otherwise.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


# --------------------------------------------------------------------------------------------
# The family list, derived rather than restated
# --------------------------------------------------------------------------------------------

def registry_identities() -> dict[str, str]:
    """Identity -> implementing namespace, read from cmake/RendererRegistry.cmake's own map.

    Derived on purpose. A second hand-maintained list is how `TINYGL`, `IGL` and `PIXIJS` each
    reached the enum while a checker went on describing 46 identities.
    """
    text = (REPO / "cmake" / "RendererRegistry.cmake").read_text(encoding="utf-8")
    # The map's own closing paren terminates it -- entries carry no ')' of their own.
    body = re.search(r"set\(_map\b(.*?)\)", text, re.S)
    if not body:
        sys.exit("check_renderer_descriptors: cannot find the identity map in RendererRegistry.cmake")
    pairs = re.findall(r"([A-Z0-9_]+)\s+([A-Za-z][\w|]*)", body.group(1))
    return {identity: entry.split("|")[0] for identity, entry in pairs}


def identity_defines() -> dict[str, list[str]]:
    """Renderer directory -> the CNA_RENDERER_* defines its own build gives that family.

    Read out of cmake/RendererSelection.cmake, arm by arm, so the answer is the build system's
    rather than this script's. EasyGL is the one family serving several identities; it also
    announces its own implementation define, which its sources guard on.
    """
    text = (REPO / "cmake" / "RendererSelection.cmake").read_text(encoding="utf-8")
    # An arm may name several identities (`OPENGLES2 OR OPENGLES3 OR ...` -- EasyGL serves five
    # public GL profiles from one directory), so an arm is opened by the FIRST STREQUAL of a
    # condition and closed by the next arm's.
    arms = [(m.start(), m.group(1)) for m in re.finditer(
        r"(?:else)?if\s*\(\s*CNA_GRAPHICS_RENDERER\s+STREQUAL\s+\"([A-Z0-9_]+)\"", text)]
    out: dict[str, list[str]] = {}
    for index, (start, identity) in enumerate(arms):
        end = arms[index + 1][0] if index + 1 < len(arms) else len(text)
        arm = text[start:end]
        directory = re.search(r'set\(RENDERER_DIR\s+"modules/renderers/([\w-]+)"', arm)
        if not directory:
            continue
        defines = re.findall(r"list\(APPEND\s+_cna_identity_defines\s+(CNA_RENDERER_[A-Z0-9_]+)", arm)
        if not defines:
            defines = [f"CNA_RENDERER_{identity}"]
        out.setdefault(directory.group(1), sorted(set(defines)))
    return out


def descriptor_files() -> list[Path]:
    return sorted((REPO / "modules" / "renderers").glob("*/src/*RendererDescriptor.cpp"))


# --------------------------------------------------------------------------------------------
# Layer 2: structure, with no dependencies at all
# --------------------------------------------------------------------------------------------

def strip_comments_and_strings(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    source = re.sub(r"//[^\n]*", "", source)
    source = re.sub(r'"(\\.|[^"\\])*"', '""', source)
    source = re.sub(r"'(\\.|[^'\\])'", "' '", source)
    return source


def structural_problems(path: Path) -> list[str]:
    code = strip_comments_and_strings(path.read_text(encoding="utf-8"))
    problems: list[str] = []

    balance = code.count("{") - code.count("}")
    if balance:
        problems.append(
            f"unbalanced braces ({balance:+d}); the translation unit cannot parse regardless of "
            f"include paths")

    stray = re.search(r"namespace[^\n{]*\n?\s*\{\s*\n\s*(return\b|;)", code)
    if stray:
        line = code[: stray.start()].count("\n") + 1
        problems.append(f"a statement sits directly inside a namespace body (around line {line})")

    return problems


# --------------------------------------------------------------------------------------------
# Layer 1: a real front-end parse
# --------------------------------------------------------------------------------------------

def include_roots() -> list[str]:
    roots = [str(p) for p in sorted((REPO / "modules").glob("*/include"))]
    roots += [str(p) for p in sorted((REPO / "modules" / "renderers").glob("*/include"))]

    # sharp-runtime is a sibling checkout, not a submodule (see the top-level CMakeLists.txt).
    sharp = os.environ.get("CNA_SHARP_RUNTIME_ROOT") or str(REPO.parent / "sharp-runtime")
    roots += [str(p) for p in sorted(Path(sharp).glob("modules/*/include"))]
    return [f"-I{root}" for root in roots]


MISSING_INCLUDE = re.compile(r"fatal error: ([^:]+): No such file or directory")


def compile_check(cxx: str, path: Path, defines: list[str], roots: list[str]):
    """Returns (status, detail) with status in {ok, unreachable, broken}."""
    command = [cxx, "-std=c++23", "-fsyntax-only"]
    command += [f"-D{define}" for define in defines]
    command += roots
    command.append(str(path))
    finished = subprocess.run(command, capture_output=True, text=True)
    if finished.returncode == 0:
        return "ok", ""

    missing = MISSING_INCLUDE.search(finished.stderr)
    if missing:
        header = missing.group(1)
        # A header this repository owns is never a legitimate excuse: that is a broken include
        # path or a genuinely missing file, not a third-party toolchain the host lacks.
        if not list(REPO.rglob(header.split("/")[-1])):
            return "unreachable", header

    first = next((line for line in finished.stderr.splitlines() if " error: " in line), "")
    return "broken", first or finished.stderr.strip().splitlines()[0]


# --------------------------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo_root", nargs="?", type=Path,
                        help="repository root (defaults to the script's parent repository)")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    global REPO
    if args.repo_root is not None:
        REPO = args.repo_root.resolve()

    files = descriptor_files()
    defines_by_dir = identity_defines()
    roots = include_roots()

    failures: list[str] = []
    compiled: list[str] = []
    structure_only: list[tuple[str, str]] = []

    for path in files:
        family = path.parts[-3]

        for problem in structural_problems(path):
            failures.append(f"{path.relative_to(REPO)}: {problem}")

        defines = defines_by_dir.get(family)
        if defines is None:
            failures.append(
                f"{path.relative_to(REPO)}: no arm of cmake/RendererSelection.cmake selects this "
                f"family, so nothing says which CNA_RENDERER_* define its sources are built with")
            continue

        status, detail = compile_check(args.cxx, path, defines, roots)
        if status == "ok":
            compiled.append(family)
        elif status == "unreachable":
            structure_only.append((family, detail))
        else:
            failures.append(f"{path.relative_to(REPO)}: does not compile -- {detail}")

    # Every registered identity must reach a descriptor: a renderer that cannot be constructed is
    # not a renderer, and this is the half the discipline gate cannot see.
    namespaces = set(registry_identities().values())
    seen = {path.name[: -len("RendererDescriptor.cpp")] for path in files}
    for namespace in sorted(namespaces):
        if namespace not in seen:
            failures.append(
                f"registry identity namespace '{namespace}' has no {namespace}RendererDescriptor.cpp")

    print(f"descriptors: {len(files)} found, {len(compiled)} compiled, "
          f"{len(structure_only)} structure-only")
    for family, header in sorted(structure_only):
        print(f"  structure-only: {family} (needs <{header}>, which this host does not provide)")
    if args.verbose:
        for family in sorted(compiled):
            print(f"  compiled: {family}")

    if failures:
        print()
        for failure in failures:
            print(f"FAIL {failure}")
        print(f"\n{len(failures)} descriptor problem(s).")
        return 1

    print("OK: every renderer descriptor parses, and every registered identity has one.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
