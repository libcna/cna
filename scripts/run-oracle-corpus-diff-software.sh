#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
# SOFTWARE-343: render the shared XNA-oracle scene corpus through Software and compare every image
# with the checked-in Microsoft XNA 4.0 reference. This is a measurement, not an exact-image gate:
# point-sampling decisions on an interpolated texel boundary and floating shader/raster arithmetic
# can legitimately differ within Direct3D's precision allowances. A scene that does not render is
# nevertheless a hard failure. Never widen a tolerance merely to turn a reported delta green.
#
# Usage: scripts/run-oracle-corpus-diff-software.sh <path-to-cna_oracle_render_software> [tolerance]

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ $# -lt 1 ]; then
    echo "usage: $0 <path-to-cna_oracle_render_software> [tolerance]" >&2
    exit 2
fi
CNA_ORACLE_RENDER_EXE="$1"
TOLERANCE="${2:-0}"

if [ ! -x "$CNA_ORACLE_RENDER_EXE" ]; then
    echo "oracle renderer is not executable: $CNA_ORACLE_RENDER_EXE" >&2
    exit 2
fi

unset DISPLAY
unset WAYLAND_DISPLAY
export SDL_VIDEODRIVER=dummy

SCENES_DIR="$REPO_ROOT/tools/xna-oracle/scenes"
REFERENCE_DIR="$REPO_ROOT/tools/xna-oracle/reference"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

TOTAL=0
MATCHED=0
DIFFERENT=0
RENDER_FAILED=0

printf '%-34s %s\n' "SCENE" "RESULT"
printf '%-34s %s\n' "----------------------------------" "------"

for scene in "$SCENES_DIR"/*.scene; do
    name="$(basename "$scene" .scene)"
    reference="$REFERENCE_DIR/$name.png"
    TOTAL=$((TOTAL + 1))

    if [ ! -f "$reference" ]; then
        printf '%-34s %s\n' "$name" "NO-REFERENCE"
        RENDER_FAILED=$((RENDER_FAILED + 1))
        continue
    fi

    out="$WORK_DIR/$name.png"
    if ! "$CNA_ORACLE_RENDER_EXE" "$scene" "$out" > "$WORK_DIR/$name.render.log" 2>&1; then
        printf '%-34s %s\n' "$name" "RENDER-FAILED"
        tail -5 "$WORK_DIR/$name.render.log" | sed 's/^/    /'
        RENDER_FAILED=$((RENDER_FAILED + 1))
        continue
    fi

    if diffOutput=$(python3 "$SCRIPT_DIR/xna-diff.py" "$reference" "$out" \
            --tolerance "$TOLERANCE" 2>&1); then
        printf '%-34s %s\n' "$name" "MATCH"
        MATCHED=$((MATCHED + 1))
    else
        printf '%-34s %s\n' "$name" "DIFF   $diffOutput"
        DIFFERENT=$((DIFFERENT + 1))
    fi
done

echo
echo "=== SOFTWARE vs real XNA 4.0 (tolerance=$TOLERANCE): ${MATCHED}/${TOTAL} matching, ${DIFFERENT} differing, ${RENDER_FAILED} not rendered ==="

if [ "$RENDER_FAILED" -ne 0 ]; then
    exit 1
fi
exit 0
