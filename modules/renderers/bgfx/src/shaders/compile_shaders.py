#!/usr/bin/env python3
"""
Compile bgfx .sc shaders to embedded C arrays and write bgfx_shaders.hpp.

Usage:
    python3 compile_shaders.py <shaderc> <bgfx-include-dir>

  shaderc          Path to bgfx shaderc binary
  bgfx-include-dir Path to bgfx/src (contains bgfx_shader.sh)

Typical invocation from the repo root:
    python3 src/CNA/Internal/Renderers/Bgfx/shaders/compile_shaders.py \\
        cmake-build-bgfx/_deps/bgfx_cmake-build/cmake/bgfx/shaderc \\
        cmake-build-bgfx/_deps/bgfx_cmake-src/bgfx/src
"""

import os
import subprocess
import sys
import re
import tempfile

SHADER_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_HPP = os.path.join(SHADER_DIR, "bgfx_shaders.hpp")
VARYING_DEF = os.path.join(SHADER_DIR, "varying.def.sc")

# (c-array-name, [(logical-name, type, source-file), ...])
# Each pair becomes one bgfx::EmbeddedShader array used with bgfx::createEmbeddedShader.
SHADER_PAIRS = [
    ("kColored3dShaders", [
        ("vs_colored3d",          "vertex",   "vs_colored3d.sc"),
        ("fs_colored3d",          "fragment", "fs_colored3d.sc"),
    ]),
    ("kTextured3dShaders", [
        ("vs_textured3d",         "vertex",   "vs_textured3d.sc"),
        ("fs_textured3d",         "fragment", "fs_textured3d.sc"),
    ]),
    ("kColoredTextured3dShaders", [
        ("vs_colored_textured3d", "vertex",   "vs_colored_textured3d.sc"),
        ("fs_colored_textured3d", "fragment", "fs_colored_textured3d.sc"),
    ]),
    ("kLitTextured3dShaders", [
        ("vs_lit_textured3d",     "vertex",   "vs_lit_textured3d.sc"),
        ("fs_lit_textured3d",     "fragment", "fs_lit_textured3d.sc"),
    ]),
    ("kLitTextured3dVertexLitShaders", [
        # Task 1104: real per-vertex-lit sibling, selected when GpuDrawParams::
        # preferPerPixelLighting is false (XNA's real BasicEffect default).
        ("vs_lit_textured3d_vertexlit", "vertex",   "vs_lit_textured3d_vertexlit.sc"),
        ("fs_lit_textured3d_vertexlit", "fragment", "fs_lit_textured3d_vertexlit.sc"),
    ]),
    ("kAlphaTest3dShaders", [
        ("vs_alpha_test3d",       "vertex",   "vs_alpha_test3d.sc"),
        ("fs_alpha_test3d",       "fragment", "fs_alpha_test3d.sc"),
        # Stride-24 (VertexPositionColorTexture) variant with VertexColorEnabled support
        # (Task 887); shares fs_alpha_test3d's fragment shader.
        ("vs_alpha_test_colored3d", "vertex", "vs_alpha_test_colored3d.sc"),
    ]),
    ("kDualTexture3dShaders", [
        ("vs_dual_texture3d",     "vertex",   "vs_dual_texture3d.sc"),
        ("fs_dual_texture3d",     "fragment", "fs_dual_texture3d.sc"),
        # Stride-24 (VertexPositionColorTexture) variant with VertexColorEnabled support
        # (Task 889); shares fs_dual_texture3d's fragment shader.
        ("vs_dual_texture_colored3d", "vertex", "vs_dual_texture_colored3d.sc"),
    ]),
    ("kSkinned3dShaders", [
        ("vs_skinned3d",          "vertex",   "vs_skinned3d.sc"),
        ("fs_skinned3d",          "fragment", "fs_skinned3d.sc"),
    ]),
    ("kSkinned3dVertexLitShaders", [
        # Task 1104: real per-vertex-lit sibling, selected when GpuDrawParams::
        # preferPerPixelLighting is false (XNA's real SkinnedEffect default).
        ("vs_skinned3d_vertexlit", "vertex",   "vs_skinned3d_vertexlit.sc"),
        ("fs_skinned3d_vertexlit", "fragment", "fs_skinned3d_vertexlit.sc"),
    ]),
    ("kEnvMap3dShaders", [
        ("vs_env_map3d",          "vertex",   "vs_env_map3d.sc"),
        ("fs_env_map3d",          "fragment", "fs_env_map3d.sc"),
    ]),
    # plans/plan_cnj.md CNB-58/60 (Phase 13A), Bgfx port: PbrEffect (stride 48) and SkinnedPbrEffect
    # (stride 68) share one fragment shader (fs_pbr3d) -- the BRDF math is identical, only the
    # vertex stage differs (plain transform vs. bone-palette skin), mirroring the
    # kAlphaTest3dShaders / vs_alpha_test_colored3d precedent of two vertex shaders sharing one
    # fragment shader in a single EmbeddedShader array.
    ("kPbr3dShaders", [
        ("vs_pbr3d",              "vertex",   "vs_pbr3d.sc"),
        ("fs_pbr3d",              "fragment", "fs_pbr3d.sc"),
        ("vs_pbr_skinned3d",      "vertex",   "vs_pbr_skinned3d.sc"),
    ]),
    ("kInstanced3dShaders", [
        ("vs_instanced3d",        "vertex",   "vs_instanced3d.sc"),
        ("fs_instanced3d",        "fragment", "fs_instanced3d.sc"),
    ]),
]

