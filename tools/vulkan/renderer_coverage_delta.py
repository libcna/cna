#!/usr/bin/env python3
"""plan_vulkan.md VULKAN-007 -- the EasyGL/Vulkan CTest coverage comparison, re-runnable.

Section 9.3's classification rests on one number -- how many EasyGL CTests have no Vulkan
equivalent -- and that number was produced once, by hand, on one commit.  VULKAN-487 has to compute
what EasyGL gained since; re-deriving the whole comparison by hand is exactly the step that gets
skipped under time pressure.  So this script produces the three sets §7.4 names, from the two
`examples/CMakeLists.txt` files themselves.

Equivalence is by SOURCE FILE, not by test name, and that is the decision this whole tool rests on:
this campaign's method is to register EasyGL's own source on Vulkan wherever the behaviour is
portable, so two tests that share a source are the same measurement under two renderers, whatever
they are called.  Comparing names would count `EasyGL_Foo` and `Vulkan_Foo` as unrelated and would
miss every registration made the other way round.

Non-goal, stated in the row and honoured here: this is not a CTest.  It reads text and prints; it
builds nothing, configures nothing, and has no side effects.

READ THE "no Vulkan equivalent" NUMBER CAREFULLY (plan_vulkan.md VULKAN-208, 2026-09-07).  It is a
count of SOURCES, and it is an UPPER BOUND on the coverage gap, not the gap.  This campaign often
wrote a Vulkan-native test of the same subject rather than registering EasyGL's source -- so
`EasyGL_BasicEffect_OneLight` and `Vulkan_BasicEffect_OneLight` are two sources and count as a gap
here while measuring the same behaviour.  The complementary sweep is by NAME, which has the opposite
bias (`EasyGL_VertexFormats_AllStrides` vs `Vulkan_VertexFormat_AllStrides` are the same subject
under two spellings, and count as a gap by name).  The real set is the intersection of the two,
minus the GLSL `*_shader_test.cpp` sources this renderer cannot take by contract -- 30 rows when
VULKAN-208 computed it, of which 6 were genuine and are now registered.  Neither sweep alone is a
finding; §10's parity matrix is what says which residual is accepted and why.

Usage:
    tools/vulkan/renderer_coverage_delta.py            # summary counts
    tools/vulkan/renderer_coverage_delta.py --list easygl-only
    tools/vulkan/renderer_coverage_delta.py --list vulkan-only
    tools/vulkan/renderer_coverage_delta.py --list shared
    tools/vulkan/renderer_coverage_delta.py --json     # machine-readable, for VULKAN-487
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CMAKELISTS = {
    "easygl": REPO / "modules/renderers/easygl/examples/CMakeLists.txt",
    "vulkan": REPO / "modules/renderers/vulkan/examples/CMakeLists.txt",
}

# cna_easygl_test(<target> <source> [...])  /  cna_vulkan_test(<target> <source> [...])
# The source may sit on the next line and may be an ${CMAKE_SOURCE_DIR}/... or
# ${CNA_GRAPHICS_EXAMPLES_DIR}/... path; both are normalised to a repo-relative path below.
TARGET_RE = re.compile(
    r"\bcna_(?:easygl|vulkan)_test\s*\(\s*([A-Za-z0-9_]+)\s+([^\s)]+)", re.MULTILINE)
# cna_register_renderer_test(NAME <test> COMMAND <target> ...)
REGISTER_RE = re.compile(
    r"\bcna_register_renderer_test\s*\(\s*NAME\s+([A-Za-z0-9_]+)\s+COMMAND\s+([A-Za-z0-9_]+)",
    re.MULTILINE)

EXAMPLES_DIRS = {
    "easygl": "modules/renderers/easygl/examples",
    "vulkan": "modules/renderers/vulkan/examples",
}


def normalise_source(raw: str, renderer: str) -> str:
    """Repo-relative path for a source as written in a CMakeLists."""
    text = raw.strip().strip('"')
    text = text.replace("${CMAKE_SOURCE_DIR}/", "")
    text = text.replace("${CNA_GRAPHICS_EXAMPLES_DIR}/", "modules/graphics/examples/")
    text = text.replace("${CMAKE_CURRENT_SOURCE_DIR}/", EXAMPLES_DIRS[renderer] + "/")
    if not text.startswith("modules/"):
        text = f"{EXAMPLES_DIRS[renderer]}/{text}"
    return text


def read(renderer: str) -> tuple[dict[str, str], list[str]]:
    """(test name -> repo-relative source, registrations this script could not map).

    The second half matters: a registration whose COMMAND target was built by something other than
    `cna_<renderer>_test` -- a demo executable, say -- has no source this script can see, and
    silently dropping it would understate every set below. They are counted and printed instead.
    """
    path = CMAKELISTS[renderer]
    text = path.read_text(encoding="utf-8")
    target_source = {t: normalise_source(s, renderer) for t, s in TARGET_RE.findall(text)}
    tests: dict[str, str] = {}
    unmapped: list[str] = []
    for test_name, target in REGISTER_RE.findall(text):
        source = target_source.get(target)
        if source is None:
            unmapped.append(f"{test_name} (COMMAND {target})")
        else:
            tests[test_name] = source
    return tests, sorted(unmapped)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--list", choices=("shared", "easygl-only", "vulkan-only"),
                        help="print one set, one entry per line, instead of the summary")
    parser.add_argument("--json", action="store_true", help="print all three sets as JSON")
    args = parser.parse_args(argv)

    easygl, easygl_unmapped = read("easygl")
    vulkan, vulkan_unmapped = read("vulkan")
    easygl_sources = set(easygl.values())
    vulkan_sources = set(vulkan.values())

    shared = sorted(easygl_sources & vulkan_sources)
    easygl_only = sorted(name for name, src in easygl.items() if src not in vulkan_sources)
    vulkan_only = sorted(name for name, src in vulkan.items() if src not in easygl_sources)

    if args.json:
        json.dump({"easygl_tests": len(easygl), "vulkan_tests": len(vulkan),
                   "shared_sources": shared, "easygl_only_tests": easygl_only,
                   "vulkan_only_tests": vulkan_only,
                   "unmapped_easygl": easygl_unmapped,
                   "unmapped_vulkan": vulkan_unmapped}, sys.stdout, indent=2)
        print()
        return 0

    if args.list == "shared":
        print("\n".join(shared))
        return 0
    if args.list == "easygl-only":
        print("\n".join(easygl_only))
        return 0
    if args.list == "vulkan-only":
        print("\n".join(vulkan_only))
        return 0

    print(f"registered CTests            EasyGL {len(easygl):4d}   Vulkan {len(vulkan):4d}")
    print(f"sources registered on both   {len(shared):4d}")
    print(f"EasyGL CTests with no Vulkan equivalent   {len(easygl_only):4d}")
    print(f"Vulkan CTests with no EasyGL equivalent   {len(vulkan_only):4d}")
    if easygl_unmapped or vulkan_unmapped:
        print()
        print("not counted above -- registrations whose COMMAND target this script cannot")
        print("trace to a source, because it was not built by cna_<renderer>_test:")
        for entry in easygl_unmapped:
            print(f"  EasyGL  {entry}")
        for entry in vulkan_unmapped:
            print(f"  Vulkan  {entry}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
