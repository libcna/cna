#!/usr/bin/env python3
"""Compile CNA's DirectX 9 PBR/basic-punctual shaders and regenerate embedded bytecode.

The compiler is the repository's small D3DCompile-based ``fxc_tool.cpp`` executable, cross-built
with MinGW and run under Wine. Run this script under Xvfb on headless hosts. The Wine prefix must
already contain the pinned native Microsoft d3dcompiler_47.dll (for example, installed with
``winetricks -q d3dcompiler_47``); Wine's built-in replacement emits different bytecode.

Usage: compile_pbr_shaders.py [--output PATH] [--wineprefix PATH]
"""

import argparse
import hashlib
import os
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).parent
COMPILER_TOOL_SOURCE = SCRIPT_DIR / "fxc_tool.cpp"
DEFAULT_OUTPUT = SCRIPT_DIR / "d3d9_pbr_shaders.hpp"
DEFAULT_WINEPREFIX = Path.home() / ".wine-cna-d3d9-spike"
NATIVE_D3DCOMPILER_SHA256 = "4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3"

SHADERS = (
    ("cna/BasicPunctual3D.hlsl", "VSBasicPunctual", "vs_3_0",
     "kBasicPunctual3DVSBytecode", "BasicPunctual3D vertex shader"),
    ("cna/BasicPunctual3D.hlsl", "VSBasicPunctualVc", "vs_3_0",
     "kBasicPunctual3DColorVSBytecode", "BasicPunctual3D vertex shader (COLOR0)"),
    ("cna/BasicPunctual3D.hlsl", "VSBasicPunctualTx", "vs_3_0",
     "kBasicPunctual3DTextureVSBytecode", "BasicPunctual3D vertex shader (TEXCOORD0)"),
    ("cna/BasicPunctual3D.hlsl", "VSBasicPunctualTxVc", "vs_3_0",
     "kBasicPunctual3DTextureColorVSBytecode",
     "BasicPunctual3D vertex shader (TEXCOORD0 + COLOR0)"),
    ("cna/BasicPunctual3D.hlsl", "VSBasicPunctualSkinned", "vs_3_0",
     "kBasicPunctualSkinned3DVSBytecode", "BasicPunctual3D skinned vertex shader"),
    ("cna/BasicPunctual3D.hlsl", "VSBasicPunctualSkinnedVc", "vs_3_0",
     "kBasicPunctualSkinned3DColorVSBytecode",
     "BasicPunctual3D skinned vertex shader (COLOR0)"),
    ("cna/BasicPunctual3D.hlsl", "PSBasicPunctual", "ps_3_0",
     "kBasicPunctual3DPSBytecode", "BasicPunctual3D pixel shader"),
    ("cna/BasicPunctual3D.hlsl", "PSBasicPunctualTx", "ps_3_0",
     "kBasicPunctual3DTexturePSBytecode", "BasicPunctual3D textured pixel shader"),
    ("cna/BasicPunctual3D.hlsl", "PSBasicPunctual", "ps_3_0",
     "kBasicPunctual3DPointShadowPSBytecode", "BasicPunctual3D point-shadow pixel shader",
     ("CNA_D3D9_SHADOWS=1", "CNA_D3D9_PUNCTUAL_SHADOW_KIND=1")),
    ("cna/BasicPunctual3D.hlsl", "PSBasicPunctual", "ps_3_0",
     "kBasicPunctual3DSpotOnlyShadowPSBytecode", "BasicPunctual3D spot-only shadow pixel shader",
     ("CNA_D3D9_SHADOWS=1", "CNA_D3D9_PUNCTUAL_SHADOW_KIND=2",
      "CNA_D3D9_DIRECTIONAL_SHADOW_MODE=0")),
    ("cna/BasicPunctual3D.hlsl", "PSBasicPunctual", "ps_3_0",
     "kBasicPunctual3DSpotDirectionalShadowPSBytecode",
     "BasicPunctual3D directional plus spot shadow pixel shader",
     ("CNA_D3D9_SHADOWS=1", "CNA_D3D9_PUNCTUAL_SHADOW_KIND=2")),
    ("cna/BasicPunctual3D.hlsl", "PSBasicPunctualTx", "ps_3_0",
     "kBasicPunctual3DTexturePointShadowPSBytecode",
     "BasicPunctual3D textured point-shadow pixel shader",
     ("CNA_D3D9_SHADOWS=1", "CNA_D3D9_PUNCTUAL_SHADOW_KIND=1")),
    ("cna/BasicPunctual3D.hlsl", "PSBasicPunctualTx", "ps_3_0",
     "kBasicPunctual3DTextureSpotOnlyShadowPSBytecode",
     "BasicPunctual3D textured spot-only shadow pixel shader",
     ("CNA_D3D9_SHADOWS=1", "CNA_D3D9_PUNCTUAL_SHADOW_KIND=2",
      "CNA_D3D9_DIRECTIONAL_SHADOW_MODE=0")),
    ("cna/BasicPunctual3D.hlsl", "PSBasicPunctualTx", "ps_3_0",
     "kBasicPunctual3DTextureSpotDirectionalShadowPSBytecode",
     "BasicPunctual3D textured directional plus spot shadow pixel shader",
     ("CNA_D3D9_SHADOWS=1", "CNA_D3D9_PUNCTUAL_SHADOW_KIND=2")),
    ("cna/Pbr3D.hlsl", "VSPbr3D", "vs_3_0", "kPbr3DVSBytecode", "Pbr3D vertex shader"),
    # GLTF-465: the stride-60 and stride-80 twins. A separate entry point rather than a
    # preprocessor define, because a vs_3_0 input with no stream behind it reads undefined -- so each
    # vertex declaration gets the program whose input struct it actually satisfies. The pixel stages
    # are shared: both vertex variants write the COLOR0 interpolant, authored or opaque white.
    ("cna/Pbr3D.hlsl", "VSPbr3DColor", "vs_3_0", "kPbr3DColorVSBytecode",
     "Pbr3D vertex shader (stride 60, COLOR_0)"),
    ("cna/Pbr3D.hlsl", "PSPbr3D", "ps_3_0", "kPbr3DPSBytecode", "Pbr3D pixel shader",
     ("CNA_D3D9_DOUBLE_SIDED=1",)),
    ("cna/Pbr3D.hlsl", "PSPbr3D", "ps_3_0", "kPbr3DPointShadowPSBytecode",
     "Pbr3D point-shadow pixel shader",
     ("CNA_D3D9_DOUBLE_SIDED=1", "CNA_D3D9_SHADOWS=1", "CNA_D3D9_PUNCTUAL_SHADOW_KIND=1")),
    ("cna/Pbr3D.hlsl", "PSPbr3D", "ps_3_0", "kPbr3DSpotOnlyShadowPSBytecode",
     "Pbr3D spot-only shadow pixel shader",
     ("CNA_D3D9_DOUBLE_SIDED=1", "CNA_D3D9_SHADOWS=1", "CNA_D3D9_PUNCTUAL_SHADOW_KIND=2",
      "CNA_D3D9_DIRECTIONAL_SHADOW_MODE=0")),
    ("cna/Pbr3D.hlsl", "PSPbr3D", "ps_3_0", "kPbr3DSpotDirectionalShadowPSBytecode",
     "Pbr3D directional plus spot shadow pixel shader",
     ("CNA_D3D9_DOUBLE_SIDED=1", "CNA_D3D9_SHADOWS=1", "CNA_D3D9_PUNCTUAL_SHADOW_KIND=2")),
    ("cna/PbrSkinned3D.hlsl", "VSPbrSkinned3D", "vs_3_0",
     "kPbrSkinned3DVSBytecode", "PbrSkinned3D vertex shader"),
    ("cna/PbrSkinned3D.hlsl", "VSPbrSkinned3DColor", "vs_3_0",
     "kPbrSkinned3DColorVSBytecode", "PbrSkinned3D vertex shader (stride 80, COLOR_0)"),
    ("cna/PbrSkinned3D.hlsl", "PSPbrSkinned3D", "ps_3_0",
     "kPbrSkinned3DPSBytecode", "PbrSkinned3D pixel shader",
     ("CNA_D3D9_DOUBLE_SIDED=1",)),
    ("cna/PbrSkinned3D.hlsl", "PSPbrSkinned3D", "ps_3_0",
     "kPbrSkinned3DPointShadowPSBytecode", "PbrSkinned3D point-shadow pixel shader",
     ("CNA_D3D9_DOUBLE_SIDED=1", "CNA_D3D9_SHADOWS=1", "CNA_D3D9_PUNCTUAL_SHADOW_KIND=1")),
    ("cna/PbrSkinned3D.hlsl", "PSPbrSkinned3D", "ps_3_0",
     "kPbrSkinned3DSpotOnlyShadowPSBytecode", "PbrSkinned3D spot-only shadow pixel shader",
     ("CNA_D3D9_DOUBLE_SIDED=1", "CNA_D3D9_SHADOWS=1", "CNA_D3D9_PUNCTUAL_SHADOW_KIND=2",
      "CNA_D3D9_DIRECTIONAL_SHADOW_MODE=0")),
    ("cna/PbrSkinned3D.hlsl", "PSPbrSkinned3D", "ps_3_0",
     "kPbrSkinned3DSpotDirectionalShadowPSBytecode",
     "PbrSkinned3D directional plus spot shadow pixel shader",
     ("CNA_D3D9_DOUBLE_SIDED=1", "CNA_D3D9_SHADOWS=1", "CNA_D3D9_PUNCTUAL_SHADOW_KIND=2")),
)


