#!/usr/bin/env bash
# plans/plan_dx7.md design decision 16: a real, automated proof that the DIRECTX7 renderer never quietly
# reaches for the proven-broken execute-buffer Direct3D path (IDirect3D/IDirect3DDevice::Execute/
# D3DEXECUTEBUFFERDESC/IDirect3DExecuteBuffer/D3DINSTRUCTION/D3DOP_*), the old D3DVERTEXTYPE-enum
# vertex-type submission (D3DVT_*), any pre-v7 DirectDraw/Direct3D interface, the removed viewport
# object (CreateViewport/AddViewport/SetCurrentViewport/DeleteViewport/Clear2), or the old
# texture-handle binding mechanism (D3DRENDERSTATE_TEXTUREHANDLE/IDirect3DTexture2::GetHandle) --
# see plans/plan_dx7.md's status note (the DX7-0 spike) for why all of these are permanently off-limits
# here. Registered as the DirectX7_ExecuteBufferDiscipline CTest (cmake/Tests/DirectX7Tests.cmake) -- pure
# text check, no compiled binary, no Wine needed, runs identically whether cross-compiling or not.
#
# Unlike DIRECTX6 (no new interface at all vs DIRECTX5, so its discipline check is identical to DIRECTX5's), DIRECTX7
# is a real architectural change -- this check is stricter in four new ways: (1) DIRECTX7 is v7-only, not
# v4-only -- IDirectDraw[2-6]\b and IDirectDrawSurface[1-6] are ALL now forbidden (the one
# legitimate exception is a bare \bIDirectDraw\b/\bIDirectDrawSurface\b prose mention, which this
# renderer's own comments make when explaining what earlier eras used -- comments are stripped
# before matching, so this is safe). (2) IDirect3D3/IDirect3DDevice3/IDirect3DViewport3 (and any
# bare \bIDirect3D7\b look-alike typo of an older version) are forbidden -- DIRECTX7 uses ONLY
# IDirect3D7/IDirect3DDevice7. (3) CreateViewport/AddViewport/SetCurrentViewport/DeleteViewport/
# Clear2 are forbidden -- the whole viewport object no longer exists in DIRECTX7 at all
# (IDirect3DDevice7::SetViewport/Clear are direct device methods instead). (4)
# D3DRENDERSTATE_TEXTUREHANDLE and IDirect3DTexture2 are forbidden -- DIRECTX7 binds textures via a
# direct SetTexture(stage, surface) call, with no handle indirection at all. (5) Real, empirically
# found while running the first full DIRECTX7 CTest pass (not anticipated by the DX7-0 spike, which never
# exercised texture blending): Wine's IDirect3DDevice7::SetRenderState REJECTS
# D3DRENDERSTATE_TEXTUREMAPBLEND outright ("Render state 0x15 is invalid in d3d7") -- the legacy
# per-render-state texture blend mode is gone as of this device revision, so it is now forbidden
# too, superseded by SetTextureStageState/D3DTSS_COLOROP (D3DTOP_MODULATE reproduces the identical
# diffuse*texture modulation).
set -uo pipefail

repo_root="$1"
dx7_src="${repo_root}/modules/renderers/directx7/src"
dx7_include="${repo_root}/modules/renderers/directx7/include/CNA/Internal/Renderers/DirectX7"

if [ ! -d "$dx7_src" ] || [ ! -d "$dx7_include" ]; then
    echo "error: DIRECTX7 renderer directories not found under ${repo_root}" >&2
    exit 1
fi

pattern='IDirect3DDevice::Execute\b|D3DEXECUTEBUFFERDESC|IDirect3DExecuteBuffer|D3DINSTRUCTION|D3DOP_[A-Z]+|\bIDirect3D\b|\bIDirect3DDevice\b|D3DVT_[A-Z]+|IDirectDraw[2-6]\b|\bIDirectDrawSurface\b|IDirectDrawSurface[1-6]\b|\bIDirect3D3\b|\bIDirect3DDevice3\b|IDirect3DViewport[0-9]*\b|CreateViewport|AddViewport|SetCurrentViewport|DeleteViewport|\bClear2\b|D3DRENDERSTATE_TEXTUREHANDLE|IDirect3DTexture2|D3DRENDERSTATE_TEXTUREMAPBLEND'

# Strip BOTH // line comments and /* ... */ block comments (this renderer's own Doxygen /** */
# class-level doc comments legitimately name several of the now-forbidden symbols -- e.g.
# "IDirect3DViewport3" or "D3DRENDERSTATE_TEXTUREHANDLE" -- while explaining what DIRECTX7 removed) before
# matching -- this check is about what the CODE references, not prose documenting the discipline by
# naming the forbidden symbols. Processed one file at a time (not concatenated via xargs) so a real
# hit reports the actual file/line. Unlike DIRECTX6's own script (plain `sed 's|//.*||'` sufficed there,
# since none of its forbidden symbols appear in that renderer's own doc comments), DIRECTX7's forbidden
# list overlaps with real explanatory prose, so a real multi-line block-comment stripper (perl, not
# line-oriented sed) is required here.
violations=0
while IFS= read -r -d '' file; do
    if perl -0777 -pe 's{/\*.*?\*/}{ (my $c = $&) =~ s/[^\n]//g; $c }gse; s{//.*}{}g' "$file" | grep -nE "$pattern" | sed "s|^|${file}:|"; then
        violations=1
    fi
done < <(find "$dx7_src" "$dx7_include" -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0)

if [ "$violations" -ne 0 ]; then
    echo "error: DIRECTX7 renderer source references a forbidden execute-buffer/legacy-interface/" >&2
    echo "old-vertex-type/removed-viewport-object/texture-handle symbol above -- DIRECTX7 must use ONLY" >&2
    echo "IDirect3DDevice7::DrawPrimitive/DrawIndexedPrimitive with the D3DFVF_TLVERTEX FVF bitmask" >&2
    echo "for its 3D layer, ONLY v7 DirectDraw for its 2D layer (IDirectDraw7/IDirectDrawSurface7 are" >&2
    echo "fine; never IDirectDraw2-6/IDirectDrawSurface1-6), NO viewport object at all (SetViewport/" >&2
    echo "Clear are direct IDirect3DDevice7 methods), and ONLY direct SetTexture(stage, surface) for" >&2
    echo "texture binding (never D3DRENDERSTATE_TEXTUREHANDLE/IDirect3DTexture2), per plans/plan_dx7.md" >&2
    echo "design decision 16." >&2
    exit 1
fi

echo "OK: DIRECTX7 renderer source uses only v7-only DirectDraw/Direct3D symbols, no viewport object, and direct SetTexture binding."
exit 0
