#!/usr/bin/env bash
# Run a Windows cross-compiled Direct2D test through Steam's Proton runtime.
#
# Direct2D is still a 2D-only CNA backend: Proton supplies Wine's Direct2D runtime and DXVK's
# D3D11/DXGI implementation only for the backend's private presentation device.  This script
# does not install native DLL overrides and does not turn application drawing into D3D11.
#
# Usage: scripts/run-proton-direct2d.sh <path-to.exe> [args...]
#
# Optional overrides:
#   CNA_DIRECT2D_PROTON_STEAM_ROOT       Steam root containing steamapps/ (auto-detected)
#   CNA_DIRECT2D_PROTON_DIR              Proton install directory (auto-detected from root)
#   CNA_DIRECT2D_PROTON_COMPAT_DATA_PATH dedicated Proton compat-data directory
#
# The compat-data directory is deliberately separate from every system-Wine prefix.  In
# particular, this runner refuses ~/.wine: Proton's Wine build must never upgrade or otherwise
# alter a developer's ordinary Wine prefix.
set -uo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <path-to.exe> [args...]" >&2
    exit 2
fi

if [ -n "${CNA_DIRECT2D_PROTON_STEAM_ROOT:-}" ]; then
    STEAM_ROOT="${CNA_DIRECT2D_PROTON_STEAM_ROOT}"
else
    for candidate in "$HOME/.steam/debian-installation" "$HOME/.steam/steam" "$HOME/.local/share/Steam"; do
        if [ -x "$candidate/steamapps/common/Proton - Experimental/proton" ]; then
            STEAM_ROOT="$candidate"
            break
        fi
    done
    STEAM_ROOT="${STEAM_ROOT:-$HOME/.steam/steam}"
fi

PROTON_DIR="${CNA_DIRECT2D_PROTON_DIR:-$STEAM_ROOT/steamapps/common/Proton - Experimental}"
if [ ! -x "${PROTON_DIR}/proton" ]; then
    echo "error: Proton launcher not found at '${PROTON_DIR}/proton'." >&2
    echo "Set CNA_DIRECT2D_PROTON_DIR or CNA_DIRECT2D_PROTON_STEAM_ROOT to the Steam install path." >&2
    exit 1
fi

export STEAM_COMPAT_DATA_PATH="${CNA_DIRECT2D_PROTON_COMPAT_DATA_PATH:-$HOME/.wine-cna-direct2d-protonrun}"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_ROOT}"
# A bare executable is not a Steam catalogue game. Proton nevertheless requires these variables
# to initialise a self-contained compat prefix; their numeric values carry no CNA meaning.
export SteamAppId="${SteamAppId:-0}"
export SteamGameId="${SteamGameId:-0}"

case "${STEAM_COMPAT_DATA_PATH}" in
    "${HOME}/.wine"|"${HOME}/.wine/"*)
        echo "error: refusing to use the default ~/.wine prefix for Direct2D Proton tests." >&2
        echo "       Set CNA_DIRECT2D_PROTON_COMPAT_DATA_PATH to a dedicated directory." >&2
        exit 1
        ;;
esac

mkdir -p "${STEAM_COMPAT_DATA_PATH}"
exec python3 "${PROTON_DIR}/proton" run "$@"
