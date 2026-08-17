#!/usr/bin/env python3
"""Compile HLSL shaders to DXBC and emit a C++ header with embedded byte arrays.

plan_dx.md DX-14-compile. Mirrors src/CNA/Internal/Renderers/Vulkan/shaders/compile_shaders.py's
role (read + compile + emit a checked-in header), but D3DCompile() only exists at runtime inside
d3dcompiler.dll (design decision 5) -- there is no native-Linux D3D shader compiler to call
in-process the way libshaderc is called for SPIR-V. So this script:

  1. cross-builds hlsl_compiler_tool.cpp (a tiny D3DCompile()-calling .exe) with the project's
     existing MinGW-w64 toolchain (the same one cmake/toolchains/mingw-w64.cmake configures CMake
     with; this script invokes the mingw compiler directly, no CMake configure needed for this
     narrow purpose),
  2. runs that .exe once per shader file through scripts/run-wine-dxvk.sh (DX-3's established
     Wine+DXVK harness -- the same one DirectX11_Smoke/DirectX11_Common CTests use), producing real DXBC
     bytecode,
  3. embeds the resulting bytes as C++ arrays into hlsl_shaders.hpp, next to the .hlsl sources.

Requires: x86_64-w64-mingw32-g++ (mingw-w64 apt package) and an initialized
CNA_D3D11_WINEPREFIX (see scripts/run-wine-dxvk.sh / programs.md SS10). Not part of any normal
CMake build target -- run by hand to regenerate hlsl_shaders.hpp after editing a .hlsl source,
exactly like compile_shaders.py is for spirv_shaders.hpp.
"""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
REPO_ROOT = SCRIPT_DIR.parents[5]
COMPILER_TOOL_SRC = SCRIPT_DIR / "hlsl_compiler_tool.cpp"
RUN_WINE = REPO_ROOT / "scripts" / "run-wine-dxvk.sh"