def verify_compiler(wineprefix: Path) -> None:
    compiler = wineprefix / "drive_c/windows/system32/d3dcompiler_47.dll"
    if not compiler.is_file():
        raise RuntimeError(
            f"Pinned native compiler is missing: {compiler}\n"
            "Install it into the prefix with: winetricks -q d3dcompiler_47")
    digest = hashlib.sha256(compiler.read_bytes()).hexdigest()
    if digest != NATIVE_D3DCOMPILER_SHA256:
        raise RuntimeError(
            f"Unexpected d3dcompiler_47.dll SHA-256: {digest}\n"
            f"Expected the pinned native compiler: {NATIVE_D3DCOMPILER_SHA256}")


def build_compiler_tool(output: Path) -> None:
    print(f"Building {COMPILER_TOOL_SOURCE.name} ...", end=" ", flush=True)
    subprocess.run(
        [
            "ccache", "x86_64-w64-mingw32-g++", "-std=c++23", "-O2",
            "-static-libgcc", "-static-libstdc++", str(COMPILER_TOOL_SOURCE),
            "-o", str(output), "-ld3dcompiler", "-Wl,--allow-multiple-definition",
        ],
        check=True,
    )
    print("OK")


def compile_shader(tool: Path, wineprefix: Path, source: Path, entry: str,
                   profile: str, output: Path, defines=()) -> bytes:
    environment = dict(os.environ)
    environment["WINEPREFIX"] = str(wineprefix)
    environment.setdefault("WINEDEBUG", "-all")
    command = ["wine", str(tool), str(source), entry, profile, str(output)]
    for definition in defines:
        command.extend(("--define", definition))
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        env=environment,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError(
            f"D3DCompile failed for {source.name} [{entry}/{profile}] "
            f"(exit {result.returncode})")
    return output.read_bytes()


