#!/usr/bin/env bash
# plans/plan_webgpu.md WEBGPU-193 / plans/plan_sdlgpu.md SDLGPU-81: run the WHOLE shared parity
# corpus under EasyGL and one comparison renderer, and record a per-fixture verdict.
#
# scripts/run-parity-fixture.sh does one fixture. This does all of them, and adds the thing a
# per-fixture run cannot: a POLICY for what "the frames agree" means for each one, written down in
# this file where it can be reviewed, so a fixture that stops being byte-identical is a failure
# rather than a tolerance somebody widened.
#
# Three policies, and every fixture has exactly one:
#
#   strict          the two frames must be identical (tolerance 2, which is rounding, not licence)
#   allow:N:reason  at most N pixels may differ, and the reason says which and why
#   internal:reason the frames are NOT compared; the fixture's own assertions are its whole
#                   contract, and the reason says why a frame comparison is inapplicable
#
# The default for a new fixture is `strict`: a fixture is added to the exception table only with a
# measurement and a sentence, never to make a run green.
#
# Usage:  scripts/run-parity-corpus.sh [easygl-build] [comparison-build] [webgpu|sdlgpu]
# DISPLAY comes from CNA_PARITY_DISPLAY (default :131). Exit 0 = every fixture passed its own
# assertions under both renderers AND met its frame policy.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EASYGL_BUILD="${1:-cmake-build-debug}"
COMPARISON_RENDERER="${3:-webgpu}"
case "$COMPARISON_RENDERER" in
    webgpu)
        COMPARISON_BUILD="${2:-cmake-build-webgpu}"
        COMPARISON_LABEL="WEBGPU"
        comparison_binary() { printf '%s/cna_parity_%s_webgpu' "$REPO_ROOT/$COMPARISON_BUILD" "$1"; }
        ;;
    sdlgpu)
        COMPARISON_BUILD="${2:-cmake-build-sdlgpu}"
        COMPARISON_LABEL="SDL_GPU"
        comparison_binary() { printf '%s/cna_test_sdlgpu_parity_%s' "$REPO_ROOT/$COMPARISON_BUILD" "$1"; }
        ;;
    *)
        echo "error: comparison renderer must be 'webgpu' or 'sdlgpu'" >&2
        exit 2
        ;;
esac
DISPLAY_VALUE="${CNA_PARITY_DISPLAY:-:131}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# --- the policy table -------------------------------------------------------------------------
# Anything not named here is `strict`.
declare -A POLICY=(
  [backbuffer_msaa]="allow:520:the triangle's three coverage boundaries; MSAA sample locations are implementation-defined, while both fixtures assert a resolved partial-coverage pixel and opaque interior -- measured EasyGL vs SDL_GPU: 506 pixels, all on those boundaries"
  [compressed_cube]="internal:a reflection's cube FACE depends on the pixel-centre convention (WEBGPU-187); this fixture's oracle is a BC cube against an RGBA8 cube WITHIN one renderer"
  [sampler_filters]="internal:magnification lands a linear gradient half a texel apart under the two pixel-centre conventions (WEBGPU-187); the claims are point-vs-linear A/Bs within one renderer"
  [rasterizer_viewport]="allow:400:the triangle hypotenuses, rasterized under the two pixel-centre conventions (WEBGPU-187) -- measured 276 of 12800, all in the four triangle cells and none in the quad cells"
  [sprite_geometry]="allow:64:the single half-pixel-destination cell top and left coverage edges (WEBGPU-187) -- measured 32 pixels, all in that one cell"
  [env_map_terms]="allow:64:EasyGL rounds the Fresnel gradient differently in its mediump fragment shader -- measured max diff 2 in the fanned-normal cell"
  [skinned_terms]="allow:64:the specular cell, for the same mediump reason -- measured max diff 2"
  [fill_mode_wireframe]="allow:512:the drawn LINE pixels themselves -- GL line rules and a WebGPU line-list rasterize a diagonal differently, on top of the pixel-centre convention (WEBGPU-187). Measured 367 of 32768, 1.1 percent"
)

