#!/usr/bin/env bash
# plans/plan_d3d10.md: run a Windows cross-compiled .exe (D3D10 renderer) under Wine with DXVK, using
# this project's own dedicated Wine prefix.
#
# Usage: scripts/run-wine-directx10.sh <path-to.exe> [args...]
#
# Set CNA_D3D10_WINEPREFIX to point at a different prefix; defaults to ~/.wine-cna-d3d10 -- a
# DEDICATED prefix (copied from the already-Vulkan/DXVK-proven ~/.wine-cna-d3d11 to reuse its
# state), NOT the shared one D3D9/D3D11/D3D12 use directly. Two real, spike-confirmed findings
# (dx10-spike/README.md) drove this:
#
# 1. DXVK 2.6.0 ships NO d3d10.dll at all (only d3d10core.dll) -- copying ~/.wine-cna-d3d11
#    inherits BROKEN, dangling d3d10.dll/d3d10_1.dll symlinks (leftover from an older DXVK
#    version). One-time fix already applied to ~/.wine-cna-d3d10: those two DLL overrides were
#    removed entirely and Wine's own real builtin d3d10.dll/d3d10_1.dll restored -- only
#    `d3d10core` (plus `dxgi`/`d3d9`/`d3d11`) stays overridden to native/DXVK.
# 2. Unlike DIRECTX8 (which could avoid a real DXGI swap chain entirely), D3D10 genuinely needs one --
#    and DXVK's own dxgi.dll has a real bug (readMonitorEdidFromKey doesn't correctly retry on a
#    too-small registry-value-size probe buffer) that crashes on the FIRST Present() call under
#    Xvfb, confirmed independent of GPU and independent of whether a valid EDID is injected into
#    the registry. Fix: run against DISPLAY=:0 (the real desktop), NOT Xvfb :99 -- this matches an
#    ALREADY-ESTABLISHED precedent for this project's own D3D11/D3D12 Wine+DXVK testing
#    (feedback_d3d11_wine_test_run_recipe), briefly flashing small windows on the real desktop
#    during test runs. DXVK_LOG_PATH/DXVK_LOG_LEVEL/DXVK_HUD are honored from the calling
#    environment if already set (e.g. a CTest driver script), otherwise default to a log level
#    that's enough to confirm DXVK actually handled the run.
#
# This wrapper also asserts that DXVK itself (not a silent WineD3D fallback) handled the run,
# mirroring run-wine-directx8.sh/run-wine-dxvk.sh's own identical gate -- a "DXVK: <version>" log line
# that only DXVK's own logger ever prints. Set CNA_D3D10_ALLOW_WINED3D=1 to bypass this gate for a
# deliberate one-off non-DXVK diagnostic run -- do not set this for normal test runs. Set
# CNA_D3D10_SKIP_DXVK_GATE=1 for a binary that legitimately never creates a D3D10 device at all
# (e.g. a bare --gtest_list_tests call).
set -uo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <path-to.exe> [args...]" >&2
    exit 2
fi

export WINEPREFIX="${CNA_D3D10_WINEPREFIX:-$HOME/.wine-cna-d3d10}"
export WINEDEBUG="${WINEDEBUG:--all}"
export DXVK_LOG_LEVEL="${DXVK_LOG_LEVEL:-info}"

# Real bug found in this session family: setting DISPLAY alone is NOT enough in a sandbox that
# also has a real Wayland session (WAYLAND_DISPLAY set) -- Wine prefers its Wayland driver over
# X11 whenever WAYLAND_DISPLAY is present, regardless of DISPLAY.
unset WAYLAND_DISPLAY

# See the header comment above for why this differs from every other renderer's own wrapper
# (which all default to Xvfb :99): D3D10's real DXGI swap chain hits a DXVK dxgi bug under Xvfb
# that DISPLAY=:0 avoids.
export DISPLAY="${CNA_D3D10_DISPLAY:-:0}"

if [ ! -f "${WINEPREFIX}/system.reg" ]; then
    echo "error: WINEPREFIX '${WINEPREFIX}' is not an initialized Wine prefix." >&2
    echo "Set it up first: WINEPREFIX=${WINEPREFIX} wineboot --init && WINEPREFIX=${WINEPREFIX} dxvk-setup install" >&2
    exit 1
fi

logFile="$(mktemp "${TMPDIR:-/tmp}/cna-d3d10-dxvk-log.XXXXXX")"
trap 'rm -f "$logFile"' EXIT

wine "$@" 2>&1 | tee "$logFile"
wineExit="${PIPESTATUS[0]}"

if [ "${CNA_D3D10_ALLOW_WINED3D:-0}" != "1" ] && [ "${CNA_D3D10_SKIP_DXVK_GATE:-0}" != "1" ]; then
    if ! grep -Eq 'DXVK: [0-9]+\.[0-9]+' "$logFile"; then
        echo "error: D3D10-0 gate failed -- no 'DXVK: <version>' log line was seen in this run's" >&2
        echo "output, so this looks like a silent WineD3D fallback rather than a real DXVK-backed" >&2
        echo "Direct3D 10 run. This invalidates any D3D10-semantics pixel-test result from this" >&2
        echo "run -- fix the d3d10core DLL override rather than ignoring this." >&2
        echo "(Set CNA_D3D10_ALLOW_WINED3D=1 to deliberately bypass this for a one-off non-DXVK diagnostic run.)" >&2
        exit 3
    fi
fi

exit "$wineExit"
