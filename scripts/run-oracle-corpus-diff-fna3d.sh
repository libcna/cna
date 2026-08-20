#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
# plans/plan_fna3d.md FNA3D-26: XNA 4.0 conformance measurement for the FNA3D renderer -- the FNA3D twin
# of scripts/run-oracle-corpus-diff-opengles1.sh.
#
# Renders every checked-in scene (tools/xna-oracle/scenes/*.scene) through
# cna_oracle_render_fna3d -- the SAME tools/xna-oracle/CnaOracleRender.cpp the D3D9, EasyGL,
# OpenGLES1 and Skia branches already build, just linked against this renderer -- and diffs each
# result against the checked-in real XNA 4.0 reference PNG (tools/xna-oracle/reference/*.png).
#
# This corpus matters more for FNA3D than for any other renderer in the tree: FNA3D executes XNA's
# OWN compiled stock-effect binaries through MojoShader, so "indistinguishable from real XNA" is
# not an aspiration here, it is close to the renderer's whole premise. The corpus is also the only
# thing in the repository that exercises DualTextureEffect, EnvironmentMapEffect, SkinnedEffect,
# the lighting variants and fog through this renderer at all.
#
# IMPORTANT -- like the EasyGL and OpenGLES1 twins, this is a measurement, not a pass/fail gate,
# and for the same reason they give: the reference PNGs were captured against the real XNA 4.0
# runtime on a different rasteriser, so this host's GL stack carries a corpus-wide divergence that
# is NOT renderer-specific. docs/opengles1-parity-report.md records EasyGL at 10/39 exact on the
# same corpus and machine; a renderer at that number is at the established baseline, not broken.
# The measurement's value is the PER-SCENE comparison against that baseline -- a scene where this
# renderer is worse than EasyGL is a candidate defect, and docs/fna3d-parity-report.md records
# which those are.
#
# Only a scene that fails to RENDER is treated as a hard failure here. Never widen the tolerance to
# turn a red row green without a per-scene reason (scripts/xna-diff.py's own standing warning).
#
# Usage:
#   scripts/run-oracle-corpus-diff-fna3d.sh <path-to-cna_oracle_render_fna3d> [tolerance]

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ $# -lt 1 ]; then
    echo "usage: $0 <path-to-cna_oracle_render_fna3d> [tolerance]" >&2
    exit 2
fi
CNA_ORACLE_RENDER_EXE="$1"
TOLERANCE="${2:-0}"

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
export DISPLAY="${DISPLAY:-:99}"
# Pin the driver the assertions were written against, exactly as every other FNA3D test does.
export FNA3D_FORCE_DRIVER="${FNA3D_FORCE_DRIVER:-OpenGL}"

SCENES_DIR="$REPO_ROOT/tools/xna-oracle/scenes"
REFERENCE_DIR="$REPO_ROOT/tools/xna-oracle/reference"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

TOTAL=0
EXACT=0
DIFFERENT=0
RENDER_FAILED=0
DIFFERING_SCENES=()

printf '%-38s %s\n' "SCENE" "RESULT"
printf '%-38s %s\n' "--------------------------------------" "------"

for scene in "$SCENES_DIR"/*.scene; do
    name="$(basename "$scene" .scene)"
    reference="$REFERENCE_DIR/$name.png"
    TOTAL=$((TOTAL + 1))

    if [ ! -f "$reference" ]; then
        printf '%-38s %s\n' "$name" "NO-REFERENCE"
        RENDER_FAILED=$((RENDER_FAILED + 1))
        continue
    fi

    out="$WORK_DIR/$name.png"
    if ! "$CNA_ORACLE_RENDER_EXE" "$scene" "$out" > "$WORK_DIR/$name.render.log" 2>&1; then
        printf '%-38s %s\n' "$name" "RENDER-FAILED"
        tail -5 "$WORK_DIR/$name.render.log" | sed 's/^/    /'
        RENDER_FAILED=$((RENDER_FAILED + 1))
        continue
    fi

    if diffOutput=$(python3 "$SCRIPT_DIR/xna-diff.py" "$reference" "$out" --tolerance "$TOLERANCE" 2>&1); then
        printf '%-38s %s\n' "$name" "EXACT"
        EXACT=$((EXACT + 1))
    else
        printf '%-38s %s\n' "$name" "DIFF   $diffOutput"
        DIFFERENT=$((DIFFERENT + 1))
        DIFFERING_SCENES+=("$name")
    fi
done

echo
echo "=== FNA3D vs real XNA 4.0 (tolerance=$TOLERANCE): ${EXACT}/${TOTAL} exact, ${DIFFERENT} differing, ${RENDER_FAILED} not rendered ==="

if [ "${#DIFFERING_SCENES[@]}" -ne 0 ]; then
    echo "differing scenes: ${DIFFERING_SCENES[*]}"
fi

# A scene that fails to render at all is a real defect; a scene that renders but differs is the
# measurement described in this script's header, judged per-scene against the recorded baseline.
if [ "$RENDER_FAILED" -ne 0 ]; then
    exit 1
fi
exit 0