# SDLGPU-81 measured the SDL_GPU comparison independently. Do not inherit WebGPU exceptions merely
# because both APIs commonly run over Vulkan: SDL_GPU's XNA half-pixel correction makes the
# rasterizer/viewport, EnvironmentMap and Skinned frames exact. Only the genuinely inapplicable
# reflection/filter pictures and the two coverage-rule cases remain exceptions.
if [[ "$COMPARISON_RENDERER" == "sdlgpu" ]]; then
    POLICY=()
    POLICY[backbuffer_msaa]="allow:520:the triangle's three coverage boundaries; MSAA sample locations are implementation-defined -- measured 506 pixels, all on those boundaries"
    POLICY[compressed_cube]="internal:a reflection's cube FACE is not a cross-renderer invariant; this fixture compares BC and RGBA8 cube results WITHIN each renderer"
    POLICY[sampler_filters]="internal:linear magnification depends on the rasterizer's sample centres; the fixture's point-vs-linear assertions are renderer-local invariants"
    POLICY[sprite_geometry]="allow:64:the single half-pixel-destination cell's top/left coverage edges -- measured 32 pixels, all in that cell"
    POLICY[fill_mode_wireframe]="allow:512:the one-pixel LINE coverage itself; GL and SDL_gpu line rasterization rules may differ, while the fixture asserts every edge and the empty interior"
fi

FIXTURES="$(sed -n '/^set(CNA_PARITY_FIXTURES/,/^)/p' \
    "$REPO_ROOT/modules/graphics/examples/parity/ParityFixtures.cmake" \
    | grep -vE '^\s*#|^set\(|^\)' | tr -d ' \t' | grep -v '^$')"

printf '%-26s %-8s %-8s %-10s %s\n' FIXTURE EASYGL "$COMPARISON_LABEL" FRAMES NOTE
printf '%s\n' "-------------------------------------------------------------------------------"

failures=0
for fixture in $FIXTURES; do
    easygl_bin="$REPO_ROOT/$EASYGL_BUILD/cna_parity_${fixture}_easygl"
    comparison_bin="$(comparison_binary "$fixture")"
    if [[ ! -x "$easygl_bin" || ! -x "$comparison_bin" ]]; then
        printf '%-26s %-8s %-8s %-10s %s\n' "$fixture" "-" "-" "-" "NOT BUILT"
        failures=$((failures + 1))
        continue
    fi

    easygl_png="$WORK/${fixture}_easygl.png"
    comparison_png="$WORK/${fixture}_${COMPARISON_RENDERER}.png"
    if env DISPLAY="$DISPLAY_VALUE" SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
            timeout 120 "$easygl_bin" "$easygl_png" > "$WORK/${fixture}_easygl.log" 2>&1; then
        easygl_verdict="pass"
    else
        easygl_verdict="FAIL"; failures=$((failures + 1))
    fi
    if env DISPLAY="$DISPLAY_VALUE" SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
            timeout 120 "$comparison_bin" "$comparison_png" \
            > "$WORK/${fixture}_${COMPARISON_RENDERER}.log" 2>&1; then
        comparison_verdict="pass"
    else
        comparison_verdict="FAIL"; failures=$((failures + 1))
    fi

    policy="${POLICY[$fixture]:-strict}"
    note=""
    frames="-"
    if [[ -s "$easygl_png" && -s "$comparison_png" ]]; then
        differing="$(python3 - "$easygl_png" "$comparison_png" <<'PY'
import sys
a = open(sys.argv[1], "rb").read()
b = open(sys.argv[2], "rb").read()
if len(a) != len(b):
    print("size-mismatch")
    raise SystemExit
n = 0
for i in range(0, len(a), 4):
    if max(abs(a[i + k] - b[i + k]) for k in range(3)) > 2:
        n += 1
print(n)
PY
)"
        case "$policy" in
            strict)
                if [[ "$differing" == "0" ]]; then
                    frames="identical"
                else
                    frames="DIFFER"; note="$differing pixels differ under a strict policy"
                    failures=$((failures + 1))
                fi
                ;;
            allow:*)
                budget="$(cut -d: -f2 <<<"$policy")"
                note="$(cut -d: -f3- <<<"$policy")"
                if [[ "$differing" =~ ^[0-9]+$ ]] && (( differing <= budget )); then
                    frames="$differing/$budget"
                else
                    frames="OVER"; note="$differing pixels differ, budget $budget -- $note"
                    failures=$((failures + 1))
                fi
                ;;
            internal:*)
                frames="n/a"; note="$(cut -d: -f2- <<<"$policy")"
                ;;
        esac
    fi
    printf '%-26s %-8s %-8s %-10s %s\n' "$fixture" "$easygl_verdict" "$comparison_verdict" "$frames" \
        "${note:0:64}"
done

printf '%s\n' "-------------------------------------------------------------------------------"
if (( failures == 0 )); then
    echo "corpus: every fixture passed under both renderers and met its frame policy"
else
    echo "corpus: $failures failure(s)"
fi
exit $(( failures == 0 ? 0 : 1 ))
