#!/usr/bin/env bash
# REMED-BUILD-014 / REMED-GFX-005/006/007/010 D3D unblocker:
# Prove the committed D3D shader bytecode is BYTE-REPRODUCIBLE in this environment, so a shader
# *source* edit can be trusted to regenerate its bytecode deterministically (the precondition the
# GRAPHICS shader campaign recorded as "BYTECODE-BLOCKED" for every D3D slice).
#
# Two independent offline HLSL->bytecode pipelines exist (see each shaders/ dir):
#
#   1. D3DCommon (D3D11/D3D12)  hlsl_shaders.hpp   vs_5_0/ps_5_0 DXBC
#        tool  : modules/renderers/common/d3d/src/shaders/hlsl_compiler_tool.cpp (D3DCompile)
#        DLL   : Wine BUILTIN d3dcompiler_47.dll (vkd3d-shader) in ~/.wine-cna-d3d11
#        flags : D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3
#        script: compile_shaders_hlsl.py
#
#   2. D3D9 (custom NOXNA shaders)  d3d9_pbr_shaders.hpp / d3d9_skinned_vertex_color_shader.hpp
#        tool  : modules/renderers/directx9/src/shaders/fxc_tool.cpp (D3DCompile)
#        DLL   : REAL Microsoft d3dcompiler_47.dll in ~/.wine-cna-d3d9-spike
#        flags : D3DCOMPILE_OPTIMIZATION_LEVEL3, profiles vs_3_0/ps_3_0
#
# This script rebuilds each tool with MinGW-w64, recompiles the *unmodified* sources, and asserts
# the freshly compiled bytecode is byte-identical to what is checked in. It edits NOTHING under
# src/. Exit 0 = fully reproducible; non-zero = a divergence to investigate before any shader edit.
#
# Requires: x86_64-w64-mingw32-g++, wine, the two Wine prefixes above (see dx9-spike/README.md and
# programs.md SS10). The D3D9 real-Microsoft prefix is only needed for the D3D9 leg; pass
# --d3dcommon-only to skip it.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
D3DC="$REPO_ROOT/modules/renderers/common/d3d/src/shaders"
D9="$REPO_ROOT/modules/renderers/directx9/src/shaders"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/cna-d3d-repro.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
D3DCOMMON_ONLY=0
[ "${1:-}" = "--d3dcommon-only" ] && D3DCOMMON_ONLY=1

fail=0
export WINEDEBUG="${WINEDEBUG:--all}"

# --- helper: md5 of a named `uint8_t NAME[...] = {0x..,..};` array inside a generated header ---
array_md5() { # header name
  python3 - "$1" "$2" <<'PY'
import re,sys,hashlib
text=open(sys.argv[1]).read()
m=re.search(r"uint8_t\s+"+re.escape(sys.argv[2])+r"\s*\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;",text,re.DOTALL)
if not m: print("MISSING"); sys.exit(0)
b=bytes(int(x,16) for x in re.findall(r"0x([0-9a-fA-F]{2})",m.group(1)))
print(hashlib.md5(b).hexdigest())
PY
}

echo "=== Environment ==="
echo "wine: $(wine --version 2>/dev/null)"
echo "mingw: $(x86_64-w64-mingw32-g++ --version 2>/dev/null | head -1)"
echo

# ============================ 1. D3DCommon (D3D11/D3D12) ============================
echo "=== D3DCommon (vkd3d-shader, ~/.wine-cna-d3d11): no-change regen of hlsl_shaders.hpp ==="
if [ ! -f "$HOME/.wine-cna-d3d11/system.reg" ]; then
  echo "  SKIP: ~/.wine-cna-d3d11 not initialized"; fail=1
