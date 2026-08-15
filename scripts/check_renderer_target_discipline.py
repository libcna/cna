#!/usr/bin/env python3
"""RENDERER_TARGET discipline gate (plan_runtimerenderer.md RTR-P6-3).

`RENDERER_TARGET` is a SCALAR naming one renderer target. In a single-renderer build that is the
only renderer there is, so any misuse of it is invisible; in a multi-renderer build it names the
DEFAULT renderer, and a family that reads it gets someone else's target.

That used to be handled by overwriting the scalar before entering each family and restoring it
afterwards, which made every family's correctness depend on an unwritten rule -- "no read may
happen outside that loop" -- that nothing enforced and that a single-renderer build could never
reveal. RTR-P6-6 replaced it: `cna_add_renderer()` / `cna_renderer_target_name()` derive a family's
target from the directory its own CMakeLists.txt lives in, and publish it into that file's scope.

This script keeps that true. It fails on:

  1. a renderer family reading ${RENDERER_TARGET} before establishing its own target
  2. a NEW file outside the allowlist reading the scalar -- outside a family directory the scalar
     means "the build default", which is a legitimate but easily-mistaken thing to want, so each
     such file is listed here on purpose rather than tolerated silently
  3. re-pointing the scalar from the per-family loop, which is the workaround this replaced

Exit codes: 0 ok, 1 violation.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Files outside modules/renderers/<family>/ that legitimately mean "the DEFAULT renderer" when they
# read the scalar. Each is a deliberate entry, not a leftover: adding to this list should be a
# decision, which is the point of having it.
ALLOWED_OUTSIDE = {
    "cmake/RendererSelection.cmake",      # defines it, per identity, and leaves it at the default
    "modules/renderers/CMakeLists.txt",   # the helpers that derive and publish it
    "modules/CMakeLists.txt",             # links the default renderer into the framework
    "modules/graphics/examples/CMakeLists.txt",   # examples run the build default (RTR-P9-13)
}

ESTABLISHES = re.compile(r"cna_add_renderer\s*\(|cna_renderer_target_name\s*\(")
READS = re.compile(r"\$\{RENDERER_TARGET\}")
REPOINTS = re.compile(r"^\s*set\s*\(\s*RENDERER_TARGET\b")


def check_families() -> list[str]:
    """Rule 1: no family may read the scalar before it establishes its own target."""
    problems = []
    for cml in sorted((ROOT / "modules" / "renderers").glob("*/CMakeLists.txt")):
        lines = cml.read_text().splitlines()
        established = None
        for n, line in enumerate(lines):
            if line.lstrip().startswith("#"):
                continue
            if ESTABLISHES.search(line):
                established = n
                break
        for n, line in enumerate(lines):
            if line.lstrip().startswith("#") or not READS.search(line):
                continue
            if established is None or n < established:
                rel = cml.relative_to(ROOT)
                problems.append(
                    f"{rel}:{n + 1}: reads ${{RENDERER_TARGET}} before this family establishes its "
                    f"own target. In a multi-renderer build that is the DEFAULT renderer's target, "
                    f"not this family's. Call cna_add_renderer() first, or derive the name with "
                    f"cna_renderer_target_name(<out_var>)."
                )
    return problems


def check_outside() -> list[str]:
    """Rule 2: only allowlisted files outside a family may read the scalar."""
    problems = []
    for path in sorted(list(ROOT.rglob("CMakeLists.txt")) + list((ROOT / "cmake").rglob("*.cmake"))):
        rel = path.relative_to(ROOT).as_posix()
        if rel.split("/")[0].startswith(("cmake-build", "build")):
            continue
        if rel.startswith("modules/renderers/") and rel.count("/") == 3:
            continue  # a family's own file: rule 1 governs it
        if rel in ALLOWED_OUTSIDE:
            continue
        # An examples/ CMakeLists.txt links its executables against the build DEFAULT, and that is
        # correct rather than tolerated: RTR-P9-13 checked all ~40 renderer families and every one
        # gates its example block on equality with CNA_GRAPHICS_RENDERER, never on membership of
        # CNA_GRAPHICS_RENDERERS. An example target therefore only exists when its family is the
        # default, and the binary it produces can only run that renderer.
        if rel.endswith("/examples/CMakeLists.txt"):
            continue
        for n, line in enumerate(path.read_text().splitlines(), 1):
            if line.lstrip().startswith("#") or not READS.search(line):
                continue
            problems.append(
                f"{rel}:{n}: reads ${{RENDERER_TARGET}} outside a renderer family. That scalar "
                f"names the BUILD DEFAULT, which is rarely what a caller means in a multi-renderer "
                f"build. Use CNA_RENDERER_TARGETS to reach every compiled-in renderer, or add this "
                f"file to ALLOWED_OUTSIDE in {pathlib.Path(__file__).name} if the default really is "
                f"what is wanted."
            )
    return problems


def check_loop() -> list[str]:
    """Rule 3: the per-family loop must not re-point the scalar again."""
    problems = []
    cml = ROOT / "modules" / "renderers" / "CMakeLists.txt"
    inside_loop = False
    for n, line in enumerate(cml.read_text().splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("foreach(_cna_family_index"):
            inside_loop = True
        elif stripped.startswith("endforeach()") and inside_loop:
            inside_loop = False
        elif inside_loop and REPOINTS.match(line):
            problems.append(
                f"modules/renderers/CMakeLists.txt:{n}: re-points RENDERER_TARGET inside the "
                f"per-family loop. That is the workaround RTR-P6-6 replaced -- it makes every "
                f"family's correctness depend on no read happening outside this loop, which a "
                f"single-renderer build can never reveal. Families derive their own target now."
            )
    return problems


def main() -> int:
    problems = check_families() + check_outside() + check_loop()
    if problems:
        print("RENDERER_TARGET discipline violations:\n")
        for problem in problems:
            print(f"  {problem}\n")
        return 1
    print("RENDERER_TARGET discipline: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
