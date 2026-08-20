#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
# plans/plan_opengles1.md OPENGLES1-78: cross-renderer pixel-parity measurement for the OpenGL ES 1.1
# renderer -- the OPENGLES1 twin of scripts/run-oracle-corpus-diff-easygl.sh.
#
# Renders every checked-in scene (tools/xna-oracle/scenes/*.scene) through
# cna_oracle_render_opengles1 -- the SAME tools/xna-oracle/CnaOracleRender.cpp the D3D9 and EasyGL
# branches already build, just linked against this renderer -- and diffs each result against the
# checked-in real XNA 4.0 reference PNG (tools/xna-oracle/reference/*.png).
#
# Comparing against the real XNA reference rather than against another CNA renderer is deliberate:
# it is the stronger oracle, and it is what the two existing corpus-diff scripts already do.
#
# IMPORTANT -- this is a measurement, not a pass/fail gate. OpenGL ES 1.1 is a fixed-function
# pipeline with no shader stage at all, so exact parity with XNA's programmable-pipeline output is
# NOT expected for every scene, and several documented deviations (see the deviation table in
# plans/plan_opengles1.md) guarantee specific scenes will differ: EnvironmentMapEffect ignores Fresnel
# edge-weighting, the alpha test collapses a tolerance band to a single comparison, per-vertex
# lighting is interpolated across the primitive rather than evaluated per pixel, and so on. The
# script therefore reports a per-scene delta table and always exits 0 unless the renderer itself
# fails; docs/opengles1-renderer.md records the measured numbers.
#
# Never widen the tolerance to turn a red comparison green without a documented, per-scene reason --
# the same discipline scripts/xna-diff.py's own header states.
#
# Needs a real ES1 driver: run it through scripts/opengles1-test-env.sh, e.g.
#   scripts/opengles1-test-env.sh scripts/run-oracle-corpus-diff-opengles1.sh \
#       ./cmake-build-opengles1/cna_oracle_render_opengles1

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ $# -lt 1 ]; then
    echo "usage: $0 <path-to-cna_oracle_render_opengles1> [tolerance]" >&2
    exit 2
fi
CNA_ORACLE_RENDER_EXE="$1"
TOLERANCE="${2:-0}"

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
# Virtual display by default -- rendering 39 scenes should not flash 39 windows on a real desktop.
export DISPLAY="${DISPLAY:-:99}"

SCENES_DIR="$REPO_ROOT/tools/xna-oracle/scenes"
REFERENCE_DIR="$REPO_ROOT/tools/xna-oracle/reference"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

TOTAL=0
EXACT=0
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

    if diffOutput=$(python3 "$SCRIPT_DIR/xna-diff.py" "$reference" "$out" --tolerance "$TOLERANCE" 2>&1); then
        printf '%-34s %s\n' "$name" "EXACT"
        EXACT=$((EXACT + 1))
    else
        printf '%-34s %s\n' "$name" "DIFF   $diffOutput"
        DIFFERENT=$((DIFFERENT + 1))
    fi
done

echo
echo "=== OPENGLES1 vs real XNA 4.0 (tolerance=$TOLERANCE): ${EXACT}/${TOTAL} exact, ${DIFFERENT} differing, ${RENDER_FAILED} not rendered ==="

# A scene that fails to render at all is a real defect; a scene that renders but differs is a
# measurement this renderer's documented fixed-function limits partly predict.
if [ "$RENDER_FAILED" -ne 0 ]; then
    exit 1
fi
exit 0
