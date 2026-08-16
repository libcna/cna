#!/usr/bin/env bash
# plan_runtimerenderer.md RTR-P9-24: the oracle scene corpus, run against SEVERAL renderers from ONE
# binary.
#
# Every existing corpus script -- run-oracle-corpus-diff.sh (D3D9), -easygl, -fna3d, -opengles1 --
# takes one renderer's own cna_oracle_render_* executable, because until a build could hold several
# renderers that was the only shape available. Comparing two renderers therefore meant two
# configures, two builds, and a comparison done by hand outside the tooling.
#
# A multi-renderer build removes that: one executable, the renderer chosen per run through
# CNA_GRAPHICS_RENDERER. This script renders the whole corpus once per requested renderer and diffs
# every scene against the SAME checked-in reference PNGs, so "do these renderers agree with real XNA,
# and therefore with each other?" is answered in one command.
#
# Usage:
#   scripts/run-oracle-corpus-multi.sh <path-to-cna_oracle_render_*> "OPENGLES3;OPENGL33"
#
# The reference PNGs are captured against the real XNA 4.0 runtime and are the fixed point here.
# tolerance=0 (exact match) is deliberate and must not be widened to turn a red comparison green --
# that warning is inherited verbatim from run-oracle-corpus-diff.sh, and applies with more force
# here, where one loosened tolerance would silently excuse every renderer at once.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ $# -lt 2 ]; then
    echo "usage: $0 <path-to-cna_oracle_render_exe> \"RENDERER;RENDERER;...\"" >&2
    exit 2
fi

ORACLE_EXE="$1"
RENDERER_LIST="$2"

if [ ! -x "$ORACLE_EXE" ]; then
    echo "FAIL: $ORACLE_EXE is not an executable" >&2
    exit 2
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
export DISPLAY="${DISPLAY:-:0}"

SCENES_DIR="$REPO_ROOT/tools/xna-oracle/scenes"
REFERENCE_DIR="$REPO_ROOT/tools/xna-oracle/reference"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

IFS=';' read -r -a RENDERERS <<< "$RENDERER_LIST"

declare -A PASSED
declare -A FAILED
declare -A UNAVAILABLE
OVERALL=0

for renderer in "${RENDERERS[@]}"; do
    echo "=== ${renderer} ==="
    PASSED[$renderer]=0
    FAILED[$renderer]=0

    # One cheap probe first: a renderer that is not compiled into THIS binary, or that cannot start
    # here, must be reported as unavailable rather than as 39 scene failures. A wall of red for a
    # renderer that never ran is the least useful possible output.
    probe_scene="$(ls "$SCENES_DIR"/*.scene | head -1)"
    if ! CNA_GRAPHICS_RENDERER="$renderer" "$ORACLE_EXE" \
            "$probe_scene" "$WORK_DIR/probe.png" > "$WORK_DIR/probe.log" 2>&1; then
        echo "SKIP: ${renderer} is not usable from this binary:"
        sed -n '1,6p' "$WORK_DIR/probe.log" | sed 's/^/    /'
        UNAVAILABLE[$renderer]=1
        continue
    fi

    for scene in "$SCENES_DIR"/*.scene; do
        name="$(basename "$scene" .scene)"
        reference="$REFERENCE_DIR/$name.png"

        if [ ! -f "$reference" ]; then
            echo "FAIL: ${renderer}/${name} -- no checked-in reference image at $reference"
            FAILED[$renderer]=$(( ${FAILED[$renderer]} + 1 ))
            continue
        fi

        out="$WORK_DIR/${renderer}.${name}.png"
        if ! CNA_GRAPHICS_RENDERER="$renderer" "$ORACLE_EXE" "$scene" "$out" \
                > "$WORK_DIR/${renderer}.${name}.render.log" 2>&1; then
            echo "FAIL: ${renderer}/${name} -- the renderer itself failed:"
            tail -10 "$WORK_DIR/${renderer}.${name}.render.log" | sed 's/^/    /'
            FAILED[$renderer]=$(( ${FAILED[$renderer]} + 1 ))
            continue
        fi

        if ! diffOutput=$(python3 "$SCRIPT_DIR/xna-diff.py" "$reference" "$out" --tolerance 0 2>&1); then
            echo "FAIL: ${renderer}/${name} -- $diffOutput"
            FAILED[$renderer]=$(( ${FAILED[$renderer]} + 1 ))
        else
            PASSED[$renderer]=$(( ${PASSED[$renderer]} + 1 ))
        fi
    done

    echo "--- ${renderer}: ${PASSED[$renderer]} passed, ${FAILED[$renderer]} failed ---"
done

echo
echo "=== Oracle corpus, one binary, ${#RENDERERS[@]} renderer(s) ==="
for renderer in "${RENDERERS[@]}"; do
    if [ -n "${UNAVAILABLE[$renderer]:-}" ]; then
        printf '%-14s %s\n' "$renderer" "SKIPPED (not usable from this binary)"
        continue
    fi
    printf '%-14s %s\n' "$renderer" "${PASSED[$renderer]} passed, ${FAILED[$renderer]} failed"
    if [ "${FAILED[$renderer]}" -ne 0 ]; then
        OVERALL=1
    fi
done

# Every renderer that ran was compared against the SAME references, so agreement with XNA implies
# agreement with each other -- which is the cross-renderer claim this script exists to make.
exit "$OVERALL"
