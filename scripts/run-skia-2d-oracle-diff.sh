#!/usr/bin/env bash
# SKIA-108: render every SpriteBatch-only real-XNA oracle scene through the Skia renderer and
# enforce tools/xna-oracle/skia-2d-policy.tsv. The policy is intentionally not a blanket fuzzy
# threshold: seven scenes are exact, while two linear-filter scenes permit only measured RGB
# rounding variance, with exact alpha plus a fixed pixel-count and footprint bound.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ $# -ne 1 ]; then
    echo "usage: $0 <path-to-cna_oracle_render_skia>" >&2
    exit 2
fi
CNA_ORACLE_RENDER_EXE="$1"
if [ ! -x "$CNA_ORACLE_RENDER_EXE" ]; then
    echo "FAIL: Skia oracle renderer is not executable: $CNA_ORACLE_RENDER_EXE" >&2
    exit 2
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
export DISPLAY="${DISPLAY:-:0}"

SCENES_DIR="$REPO_ROOT/tools/xna-oracle/scenes"
REFERENCE_DIR="$REPO_ROOT/tools/xna-oracle/reference"
POLICY_FILE="$REPO_ROOT/tools/xna-oracle/skia-2d-policy.tsv"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

declare -a POLICY_SCENES=()
declare -A RGB_TOLERANCE=()
declare -A ALPHA_TOLERANCE=()
declare -A MAX_RAW_DIFFERING=()
declare -A ALLOWED_RECT=()

while read -r name rgb_tolerance alpha_tolerance max_raw_differing allowed_rect extra; do
    if [ -z "${name:-}" ] || [[ "$name" == \#* ]]; then
        continue
    fi
    if [ -n "${extra:-}" ] || [ -z "${allowed_rect:-}" ]; then
        echo "FAIL: malformed Skia 2D policy row for '$name'" >&2
        exit 2
    fi
    if [ -n "${RGB_TOLERANCE[$name]+present}" ]; then
        echo "FAIL: duplicate Skia 2D policy row for '$name'" >&2
        exit 2
    fi
    POLICY_SCENES+=("$name")
    RGB_TOLERANCE[$name]="$rgb_tolerance"
    ALPHA_TOLERANCE[$name]="$alpha_tolerance"
    MAX_RAW_DIFFERING[$name]="$max_raw_differing"
    ALLOWED_RECT[$name]="$allowed_rect"
done < "$POLICY_FILE"

mapfile -t ACTUAL_2D_SCENES < <(
    grep -l '^spritebatchmode=true$' "$SCENES_DIR"/*.scene \
        | sed -e 's#.*/##' -e 's/\.scene$//' \
        | sort
)

POLICY_ERROR=0
for name in "${ACTUAL_2D_SCENES[@]}"; do
    if [ -z "${RGB_TOLERANCE[$name]+present}" ]; then
        echo "FAIL: SpriteBatch oracle scene '$name' has no Skia 2D policy row" >&2
        POLICY_ERROR=1
    fi
done
for name in "${POLICY_SCENES[@]}"; do
    found=0
    for actual_name in "${ACTUAL_2D_SCENES[@]}"; do
        if [ "$actual_name" = "$name" ]; then
            found=1
            break
        fi
    done
    if [ "$found" -eq 0 ]; then
        echo "FAIL: policy scene '$name' is not a SpriteBatch-only oracle scene" >&2
        POLICY_ERROR=1
    fi
done
if [ "$POLICY_ERROR" -ne 0 ]; then
    exit 2
fi

TOTAL=0
FAILED=0
for name in "${POLICY_SCENES[@]}"; do
    scene="$SCENES_DIR/$name.scene"
    reference="$REFERENCE_DIR/$name.png"
    out="$WORK_DIR/$name.png"
    TOTAL=$((TOTAL + 1))

    if [ ! -f "$reference" ]; then
        echo "FAIL: $name -- no checked-in real-XNA reference at $reference"
        FAILED=$((FAILED + 1))
        continue
    fi
    if ! "$CNA_ORACLE_RENDER_EXE" "$scene" "$out" > "$WORK_DIR/$name.render.log" 2>&1; then
        echo "FAIL: $name -- cna_oracle_render_skia failed:"
        tail -20 "$WORK_DIR/$name.render.log"
        FAILED=$((FAILED + 1))
        continue
    fi

    diff_args=(
        "$reference" "$out"
        --rgb-tolerance "${RGB_TOLERANCE[$name]}"
        --alpha-tolerance "${ALPHA_TOLERANCE[$name]}"
        --max-raw-differing-pixels "${MAX_RAW_DIFFERING[$name]}"
    )
    if [ "${ALLOWED_RECT[$name]}" != "-" ]; then
        diff_args+=(--allowed-raw-diff-rect "${ALLOWED_RECT[$name]}")
    fi

    if ! diff_output=$(python3 "$SCRIPT_DIR/xna-diff.py" "${diff_args[@]}" 2>&1); then
        echo "FAIL: $name -- $diff_output"
        FAILED=$((FAILED + 1))
    else
        echo "PASS: $name -- $diff_output"
    fi
done

PASSED=$((TOTAL - FAILED))
echo "=== $PASSED/$TOTAL Skia 2D scenes satisfy the real-XNA oracle policy ==="
if [ "$FAILED" -eq 0 ]; then
    exit 0
fi
exit 1
