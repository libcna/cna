#!/usr/bin/env bash
# plans/plan_dx9.md Phase D9-7 (D9-70): mechanically enforce "not one line edited" for the vendored
# Microsoft XNA 4.0 Stock Effects HLSL sources -- design decision 3 says these files must stay
# byte-identical to the FNA reference tree; this script is that enforcement, not just a comment.
#
# Usage: scripts/verify-d3d9-stock-effects-vendored.sh [path-to-FNA-tree]
# Defaults to /rv/data/library/github.com/FNA-XNA/FNA (this project's own documented reference
# tree location, per CLAUDE.md's "Source Reference"). Override via the first argument or
# CNA_FNA_TREE if the reference tree lives somewhere else on a given machine.
#
# Exit code 0 = every vendored file is byte-identical to the FNA tree; 1 = at least one delta
# (printed as a diff), or the FNA tree/vendored directory could not be found.

set -euo pipefail

FNA_TREE="${1:-${CNA_FNA_TREE:-/rv/data/library/github.com/FNA-XNA/FNA}}"
SRC_DIR="$FNA_TREE/src/Graphics/Effect/StockEffects/HLSL"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DST_DIR="$REPO_ROOT/modules/renderers/directx9/src/shaders/xna"

FILES=(
    BasicEffect.fx AlphaTestEffect.fx DualTextureEffect.fx EnvironmentMapEffect.fx
    SkinnedEffect.fx SpriteEffect.fx Macros.fxh Common.fxh Lighting.fxh Structures.fxh
)

if [ ! -d "$SRC_DIR" ]; then
    echo "ERROR: FNA reference tree not found at: $SRC_DIR" >&2
    echo "       Pass the FNA tree root as an argument, or set CNA_FNA_TREE." >&2
    exit 1
fi

fail=0
for f in "${FILES[@]}"; do
    if [ ! -f "$DST_DIR/$f" ]; then
        echo "MISSING: $DST_DIR/$f" >&2
        fail=1
        continue
    fi
    if ! diff -u "$SRC_DIR/$f" "$DST_DIR/$f"; then
        echo "MISMATCH: $f differs from the FNA reference tree (see diff above)" >&2
        fail=1
    fi
done

if [ "$fail" -eq 0 ]; then
    echo "OK: all ${#FILES[@]} vendored D3D9 stock-effect HLSL files are byte-identical to the FNA tree."
fi
exit "$fail"