# (filename, entry_point, target_profile, C++ array name[, preprocessor define])
SHADERS = [
    ("colored3d.vert.hlsl",          "main", "vs_5_0", "kColored3dVertDxbc"),
    ("colored3d.frag.hlsl",          "main", "ps_5_0", "kColored3dFragDxbc"),
    ("textured3d.vert.hlsl",         "main", "vs_5_0", "kTextured3dVertDxbc"),
    ("textured3d.frag.hlsl",         "main", "ps_5_0", "kTextured3dFragDxbc"),
    ("colored_textured3d.vert.hlsl", "main", "vs_5_0", "kColoredTextured3dVertDxbc"),
    ("colored_textured3d.frag.hlsl", "main", "ps_5_0", "kColoredTextured3dFragDxbc"),
    ("lit_textured3d.vert.hlsl",     "main", "vs_5_0", "kLitTextured3dVertDxbc"),
    ("lit_textured3d.frag.hlsl",     "main", "ps_5_0", "kLitTextured3dFragDxbc"),
    ("lit_textured3d_vertexlit.vert.hlsl", "main", "vs_5_0", "kLitTextured3dVertexLitVertDxbc"),
    ("lit_textured3d_vertexlit.frag.hlsl", "main", "ps_5_0", "kLitTextured3dVertexLitFragDxbc"),
    ("alpha_test3d.vert.hlsl",       "main", "vs_5_0", "kAlphaTest3dVertDxbc"),
    ("alpha_test3d.frag.hlsl",       "main", "ps_5_0", "kAlphaTest3dFragDxbc"),
    ("dual_texture3d.vert.hlsl",     "main", "vs_5_0", "kDualTexture3dVertDxbc"),
    ("dual_texture3d.frag.hlsl",     "main", "ps_5_0", "kDualTexture3dFragDxbc"),
    ("env_map3d.vert.hlsl",          "main", "vs_5_0", "kEnvMap3dVertDxbc"),
    ("env_map3d.frag.hlsl",          "main", "ps_5_0", "kEnvMap3dFragDxbc"),
    ("skinned3d.vert.hlsl",          "main", "vs_5_0", "kSkinned3dVertDxbc"),
    ("skinned3d.frag.hlsl",          "main", "ps_5_0", "kSkinned3dFragDxbc"),
    ("skinned3d_vertexlit.vert.hlsl", "main", "vs_5_0", "kSkinned3dVertexLitVertDxbc"),
    ("skinned3d_vertexlit.frag.hlsl", "main", "ps_5_0", "kSkinned3dVertexLitFragDxbc"),
    ("sprite2d.vert.hlsl",           "main", "vs_5_0", "kSprite2dVertDxbc"),
    ("sprite2d.frag.hlsl",           "main", "ps_5_0", "kSprite2dFragDxbc"),
    ("instanced3d.vert.hlsl",        "main", "vs_5_0", "kInstanced3dVertDxbc"),
    ("instanced3d.frag.hlsl",        "main", "ps_5_0", "kInstanced3dFragDxbc"),
    ("alpha_test_colored3d.vert.hlsl", "main", "vs_5_0", "kAlphaTestColored3dVertDxbc"),
    ("alpha_test_colored3d.frag.hlsl", "main", "ps_5_0", "kAlphaTestColored3dFragDxbc"),
    # plan_cnj.md CNB-58/CNB-67 follow-up: PbrEffect/SkinnedPbrEffect + SkinnedEffect vertex-color
    # (stride 56) D3D11 shader variants.
    ("pbr3d.vert.hlsl",                       "main", "vs_5_0", "kPbr3dVertDxbc"),
    ("pbr3d.frag.hlsl",                       "main", "ps_5_0", "kPbr3dFragDxbc"),
    # plan_gltf.md GLTF-462/GLTF-465: the rigid dual-UV variant is only ever selected for stride
    # 60, and InferredLayoutForStride(60) always has a Color element at offset 56 -- so it always
    # carries the vertex-colour define too, and VertexColorFlags.x decides whether it multiplies.
    ("pbr3d.vert.hlsl",                       "main", "vs_5_0", "kPbr3dDualUvVertDxbc", "CNA_PBR_DUAL_UV", "CNA_PBR_VERTEX_COLOR"),
    ("pbr3d.frag.hlsl",                       "main", "ps_5_0", "kPbr3dDualUvFragDxbc", "CNA_PBR_DUAL_UV", "CNA_PBR_VERTEX_COLOR"),
    ("pbr_skinned3d.vert.hlsl",                "main", "vs_5_0", "kPbrSkinned3dVertDxbc"),
    ("pbr_skinned3d.frag.hlsl",                "main", "ps_5_0", "kPbrSkinned3dFragDxbc"),
    ("pbr_skinned3d.vert.hlsl",                "main", "vs_5_0", "kPbrSkinned3dDualUvVertDxbc", "CNA_PBR_DUAL_UV"),
    ("pbr_skinned3d.frag.hlsl",                "main", "ps_5_0", "kPbrSkinned3dDualUvFragDxbc", "CNA_PBR_DUAL_UV"),
    # plan_gltf.md GLTF-463: stride 80 -- the stride-76 skinned PBR record with a packed COLOR_0
    # appended. Stride 76 has no colour slot, so this needs its own DXBC rather than a flag.
    ("pbr_skinned3d.vert.hlsl",                "main", "vs_5_0", "kPbrSkinned3dDualUvColorVertDxbc", "CNA_PBR_DUAL_UV", "CNA_PBR_VERTEX_COLOR"),
    ("pbr_skinned3d.frag.hlsl",                "main", "ps_5_0", "kPbrSkinned3dDualUvColorFragDxbc", "CNA_PBR_DUAL_UV", "CNA_PBR_VERTEX_COLOR"),
    ("skinned_colored3d.vert.hlsl",            "main", "vs_5_0", "kSkinned3dColoredVertDxbc"),
    ("skinned_colored3d.frag.hlsl",            "main", "ps_5_0", "kSkinned3dColoredFragDxbc"),
    ("skinned_colored3d_vertexlit.vert.hlsl",  "main", "vs_5_0", "kSkinned3dVertexLitColoredVertDxbc"),
    ("skinned_colored3d_vertexlit.frag.hlsl",  "main", "ps_5_0", "kSkinned3dVertexLitColoredFragDxbc"),
]


