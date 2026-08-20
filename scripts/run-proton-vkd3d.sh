#!/usr/bin/env bash
# plans/plan_dx.md DX-102/DX-114: run a Windows cross-compiled .exe (D3D12 renderer) through a REAL,
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
# modules/renderers/directx12/examples/directx12_swapchain_diag.cpp's own file-based log (proton's process-launch plumbing does not
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

# --- Safety guard (added 2026-07-14 after a real incident, see below) -------------------------
#
# HARD refusal to ever operate on the developer's own default Wine prefix (~/.wine). This is not
# theoretical: on 2026-07-14 an earlier iteration of this script's own investigation ran Proton's
# `wine` binary WITHOUT WINEPREFIX set, so it fell back to ~/.wine -- the developer's real, personal
# prefix (with unrelated apps installed in it). Proton ships wine-11.0 while the system wine is
# 10.0, so that silently triggered an automatic prefix UPGRADE of ~/.wine: it rewrote system.reg/
# user.reg/userdef.reg/dosdevices, then HUNG forever on the setupapi "found new hardware" /
# install_mono wizard, leaving the personal prefix half-upgraded and two zombie Wine process trees
# running for 9.5 hours (visible as "steam_proton is not responding" desktop popups). The
# WINEPREFIX= assignments below are the actual fix for the fallback; this guard is the seatbelt in
# case a future edit ever drops one of them again.
case "${PFX}" in
    "${HOME}/.wine"|"${HOME}/.wine/"*)
        echo "error: refusing to run -- the prefix path resolves to the default ~/.wine prefix." >&2
        echo "       That is the developer's own personal Wine prefix, not a CNA test prefix." >&2
        echo "       Set CNA_D3D12_PROTON_COMPAT_DATA_PATH to a dedicated directory." >&2
        exit 1
        ;;
esac

# Scoped cleanup: kills ONLY the wineserver owning this specific prefix (WINEPREFIX-scoped, so it
# can never touch the developer's own ~/.wine session or any other prefix's processes).
kill_this_prefix() {
    WINEPREFIX="${PFX}" "${PROTON_DIR}/files/bin/wineserver" -k 2>/dev/null || true
}

# First-ever run for this prefix: `proton run` itself bootstraps the prefix automatically (its own
# main() calls make_default_prefix() unconditionally before running any target) -- no separate
# wineboot step needed, and none should be attempted here: a hand-built `wineboot --init` hits an
# unrelated first-run "found new hardware" GUI-wizard hang under Proton's own DLL set (this
# script's own 2026-07-14 investigation). The system32 directory the DLL overlay below writes into
# does not exist until that bootstrap has run once, so on a fresh prefix, run the real target once
# first (bootstraps the prefix; its result is discarded -- it runs under Proton's own *default*
# D3D12 support, not vkd3d-proton, since the overlay hasn't been applied yet) before overlaying and
# running for real.
#
# The bootstrap is wrapped in a timeout: the same setupapi/install_mono wizard that hung the
# personal prefix above can in principle hang here too, and an unbounded hang leaves zombie Wine
# process trees (and "not responding" desktop popups) behind for hours. On timeout, tear down this
# prefix's own wineserver and fail loudly rather than hanging.
BOOTSTRAP_TIMEOUT="${CNA_D3D12_PROTON_BOOTSTRAP_TIMEOUT:-300}"
if [ ! -f "${PFX}/system.reg" ]; then
    echo "run-proton-vkd3d.sh: bootstrapping a fresh Proton-managed prefix at ${STEAM_COMPAT_DATA_PATH} (one-time, timeout ${BOOTSTRAP_TIMEOUT}s)..." >&2
    timeout --kill-after=15 "${BOOTSTRAP_TIMEOUT}" \
        python3 "${PROTON_DIR}/proton" run "$1" >/dev/null 2>&1
    bootstrap_rc=$?   # captured immediately -- a later [ ] would clobber $?
    if [ "${bootstrap_rc}" -eq 124 ] || [ "${bootstrap_rc}" -eq 137 ]; then
        echo "error: prefix bootstrap timed out after ${BOOTSTRAP_TIMEOUT}s (the known setupapi/" >&2
        echo "       install_mono first-run wizard hang). Killing this prefix's wineserver." >&2
        kill_this_prefix
        exit 1
    fi
    if [ ! -f "${PFX}/system.reg" ]; then
        echo "error: prefix bootstrap did not produce '${PFX}/system.reg'." >&2
        kill_this_prefix
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
# falls back to its own default ~/.wine prefix. That is not a hypothetical: it really happened on
# 2026-07-14 and half-upgraded the developer's personal prefix (see the guard block above for the
# full incident). Never drop these WINEPREFIX= assignments; the guard above is the backstop.
# Timeout-wrapped for the same reason the bootstrap is -- a hung `wine reg add` would otherwise
# leave a zombie Wine process tree behind indefinitely.
timeout --kill-after=5 60 env WINEPREFIX="${PFX}" "${PROTON_DIR}/files/bin/wine" reg add "HKCU\\Software\\Wine\\DllOverrides" /v d3d12 /d native /f >/dev/null 2>&1 || true
timeout --kill-after=5 60 env WINEPREFIX="${PFX}" "${PROTON_DIR}/files/bin/wine" reg add "HKCU\\Software\\Wine\\DllOverrides" /v d3d12core /d native /f >/dev/null 2>&1 || true

exec python3 "${PROTON_DIR}/proton" run "$@"
