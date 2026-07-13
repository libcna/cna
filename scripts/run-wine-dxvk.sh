#!/usr/bin/env bash
# plan_dx.md DX-3: run a Windows cross-compiled .exe (D3D11 backend) under Wine
# with DXVK, using this project's own dedicated Wine prefix.
#
# Usage: scripts/run-wine-dxvk.sh <path-to.exe> [args...]
#
# Set CNA_D3D11_WINEPREFIX to point at a different prefix; defaults to
# ~/.wine-cna-d3d11 (created via `wineboot --init` + `dxvk-setup install`, see
# programs.md §10). DXVK_LOG_PATH/DXVK_LOG_LEVEL/DXVK_HUD are honored from the
# calling environment if already set (e.g. a CTest driver script), otherwise
# default to a log level that's enough to confirm DXVK (not WineD3D) actually
# handled the run.
set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <path-to.exe> [args...]" >&2
    exit 2
fi

export WINEPREFIX="${CNA_D3D11_WINEPREFIX:-$HOME/.wine-cna-d3d11}"
export WINEDEBUG="${WINEDEBUG:--all}"
export DXVK_LOG_LEVEL="${DXVK_LOG_LEVEL:-info}"

if [ ! -f "${WINEPREFIX}/system.reg" ]; then
    echo "error: WINEPREFIX '${WINEPREFIX}' is not an initialized Wine prefix." >&2
    echo "Set it up first: WINEPREFIX=${WINEPREFIX} wineboot --init && WINEPREFIX=${WINEPREFIX} dxvk-setup install" >&2
    exit 1
fi

exec wine "$@"
