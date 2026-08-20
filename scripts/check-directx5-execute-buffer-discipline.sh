#!/usr/bin/env bash
# plans/plan_dx5.md design decision 12: a real, automated proof that the DIRECTX5 renderer never quietly
# reaches for the proven-broken execute-buffer Direct3D path (IDirect3D/IDirect3DDevice::Execute/
# D3DEXECUTEBUFFERDESC/IDirect3DExecuteBuffer/D3DINSTRUCTION/D3DOP_*) or the old D3DVERTEXTYPE-enum
# vertex-type submission (D3DVT_*) instead of the working IDirect3DDevice3::DrawPrimitive/
# DrawIndexedPrimitive + D3DFVF_TLVERTEX one -- see plans/plan_dx5.md's status note (the DX5-0 spike,
# itself building on DX2-0/DX30-0) for why both are permanently off-limits here. Registered as the
# DirectX5_ExecuteBufferDiscipline CTest (cmake/Tests/DirectX5Tests.cmake) -- pure text check, no compiled
# binary, no Wine needed, runs identically whether cross-compiling or not.
#
# Adapted from scripts/check-directx3-execute-buffer-discipline.sh with three changes: (1) DIRECTX5
# legitimately uses IDirectDraw4/IDirectDrawSurface4/DDSURFACEDESC2 throughout (plans/plan_dx5.md design
# decision 2 -- every surface, not just the top object, unlike DIRECTX3's more limited v2 upgrade), so
# those are NOT forbidden here. IDirectDraw2/3/7+ remain forbidden -- DIRECTX5 is v4-only, not v2/v3/7+
# (the one legitimate bare IDirectDraw usage is the transient v1 pointer DirectDrawCreate() itself
# always returns, immediately upgraded and released -- matched as \bIDirectDraw\b so it isn't
# flagged, same as DIRECTX3's own script does for its own v1 upgrade step). (2) unlike DIRECTX3 (which
# never upgrades its surfaces past v1, so leaves bare IDirectDrawSurface unrestricted), DIRECTX5
# upgrades EVERY surface to v4 -- bare IDirectDrawSurface (v1) is now ALSO forbidden, a real,
# stricter proof the v4 upgrade is complete throughout (every prose mention of the general
# IDirectDrawSurface concept in this renderer's own source was updated to say IDirectDrawSurface4
# specifically before this check was written, so no false positives). (3) D3DVT_[A-Z]+ (the old
# D3DVERTEXTYPE enum's value names, e.g. D3DVT_TLVERTEX) are newly forbidden -- DIRECTX5's whole reason
# for being "real DIRECTX5" instead of "real DIRECTX3" is submitting via the new D3DFVF_TLVERTEX bitmask
# instead.
set -uo pipefail

repo_root="$1"
dx5_src="${repo_root}/modules/renderers/directx5/src"
dx5_include="${repo_root}/modules/renderers/directx5/include/CNA/Internal/Renderers/DirectX5"

if [ ! -d "$dx5_src" ] || [ ! -d "$dx5_include" ]; then
    echo "error: DIRECTX5 renderer directories not found under ${repo_root}" >&2
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
done < <(find "$dx5_src" "$dx5_include" -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0)

if [ "$violations" -ne 0 ]; then
    echo "error: DIRECTX5 renderer source references a forbidden execute-buffer/legacy-interface/" >&2
    echo "old-vertex-type symbol above -- DIRECTX5 must use ONLY IDirect3DDevice3::DrawPrimitive/" >&2
    echo "DrawIndexedPrimitive with the D3DFVF_TLVERTEX FVF bitmask for its 3D layer (never" >&2
    echo "IDirect3DDevice::Execute/D3DEXECUTEBUFFERDESC/IDirect3DExecuteBuffer/D3DINSTRUCTION/" >&2
    echo "D3DOP_*/the un-versioned IDirect3D/IDirect3DDevice/the old D3DVT_* vertex-type enum" >&2
    echo "values), and ONLY v4 DirectDraw for its 2D layer (IDirectDrawSurface4/DDSURFACEDESC2 are" >&2
    echo "fine; never IDirectDraw2/3/7+/IDirectDrawSurface[1235-7]+), per plans/plan_dx5.md design" >&2
    echo "decision 12." >&2
    exit 1
fi

echo "OK: DIRECTX5 renderer source uses only FVF-based DrawPrimitive Direct3D v3 and v4-only DirectDraw symbols."
exit 0
