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

# Documents that deliberately state a count. Each entry is a decision: a document is
# better off describing the registry than restating its size, so adding to this list
# should be rarer than removing from it.
COUNTED_DOCUMENTS = (
    "docs/runtime-renderer-selection.md",
    "docs/renderer-expansion-candidates.md",
    "docs/physical-modules.md",
    "AUDIT.md",
    "CHECKLIST.md",
)

# "49 public renderer identities", "45 families / 49 public identities", "45 implementation
# families". Deliberately anchored on the words that mean the WHOLE registry: a legitimate
# sub-count ("the five GL identities of the easygl family") does not match, because it never
# says "public".
#
# The leading \b is load-bearing rather than decorative: without it the family pattern read the
# "12" out of "the d3d11 and d3d12 families only" and reported the repo as having 12 renderer
# families. Found by running this against the clean tree before trusting it, which is the only
# reason it is not still there.
IDENTITY_COUNT = re.compile(r"\b(\d+)\s+public\s+(?:renderer\s+)?identities")
FAMILY_COUNT = re.compile(r"\b(\d+)\s+(?:implementation\s+)?families\b")

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
    """Reports every stated count that disagrees with the registry."""
    problems = []
    for relative in COUNTED_DOCUMENTS:
        path = os.path.join(REPO, relative)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as handle:
            for number, line in enumerate(handle, 1):
                for match in IDENTITY_COUNT.finditer(line):
                    if int(match.group(1)) != identities:
                        problems.append(
                            f"{relative}:{number}: says {match.group(1)} public renderer "
                            f"identities; there are {identities}. Either correct it, or -- better "
                            f"-- drop the number and name the registry, so the fact has an owner.")
                for match in FAMILY_COUNT.finditer(line):
                    if int(match.group(1)) != families:
                        problems.append(
                            f"{relative}:{number}: says {match.group(1)} renderer families; there "
                            f"are {families} (directories under modules/renderers with a src/).")
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
