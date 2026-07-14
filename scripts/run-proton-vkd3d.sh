#!/usr/bin/env bash
# plan_dx.md DX-102/DX-114: run a Windows cross-compiled .exe (D3D12 backend) through a REAL,
# properly Steam/Proton-managed launch -- distinct from run-wine-vkd3d.sh, which runs vkd3d-proton's
# d3d12.dll/d3d12core.dll natively-overridden inside a hand-built *system* Wine prefix.
#
# Why this script exists: run-wine-vkd3d.sh's approach proves the D3D12 device/queue/heap/command-
# list/fence path (real, works well), but CreateSwapChainForHwnd crashes under it -- root-caused
# (2026-07-14) to a genuine ABI/integration mismatch: Debian's *system* dxgi.dll (paired with
# system Wine's own wined3d.dll/kernel32.dll/etc, all from one consistent build) cannot hand a
# D3D12 command queue to vkd3d-proton's separately-overridden d3d12.dll -- vkd3d-proton's own
# dxgi.dll (the one that DOES understand this handoff) is only ABI-compatible with the REST of
# Proton's own matched Wine build (its own wined3d.dll, its own libvkd3d-*.dll support libraries,
# etc.), not with a foreign system Wine installation piecemeal.
#
# This script runs the target .exe through Proton's own `proton run` launcher (found via this
# machine's local Steam "Proton - Experimental" install -- the same source run-wine-vkd3d.sh's own
# vkd3d-proton DLLs came from), in a dedicated Proton-managed prefix (STEAM_COMPAT_DATA_PATH) that
# is Proton's own self-consistent DLL set end to end -- not the hand-built system-Wine prefix
# run-wine-vkd3d.sh uses. vkd3d-proton's d3d12.dll/d3d12core.dll (its own, standalone "vkd3d-proton"
# subdirectory build, distinct from whatever Proton's own *default* D3D12 support is) are layered
# on top as a native DLL override, exactly like run-wine-vkd3d.sh's own approach -- the difference
# is *everything else* (dxgi.dll, wined3d.dll, libvkd3d-*.dll, kernel32.dll, ...) now comes from
# one single, mutually-consistent Proton build instead of a system Wine + foreign DLL patchwork.
#
# Real result (2026-07-14, this exact script's manual predecessor): CreateSwapChainForHwnd genuinely
# returns S_OK through this launch path -- IsSwapChainAvailableEXT() == true, confirmed via
# examples/d3d12_swapchain_diag.cpp's own file-based log (proton's process-launch plumbing does not
# reliably forward the child's stdout back to the invoking shell -- write results to a file from
# within the target .exe if you need to read them back, not stdout).
#
# Usage: scripts/run-proton-vkd3d.sh <path-to.exe> [args...]
#
# Set CNA_D3D12_PROTON_DIR to override the "Proton - Experimental" install directory (auto-detected
# under ~/.steam/steam/steamapps/common by default). Set CNA_D3D12_PROTON_COMPAT_DATA_PATH to
# override the persistent prefix directory (defaults to ~/.wine-cna-d3d12-protonrun). The prefix is
# bootstrapped automatically on first use via Proton's own launcher -- no manual wineboot step
# needed (unlike run-wine-vkd3d.sh's hand-built system-Wine prefix).
set -uo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <path-to.exe> [args...]" >&2
    exit 2
fi

STEAM_ROOT="${CNA_D3D12_PROTON_STEAM_ROOT:-$HOME/.steam/steam}"
PROTON_DIR="${CNA_D3D12_PROTON_DIR:-$STEAM_ROOT/steamapps/common/Proton - Experimental}"

if [ ! -x "${PROTON_DIR}/proton" ]; then
    echo "error: Proton launcher not found at '${PROTON_DIR}/proton'." >&2
    echo "Set CNA_D3D12_PROTON_DIR to the correct 'Proton - Experimental' (or similar) install dir." >&2
    exit 1
