#!/usr/bin/env bash
# plans/plan_d3d10.md: a real, automated proof that the D3D10 renderer never quietly reaches for any of
# the fixed-function-era symbols this whole DIRECTX1..DIRECTX8 family used but D3D10 genuinely removed --
# D3DFVF_* (the generic FVF bitmask model itself is gone, replaced by D3D10_INPUT_ELEMENT_DESC),
# D3DTLVERTEX/D3DVT_*/D3DRENDERSTATE_*/D3DRS_* (D3D10 has no per-call render-state setter at all --
# ID3D10BlendState/DepthStencilState/RasterizerState are real objects instead), and
# SetVertexShader(rawFvf) (D3D8's own real-but-legacy idiom, meaningless here since D3D10 has no
# fixed-function vertex processing to select via an FVF value at all). A bare reference to any of
# these would mean accidental copy-paste from an earlier renderer, not real D3D10 code. Registered
# as the DirectX10_LegacyInterfaceDiscipline CTest (cmake/Tests/DirectX10Tests.cmake) -- pure text check, no
# compiled binary, no Wine needed, runs identically whether cross-compiling or not.
set -uo pipefail

repo_root="$1"
d3d10_src="${repo_root}/modules/renderers/directx10/src"
d3d10_include="${repo_root}/modules/renderers/directx10/include/CNA/Internal/Renderers/DirectX10"

if [ ! -d "$d3d10_src" ] || [ ! -d "$d3d10_include" ]; then
    echo "error: D3D10 renderer directories not found under ${repo_root}" >&2
    exit 1
fi

pattern='D3DFVF_[A-Z0-9]+|D3DTLVERTEX|D3DVT_[A-Z]+|D3DRENDERSTATE_[A-Z]+|D3DRS_[A-Z]+|SetVertexShader\('

# Strip BOTH // line comments and /* ... */ block comments (this renderer's own Doxygen /** */
# doc comments legitimately name several of these forbidden symbols while explaining what D3D10
# removed) before matching -- this check is about what the CODE references, not prose documenting
# the discipline by naming the forbidden symbols. Line numbers are preserved (the matched comment
# text is replaced by an equal count of bare newlines) so a real hit reports the actual file/line.
violations=0
while IFS= read -r -d '' file; do
    if perl -0777 -pe 's{/\*.*?\*/}{ (my $c = $&) =~ s/[^\n]//g; $c }gse; s{//.*}{}g' "$file" | grep -nE "$pattern" | sed "s|^|${file}:|"; then
        violations=1
    fi
done < <(find "$d3d10_src" "$d3d10_include" -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0)

if [ "$violations" -ne 0 ]; then
    echo "error: D3D10 renderer source references a forbidden fixed-function-era symbol above --" >&2
    echo "D3D10 has no D3DFVF_*/D3DTLVERTEX/D3DVT_*/D3DRENDERSTATE_*/D3DRS_* render-state naming" >&2
    echo "and no SetVertexShader(rawFvf) idiom at all -- every draw uses real D3D10_INPUT_ELEMENT_DESC" >&2
    echo "input layouts and real ID3D10BlendState/DepthStencilState/RasterizerState objects instead." >&2
    exit 1
fi

echo "OK: D3D10 renderer source has no fixed-function-era symbols and uses only real HLSL shaders + state objects."
exit 0
