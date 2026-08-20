#!/usr/bin/env python3
"""Compile the vendored XNA Stock Effects HLSL to real D3D9 (SM2) bytecode and emit a checked-in
C++ header with embedded byte arrays.

plans/plan_dx9.md Phase D9-7 (D9-71). Mirrors src/CNA/Internal/Renderers/D3DCommon/shaders/
compile_shaders_hlsl.py's role (cross-build a tiny D3DCompile()-calling .exe, run it once per
shader through Wine, embed the resulting bytecode as C++ arrays) with three D3D9-specific
differences:

  1. The entry-point list is *parsed* from the vendored .fx files' own `compile [vp]s_2_0 ...`
     statements (design decision: never hand-maintained -- D9-70's own README table already found
     a hand-typed entry-point list can silently invent wrong names).
  2. Compilation needs the REAL Microsoft d3dcompiler_47.dll, which only exists in the
     `~/.wine-cna-d3d9-spike` prefix (not `~/.wine-cna-d3d11`, which has DXVK for runtime device
     tests but a stub/wrong compiler). This script invokes `wine` directly with that prefix, NOT
     through scripts/run-wine-dxvk9.sh -- compiling a shader never opens a D3D9 device, so that
     script's DXVK-marker gate would reject this run for an unrelated reason.
  3. Locked-in compile flags: D3DCOMPILE_OPTIMIZATION_LEVEL3, no others -- fxc_tool.cpp hardcodes
     this already; it is the exact configuration Phase D9-0's own D9-73 spike proved gives 61/66
     byte-identical matches against Microsoft's shipped .fxb bytecode. Do not change without
     re-running dx9-spike/compare_against_fxb.py and updating D9-73's own recorded numbers.

Requires: x86_64-w64-mingw32-g++ (mingw-w64 apt package) and an initialized
~/.wine-cna-d3d9-spike prefix with the real d3dcompiler_47.dll (see dx9-spike/README.md). Not part
of any CMake target -- run by hand after a vendor refresh (D9-70) or a compile-flag change.

Usage: python3 compile_shaders_sm2.py [--output PATH] [--wineprefix PATH]
"""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
XNA_DIR = SCRIPT_DIR / "xna"
COMPILER_TOOL_SRC = SCRIPT_DIR / "fxc_tool.cpp"
DEFAULT_WINEPREFIX = Path.home() / ".wine-cna-d3d9-spike"

# (filename, effect name) -- the 6 real .fx files that contain `compile` statements. The .fxh
# headers (Macros/Common/Lighting/Structures) are #include-only, never compiled directly.
EFFECT_FILES = [
    ("BasicEffect.fx", "BasicEffect"),
    ("AlphaTestEffect.fx", "AlphaTestEffect"),
    ("DualTextureEffect.fx", "DualTextureEffect"),
    ("EnvironmentMapEffect.fx", "EnvironmentMapEffect"),
    ("SkinnedEffect.fx", "SkinnedEffect"),
    ("SpriteEffect.fx", "SpriteEffect"),
]

COMPILE_STATEMENT_RE = re.compile(r"compile\s+(vs_2_0|ps_2_0)\s+([A-Za-z0-9_]+)")


def discover_entry_points():
    """Parses each vendored .fx file's own `compile vs_2_0/ps_2_0 <entry>` statements.

    Returns a list of (fx_filename, effect_name, entry, profile, cpp_array_name) tuples, in
    file order then first-seen order within a file (stable, reproducible header output).
    """
    entries = []
    seen_total = set()
    for filename, effect_name in EFFECT_FILES:
        fx_path = XNA_DIR / filename
        if not fx_path.exists():
            print(f"ERROR: {fx_path} not found -- run D9-70's vendoring step first", file=sys.stderr)
            sys.exit(1)
        text = fx_path.read_text()
        seen_in_file = set()
        for m in COMPILE_STATEMENT_RE.finditer(text):
            profile, entry = m.group(1), m.group(2)
            if entry in seen_in_file:
                continue  # same entry point referenced more than once in the ShaderIndex arrays
            seen_in_file.add(entry)
            if entry in seen_total:
                print(f"ERROR: entry point '{entry}' appears in more than one .fx file", file=sys.stderr)
                sys.exit(1)
            seen_total.add(entry)
            cpp_name = f"k{effect_name}_{entry}"
            entries.append((filename, effect_name, entry, profile, cpp_name))
    return entries


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


