#!/usr/bin/env python3
"""Compile CNA's DirectX 9 PBR shaders and regenerate their embedded bytecode header.

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
import tempfile
from pathlib import Path


SCRIPT_DIR = Path(__file__).parent
COMPILER_TOOL_SOURCE = SCRIPT_DIR / "fxc_tool.cpp"
DEFAULT_OUTPUT = SCRIPT_DIR / "d3d9_pbr_shaders.hpp"
DEFAULT_WINEPREFIX = Path.home() / ".wine-cna-d3d9-spike"
NATIVE_D3DCOMPILER_SHA256 = "4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3"

SHADERS = (
    ("cna/Pbr3D.hlsl", "VSPbr3D", "vs_3_0", "kPbr3DVSBytecode", "Pbr3D vertex shader"),
    # plan_gltf.md GLTF-465: the stride-60 and stride-80 twins. A separate entry point rather than a
    # preprocessor define, because a vs_3_0 input with no stream behind it reads undefined -- so each
    # vertex declaration gets the program whose input struct it actually satisfies. The pixel stages
    # are shared: both vertex variants write the COLOR0 interpolant, authored or opaque white.
    ("cna/Pbr3D.hlsl", "VSPbr3DColor", "vs_3_0", "kPbr3DColorVSBytecode",
     "Pbr3D vertex shader (stride 60, COLOR_0)"),
    ("cna/Pbr3D.hlsl", "PSPbr3D", "ps_3_0", "kPbr3DPSBytecode", "Pbr3D pixel shader"),
    ("cna/PbrSkinned3D.hlsl", "VSPbrSkinned3D", "vs_3_0",
     "kPbrSkinned3DVSBytecode", "PbrSkinned3D vertex shader"),
    ("cna/PbrSkinned3D.hlsl", "VSPbrSkinned3DColor", "vs_3_0",
     "kPbrSkinned3DColorVSBytecode", "PbrSkinned3D vertex shader (stride 80, COLOR_0)"),
    ("cna/PbrSkinned3D.hlsl", "PSPbrSkinned3D", "ps_3_0",
     "kPbrSkinned3DPSBytecode", "PbrSkinned3D pixel shader"),
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
            "x86_64-w64-mingw32-g++", "-std=c++23", "-O2",
            "-static-libgcc", "-static-libstdc++", str(COMPILER_TOOL_SOURCE),
            "-o", str(output), "-ld3dcompiler", "-Wl,--allow-multiple-definition",
        ],
        check=True,
    )
    print("OK")


def compile_shader(tool: Path, wineprefix: Path, source: Path, entry: str,
                   profile: str, output: Path) -> bytes:
    environment = dict(os.environ)
    environment["WINEPREFIX"] = str(wineprefix)
    environment.setdefault("WINEDEBUG", "-all")
    result = subprocess.run(
        ["wine", str(tool), str(source), entry, profile, str(output)],
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
    args = parser.parse_args()

    if not (args.wineprefix / "system.reg").is_file():
        parser.error(f"Wine prefix is not initialized: {args.wineprefix}")
    verify_compiler(args.wineprefix)

    with tempfile.TemporaryDirectory() as temporary:
        temporary_path = Path(temporary)
        compiler_tool = temporary_path / "fxc_tool.exe"
        build_compiler_tool(compiler_tool)

        parts = [
            "// SPDX-License-Identifier: MS-PL\n"
            "// AUTO-GENERATED by compile_pbr_shaders.py -- do not edit by hand.\n"
            "//\n"
            "// Real vs_3_0/ps_3_0 bytecode for CNA's DirectX 9 PBR shaders, compiled through\n"
            "// fxc_tool.cpp and d3dcompiler_47.dll under Wine. Re-run this generator after any\n"
            "// Pbr3D.hlsl or PbrSkinned3D.hlsl change, then verify the constant-register table\n"
            "// in D3D9CnaShaderRegisters.hpp against D3DDisassemble.\n"
            "#pragma once\n"
            "#include <cstddef>\n"
            "#include <cstdint>\n\n"
            "namespace CNA::Internal::Renderers::DirectX9::Shaders\n"
            "{\n\n"
        ]

        for relative_source, entry, profile, name, description in SHADERS:
            source = SCRIPT_DIR / relative_source
            output = temporary_path / f"{name}.bin"
            print(f"Compiling {source.name} [{entry}/{profile}] ...", end=" ", flush=True)
            bytecode = compile_shader(
                compiler_tool, args.wineprefix, source, entry, profile, output)
            print(f"OK ({len(bytecode)} bytes)")
            parts.append(byte_array(name, description, bytecode) + "\n")

        parts.append("} // namespace CNA::Internal::Renderers::DirectX9::Shaders\n")
        args.output.write_text("".join(parts))
        print(f"Written: {args.output}")


if __name__ == "__main__":
    main()
