#!/usr/bin/env python3
"""Renderer-identity registry gate (MODULARIZATION_PLAN.md §2.3).

CNA has exactly 41 public renderer identities. This check mechanically compares
the two authoritative registries -- the public GraphicsBackendType enum and the
CNA_GRAPHICS_BACKEND cmake selection list -- against the canonical identity
table below. Any addition, removal or rename of a public identity fails here
until the table (and therefore the documented public count) is deliberately
updated.

Exit codes: 0 ok, 1 mismatch.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Canonical public identities: (cmake selection name, enum name). 41 entries.
IDENTITIES = [
    ("SDL_RENDERER", "SdlRenderer"),
    ("OPENGLES", "OpenGLES"),
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
    ("D3D11", "D3D11"),
    ("D3D12", "D3D12"),
    ("DIRECT2D", "Direct2D"),
    ("CANVAS", "Canvas"),
    ("HTML_DOM", "HtmlDom"),
    ("SKIA", "Skia"),
    ("ASCII", "Ascii"),
    ("FREEDIRECT", "FreeDirect"),
    ("D3D9", "D3D9"),
    ("DX1", "Dx1"),
    ("DX2", "Dx2"),
    ("DX3", "Dx3"),
    ("DX5", "Dx5"),
    ("DX6", "Dx6"),
    ("DX7", "Dx7"),
    ("DX8", "Dx8"),
    ("D3D10", "D3D10"),
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
]


def enum_identities():
    path = os.path.join(REPO, "include", "CNA", "GraphicsBackendType.hpp")
    text = open(path, encoding="utf-8").read()
    body = re.search(r"enum class GraphicsBackendType\s*\{(.*?)\n\s*\};", text, re.S)
    if not body:
        sys.exit("cannot locate enum class GraphicsBackendType")
    stripped = re.sub(r"/\*.*?\*/|//[^\n]*", "", body.group(1), flags=re.S)
    return re.findall(r"\b([A-Za-z_]\w*)\b", stripped)


def cmake_identities():
    path = os.path.join(REPO, "cmake", "BackendSelection.cmake")
    text = open(path, encoding="utf-8").read()
    m = re.search(
        r"set_property\(CACHE CNA_GRAPHICS_BACKEND PROPERTY STRINGS((?:\s+\"[A-Z0-9_]+\")+)\)",
        text)
    if not m:
        sys.exit("cannot locate CNA_GRAPHICS_BACKEND STRINGS property")
    return re.findall(r"\"([A-Z0-9_]+)\"", m.group(1))


def main():
    expected_cmake = [c for c, _ in IDENTITIES]
    expected_enum = [e for _, e in IDENTITIES]
    ok = True

    actual_enum = enum_identities()
    if actual_enum != expected_enum:
        ok = False
        print("GraphicsBackendType enum diverges from the canonical identity table:")
        print(f"  expected ({len(expected_enum)}): {expected_enum}")
        print(f"  actual   ({len(actual_enum)}): {actual_enum}")

    # The STRINGS property is a UI list -- its member SET is the identity registry, its
    # ordering is cosmetic (and has historically differed from the enum's order).
    actual_cmake = cmake_identities()
    if sorted(actual_cmake) != sorted(expected_cmake) or len(actual_cmake) != len(expected_cmake):
        ok = False
        print("CNA_GRAPHICS_BACKEND STRINGS diverge from the canonical identity table:")
        print(f"  expected ({len(expected_cmake)}): {sorted(expected_cmake)}")
        print(f"  actual   ({len(actual_cmake)}): {sorted(actual_cmake)}")

    if ok:
        print(f"OK: {len(IDENTITIES)} public renderer identities preserved in both registries")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
