#!/usr/bin/env python3
"""Renderer-identity registry gate (MODULARIZATION_PLAN.md §2.3).

CNA has exactly 49 public renderer identities. This check mechanically compares
the two authoritative registries -- the public GraphicsRendererType enum and the
CNA_GRAPHICS_RENDERER cmake selection list -- against the canonical identity
table below. Any addition, removal or rename of a public identity fails here
until the table (and therefore the documented public count) is deliberately
updated.

It also checks the DOCUMENTED count, in the handful of documents that state one
(plan_runtimerenderer.md RTR-P13-8). A count written into prose is a fact with no
owner: TINYGL, IGL and PIXIJS were each added without it, so documents went on
saying 46 and 47 while the registry said 49, and a reader has no way to tell which
number is the live one. Correcting them by hand does not hold either -- the pass
that fixed four such documents still left three wrong, which is what this check
was written to stop. Prefer not stating a number at all; where a document really
wants one, this keeps it true.

Exit codes: 0 ok, 1 mismatch.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Documents that may state a whole-registry count, and whether one must actually be VISIBLE to the
# patterns below. Each entry is a decision: a document is better off describing the registry than
# restating its size, so adding here should be rarer than removing.
#
# The True/False matters more than it looks, and exists because tightening the patterns created a
# new way to be silently wrong. They are deliberately narrow -- "public ... identities",
# "implementation families" -- so that a legitimate sub-count ("11 renderer families need it",
# "SDL renderer/GPU (5 families)") is not reported as a stale total. The cost of that precision is
# recall: a count phrased any other way is not checked at all, and a check that cannot see a count
# leaves it exactly as unowned as one nobody wrote down. That is not hypothetical -- the first
# attempt at plan_platform.md's rule 5 wrote "is **49** today" and this check sailed straight past
# it.
#
# So a document marked True must keep stating its count in the canonical phrasing. Rewording it
# into something this check cannot see is itself a failure, reported as such.
COUNTED_DOCUMENTS = {
    "docs/runtime-renderer-selection.md": True,
    "docs/renderer-expansion-candidates.md": True,
    "docs/physical-modules.md": True,
    # plan_platform.md states the count in three load-bearing places -- rule 5 ("no task may
    # reduce renderer coverage"), PLAT-76's allowlist evidence and the definition of done -- and
    # every one of them read "46" for three identities after the count moved. It is the document
    # a reviewer checks the boundary against, so a stale number there is worse than elsewhere.
    "plan_platform.md": True,
    # These two deliberately state no total: both used to, and both now name the registry instead,
    # which is the outcome this check recommends in its own failure message. Listed rather than
    # dropped so that a count reappearing in them is still checked.
    "AUDIT.md": False,
    "CHECKLIST.md": False,
}

# "49 public renderer identities", "45 families / 49 public identities", "45 implementation
# families". Deliberately anchored on the words that mean the WHOLE registry: a legitimate
# sub-count ("the five GL identities of the easygl family") does not match, because it never
# says "public".
#
# The leading \b is load-bearing rather than decorative: without it the family pattern read the
# "12" out of "the d3d11 and d3d12 families only" and reported the repo as having 12 renderer
# families. Found by running this against the clean tree before trusting it, which is the only
# reason it is not still there.
#
# The family pattern needs a whole-registry marker for the same reason the identity pattern needs
# "public", and "renderer families" is NOT one -- it is how sub-counts are phrased too. Adding
# plan_platform.md produced three false positives in one run, each a true statement:
#
#   "SDL renderer/GPU (5 families)"                 the families touching a native window
#   "(11 renderer families need it)"                the families needing IPlatformGlContext
#   "nine across the four PLAT-76 renderer families"  the allowlisted four
#
# So the marker is "implementation families", the phrase already used for the real claim. The last
# example also exposed a second defect: `\b(\d+)` happily captured the 76 of "PLAT-76", because \b
# matches after a hyphen. Same shape as the "d3d12" near-miss above, one lookbehind away from
# reporting the repository as having 76 renderer families. A count is only checkable if the words
# around it say it is a count of everything.
IDENTITY_COUNT = re.compile(r"(?<![-\w])(\d+)\s+public\s+(?:renderer\s+)?identities")
FAMILY_COUNT = re.compile(r"(?<![-\w])(\d+)\s+implementation\s+families\b")

# Canonical public identities: (cmake selection name, enum name). 49 entries.
IDENTITIES = [
    ("SDL_RENDERER", "SdlRenderer"),
    ("OPENGLES2", "OpenGLES2"),
    ("OPENGLES3", "OpenGLES3"),
    ("OPENGL33", "OpenGL33"),
    ("WEBGL1", "WebGL1"),
    ("WEBGL2", "WebGL2"),
    ("BGFX", "Bgfx"),
    ("VULKAN", "Vulkan"),
    ("WEBGPU", "WebGPU"),
    ("MAGNUM", "Magnum"),
    ("HEADLESS", "Headless"),
    ("SOFTWARE", "Software"),
    ("STUB", "Stub"),
    ("DIRECTX11", "DirectX11"),
    ("DIRECTX12", "DirectX12"),
    ("DIRECT2D", "Direct2D"),
    ("CANVAS", "Canvas"),
    ("HTML_DOM", "HtmlDom"),
    ("SKIA", "Skia"),
    ("BLEND2D", "Blend2D"),
    ("FREEDIRECT", "FreeDirect"),
    ("DIRECTX9", "DirectX9"),
    ("DIRECTX1", "DirectX1"),
    ("DIRECTX2", "DirectX2"),
    ("DIRECTX3", "DirectX3"),
    ("DIRECTX5", "DirectX5"),
    ("DIRECTX6", "DirectX6"),
    ("DIRECTX7", "DirectX7"),
    ("DIRECTX8", "DirectX8"),
    ("DIRECTX10", "DirectX10"),
    ("SDL_GPU", "SdlGpu"),
    ("OPENGLES1", "OpenGLES1"),
    ("OPENGL4", "OpenGL4"),
    ("OPENGL1", "OpenGL1"),
    ("OPENGL2", "OpenGL2"),
    ("WICKED", "Wicked"),
    ("SOKOL", "Sokol"),
    ("DILIGENT", "Diligent"),
    ("GLIDE", "Glide"),
    ("GDI", "Gdi"),
    ("LLGL", "Llgl"),
    ("METAL", "Metal"),
    ("FNA3D", "Fna3d"),
    ("SVG_DOM", "SvgDom"),
    ("OPENVG", "OpenVg"),
    ("PORTABLEGL", "PortableGL"),
    ("TINYGL", "TinyGL"),
    ("IGL", "Igl"),
    ("PIXIJS", "PixiJs"),
]


def enum_identities():
    path = os.path.join(REPO, "modules", "core", "include", "CNA", "GraphicsRendererType.hpp")
    text = open(path, encoding="utf-8").read()
    body = re.search(r"enum class GraphicsRendererType\s*\{(.*?)\n\s*\};", text, re.S)
    if not body:
        sys.exit("cannot locate enum class GraphicsRendererType")
    stripped = re.sub(r"/\*.*?\*/|//[^\n]*", "", body.group(1), flags=re.S)
    return re.findall(r"\b([A-Za-z_]\w*)\b", stripped)


def cmake_identities():
    path = os.path.join(REPO, "cmake", "RendererSelection.cmake")
    text = open(path, encoding="utf-8").read()
    m = re.search(
        r"set_property\(CACHE CNA_GRAPHICS_RENDERER PROPERTY STRINGS((?:\s+\"[A-Z0-9_]+\")+)\)",
        text)
    if not m:
        sys.exit("cannot locate CNA_GRAPHICS_RENDERER STRINGS property")
    return re.findall(r"\"([A-Z0-9_]+)\"", m.group(1))


def family_count():
    """Renderer implementation families, counted the same way the discipline gate counts them."""
    renderers = os.path.join(REPO, "modules", "renderers")
    return sum(1 for name in os.listdir(renderers)
               if os.path.isdir(os.path.join(renderers, name, "src")))


def documented_counts(identities, families):
    """Reports every stated count that disagrees with the registry, and every one gone invisible."""
    problems = []
    for relative, must_be_visible in COUNTED_DOCUMENTS.items():
        path = os.path.join(REPO, relative)
        if not os.path.exists(path):
            continue
        seen = 0
        with open(path, encoding="utf-8") as handle:
            for number, line in enumerate(handle, 1):
                for match in IDENTITY_COUNT.finditer(line):
                    seen += 1
                    if int(match.group(1)) != identities:
                        problems.append(
                            f"{relative}:{number}: says {match.group(1)} public renderer "
                            f"identities; there are {identities}. Either correct it, or -- better "
                            f"-- drop the number and name the registry, so the fact has an owner.")
                for match in FAMILY_COUNT.finditer(line):
                    seen += 1
                    if int(match.group(1)) != families:
                        problems.append(
                            f"{relative}:{number}: says {match.group(1)} implementation families; "
                            f"there are {families} (directories under modules/renderers with a "
                            f"src/).")
        if must_be_visible and seen == 0:
            problems.append(
                f"{relative}: is listed as stating a whole-registry count, and this check can no "
                f"longer see one. A count it cannot see is as unowned as one nobody wrote down. "
                f"State it as '<N> public renderer identities' and/or '<N> implementation "
                f"families', or move this file to the no-count entries in "
                f"{os.path.basename(__file__)} if the number was removed on purpose.")
    return problems


def main():
    expected_cmake = [c for c, _ in IDENTITIES]
    expected_enum = [e for _, e in IDENTITIES]
    ok = True

    actual_enum = enum_identities()
    if actual_enum != expected_enum:
        ok = False
        print("GraphicsRendererType enum diverges from the canonical identity table:")
        print(f"  expected ({len(expected_enum)}): {expected_enum}")
        print(f"  actual   ({len(actual_enum)}): {actual_enum}")

    # The STRINGS property is a UI list -- its member SET is the identity registry, its
    # ordering is cosmetic (and has historically differed from the enum's order).
    actual_cmake = cmake_identities()
    if sorted(actual_cmake) != sorted(expected_cmake) or len(actual_cmake) != len(expected_cmake):
        ok = False
        print("CNA_GRAPHICS_RENDERER STRINGS diverge from the canonical identity table:")
        print(f"  expected ({len(expected_cmake)}): {sorted(expected_cmake)}")
        print(f"  actual   ({len(actual_cmake)}): {sorted(actual_cmake)}")

    families = family_count()
    stale = documented_counts(len(IDENTITIES), families)
    if stale:
        ok = False
        print("Documented renderer counts disagree with the registry:")
        for problem in stale:
            print(f"  - {problem}")

    if ok:
        print(f"OK: {len(IDENTITIES)} public renderer identities preserved in both registries, "
              f"over {families} implementation families; every documented count agrees")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
