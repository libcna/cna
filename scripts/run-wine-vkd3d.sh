#!/usr/bin/env bash
# plans/plan_dx.md DX-100/DX-102: run a Windows cross-compiled .exe (D3D12 renderer) under Wine with
# vkd3d-proton, using a dedicated Wine prefix distinct from D3D11's own DXVK prefix
# (run-wine-dxvk.sh / ~/.wine-cna-d3d11) -- D3D11 and D3D12 need different translation layers
# (DXVK vs. vkd3d-proton) with different native-DLL overrides, so sharing one prefix would require
# constantly re-registering overrides between test runs.
#
# Usage: scripts/run-wine-vkd3d.sh <path-to.exe> [args...]
#
# Set CNA_D3D12_WINEPREFIX to point at a different prefix; defaults to ~/.wine-cna-d3d12.
# DX-100's own spike set this prefix up by copying vkd3d-proton's d3d12.dll/d3d12core.dll (found
# locally via this machine's Steam "Proton - Experimental" install, no new install/sudo needed --
# same no-elevated-changes bar DXVK's own setup met) into the prefix's system32/syswow64 and
# registering them as native DLL overrides (`d3d12`/`d3d12core` = native).
#
# Mirrors DX-85's DXVK-engagement gate: vkd3d-proton's own logger (VKD3D_DEBUG=info, set below)
# prints a real, distinguishing "vkd3d-proton - applicationVersion: <version>" line the moment it
# actually initializes -- confirmed empirically by this project's own real run (plans/plan_dx.md DX-102's
# row) against this exact prefix. A vanilla Wine WineD3D fallback for D3D12 does not exist at all
# (WineD3D never implemented D3D12) -- if device creation had silently gone through some other
# path, feature-level negotiation itself would have failed outright (D3D12CreateDevice has no
# non-vkd3d-proton implementation to fall back to under Wine), so this gate mainly guards against a
# stale/misconfigured prefix rather than a silent wrong-renderer substitution the way DXVK's own gate
# does. Set CNA_D3D12_SKIP_VKD3D_GATE=1 for a binary that legitimately never creates a device (none
# exist yet, but mirrors DX-85's own escape hatch for consistency).
set -uo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <path-to.exe> [args...]" >&2
    exit 2
fi

export WINEPREFIX="${CNA_D3D12_WINEPREFIX:-$HOME/.wine-cna-d3d12}"
export WINEDEBUG="${WINEDEBUG:--all}"
export VKD3D_DEBUG="${VKD3D_DEBUG:-info}"

if [ ! -f "${WINEPREFIX}/system.reg" ]; then
    echo "error: WINEPREFIX '${WINEPREFIX}' is not an initialized Wine prefix." >&2
    echo "Set it up first (see plans/plan_dx.md DX-100's own row for the exact steps this machine used)." >&2
    exit 1
fi

logFile="$(mktemp "${TMPDIR:-/tmp}/cna-d3d12-vkd3d-log.XXXXXX")"
trap 'rm -f "$logFile"' EXIT

wine "$@" 2>&1 | tee "$logFile"
wineExit="${PIPESTATUS[0]}"

if [ "${CNA_D3D12_SKIP_VKD3D_GATE:-0}" != "1" ]; then
    if ! grep -Eq 'vkd3d-proton - applicationVersion: [0-9]+\.[0-9]+\.[0-9]+' "$logFile"; then
        echo "error: DX-102 gate failed -- no 'vkd3d-proton - applicationVersion: <version>' log" >&2
        echo "line was seen in this run's output, so device creation may not have actually" >&2
        echo "succeeded through vkd3d-proton. Fix the prefix/vkd3d-proton install rather than" >&2
        echo "ignoring this. (Set CNA_D3D12_SKIP_VKD3D_GATE=1 for a binary that legitimately never" >&2
        echo "creates a D3D12 device.)" >&2
        exit 3
    fi
fi

exit "$wineExit"
