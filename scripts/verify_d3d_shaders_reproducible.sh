#!/usr/bin/env bash
# the reproducible D3D shader build/006/007/010 D3D unblocker:
# Prove the committed D3D shader bytecode is BYTE-REPRODUCIBLE in this environment, so a shader
# *source* edit can be trusted to regenerate its bytecode deterministically (the precondition the
# GRAPHICS shader campaign recorded as "BYTECODE-BLOCKED" for every D3D slice).
#
# The offline HLSL->bytecode pipeline of the D3D9 custom CNAEXT shaders
# (d3d9_pbr_shaders.hpp / d3d9_skinned_vertex_color_shader.hpp):
#   tool  : modules/renderers/directx9/src/shaders/fxc_tool.cpp (D3DCompile)
#   DLL   : REAL Microsoft d3dcompiler_47.dll in ~/.wine-cna-d3d9-spike
#   flags : D3DCOMPILE_OPTIMIZATION_LEVEL3, profiles vs_3_0/ps_3_0
#
# This script rebuilds the tool with MinGW-w64, recompiles the *unmodified* sources, and asserts
# the freshly compiled bytecode is byte-identical to what is checked in. It edits NOTHING under
# src/. Exit 0 = fully reproducible; non-zero = a divergence to investigate before any shader edit.
#
# Requires: x86_64-w64-mingw32-g++, wine and the Wine prefix above (see dx9-spike/README.md and
# programs.md SS10).
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
D9="$REPO_ROOT/modules/renderers/directx9/src/shaders"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/cna-d3d-repro.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

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

# ============================ D3D9 custom CNAEXT shaders ============================
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
      "cna/BasicPunctual3D.hlsl VSBasicPunctual vs_3_0 d3d9_pbr_shaders.hpp kBasicPunctual3DVSBytecode" \
      "cna/BasicPunctual3D.hlsl VSBasicPunctualVc vs_3_0 d3d9_pbr_shaders.hpp kBasicPunctual3DColorVSBytecode" \
      "cna/BasicPunctual3D.hlsl VSBasicPunctualTx vs_3_0 d3d9_pbr_shaders.hpp kBasicPunctual3DTextureVSBytecode" \
      "cna/BasicPunctual3D.hlsl VSBasicPunctualTxVc vs_3_0 d3d9_pbr_shaders.hpp kBasicPunctual3DTextureColorVSBytecode" \
      "cna/BasicPunctual3D.hlsl VSBasicPunctualSkinned vs_3_0 d3d9_pbr_shaders.hpp kBasicPunctualSkinned3DVSBytecode" \
      "cna/BasicPunctual3D.hlsl VSBasicPunctualSkinnedVc vs_3_0 d3d9_pbr_shaders.hpp kBasicPunctualSkinned3DColorVSBytecode" \
      "cna/BasicPunctual3D.hlsl PSBasicPunctual ps_3_0 d3d9_pbr_shaders.hpp kBasicPunctual3DPSBytecode" \
      "cna/BasicPunctual3D.hlsl PSBasicPunctualTx ps_3_0 d3d9_pbr_shaders.hpp kBasicPunctual3DTexturePSBytecode" \
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

if [ "$fail" -eq 0 ]; then
  echo "RESULT: D3D shader bytecode is FULLY REPRODUCIBLE in this environment."
else
  echo "RESULT: the D3D9 shader bytecode did not reproduce byte-for-byte (see above)."
fi
exit "$fail"
