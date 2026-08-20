#!/usr/bin/env bash
# plans/plan_dx1.md section 1: a real, automated proof that the DIRECTX1 renderer never silently drifts into
# DIRECTX3+ (or Direct3D) territory -- greps its own source for any IDirectDraw2+/IDirectDrawSurface2+/
# DDSURFACEDESC2 symbol, or anything from d3d.h. Registered as the DirectX1_V1OnlyDiscipline CTest
# (cmake/Tests/DirectX1Tests.cmake) -- pure text check, no compiled binary, no Wine needed, runs
# identically whether cross-compiling or not.
set -uo pipefail

repo_root="$1"
dx1_src="${repo_root}/modules/renderers/directx1/src"
dx1_include="${repo_root}/modules/renderers/directx1/include/CNA/Internal/Renderers/DirectX1"

if [ ! -d "$dx1_src" ] || [ ! -d "$dx1_include" ]; then
    echo "error: DIRECTX1 renderer directories not found under ${repo_root}" >&2
    exit 1
fi

pattern='IDirectDraw[247]\b|IDirectDrawSurface[234567]\b|DDSURFACEDESC2\b|IDirect3D|[<"]d3d\.h[">]'

# Strip // line comments before matching -- this check is about what the CODE references, not
# about prose that documents the v1-only discipline by naming the forbidden symbols (which every
# file here legitimately does, in its own header comment). Processed one file at a time (not
# concatenated via xargs) so a real hit reports the actual file/line.
violations=0
while IFS= read -r -d '' file; do
    if sed -E 's|//.*||' "$file" | grep -nE "$pattern" | sed "s|^|${file}:|"; then
        violations=1
    fi
done < <(find "$dx1_src" "$dx1_include" -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0)

if [ "$violations" -ne 0 ]; then
    echo "error: DIRECTX1 renderer source references a DIRECTX3+/Direct3D symbol above -- DIRECTX1 must use ONLY" >&2
    echo "v1 DirectDraw interfaces (IDirectDraw/IDirectDrawSurface/DDSURFACEDESC), never" >&2
    echo "IDirectDraw2+/IDirectDrawSurface2+/DDSURFACEDESC2/anything from d3d.h (plans/plan_dx1.md section 1)." >&2
    exit 1
fi

echo "OK: DIRECTX1 renderer source uses only v1 DirectDraw symbols."
exit 0