fi

VKD3D_PROTON_DLL_DIR="${PROTON_DIR}/files/lib/wine/vkd3d-proton/x86_64-windows"
if [ ! -f "${VKD3D_PROTON_DLL_DIR}/d3d12.dll" ] || [ ! -f "${VKD3D_PROTON_DLL_DIR}/d3d12core.dll" ]; then
    echo "error: vkd3d-proton d3d12.dll/d3d12core.dll not found under '${VKD3D_PROTON_DLL_DIR}'." >&2
    exit 1
fi

export STEAM_COMPAT_DATA_PATH="${CNA_D3D12_PROTON_COMPAT_DATA_PATH:-$HOME/.wine-cna-d3d12-protonrun}"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_ROOT}"
# Dummy app/game IDs -- this is a bare, non-Steam-catalog invocation; Proton just needs these set,
# their actual values are not meaningful outside Steam's own library/overlay integration.
export SteamAppId="${SteamAppId:-0}"
export SteamGameId="${SteamGameId:-0}"

PFX="${STEAM_COMPAT_DATA_PATH}/pfx"
mkdir -p "${STEAM_COMPAT_DATA_PATH}"

# First-ever run for this prefix: `proton run` itself bootstraps the prefix automatically (its own
# main() calls make_default_prefix() unconditionally before running any target) -- no separate
# wineboot step needed, and none should be attempted here: a hand-built `wineboot --init` hits an
# unrelated first-run "found new hardware" GUI-wizard hang under Proton's own DLL set (this
# script's own 2026-07-14 investigation). The system32 directory the DLL overlay below writes into
# does not exist until that bootstrap has run once, so on a fresh prefix, run the real target once
# first (bootstraps the prefix; its result is discarded -- it runs under Proton's own *default*
# D3D12 support, not vkd3d-proton, since the overlay hasn't been applied yet) before overlaying and
# running for real.
if [ ! -f "${PFX}/system.reg" ]; then
    echo "run-proton-vkd3d.sh: bootstrapping a fresh Proton-managed prefix at ${STEAM_COMPAT_DATA_PATH} (one-time)..." >&2
    python3 "${PROTON_DIR}/proton" run "$1" >/dev/null 2>&1 || true
    if [ ! -f "${PFX}/system.reg" ]; then
        echo "error: prefix bootstrap did not produce '${PFX}/system.reg'." >&2
        exit 1
    fi
fi

# Layer vkd3d-proton's own d3d12.dll/d3d12core.dll on top of Proton's own (self-consistent) default
# D3D12 support, as a native override -- same overlay run-wine-vkd3d.sh applies to a system Wine
# prefix, just onto a Proton-managed one instead.
cp -f "${VKD3D_PROTON_DLL_DIR}/d3d12.dll" "${PFX}/drive_c/windows/system32/d3d12.dll"
cp -f "${VKD3D_PROTON_DLL_DIR}/d3d12core.dll" "${PFX}/drive_c/windows/system32/d3d12core.dll"
# WINEPREFIX must be set explicitly here -- these two calls bypass `proton run` (which is what
# normally translates STEAM_COMPAT_DATA_PATH into the real prefix path), so without it `wine`
# falls back to its own default ~/.wine prefix (auto-initializing one from scratch, which hits an
# unrelated first-run "found new hardware" GUI-wizard hang under a raw Proton wine binary).
WINEPREFIX="${PFX}" "${PROTON_DIR}/files/bin/wine" reg add "HKCU\\Software\\Wine\\DllOverrides" /v d3d12 /d native /f >/dev/null 2>&1
WINEPREFIX="${PFX}" "${PROTON_DIR}/files/bin/wine" reg add "HKCU\\Software\\Wine\\DllOverrides" /v d3d12core /d native /f >/dev/null 2>&1

exec python3 "${PROTON_DIR}/proton" run "$@"
