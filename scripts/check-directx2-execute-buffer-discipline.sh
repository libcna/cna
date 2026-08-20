#!/usr/bin/env bash
# plans/plan_dx2.md design decision 12: a real, automated proof that the DIRECTX2 renderer never quietly
# reaches for the proven-broken execute-buffer Direct3D path (IDirect3D/IDirect3DDevice::Execute/
# D3DEXECUTEBUFFERDESC/IDirect3DExecuteBuffer/D3DINSTRUCTION/D3DOP_*) instead of the working
# IDirect3DDevice2::DrawPrimitive/DrawIndexedPrimitive one -- see plans/plan_dx2.md's status note (the
# DX2-0 spike) for why the execute-buffer surface is permanently off-limits here. Registered as the
# DirectX2_ExecuteBufferDiscipline CTest (cmake/Tests/DirectX2Tests.cmake) -- pure text check, no compiled
# binary, no Wine needed, runs identically whether cross-compiling or not.
#
# Unlike scripts/check-directx1-v1-only.sh (which forbids ALL of IDirect3D*/d3d.h, since DirectX 1 has no
# Direct3D at all), DIRECTX2 legitimately uses IDirect3D2/IDirect3DDevice2/IDirect3DViewport2/
# IDirect3DTexture2/D3DTLVERTEX/DrawPrimitive/DrawIndexedPrimitive for its own real 3D layer
# (plans/plan_dx2.md section 1) -- those must NOT be flagged. What's forbidden here is narrower and
# permanent:
#   - IDirect3DDevice::Execute (the literal execute-buffer submission call; IDirect3DDevice2::
#     methods are unaffected -- matched as a literal substring, not just a bare `Execute` grep,
#     which would also hit IDirect3DDevice2's own DrawPrimitive/DrawIndexedPrimitive call sites)
#   - D3DEXECUTEBUFFERDESC / IDirect3DExecuteBuffer / D3DINSTRUCTION / D3DOP_<NAME> (the
#     execute-buffer instruction-stream types)
#   - the plain, un-versioned IDirect3D/IDirect3DDevice interface names used as a type -- matched as
#     WHOLE WORDS (\b...\b) so this does NOT also flag IDirect3D2/IDirect3DDevice2 (the extra `2`
#     character immediately after is itself a word character, so \b does not match between `D` and
#     `2` -- verified with a throwaway grep before writing this script)
# DIRECTX2's DirectDraw (2D) layer stays v1-only, same permanent boundary DX1-1's own grep CTest already
# enforces: IDirectDraw2+/IDirectDrawSurface2+/DDSURFACEDESC2 are forbidden here too.
set -uo pipefail

repo_root="$1"
dx2_src="${repo_root}/modules/renderers/directx2/src"
dx2_include="${repo_root}/modules/renderers/directx2/include/CNA/Internal/Renderers/DirectX2"

if [ ! -d "$dx2_src" ] || [ ! -d "$dx2_include" ]; then
    echo "error: DIRECTX2 renderer directories not found under ${repo_root}" >&2
    exit 1
fi

pattern='IDirect3DDevice::Execute\b|D3DEXECUTEBUFFERDESC|IDirect3DExecuteBuffer|D3DINSTRUCTION|D3DOP_[A-Z]+|\bIDirect3D\b|\bIDirect3DDevice\b|IDirectDraw[247]\b|IDirectDrawSurface[234567]\b|DDSURFACEDESC2\b'

# Strip // line comments before matching -- this check is about what the CODE references, not
# about prose that documents the discipline by naming the forbidden symbols (which this renderer's
# own header/source comments legitimately do). Processed one file at a time (not concatenated via
# xargs) so a real hit reports the actual file/line.
violations=0
while IFS= read -r -d '' file; do
    if sed -E 's|//.*||' "$file" | grep -nE "$pattern" | sed "s|^|${file}:|"; then
        violations=1
    fi
done < <(find "$dx2_src" "$dx2_include" -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0)

if [ "$violations" -ne 0 ]; then
    echo "error: DIRECTX2 renderer source references a forbidden execute-buffer/legacy-interface symbol" >&2
    echo "above -- DIRECTX2 must use ONLY IDirect3DDevice2::DrawPrimitive/DrawIndexedPrimitive for its 3D" >&2
    echo "layer (never IDirect3DDevice::Execute/D3DEXECUTEBUFFERDESC/IDirect3DExecuteBuffer/" >&2
    echo "D3DINSTRUCTION/D3DOP_*/the un-versioned IDirect3D/IDirect3DDevice), and ONLY v1 DirectDraw" >&2
    echo "interfaces for its 2D layer (never IDirectDraw2+/IDirectDrawSurface2+/DDSURFACEDESC2)," >&2
    echo "per plans/plan_dx2.md design decision 12." >&2
    exit 1
fi

echo "OK: DIRECTX2 renderer source uses only DrawPrimitive-based Direct3D v2 and v1-only DirectDraw symbols."
exit 0
