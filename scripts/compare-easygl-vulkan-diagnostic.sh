#!/usr/bin/env bash
# plan_vulkan.md VULKAN-430 / VULKAN-431 -- the EasyGL <-> Vulkan cross-renderer diagnostic gate.
#
# WHY THIS IS A SCRIPT AND NOT A CTEST. A CNA build links exactly one renderer, so the two dumps
# this compares come from two different build directories. No test inside either one can produce
# the other's output, and a CTest that could would have to build the other configuration -- which
# is not a test, it is a build. The row that opened this said so, and this file is that decision
# made real rather than worked around.
#
# WHAT IT COMPARES. Two renderer-agnostic sources, each built once per renderer, each dumping a
# 64x64 RGBA8 backbuffer in the format cna_diag_compare already reads:
#
#   cross_renderer_diagnostic_scene.cpp -> cna_diag_easygl   / cna_diag_vulkan    (3D: a
#       VertexColorEnabled BasicEffect triangle through a VertexBuffer)
#   cross_renderer_2d_corpus.cpp        -> cna_corpus2d_easygl / cna_corpus2d_vulkan (2D: a fixed
#       corpus of public SpriteBatch commands, deliberately excluding every construct where two
#       conforming renderers may legitimately disagree -- see that file's own whitelist)
#
# THE TOLERANCE, and why it is not zero. cna_diag_compare takes a per-channel tolerance and
# defaults to 40. These two renderers rasterize the same triangle through different hardware
# pipelines: edge coverage on a diagonal, and the last bit of an 8-bit channel after a different
# order of floating-point operations, are not a contract either renderer breaks. 40 is the value
# the existing Software/EasyGL comparison already uses, and it is inherited rather than re-picked
# so a difference that matters here is a difference that would have mattered there.
#
# EXPECTED DIFFERENCES. None, on the two scenes above -- and that is measured rather than hoped
# for. First run, 2026-09-06, llvmpipe on Xvfb :99:
#
#   diagnostic_scene: max diff 1 at (41,22) channel 2, mean 0.005
#   2d_corpus:        max diff 0 -- BYTE-IDENTICAL
#
# So the inherited tolerance of 40 is doing no work at all here; the same run passes at --tolerance 2.
# The tolerance is kept at 40 because it is the value the existing Software/EasyGL comparison uses
# and a difference that matters here should be one that would have mattered there, not because
# anything needs it. If an entry ever has to be added to this list it belongs here with its reason,
# never in a widened tolerance:
#
#   (none)
#
# USAGE
#   scripts/compare-easygl-vulkan-diagnostic.sh [--easygl-dir DIR] [--vulkan-dir DIR]
#                                               [--tolerance N] [--perturb]
#
# --perturb is the acceptance check the row requires: it corrupts one renderer's dump before the
# comparison, so a run that still reports success has proved the gate cannot fail and is therefore
# worthless. Use it to verify the gate, never in CI.
set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"
EASYGL_DIR="$REPO/cmake-build-easygl"
VULKAN_DIR="$REPO/cmake-build-vulkan"
TOLERANCE=40
PERTURB=0
DISPLAY_ARG="${CNA_TEST_DISPLAY:-:99}"

while [ $# -gt 0 ]; do
    case "$1" in
        --easygl-dir) EASYGL_DIR="$2"; shift 2 ;;
        --vulkan-dir) VULKAN_DIR="$2"; shift 2 ;;
        --tolerance)  TOLERANCE="$2";  shift 2 ;;
        --perturb)    PERTURB=1;       shift 1 ;;
        -h|--help)    sed -n '1,45p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

COMPARE="$EASYGL_DIR/cna_diag_compare"
[ -x "$COMPARE" ] || COMPARE="$VULKAN_DIR/cna_diag_compare"
if [ ! -x "$COMPARE" ]; then
    echo "cna_diag_compare not found in either build directory -- build it first:" >&2
    echo "  cmake --build $EASYGL_DIR --target cna_diag_compare" >&2
    exit 2
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

run_dump() {   # run_dump <build dir> <target> <output file>
    local dir="$1" target="$2" out="$3"
    if [ ! -x "$dir/$target" ]; then
        echo "missing $dir/$target -- build it first: cmake --build $dir --target $target" >&2
        return 2
    fi
    # The renderers need a display; the same one the CTests use.
    ( cd "$dir" && env -u WAYLAND_DISPLAY DISPLAY="$DISPLAY_ARG" SDL_VIDEODRIVER=x11 \
        "./$target" "$out" >/dev/null 2>&1 )
    if [ ! -s "$out" ]; then
        echo "$target produced no dump" >&2
        return 2
    fi
    return 0
}

status=0
for pair in "diagnostic_scene cna_diag_easygl cna_diag_vulkan" \
            "2d_corpus cna_corpus2d_easygl cna_corpus2d_vulkan"; do
    set -- $pair
    name="$1"; egl_target="$2"; vk_target="$3"
    egl_out="$OUT/$name.easygl.rgba"
    vk_out="$OUT/$name.vulkan.rgba"

    run_dump "$EASYGL_DIR" "$egl_target" "$egl_out" || { status=2; continue; }
    run_dump "$VULKAN_DIR" "$vk_target"  "$vk_out"  || { status=2; continue; }

    if [ "$PERTURB" -eq 1 ]; then
        # Deliberately corrupt the Vulkan dump so a gate that cannot fail is exposed as such.
        printf '\xFF\x00\xFF\xFF' | dd of="$vk_out" bs=1 seek=0 conv=notrunc status=none
        printf '\xFF\x00\xFF\xFF' | dd of="$vk_out" bs=1 seek=8192 conv=notrunc status=none
    fi

    echo "== $name =="
    if "$COMPARE" "$egl_out" "$vk_out" "$TOLERANCE"; then
        echo "   EasyGL and Vulkan agree within tolerance $TOLERANCE"
    else
        echo "   DIFFERS beyond tolerance $TOLERANCE" >&2
        status=1
    fi
done

if [ "$PERTURB" -eq 1 ]; then
    if [ "$status" -eq 0 ]; then
        echo "PERTURBATION WAS NOT DETECTED -- this gate cannot fail and proves nothing" >&2
        exit 3
    fi
    echo "perturbation detected as expected; the gate discriminates"
    exit 0
fi
exit "$status"
