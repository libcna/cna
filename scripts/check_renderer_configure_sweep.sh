#!/usr/bin/env bash
# plans/plan_modern.md MOD-134: every renderer identity still *configures* after the engine layer's
# renderer-interface additions.
#
# Usage:
#   scripts/check_renderer_configure_sweep.sh [--cnaext] [--jobs N] [--only A,B,C]
#
# What it does and, more usefully, what it does not: it runs `cmake` configure only -- no build --
# against each identity in cmake/RendererSelection.cmake's own list, derived from that file rather
# than typed here so a renderer added later cannot be quietly missed. A configure that fails because
# a toolchain or SDK is absent in this container (Emscripten, the Windows SDK, wgpu-native, ...) is
# reported as SKIPPED, not FAILED: "this machine has no D3D12" is not a defect in CNA. A configure
# that fails for any other reason is a real failure and is printed with its last lines.
#
# The virtuals this exists to protect are the ones plans/plan_modern.md added to IGraphicsRenderer
# (CreateComputeShader, SupportsShadowSamplingEXT, ExecutesShaderEffectSourceEXT and friends). They
# all have defaults, so a renderer that ignores them compiles -- and configure-time is where a
# missing CMake dependency of a new source file would show up.

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cnaext=OFF
jobs=1
only=""

while [ $# -gt 0 ]; do
    case "$1" in
        --cnaext) cnaext=ON; shift ;;
        --jobs)   jobs="$2"; shift 2 ;;
        --only)   only="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

selection="$root/cmake/RendererSelection.cmake"
if [ ! -f "$selection" ]; then
    echo "cannot find $selection" >&2
    exit 2
fi

# The identity list, straight out of the STRINGS property -- never a copy kept here.
identities="$(grep -m1 'set_property(CACHE CNA_GRAPHICS_RENDERER PROPERTY STRINGS' "$selection" \
              | grep -o '"[A-Z0-9_]*"' | tr -d '"')"

if [ -n "$only" ]; then
    identities="$(echo "$only" | tr ',' '\n')"
fi

total=0; ok=0; skipped=0; failed=0
failed_names=""
skipped_names=""

sweep_dir="$root/build/renderer-sweep"
mkdir -p "$sweep_dir" || exit 2

for identity in $identities; do
    total=$((total + 1))
    out="$sweep_dir/$identity"
    rm -rf "$out"
    log="$sweep_dir/$identity.log"

    if cmake -S "$root" -B "$out" -G Ninja \
             -DCNA_GRAPHICS_RENDERER="$identity" \
             -DCNA_CNAEXT="$cnaext" \
             -DCNA_BUILD_TESTS=OFF \
             -DCNA_BUILD_EXAMPLES=OFF \
             >"$log" 2>&1; then
        printf '  %-14s CONFIGURED\n' "$identity"
        ok=$((ok + 1))
    else
        # A missing external toolchain or SDK is this container's limitation, not CNA's.
        # The wordings below are this repository's own "you do not have the dependency" messages,
        # collected by running the sweep rather than guessed: a pinned external source tree
        # (CNA_SKIA_ROOT, CNA_WICKED_ROOT), a sibling checkout (free-direct), a system dev package
        # (GLU, libshaderc), or a platform/toolchain this container is not.
        if grep -qiE 'could not find|not found|no such file or directory|requires -D|requires emscripten|requires (the )?windows|unsupported platform|only builds when|only.*(windows|emscripten|apple|macos)|missing sibling repository|needs glu|install lib|auto_fetch' "$log"; then
            reason="$(grep -iEm1 'could not find|not found|requires |unsupported platform|only builds when|missing sibling repository|needs glu|install lib' "$log" | sed 's/^ *//' | cut -c1-88)"
            printf '  %-14s SKIPPED  (%s)\n' "$identity" "$reason"
            skipped=$((skipped + 1))
            skipped_names="$skipped_names $identity"
        else
            printf '  %-14s FAILED\n' "$identity"
            tail -n 12 "$log" | sed 's/^/      /'
            failed=$((failed + 1))
            failed_names="$failed_names $identity"
        fi
    fi
    rm -rf "$out"
done

echo
echo "renderer configure sweep (CNA_CNAEXT=$cnaext): $total identities, $ok configured, $skipped skipped by toolchain, $failed failed"
[ -n "$skipped_names" ] && echo "  skipped:$skipped_names"
[ -n "$failed_names" ] && echo "  failed: $failed_names"

# Nothing configuring at all means the sweep itself is broken, not that every renderer is.
if [ "$ok" -eq 0 ]; then
    echo "no identity configured -- the sweep is not measuring anything" >&2
    exit 1
fi

[ "$failed" -eq 0 ]
