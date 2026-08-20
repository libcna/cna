#!/usr/bin/env python3
"""Transcribe Microsoft's real, compiler-verified per-shader constant register layout into a
checked-in C++ table.

plans/plan_dx9.md Phase D9-7 (D9-72). "The layout is given, not designed" (design decision 6) --
this script does not hand-derive register counts from the vendored .fx files' own `_vs(cN)`/
`_ps(cN)` source annotations, because **that would be wrong**: a real, empirical check (compiling
EnvironmentMapEffect.fx's VSEnvMap and disassembling it) found the compiler allocates `World` only
3 registers at runtime (c16-c18), not the 4 its `float4x4` declaration implies -- `pos_ws.w` is
never read by that specific entry point, so the compiler drops the constant register that would
have produced it. Register occupancy genuinely depends on what a given ENTRY POINT reads, not just
a constant's declared HLSL type -- so this script extracts the ground truth directly from each of
the 66 compiled shaders' own D3DDisassemble() output, which prints the exact register/size table
the real compiler decided on, per entry point.

Reuses compile_shaders_sm2.py's own entry-point discovery (never hand-maintained) and its
build_compiler_tool() pattern for a small D3DDisassemble()-calling companion tool
(disasm_tool.cpp, cross-built alongside fxc_tool.cpp). Requires the same ~/.wine-cna-d3d9-spike
prefix D9-71 uses. Not part of any CMake target -- run by hand after a vendor refresh or a
compile-flag change (same as compile_shaders_sm2.py).

Usage: python3 extract_shader_registers.py [--output PATH] [--wineprefix PATH]
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from compile_shaders_sm2 import (
    discover_entry_points, build_compiler_tool, DEFAULT_WINEPREFIX, SCRIPT_DIR, XNA_DIR,
)

DISASM_TOOL_SRC = SCRIPT_DIR / "disasm_tool.cpp"

REGISTER_LINE_RE = re.compile(r"^//\s+(\S+)\s+c(\d+)\s+(\d+)\s*$")


def build_disasm_tool(out_exe: Path) -> None:
    print(f"Building {DISASM_TOOL_SRC.name} ...", end=" ", flush=True)
    subprocess.run(
        [
            "x86_64-w64-mingw32-g++", "-std=c++23", "-O2",
            "-static-libgcc", "-static-libstdc++",
            str(DISASM_TOOL_SRC), "-o", str(out_exe),
            "-ld3dcompiler", "-Wl,--allow-multiple-definition",
        ],
        check=True,
    )
    print("OK")


def run_wine(wineprefix: Path, args) -> subprocess.CompletedProcess:
    full_env = dict(os.environ)
    full_env["WINEPREFIX"] = str(wineprefix)
    return subprocess.run(["wine", *args], capture_output=True, text=True, env=full_env)


def compile_and_disassemble(fxc_exe: Path, disasm_exe: Path, wineprefix: Path,
                             fx_path: Path, entry: str, profile: str, tmp_dir: Path):
    out_bin = tmp_dir / f"{entry}.bin"
    result = run_wine(wineprefix, [str(fxc_exe), str(fx_path), entry, profile, str(out_bin)])
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"D3DCompile failed for {fx_path.name} [{entry}/{profile}]")

    result = run_wine(wineprefix, [str(disasm_exe), str(out_bin)])
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"D3DDisassemble failed for {fx_path.name} [{entry}/{profile}]")
    return result.stdout


def parse_registers(disassembly: str):
    """Parses the `// Registers:` comment block's `Name  cN  Size` rows.

    Returns a list of (name, register, size) tuples, in the order the compiler printed them
    (not necessarily ascending register order -- preserved as-is, since that is what the
    compiler itself considered the natural declaration order).
    """
    entries = []
    in_table = False
    for line in disassembly.splitlines():
        if line.strip() == "// Registers:":
            in_table = True
            continue
        if not in_table:
            continue
        if line.startswith("//   ---"):
            continue
        if line.strip() == "//" or line.strip() == "":
            if entries:
                break
            continue
        m = REGISTER_LINE_RE.match(line)
        if m:
            entries.append((m.group(1), int(m.group(2)), int(m.group(3))))
    return entries


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=SCRIPT_DIR.parents[1] / "include/CNA/Internal/Renderers/DirectX9/shaders/D3D9ShaderRegisters.hpp")
    parser.add_argument("--wineprefix", type=Path, default=DEFAULT_WINEPREFIX)
    args = parser.parse_args()

    entries = discover_entry_points()
    print(f"Discovered {len(entries)} entry points (reusing compile_shaders_sm2.py's own parser).")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        fxc_exe = tmp_dir / "fxc_tool.exe"
        disasm_exe = tmp_dir / "disasm_tool.exe"
        build_compiler_tool(fxc_exe)
        build_disasm_tool(disasm_exe)

        parts = [
            "// SPDX-License-Identifier: MS-PL\n"
            "// AUTO-GENERATED by extract_shader_registers.py -- do not edit by hand.\n"
            "//\n"
            "// plans/plan_dx9.md D9-72: the real, compiler-verified constant register layout for each of\n"
            "// the 66 compiled Stock Effect shaders, extracted from D3DDisassemble()'s own\n"
            "// \"// Registers:\" comment block -- NOT hand-derived from the .fx files' _vs(cN)/\n"
            "// _ps(cN) source annotations. Those source annotations give the STARTING register\n"
            "// correctly but NOT always the correct register COUNT: a real check found\n"
            "// EnvironmentMapEffect.fx's World constant (declared float4x4, naively 4 registers)\n"
            "// is allocated only 3 registers by the real compiler in VSEnvMap specifically, because\n"
            "// that entry point never reads pos_ws.w -- register occupancy depends on what a given\n"
            "// ENTRY POINT actually reads, not just a constant's declared HLSL type. Re-run this\n"
            "// script (not hand-edit) after any vendored .fx change or compile-flag change.\n"
            "#pragma once\n"
            "#include <cstdint>\n\n"
            "namespace CNA::Internal::Renderers::DirectX9::Shaders {\n\n"
            "/// One named HLSL constant's real register slot within a single compiled shader.\n"
            "/// `space` is 'v' for a vs_2_0 constant register (SetVertexShaderConstantF) or 'p' for\n"
            "/// a ps_2_0 one (SetPixelShaderConstantF) -- never both; D3D9 vertex and pixel shader\n"
            "/// constant register files are entirely independent address spaces.\n"
            "struct D3D9ShaderConstantSlot\n"
            "{\n"
            "    const char* name;\n"
            "    char space;\n"
            "    uint8_t registerIndex;\n"
            "    uint8_t registerCount;\n"
            "};\n\n"
        ]

        for filename, effect_name, entry, profile, cpp_name in entries:
            fx_path = XNA_DIR / filename
            space = 'v' if profile == 'vs_2_0' else 'p'
            print(f"Disassembling {filename} [{entry}/{profile}] ...", end=" ", flush=True)
            disassembly = compile_and_disassemble(fxc_exe, disasm_exe, args.wineprefix,
                                                   fx_path, entry, profile, tmp_dir)
            regs = parse_registers(disassembly)
            print(f"OK ({len(regs)} constants)")

            array_name = f"{cpp_name}_Registers"  # cpp_name already has its own 'k' prefix
            if regs:
                rows = ",\n    ".join(
                    f'{{"{name}", \'{space}\', {reg}, {size}}}' for name, reg, size in regs
                )
                parts.append(
                    f"static constexpr D3D9ShaderConstantSlot {array_name}[] = {{\n    {rows}\n}};\n\n"
                )
            else:
                parts.append(
                    f"static constexpr D3D9ShaderConstantSlot* {array_name} = nullptr; // no named constants\n\n"
                )

        parts.append("} // namespace CNA::Internal::Renderers::DirectX9::Shaders\n")

        args.output.write_text("".join(parts))
        print(f"Written: {args.output} ({len(entries)} shaders)")


if __name__ == "__main__":
    main()
