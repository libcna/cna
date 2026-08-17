#!/usr/bin/env python3
"""Renderer-identity registry gate (MODULARIZATION_PLAN.md §2.3).

CNA has exactly 48 public renderer identities. This check mechanically compares
the two authoritative registries -- the public GraphicsRendererType enum and the
CNA_GRAPHICS_RENDERER cmake selection list -- against the canonical identity
table below. Any addition, removal or rename of a public identity fails here
until the table (and therefore the documented public count) is deliberately
updated.

Exit codes: 0 ok, 1 mismatch.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Canonical public identities: (cmake selection name, enum name). 48 entries.
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

    if ok:
        print(f"OK: {len(IDENTITIES)} public renderer identities preserved in both registries")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