def build_compiler_tool(out_exe: Path) -> None:
    print(f"Building {COMPILER_TOOL_SRC.name} ...", end=" ", flush=True)
    subprocess.run(
        [
            "x86_64-w64-mingw32-g++", "-std=c++23", "-O2",
            "-static-libgcc", "-static-libstdc++",
            str(COMPILER_TOOL_SRC), "-o", str(out_exe),
            "-ld3dcompiler", "-Wl,--allow-multiple-definition",
        ],
        check=True,
    )
    print("OK")


def compile_one(tool_exe: Path, hlsl_path: Path, entry: str, profile: str, out_dxbc: Path) -> bytes:
    # DX-85's DXVK gate in run-wine-dxvk.sh fails any run that prints no "DXVK: <version>" line,
    # which this one never does: hlsl_compiler_tool.exe calls D3DCompile() out of d3dcompiler.dll
    # and never creates a D3D11 device at all. That is exactly the case the wrapper documents
    # CNA_D3D11_SKIP_DXVK_GATE=1 for -- without it the first shader compiles fine and the gate
    # still fails the script (plan_gltf.md GLTF-465).
    env = dict(os.environ, CNA_D3D11_SKIP_DXVK_GATE="1")
    result = subprocess.run(
        [str(RUN_WINE), str(tool_exe), str(hlsl_path), entry, profile, str(out_dxbc)],
        capture_output=True, text=True, env=env,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"D3DCompile failed for {hlsl_path.name} [{entry}/{profile}] (exit {result.returncode})")
    return out_dxbc.read_bytes()


def dxbc_to_cpp_array(name: str, dxbc: bytes) -> str:
    hex_bytes = [f"0x{b:02x}u" for b in dxbc]
    rows = [", ".join(hex_bytes[i:i + 12]) for i in range(0, len(hex_bytes), 12)]
    body = ",\n    ".join(rows)
    return (
        f"static constexpr uint8_t {name}[] = {{\n"
        f"    {body}\n"
        f"}};\n"
        f"static constexpr size_t {name}_size = sizeof({name});\n"
    )


def main() -> None:
    output_path = Path(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[1] == "--output" else \
        SCRIPT_DIR / "hlsl_shaders.hpp"

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        tool_exe = tmp_dir / "hlsl_compiler_tool.exe"
        build_compiler_tool(tool_exe)

        parts = [
            "// AUTO-GENERATED by compile_shaders_hlsl.py -- do not edit by hand.\n"
            "// Re-run compile_shaders_hlsl.py to regenerate after a .hlsl source changes.\n"
            "#pragma once\n"
            "#include <cstddef>\n"
            "#include <cstdint>\n\n"
            "namespace CNA::Internal::Renderers::D3DCommon::Shaders {\n\n"
        ]

        for shader in SHADERS:
            filename, entry, profile, cname, *defines = shader
            hlsl_path = SCRIPT_DIR / filename
            if not hlsl_path.exists():
                print(f"ERROR: {hlsl_path} not found", file=sys.stderr)
                sys.exit(1)
            compile_path = hlsl_path
            if defines:
                compile_path = tmp_dir / (cname + ".hlsl")
                define_lines = "".join(f"#define {define} 1\n" for define in defines)
                compile_path.write_text(define_lines + hlsl_path.read_text())
            out_dxbc = tmp_dir / (cname + ".dxbc")
            print(f"Compiling {filename} [{entry}/{profile}] ...", end=" ", flush=True)
            dxbc = compile_one(tool_exe, compile_path, entry, profile, out_dxbc)
            print(f"OK ({len(dxbc)} bytes)")
            parts.append(dxbc_to_cpp_array(cname, dxbc) + "\n")

        parts.append("} // namespace CNA::Internal::Renderers::D3DCommon::Shaders\n")

        output_path.write_text("".join(parts))
        print(f"Written: {output_path}")


if __name__ == "__main__":
    main()
