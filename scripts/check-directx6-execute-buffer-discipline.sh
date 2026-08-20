#!/usr/bin/env bash
# plans/plan_dx6.md design decision 12: a real, automated proof that the DIRECTX6 renderer never quietly
# reaches for the proven-broken execute-buffer Direct3D path (IDirect3D/IDirect3DDevice::Execute/
# D3DEXECUTEBUFFERDESC/IDirect3DExecuteBuffer/D3DINSTRUCTION/D3DOP_*) or the old D3DVERTEXTYPE-enum
# vertex-type submission (D3DVT_*) instead of the working IDirect3DDevice3::DrawPrimitive/
# DrawIndexedPrimitive + D3DFVF_TLVERTEX one -- see plans/plan_dx6.md's status note (the DX6-0 spike,
# itself building on DX2-0/DX30-0/DX5-0) for why both are permanently off-limits here. Registered
# as the DirectX6_ExecuteBufferDiscipline CTest (cmake/Tests/DirectX6Tests.cmake) -- pure text check, no
# compiled binary, no Wine needed, runs identically whether cross-compiling or not.
#
# Adapted verbatim from scripts/check-directx5-execute-buffer-discipline.sh -- DIRECTX6 introduces no new
# COM interface at all (confirmed via header inspection during the DX6-0 spike: no
# IDirect3D4/IDirect3DDevice4 exists), so its execute-buffer/legacy-interface/old-vertex-type
# discipline is identical to DIRECTX5's: (1) IDirectDraw4/IDirectDrawSurface4/DDSURFACEDESC2 are
# legitimate throughout; IDirectDraw2/3/7+ remain forbidden (the one legitimate bare IDirectDraw
# usage is the transient v1 pointer DirectDrawCreate() itself always returns, immediately upgraded
# and released -- matched as \bIDirectDraw\b so it isn't flagged). (2) every surface is upgraded to
# v4, so bare IDirectDrawSurface (v1) is also forbidden. (3) D3DVT_[A-Z]+ (the old D3DVERTEXTYPE
# enum's value names, e.g. D3DVT_TLVERTEX) are forbidden -- DIRECTX6 keeps DIRECTX5's D3DFVF_TLVERTEX
# submission unchanged; the new-for-DIRECTX6 stencil render states (D3DRENDERSTATE_STENCILENABLE etc.,
# D3DSTENCILOP_* values) are legitimate on the same IDirect3DDevice3 and are not restricted by this
# check.
set -uo pipefail

repo_root="$1"
dx6_src="${repo_root}/modules/renderers/directx6/src"
dx6_include="${repo_root}/modules/renderers/directx6/include/CNA/Internal/Renderers/DirectX6"

if [ ! -d "$dx6_src" ] || [ ! -d "$dx6_include" ]; then
    echo "error: DIRECTX6 renderer directories not found under ${repo_root}" >&2
    exit 1
fi

pattern='IDirect3DDevice::Execute\b|D3DEXECUTEBUFFERDESC|IDirect3DExecuteBuffer|D3DINSTRUCTION|D3DOP_[A-Z]+|\bIDirect3D\b|\bIDirect3DDevice\b|D3DVT_[A-Z]+|IDirectDraw[237]\b|\bIDirectDrawSurface\b|IDirectDrawSurface[237]\b'

# Strip // line comments before matching -- this check is about what the CODE references, not
# about prose that documents the discipline by naming the forbidden symbols (which this renderer's
# own header/source comments legitimately do). Processed one file at a time (not concatenated via
# xargs) so a real hit reports the actual file/line.
violations=0
while IFS= read -r -d '' file; do
    if sed -E 's|//.*||' "$file" | grep -nE "$pattern" | sed "s|^|${file}:|"; then
        violations=1
    fi
done < <(find "$dx6_src" "$dx6_include" -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0)

if [ "$violations" -ne 0 ]; then
    echo "error: DIRECTX6 renderer source references a forbidden execute-buffer/legacy-interface/" >&2
    echo "old-vertex-type symbol above -- DIRECTX6 must use ONLY IDirect3DDevice3::DrawPrimitive/" >&2
    echo "DrawIndexedPrimitive with the D3DFVF_TLVERTEX FVF bitmask for its 3D layer (never" >&2
    echo "IDirect3DDevice::Execute/D3DEXECUTEBUFFERDESC/IDirect3DExecuteBuffer/D3DINSTRUCTION/" >&2
    echo "D3DOP_*/the un-versioned IDirect3D/IDirect3DDevice/the old D3DVT_* vertex-type enum" >&2
    echo "values), and ONLY v4 DirectDraw for its 2D layer (IDirectDrawSurface4/DDSURFACEDESC2 are" >&2
    echo "fine; never IDirectDraw2/3/7+/IDirectDrawSurface[1235-7]+), per plans/plan_dx6.md design" >&2
    echo "decision 12." >&2
    exit 1
fi

echo "OK: DIRECTX6 renderer source uses only FVF-based DrawPrimitive Direct3D v3 and v4-only DirectDraw symbols."
exit 0
