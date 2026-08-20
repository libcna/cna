#!/usr/bin/env bash
# plans/plan_dx8.md DX8-0/design decision 2: run a Windows cross-compiled .exe (DIRECTX8 renderer) under Wine
# with DXVK, using this project's own dedicated Wine prefix.
#
# Usage: scripts/run-wine-directx8.sh <path-to.exe> [args...]
#
# This is a NEW script, deliberately not a shared edit to scripts/run-wine-dxvk.sh/
# run-wine-dxvk9.sh (this repo's own convention keeps every renderer's runtime wrapper independent).
#
# Set CNA_DX8_WINEPREFIX to point at a different prefix; defaults to ~/.wine-cna-dx8 -- a DEDICATED
# prefix, deliberately NOT the shared ~/.wine-cna-d3d11 that D3D9/D3D11/D3D12 use. Real, spike-
# confirmed finding (this session): SDL3's own Windows video renderer does its own internal, dynamic
# `LoadLibrary("dxgi.dll")` probe during SDL_InitSubSystem(SDL_INIT_VIDEO) (for HDR/colorimetry
# capability detection), entirely independent of whether the game itself ever touches D3D. Under
# Xvfb (no real monitor EDID available), DXVK 2.6.0's own EDID/colorimetry-fallback code path hits a
# real bug (an integer divide-by-zero, silently swallowed by Wine's SEH) that corrupts Wine's
# per-process monitor-enumeration state -- so if `dxgi` is overridden to DXVK/native in the prefix,
# EVERY SDL3 program run in it (not just ones that use Direct3D 8) fails at SDL_InitSubSystem with
# "No displays available", reproduced even in a plain SDL-only test binary with zero D3D imports.
# D8VK (D3D8-via-D3D9) genuinely NEEDS d3d9 (and d3d10/d3d10_1/d3d10core/d3d11, all harmless) native
# -- removing those breaks Direct3DCreate8 itself -- but it does NOT need `dxgi` native at all, and
# leaving `dxgi` as Wine's own builtin avoids the crash entirely while D8VK keeps working. One-time
# setup for ~/.wine-cna-dx8 (already done in this environment):
#
#   cp -a ~/.wine-cna-d3d11 ~/.wine-cna-dx8   # reuse the already-working DXVK/Vulkan prefix state
#   WINEPREFIX=~/.wine-cna-dx8 wine reg add 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v d3d8 /d native /f
#   WINEPREFIX=~/.wine-cna-dx8 wine reg delete 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v dxgi /f
#   SYS32="$HOME/.wine-cna-dx8/drive_c/windows/system32"
#   mv "$SYS32/d3d8.dll" "$SYS32/d3d8.dll.old"   # back up Wine's own builtin d3d8.dll (if not already a symlink)
#   ln -sf /usr/lib/dxvk/wine64/d3d8.dll.so "$SYS32/d3d8.dll"
#
# DXVK_LOG_PATH/DXVK_LOG_LEVEL/DXVK_HUD are honored from the calling environment if already set
# (e.g. a CTest driver script), otherwise default to a log level that's enough to confirm DXVK
# (not WineD3D) actually handled the run.
#
# This wrapper also automatically ASSERTS that DXVK itself (not a silent WineD3D fallback) handled
# the run, mirroring run-wine-dxvk9.sh's own identical gate -- a "DXVK: <version>" log line that
# only DXVK's own logger ever prints (vanilla WineD3D never does). Without this, a broken/missing
# d3d8 DLL override could silently degrade every DIRECTX8 pixel test in this suite to validating
# WineD3D's own (different) Direct3D 8 implementation instead, and green CTest output alone would
# never reveal it. Set CNA_DX8_ALLOW_WINED3D=1 to bypass this gate for a deliberate one-off
# non-DXVK diagnostic run -- do not set this for normal test runs. Set CNA_DX8_SKIP_DXVK_GATE=1 for
# a binary that legitimately never creates a DIRECTX8 device at all (e.g. a bare --gtest_list_tests
# call) -- such a run never prints a DXVK line for entirely unrelated, legitimate reasons, and this
# gate would otherwise misreport it as a WineD3D fallback.
set -uo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <path-to.exe> [args...]" >&2
    exit 2
fi

export WINEPREFIX="${CNA_DX8_WINEPREFIX:-$HOME/.wine-cna-dx8}"
export WINEDEBUG="${WINEDEBUG:--all}"

# Real, spike-confirmed driver bug (this session): under this sandbox's AMD RADV GPU, DXVK's
# swapchain (created with VK_EXT_swapchain_maintenance1's dynamic present-mode switching) hits
# VK_ERROR_SURFACE_LOST_KHR on the SECOND consecutive Present() call against an Xvfb window --
# reproduced even in a minimal, CNA/SDL-free raw D3D8 program (two Clear()+Present() calls, no
# resize, no message-pump gap). Forcing DXVK onto the llvmpipe (software) Vulkan device instead of
# the real RADV GPU avoids the bug entirely (confirmed: 3 consecutive Present() calls all succeed).
# This is an environment/driver workaround, not a defect in DirectX8Renderer -- every DIRECTX8 test's
# Draw() calls Present() once explicitly and the framework calls it again automatically afterward
# (EndDraw()), so this is hit by every test in this suite, not just a specific one.
export DXVK_FILTER_DEVICE_NAME="${DXVK_FILTER_DEVICE_NAME:-llvmpipe}"
export DXVK_LOG_LEVEL="${DXVK_LOG_LEVEL:-info}"

# Real bug found earlier in this session family: setting DISPLAY alone is NOT enough in a sandbox
# that also has a real Wayland session (WAYLAND_DISPLAY set) -- Wine prefers its Wayland driver
# over X11 whenever WAYLAND_DISPLAY is present, regardless of DISPLAY.
unset WAYLAND_DISPLAY

if [ ! -f "${WINEPREFIX}/system.reg" ]; then
    echo "error: WINEPREFIX '${WINEPREFIX}' is not an initialized Wine prefix." >&2
    echo "Set it up first: WINEPREFIX=${WINEPREFIX} wineboot --init && WINEPREFIX=${WINEPREFIX} dxvk-setup install" >&2
    exit 1
fi

logFile="$(mktemp "${TMPDIR:-/tmp}/cna-dx8-dxvk-log.XXXXXX")"
trap 'rm -f "$logFile"' EXIT

wine "$@" 2>&1 | tee "$logFile"
wineExit="${PIPESTATUS[0]}"

if [ "${CNA_DX8_ALLOW_WINED3D:-0}" != "1" ] && [ "${CNA_DX8_SKIP_DXVK_GATE:-0}" != "1" ]; then
    if ! grep -Eq 'DXVK: [0-9]+\.[0-9]+' "$logFile"; then
        echo "error: DX8-0 gate failed -- no 'DXVK: <version>' log line was seen in this run's" >&2
        echo "output, so this looks like a silent WineD3D fallback rather than a real DXVK-backed" >&2
        echo "Direct3D 8 run. This invalidates any DX8-semantics pixel-test result from this" >&2
        echo "run -- fix the d3d8 DLL override rather than ignoring this." >&2
        echo "(Set CNA_DX8_ALLOW_WINED3D=1 to deliberately bypass this for a one-off non-DXVK diagnostic run.)" >&2
        exit 3
    fi
fi

exit "$wineExit"