# Flat list for compilation loop
SHADERS = [(name, stype, sc) for (_, entries) in SHADER_PAIRS for (name, stype, sc) in entries]

# (type, profile, platform, array-suffix, bgfx RendererType)
# Compile only what shaderc can produce on Linux.
# Do NOT use BGFX_EMBEDDED_SHADER macro — it activates DXBC on Linux too
# (BGFX_PLATFORM_SUPPORTS_DXBC includes BX_PLATFORM_LINUX), which can't be
# compiled without D3D4Linux. We build the EmbeddedShader struct manually.
TARGETS = [
    ("vertex",   "140",    "linux", "glsl", "bgfx::RendererType::OpenGL"),
    ("vertex",   "300_es", "linux", "essl", "bgfx::RendererType::OpenGLES"),
    ("vertex",   "spirv",  "linux", "spv",  "bgfx::RendererType::Vulkan"),
    ("vertex",   "wgsl",   "linux", "wgsl", "bgfx::RendererType::WebGPU"),
    ("fragment", "140",    "linux", "glsl", "bgfx::RendererType::OpenGL"),
    ("fragment", "300_es", "linux", "essl", "bgfx::RendererType::OpenGLES"),
    ("fragment", "spirv",  "linux", "spv",  "bgfx::RendererType::Vulkan"),
    ("fragment", "wgsl",   "linux", "wgsl", "bgfx::RendererType::WebGPU"),
]

# Minimal no-op shader payloads required by createEmbeddedShader for Noop renderer.
NOOP_VSH = r'(const uint8_t*)"VSH\x5\x0\x0\x0\x0\x0\x0"'
NOOP_FSH = r'(const uint8_t*)"FSH\x5\x0\x0\x0\x0\x0\x0"'


def compile_one(shaderc, bgfx_include, sc_path, shader_type, profile, platform, c_name):
    with tempfile.NamedTemporaryFile(suffix=".bin.h", delete=False) as tmp:
        out_path = tmp.name
    try:
        cmd = [
            shaderc,
            "-f", sc_path,
            "-o", out_path,
            "--type", shader_type,
            "--platform", platform,
            "-p", profile,
            "-i", bgfx_include,
            "--varyingdef", VARYING_DEF,
            "--bin2c", c_name,
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"  FAILED {shader_type}/{profile}: {result.stdout.strip()} {result.stderr.strip()}")
            return None, None
        with open(out_path, "r") as f:
            text = f.read()
        m = re.search(rf"uint8_t {re.escape(c_name)}\[(\d+)\]", text)
        size = m.group(1) if m else "?"
        return text, size
    finally:
        if os.path.exists(out_path):
            os.remove(out_path)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    shaderc = sys.argv[1]
    bgfx_include = sys.argv[2]

    if not os.path.isfile(shaderc):
        print(f"ERROR: shaderc not found: {shaderc}")
        sys.exit(1)

    # Compile all shader variants
    array_texts = []          # C array text fragments
    present = {}              # name → { suffix: renderer_type }

    for (name, stype, sc_file) in SHADERS:
        sc_path = os.path.join(SHADER_DIR, sc_file)
        present[name] = {}
        print(f"  {name} ({stype}):")
        for (ttype, profile, platform, suffix, renderer) in TARGETS:
            if ttype != stype:
                continue
            c_name = f"{name}_{suffix}"
            print(f"    {profile:10s} → {c_name} ... ", end="", flush=True)
            text, size = compile_one(shaderc, bgfx_include, sc_path, stype,
                                     profile, platform, c_name)
            if text:
                array_texts.append(text)
                present[name][suffix] = renderer
                print(f"OK ({size} bytes)")
            else:
                print("FAILED")

    # --- Generate bgfx_shaders.hpp ---
    lines = [
        "// Auto-generated by compile_shaders.py — do not edit manually.",
        "#pragma once",
        "#include <bgfx/bgfx.h>",
        "#include <bgfx/embedded_shader.h>",
        "",
    ]
    for text in array_texts:
        lines.append(text)

    # Build one EmbeddedShader array per shader pair, manually, to avoid
    # BGFX_EMBEDDED_SHADER which on Linux also activates DXBC
    # (BX_PLATFORM_LINUX is in BGFX_PLATFORM_SUPPORTS_DXBC) but shaderc
    # cannot produce DXBC without D3D4Linux.
    for (array_name, entries) in SHADER_PAIRS:
        lines.append(f"static const bgfx::EmbeddedShader {array_name}[] =")
        lines.append("{")
        for (name, stype, _) in entries:
            lines.append(f"    // {name}")
            lines.append(f"    {{")
            lines.append(f'        "{name}",')
            lines.append(f"        {{")
            for suffix, renderer in present.get(name, {}).items():
                c_name = f"{name}_{suffix}"
                lines.append(f"            {{ {renderer}, {c_name}, sizeof({c_name}) }},")
            noop_data = NOOP_VSH if stype == "vertex" else NOOP_FSH
            lines.append(f"            {{ bgfx::RendererType::Noop, {noop_data}, 10 }},")
            lines.append(f"            {{ bgfx::RendererType::Count, nullptr, 0 }},")
            lines.append(f"        }}")
            lines.append(f"    }},")
        lines.append("    BGFX_EMBEDDED_SHADER_END()")
        lines.append("};")
        lines.append("")

    with open(OUTPUT_HPP, "w") as f:
        f.write("\n".join(lines))

    print(f"\nWrote {OUTPUT_HPP}")
    print("Done.")


if __name__ == "__main__":
    main()
