#!/usr/bin/env bash
# plans/plan_dx9.md Phase D9-A (D9-A6): cross-renderer twin of scripts/run-oracle-corpus-diff.sh -- renders
# every checked-in scene (tools/xna-oracle/scenes/*.scene) through cna_oracle_render_easygl (the SAME
# tools/xna-oracle/CnaOracleRender.cpp D9-A3 wrote, built natively against the EasyGL renderer by this
# repo's own CMakeLists.txt EasyGL-tests section) and diffs each result against the checked-in real
# XNA 4.0 reference PNG (tools/xna-oracle/reference/*.png), at the SAME D9-A4 tolerance=0 (exact
# match) scripts/run-oracle-corpus-diff.sh already uses.
#
# Deliberately does NOT go through scripts/run-wine-dxvk9.sh at all: this binary is a native Linux
# ELF executable (EasyGL/OpenGL, no Wine/DXVK/D3D9 involved), unlike the D3D9-branch's own
# cna_oracle_render.exe. It needs a real X11 display to create its window (same requirement every
# other EasyGL CTest in CMakeLists.txt already has -- see CNA_TEST_DISPLAY), so SDL_VIDEODRIVER=x11
# and DISPLAY are set for every invocation below, defaulting to :0 (this project's own CNA_TEST_DISPLAY
# default) unless the caller already exported DISPLAY.
#
# Unlike D3D9's own D3D9_XNA_Diff CTest (scripts/run-oracle-corpus-diff.sh, wired into CMakeLists.txt
# as a real add_test()), this script is NOT registered as a CTest -- D9-A6's own task description is a
# one-time cross-renderer measurement ("record the deltas"), not a new permanently-enforced pixel-exact
# gate; EasyGL is not expected to be pixel-identical to real XNA the way D3D9/DXVK is (see
# docs/d3d9-divergence-report.md's "Cross-renderer measurement (D9-A6)" section for the actual
# per-scene results and root-cause guesses). Never widen --tolerance below to turn a red comparison
# green without a documented, per-scene reason -- same discipline xna-diff.py's own header already
# states.
#
# Usage: scripts/run-oracle-corpus-diff-easygl.sh <path-to-cna_oracle_render_easygl>

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ $# -lt 1 ]; then
    echo "usage: $0 <path-to-cna_oracle_render_easygl>" >&2
    exit 2
fi
CNA_ORACLE_RENDER_EXE="$1"

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
export DISPLAY="${DISPLAY:-:0}"

SCENES_DIR="$REPO_ROOT/tools/xna-oracle/scenes"
REFERENCE_DIR="$REPO_ROOT/tools/xna-oracle/reference"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

TOTAL=0
FAILED=0

for scene in "$SCENES_DIR"/*.scene; do
    name="$(basename "$scene" .scene)"
    reference="$REFERENCE_DIR/$name.png"
    TOTAL=$((TOTAL + 1))

    if [ ! -f "$reference" ]; then
        echo "FAIL: $name -- no checked-in reference image at $reference"
        FAILED=$((FAILED + 1))
        continue
    fi

    out="$WORK_DIR/$name.png"
    if ! "$CNA_ORACLE_RENDER_EXE" "$scene" "$out" \
            > "$WORK_DIR/$name.render.log" 2>&1; then
        echo "FAIL: $name -- cna_oracle_render_easygl itself failed:"
        tail -20 "$WORK_DIR/$name.render.log"
        FAILED=$((FAILED + 1))
        continue
    fi

    if ! diffOutput=$(python3 "$SCRIPT_DIR/xna-diff.py" "$reference" "$out" --tolerance 0 2>&1); then
        echo "FAIL: $name -- $diffOutput"
        FAILED=$((FAILED + 1))
    else
        echo "PASS: $name -- $diffOutput"
    fi
done

PASSED=$((TOTAL - FAILED))
echo "=== $PASSED/$TOTAL scenes match real XNA 4.0 exactly (checked-in reference images) -- EasyGL renderer ==="

if [ "$FAILED" -eq 0 ]; then
    exit 0
else
    exit 1
fi