else
  CNA_D3D11_SKIP_DXVK_GATE=1 python3 "$D3DC/compile_shaders_hlsl.py" \
      --output "$WORK/hlsl_shaders.regen.hpp" >"$WORK/d3dc.log" 2>&1
  if [ $? -ne 0 ]; then echo "  FAIL: regen errored (see below)"; tail -5 "$WORK/d3dc.log"; fail=1
  elif diff -q "$D3DC/hlsl_shaders.hpp" "$WORK/hlsl_shaders.regen.hpp" >/dev/null; then
    n=$(grep -c "static constexpr uint8_t" "$D3DC/hlsl_shaders.hpp")
    echo "  OK: all $n DXBC arrays BYTE-IDENTICAL to the committed header (whole-file diff empty)"
    echo "  (committed DXBC embeds creator 'vkd3d-shader 1.14 (Wine bundled)'; reproduced here by Wine 10.0's bundled vkd3d-shader 1.14)"
  else
    echo "  FAIL: regenerated hlsl_shaders.hpp differs from committed"; fail=1
  fi
fi
echo

# ============================ 2. D3D9 custom NOXNA shaders ============================
if [ "$D3DCOMMON_ONLY" -eq 0 ]; then
  echo "=== D3D9 custom (real Microsoft d3dcompiler_47.dll, ~/.wine-cna-d3d9-spike): no-change regen ==="
  if [ ! -f "$HOME/.wine-cna-d3d9-spike/system.reg" ]; then
    echo "  SKIP: ~/.wine-cna-d3d9-spike not initialized"; fail=1
  else
    x86_64-w64-mingw32-g++ -std=c++23 -O2 -static-libgcc -static-libstdc++ \
        "$D9/fxc_tool.cpp" -o "$WORK/fxc_tool.exe" -ld3dcompiler -Wl,--allow-multiple-definition \
        2>"$WORK/fxc_build.log"
    if [ ! -f "$WORK/fxc_tool.exe" ]; then echo "  FAIL: fxc_tool build failed"; cat "$WORK/fxc_build.log"; fail=1; else
      export WINEPREFIX="$HOME/.wine-cna-d3d9-spike"
      # entry, profile, header, committed-array-name
      set -- \
        "cna/Pbr3D.hlsl VSPbr3D vs_3_0 d3d9_pbr_shaders.hpp kPbr3DVSBytecode" \
        "cna/Pbr3D.hlsl PSPbr3D ps_3_0 d3d9_pbr_shaders.hpp kPbr3DPSBytecode" \
        "cna/PbrSkinned3D.hlsl VSPbrSkinned3D vs_3_0 d3d9_pbr_shaders.hpp kPbrSkinned3DVSBytecode" \
        "cna/PbrSkinned3D.hlsl PSPbrSkinned3D ps_3_0 d3d9_pbr_shaders.hpp kPbrSkinned3DPSBytecode" \
        "cna/SkinnedVertexColor3D.hlsl VSSkinnedVertexColor3D vs_3_0 d3d9_skinned_vertex_color_shader.hpp kSkinnedVertexColor3DVSBytecode" \
        "cna/SkinnedVertexColor3D.hlsl PSSkinnedVertexColor3D ps_3_0 d3d9_skinned_vertex_color_shader.hpp kSkinnedVertexColor3DPSBytecode"
      ( cd "$D9" && for spec in "$@"; do
          read -r f entry prof header arr <<<"$spec"
          wine "$WORK/fxc_tool.exe" "$f" "$entry" "$prof" "$WORK/out.bin" >/dev/null 2>&1
          got=$(md5sum "$WORK/out.bin" 2>/dev/null | awk '{print $1}')
          exp=$(array_md5 "$D9/$header" "$arr")
          if [ "$got" = "$exp" ]; then echo "  OK  $arr byte-identical"; else echo "  FAIL $arr got=$got exp=$exp"; exit 7; fi
        done ) || fail=1
    fi
  fi
  echo
fi

if [ "$fail" -eq 0 ]; then
  echo "RESULT: D3D shader bytecode is FULLY REPRODUCIBLE in this environment."
else
  echo "RESULT: at least one D3D pipeline did not reproduce byte-for-byte (see above)."
fi
exit "$fail"