def compile_one(tool_exe: Path, wineprefix: Path, fx_path: Path, entry: str, profile: str, out_bin: Path) -> bytes:
    env = {"WINEPREFIX": str(wineprefix)}
    import os
    full_env = dict(os.environ)
    full_env.update(env)
    result = subprocess.run(
        ["wine", str(tool_exe), str(fx_path), entry, profile, str(out_bin)],
        capture_output=True, text=True, env=full_env,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"D3DCompile failed for {fx_path.name} [{entry}/{profile}] (exit {result.returncode})")
    return out_bin.read_bytes()


def bytecode_to_cpp_array(name: str, data: bytes) -> str:
    hex_bytes = [f"0x{b:02x}u" for b in data]
    rows = [", ".join(hex_bytes[i:i + 12]) for i in range(0, len(hex_bytes), 12)]
    body = ",\n    ".join(rows)
    return (
        f"static constexpr uint8_t {name}[] = {{\n"
        f"    {body}\n"
        f"}};\n"
        f"static constexpr size_t {name}_size = sizeof({name});\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=SCRIPT_DIR / "d3d9_shaders.hpp")
    parser.add_argument("--wineprefix", type=Path, default=DEFAULT_WINEPREFIX)
    args = parser.parse_args()

    entries = discover_entry_points()
    print(f"Discovered {len(entries)} entry points across {len(EFFECT_FILES)} .fx files "
          f"(parsed from their own compile statements, not hand-maintained).")
    if len(entries) != 66:
        print(f"WARNING: expected 66 entry points (D9-1's own verified count), found {len(entries)}. "
              f"Continuing, but double-check the vendored .fx files.", file=sys.stderr)

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        tool_exe = tmp_dir / "fxc_tool.exe"
        build_compiler_tool(tool_exe)

        parts = [
            "// SPDX-License-Identifier: MS-PL\n"
            "// AUTO-GENERATED by compile_shaders_sm2.py -- do not edit by hand.\n"
            "// Re-run compile_shaders_sm2.py to regenerate after a vendored .fx source changes\n"
            "// (D9-70) or the compile flags change (D9-71/D9-73).\n"
            "#pragma once\n"
            "#include <cstddef>\n"
            "#include <cstdint>\n\n"
            "namespace CNA::Internal::Renderers::DirectX9::Shaders {\n\n"
        ]

        manifest_rows = []
        for filename, effect_name, entry, profile, cpp_name in entries:
            fx_path = XNA_DIR / filename
            out_bin = tmp_dir / f"{effect_name}_{entry}.bin"
            print(f"Compiling {filename} [{entry}/{profile}] ...", end=" ", flush=True)
            bytecode = compile_one(tool_exe, args.wineprefix, fx_path, entry, profile, out_bin)
            print(f"OK ({len(bytecode)} bytes)")
            parts.append(bytecode_to_cpp_array(cpp_name, bytecode) + "\n")

            is_vs = "true" if profile == "vs_2_0" else "false"
            manifest_rows.append(
                f'{{"{effect_name}_{entry}", {cpp_name}, {cpp_name}_size, {is_vs}}}'
            )

        parts.append(
            "/// D9-74: every compiled entry point above, in one lookup-by-name table --\n"
            "/// D3D9ShaderCache's own source of truth for CreateVertexShader()/CreatePixelShader().\n"
            "/// Regenerated alongside the arrays above; never hand-maintained.\n"
            "struct D3D9EmbeddedShader\n"
            "{\n"
            "    const char* name;      ///< \"<EffectName>_<EntryPointName>\", e.g. \"BasicEffect_VSBasic\".\n"
            "    const uint8_t* bytecode;\n"
            "    size_t size;\n"
            "    bool isVertexShader;   ///< true = vs_2_0 (CreateVertexShader), false = ps_2_0 (CreatePixelShader).\n"
            "};\n\n"
            "static constexpr D3D9EmbeddedShader kAllShaders[] = {\n    "
            + ",\n    ".join(manifest_rows) +
            "\n};\n"
            f"static constexpr size_t kAllShadersCount = {len(manifest_rows)};\n\n"
        )

        parts.append("} // namespace CNA::Internal::Renderers::DirectX9::Shaders\n")

        args.output.write_text("".join(parts))
        print(f"Written: {args.output} ({len(entries)} shaders)")


if __name__ == "__main__":
    main()
