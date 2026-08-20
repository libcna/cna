#!/usr/bin/env bash
# plans/plan_dx3.md design decision 9: a real, automated proof that the DIRECTX3 renderer never quietly
# reaches for the proven-broken execute-buffer Direct3D path (IDirect3D/IDirect3DDevice::Execute/
# D3DEXECUTEBUFFERDESC/IDirect3DExecuteBuffer/D3DINSTRUCTION/D3DOP_*) instead of the working
# IDirect3DDevice2::DrawPrimitive/DrawIndexedPrimitive one -- see plans/plan_dx3.md's status note (the
# DX30-0 spike, itself building on DX2-0) for why the execute-buffer surface is permanently
# off-limits here. Registered as the DirectX3_ExecuteBufferDiscipline CTest
# (cmake/Tests/DirectX3Tests.cmake) -- pure text check, no compiled binary, no Wine needed, runs
# identically whether cross-compiling or not.
#
# Adapted from scripts/check-directx2-execute-buffer-discipline.sh with exactly one change: DIRECTX3
# legitimately uses IDirectDraw2 (plans/plan_dx3.md design decision 2 -- this renderer's whole reason for
# being "real DIRECTX3" instead of "real DIRECTX2"), so IDirectDraw2 is NOT forbidden here, unlike DIRECTX2's own
# script (which forbids IDirectDraw2+ entirely, since DIRECTX2's 2D layer stays v1-only).
# IDirectDraw3+/IDirectDrawSurface2+/DDSURFACEDESC2 remain forbidden -- DIRECTX3 is v2-only, not v3+.
set -uo pipefail

repo_root="$1"
dx3_src="${repo_root}/modules/renderers/directx3/src"
dx3_include="${repo_root}/modules/renderers/directx3/include/CNA/Internal/Renderers/DirectX3"

if [ ! -d "$dx3_src" ] || [ ! -d "$dx3_include" ]; then
    echo "error: DIRECTX3 renderer directories not found under ${repo_root}" >&2
    exit 1
fi

pattern='IDirect3DDevice::Execute\b|D3DEXECUTEBUFFERDESC|IDirect3DExecuteBuffer|D3DINSTRUCTION|D3DOP_[A-Z]+|\bIDirect3D\b|\bIDirect3DDevice\b|IDirectDraw[347]\b|IDirectDrawSurface[234567]\b|DDSURFACEDESC2\b'

# Strip // line comments before matching -- this check is about what the CODE references, not
# about prose that documents the discipline by naming the forbidden symbols (which this renderer's
# own header/source comments legitimately do). Processed one file at a time (not concatenated via
# xargs) so a real hit reports the actual file/line.
violations=0
while IFS= read -r -d '' file; do
    if sed -E 's|//.*||' "$file" | grep -nE "$pattern" | sed "s|^|${file}:|"; then
        violations=1
    fi
done < <(find "$dx3_src" "$dx3_include" -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0)

if [ "$violations" -ne 0 ]; then
    echo "error: DIRECTX3 renderer source references a forbidden execute-buffer/legacy-interface symbol" >&2
    echo "above -- DIRECTX3 must use ONLY IDirect3DDevice2::DrawPrimitive/DrawIndexedPrimitive for its" >&2
    echo "3D layer (never IDirect3DDevice::Execute/D3DEXECUTEBUFFERDESC/IDirect3DExecuteBuffer/" >&2
    echo "D3DINSTRUCTION/D3DOP_*/the un-versioned IDirect3D/IDirect3DDevice), and ONLY v2 DirectDraw" >&2
    echo "for its 2D layer (IDirectDraw2 is fine; never IDirectDraw3+/IDirectDrawSurface2+/" >&2
    echo "DDSURFACEDESC2), per plans/plan_dx3.md design decision 9." >&2
    exit 1
fi

echo "OK: DIRECTX3 renderer source uses only DrawPrimitive-based Direct3D v2 and v2-only DirectDraw symbols."
exit 0