def byte_array(name: str, description: str, bytecode: bytes) -> str:
    values = [f"0x{value:02x}u" for value in bytecode]
    rows = [", ".join(values[offset:offset + 12])
            for offset in range(0, len(values), 12)]
    body = ",\n    ".join(rows)
    return (
        f"// {description} ({len(bytecode)} bytes)\n"
        f"static constexpr uint8_t {name}[{len(bytecode)}] = {{\n"
        f"    {body}\n"
        f"}};\n"
        f"static constexpr std::size_t {name}_size = {len(bytecode)};\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--wineprefix", type=Path, default=DEFAULT_WINEPREFIX)
    parser.add_argument("--work-dir", type=Path,
                        default=SCRIPT_DIR.parents[4] / "cmake-build-mingw/generated/shader-tools",
                        help="Persistent in-repository compiler/output directory (never /tmp)")
    args = parser.parse_args()

    if not (args.wineprefix / "system.reg").is_file():
        parser.error(f"Wine prefix is not initialized: {args.wineprefix}")
    verify_compiler(args.wineprefix)

    args.work_dir.mkdir(parents=True, exist_ok=True)
    compiler_tool = args.work_dir / "fxc_tool.exe"
    build_compiler_tool(compiler_tool)

    parts = [
            "// SPDX-License-Identifier: MS-PL\n"
            "// AUTO-GENERATED by compile_pbr_shaders.py -- do not edit by hand.\n"
            "//\n"
            "// Real vs_3_0/ps_3_0 bytecode for CNA's DirectX 9 extension shaders, compiled through\n"
            "// fxc_tool.cpp and d3dcompiler_47.dll under Wine. Re-run this generator after any\n"
            "// BasicPunctual3D/Pbr3D/PbrSkinned3D HLSL change, then verify the constant table\n"
            "// in D3D9CnaShaderRegisters.hpp against D3DDisassemble.\n"
            "#pragma once\n"
            "#include <cstddef>\n"
            "#include <cstdint>\n\n"
            "namespace CNA::Internal::Renderers::DirectX9::Shaders\n"
            "{\n\n"
    ]

    for shader in SHADERS:
        relative_source, entry, profile, name, description = shader[:5]
        defines = shader[5] if len(shader) > 5 else ()
        source = SCRIPT_DIR / relative_source
        output = args.work_dir / f"{name}.bin"
        print(f"Compiling {source.name} [{entry}/{profile}] ...", end=" ", flush=True)
        bytecode = compile_shader(
            compiler_tool, args.wineprefix, source, entry, profile, output, defines)
        print(f"OK ({len(bytecode)} bytes)")
        parts.append(byte_array(name, description, bytecode) + "\n")

    parts.append("} // namespace CNA::Internal::Renderers::DirectX9::Shaders\n")
    args.output.write_text("".join(parts))
    print(f"Written: {args.output}")


if __name__ == "__main__":
    main()
