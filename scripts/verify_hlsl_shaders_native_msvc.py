#!/usr/bin/env python3
"""Real-Windows/MSVC counterpart to compile_shaders_hlsl.py's own Wine+MinGW compile loop.

plans/plan_dx.md DX-90/DX-114: those rows explicitly call out "shader-generation check" as something a
GitHub-hosted Windows CI runner can usefully verify, distinct from (and not a substitute for) the
swap-chain/tearing/device-lost/driver-parity items that need a real display + GPU driver. This
script is that check: it builds hlsl_compiler_tool.cpp with the real, native cl.exe (assumes an
MSVC developer environment is already active on PATH -- e.g. via ilammy/msvc-dev-cmd in CI) and
recompiles every one of DX-13-hlsl's 20 checked-in .hlsl sources against a genuine
D3DCOMPILER_47.dll (not Wine's), confirming the shaders this project ships are still real,
compiler-accepted HLSL on an actual Windows machine -- something Wine+DXVK could never prove on
its own, no matter how many times it passed.

This intentionally does NOT byte-diff the result against the checked-in hlsl_shaders.hpp: a
different D3DCOMPILER_47.dll version/build can legitimately produce different-but-equivalent DXBC
bytes (different compiler build number, optimizer revision, etc.) -- that would be a false-alarm
hard failure, not a real regression. The real signal this script provides is "does D3DCompile()
on real Windows still accept this HLSL at all", which a MinGW-only Wine dev loop cannot check.

Usage: python scripts/verify_hlsl_shaders_native_msvc.py
Exit code 0 = every shader compiled; nonzero = at least one real compile failure (printed).
"""

import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SHADERS_DIR = REPO_ROOT / "src" / "CNA" / "Internal" / "Renderers" / "D3DCommon" / "shaders"
sys.path.insert(0, str(SHADERS_DIR))
from compile_shaders_hlsl import SHADERS  # noqa: E402  (reuses the one canonical shader list)


def build_compiler_tool(out_exe: Path) -> None:
    print("Building hlsl_compiler_tool.exe with native cl.exe ...", end=" ", flush=True)
    result = subprocess.run(
        [
            "cl.exe", "/nologo", "/std:c++20", "/EHsc", "/O2",
            str(SHADERS_DIR / "hlsl_compiler_tool.cpp"),
            "/Fe:" + str(out_exe),
            "/link", "d3dcompiler.lib",
        ],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"cl.exe failed to build hlsl_compiler_tool.exe (exit {result.returncode})")
    print("OK")


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        tool_exe = tmp_dir / "hlsl_compiler_tool.exe"
        build_compiler_tool(tool_exe)

        failures = []
        for filename, entry, profile, cname in SHADERS:
            hlsl_path = SHADERS_DIR / filename
            out_dxbc = tmp_dir / (filename + ".dxbc")
            result = subprocess.run(
                [str(tool_exe), str(hlsl_path), entry, profile, str(out_dxbc)],
                capture_output=True, text=True,
            )
            if result.returncode != 0 or not out_dxbc.exists():
                failures.append(filename)
                print(f"[FAIL] {filename} [{entry}/{profile}]")
                sys.stderr.write(result.stdout)
                sys.stderr.write(result.stderr)
                continue
            blob = out_dxbc.read_bytes()
            if blob[:4] != b"DXBC":
                failures.append(filename)
                print(f"[FAIL] {filename}: output does not start with the DXBC magic bytes")
                continue
            print(f"[PASS] {filename} [{entry}/{profile}] -> {len(blob)} bytes, real DXBC")

        print()
        if failures:
            print(f"RESULT: {len(failures)}/{len(SHADERS)} shaders FAILED real MSVC D3DCompile(): {failures}")
            return 1

        print(f"RESULT: all {len(SHADERS)} shaders compiled cleanly against a real D3DCOMPILER_47.dll")
        return 0


if __name__ == "__main__":
    sys.exit(main())
