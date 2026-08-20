#!/usr/bin/env bash
# plans/plan_dx8.md design decision 16: a real, automated proof that the DIRECTX8 renderer never quietly
# reaches for any DirectDraw interface at all (this whole family's own DIRECTX1..DIRECTX7 lineage --
# "DirectDraw+Direct3D merged" means DIRECTX8 genuinely has no DirectDraw concept, so any
# IDirectDraw*/IDirectDrawSurface* reference here would mean accidental copy-paste from an earlier
# renderer, not real DIRECTX8 code), the removed D3DTLVERTEX macro/D3DVT_* old vertex-type enum (neither
# exists in the real d3d8types.h headers at all, but a copy-paste from an earlier renderer could
# still introduce a bare reference to the name), or the legacy D3DRENDERSTATE_* render-state naming
# DIRECTX8 renamed to D3DRS_* (the underlying enum values are identical, but this renderer should use
# only the modern name, matching its own header-containment goal of never mixing eras). Registered
# as the DirectX8_LegacyInterfaceDiscipline CTest (cmake/Tests/DirectX8Tests.cmake) -- pure text check, no
# compiled binary, no Wine needed, runs identically whether cross-compiling or not.
set -uo pipefail

repo_root="$1"
dx8_src="${repo_root}/modules/renderers/directx8/src"
dx8_include="${repo_root}/modules/renderers/directx8/include/CNA/Internal/Renderers/DirectX8"

if [ ! -d "$dx8_src" ] || [ ! -d "$dx8_include" ]; then
    echo "error: DIRECTX8 renderer directories not found under ${repo_root}" >&2
    exit 1
fi

pattern='IDirectDraw[A-Za-z0-9]*|DDSURFACEDESC|DDSCAPS|D3DTLVERTEX|D3DVT_[A-Z]+|D3DRENDERSTATE_[A-Z]+'

# Strip BOTH // line comments and /* ... */ block comments (this renderer's own Doxygen /** */
# class-level doc comments legitimately name several of these forbidden symbols while explaining
# what DIRECTX8 removed/renamed) before matching -- this check is about what the CODE references, not
# prose documenting the discipline by naming the forbidden symbols. Line numbers are preserved (the
# matched comment text is replaced by an equal count of bare newlines) so a real hit reports the
# actual file/line.
violations=0
while IFS= read -r -d '' file; do
    if perl -0777 -pe 's{/\*.*?\*/}{ (my $c = $&) =~ s/[^\n]//g; $c }gse; s{//.*}{}g' "$file" | grep -nE "$pattern" | sed "s|^|${file}:|"; then
        violations=1
    fi
done < <(find "$dx8_src" "$dx8_include" -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0)

if [ "$violations" -ne 0 ]; then
    echo "error: DIRECTX8 renderer source references a forbidden DirectDraw/legacy-vertex-type/legacy-" >&2
    echo "render-state-naming symbol above -- DIRECTX8 has no DirectDraw concept at all ('DirectDraw+" >&2
    echo "Direct3D merged'), no D3DTLVERTEX/D3DVT_* (both gone from the real headers), and uses" >&2
    echo "only the modern D3DRS_* render-state naming (never the legacy D3DRENDERSTATE_* names" >&2
    echo "DIRECTX1..DIRECTX7 used), per plans/plan_dx8.md design decision 16." >&2
    exit 1
fi

echo "OK: DIRECTX8 renderer source has no DirectDraw references, no legacy vertex-type symbols, and uses only modern D3DRS_* render-state naming."
exit 0
