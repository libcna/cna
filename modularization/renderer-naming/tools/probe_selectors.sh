#!/usr/bin/env bash
# All-selector configure probe (DESIGN.md validation matrix).
#
# Configures the repository once per public renderer identity in one shared probe
# build dir and classifies the outcome:
#   ACCEPTED-USING    -- reached "CNA: Using <sel> ..." (selector accepted, deps found)
#   ACCEPTED-GATED    -- rejected by the *platform/dependency* gate with the new
#                        name in the message (selector accepted, host can't build it)
#   UNKNOWN           -- hit the "Unknown graphics renderer" fallback (NOT accepted)
#   OTHER             -- anything else (inspect log)
# Old selectors are probed too and must classify UNKNOWN.
set -u
REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
B="${CNA_PROBE_DIR:-$REPO/build-probe/cmake-probe-selector}"
export CCACHE_DIR="${CCACHE_DIR:-$HOME/.cache/ccache}"
LOGDIR="${CNA_PROBE_LOGDIR:-$REPO/build-probe/selector-probes}"
mkdir -p "$LOGDIR"

NEW_SELECTORS="SDL_RENDERER OPENGLES3 OPENGL33 WEBGL1 WEBGL2 BGFX VULKAN WEBGPU MAGNUM \
HEADLESS SOFTWARE STUB DIRECTX11 DIRECTX12 DIRECT2D CANVAS HTML_DOM SKIA ASCII FREEDIRECT \
DIRECTX9 DIRECTX1 DIRECTX2 DIRECTX3 DIRECTX5 DIRECTX6 DIRECTX7 DIRECTX8 DIRECTX10 SDL_GPU \
OPENGLES1 OPENGL4 OPENGL1 OPENGL2 WICKED SOKOL DILIGENT GLIDE GDI LLGL METAL"
OLD_SELECTORS="OPENGLES DX1 DX2 DX3 DX5 DX6 DX7 DX8 D3D9 D3D10 D3D11 D3D12"

probe() {
    local sel="$1" expect="$2"
    local log="$LOGDIR/$sel.log"
    timeout 240 cmake -S "$REPO" -B "$B" \
        -DCNA_GRAPHICS_RENDERER="$sel" \
        -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
        -DCNA_CNAEXT=OFF -DCNA_DEVICES=OFF \
        >"$log" 2>&1
    local cls="OTHER"
    if grep -q "CNA: Using $sel" "$log"; then
        cls="ACCEPTED-USING"
    elif grep -q "Unknown graphics renderer" "$log"; then
        cls="UNKNOWN"
    elif grep -Eq "CNA: $sel (renderer|is)|CNA: \\\$?\{?CNA_GRAPHICS_RENDERER\}? " "$log" \
        || grep -q "CNA: $sel" "$log"; then
        cls="ACCEPTED-GATED"
    elif grep -Eq "only builds when targeting|cannot target|currently supported only|targets the native" "$log"; then
        cls="ACCEPTED-GATED"
    elif grep -Eq "CMake Error" "$log"; then
        cls="ACCEPTED-DEP-ERROR"
    fi
    printf "%-14s expect=%-16s got=%s\n" "$sel" "$expect" "$cls"
}

echo "== new selectors (must all be accepted) =="
for s in $NEW_SELECTORS; do probe "$s" accepted; done
echo "== old selectors (must all be UNKNOWN) =="
for s in $OLD_SELECTORS; do probe "$s" unknown; done
