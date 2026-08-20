#!/usr/bin/env bash
# Run a Windows cross-compiled Direct2D test through Steam's Proton runtime.
#
# Direct2D is still a 2D-only CNA renderer: Proton supplies Wine's Direct2D runtime and DXVK's
# D3D11/DXGI implementation only for the renderer's private presentation device.  This script
# does not install native DLL overrides and does not turn application drawing into D3D11.
#
# Usage: scripts/run-proton-direct2d.sh <path-to.exe> [args...]
#
# D2D-122: the runtime is PINNED by scripts/direct2d-proton-pin.txt and its exact identity
# (Proton build id, Wine build, DXVK version) is published to stderr and to an identity file, so a
# recorded Direct2D Proton result names the runtime that produced it. "Proton - Experimental" moves
# under a passing run and is refused unless explicitly allowed for a one-off investigation.
#
# Optional overrides:
#   CNA_DIRECT2D_PROTON_STEAM_ROOT       Steam root containing steamapps/ (auto-detected)
#   CNA_DIRECT2D_PROTON_DIR              Proton install directory (overrides the pin list)
#   CNA_DIRECT2D_PROTON_COMPAT_DATA_PATH dedicated Proton compat-data directory
#   CNA_DIRECT2D_PROTON_PIN_FILE         alternative pin list
#   CNA_DIRECT2D_PROTON_IDENTITY_FILE    where to write the identity record
#   CNA_DIRECT2D_PROTON_ALLOW_EXPERIMENTAL=1  permit the moving Experimental runtime
#   CNA_DIRECT2D_PROTON_DRY_RUN=1        resolve and publish the identity, then exit without running
#
# The compat-data directory is deliberately separate from every system-Wine prefix.  In
# particular, this runner refuses ~/.wine: Proton's Wine build must never upgrade or otherwise
# alter a developer's ordinary Wine prefix.
set -uo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <path-to.exe> [args...]" >&2
    exit 2
fi

# D2D-120: identical canonical virtual-display entry as the Wine runner; see that script.
if [ "${CNA_DIRECT2D_VIRTUAL_DISPLAY:-0}" != "1" ] && [ "${CNA_DIRECT2D_USE_HOST_DISPLAY:-0}" != "1" ]; then
    exec "$(dirname "$0")/run-direct2d-virtual-display.sh" "$0" "$@"
fi

PIN_FILE="${CNA_DIRECT2D_PROTON_PIN_FILE:-$(dirname "$0")/direct2d-proton-pin.txt}"
ALLOW_EXPERIMENTAL="${CNA_DIRECT2D_PROTON_ALLOW_EXPERIMENTAL:-0}"

# Checked here, in the parent shell: read_pinned_runtimes runs inside command substitution below,
# where an exit would only end the subshell and let an unpinned run continue.
if [ ! -f "${PIN_FILE}" ] && [ -z "${CNA_DIRECT2D_PROTON_DIR:-}" ]; then
    echo "error: missing Proton pin file '${PIN_FILE}'; the Direct2D lane refuses an unpinned runtime." >&2
    exit 1
fi

read_pinned_runtimes() {
    [ -f "${PIN_FILE}" ] || return 0
    grep -vE '^[[:space:]]*(#|$)' "${PIN_FILE}"
}

STEAM_ROOTS=()
if [ -n "${CNA_DIRECT2D_PROTON_STEAM_ROOT:-}" ]; then
    STEAM_ROOTS+=("${CNA_DIRECT2D_PROTON_STEAM_ROOT}")
else
    STEAM_ROOTS+=("$HOME/.steam/debian-installation" "$HOME/.steam/steam" "$HOME/.local/share/Steam")
fi

STEAM_ROOT="${STEAM_ROOTS[0]}"
PROTON_DIR=""
if [ -n "${CNA_DIRECT2D_PROTON_DIR:-}" ]; then
    PROTON_DIR="${CNA_DIRECT2D_PROTON_DIR}"
else
    while IFS= read -r pinned; do
        [ -n "${pinned}" ] || continue
        for candidate_root in "${STEAM_ROOTS[@]}"; do
            if [ -x "${candidate_root}/steamapps/common/${pinned}/proton" ]; then
                STEAM_ROOT="${candidate_root}"
                PROTON_DIR="${candidate_root}/steamapps/common/${pinned}"
                break 2
            fi
        done
    done <<< "$(read_pinned_runtimes)"
fi

if [ -z "${PROTON_DIR}" ]; then
    echo "error: none of the pinned Proton runtimes in '${PIN_FILE}' is installed:" >&2
    read_pinned_runtimes | sed 's/^/       - /' >&2
    echo "       searched: ${STEAM_ROOTS[*]}" >&2
    echo "       Install one of them, or set CNA_DIRECT2D_PROTON_DIR deliberately." >&2
    exit 1
fi

case "$(basename "${PROTON_DIR}")" in
    *Experimental*)
        if [ "${ALLOW_EXPERIMENTAL}" != "1" ]; then
            echo "error: '$(basename "${PROTON_DIR}")' is a moving runtime; Steam upgrades it underneath a" >&2
            echo "       passing run, so its result is not reproducible and must not be quoted as Direct2D" >&2
            echo "       evidence. Use a pinned runtime, or set" >&2
            echo "       CNA_DIRECT2D_PROTON_ALLOW_EXPERIMENTAL=1 for a one-off investigation." >&2
            exit 1
        fi
        echo "warning: running on the moving '$(basename "${PROTON_DIR}")' runtime; this result is NOT" >&2
        echo "         reproducible evidence for plans/plan_direct2d.md." >&2
        ;;
esac

if [ ! -x "${PROTON_DIR}/proton" ]; then
    echo "error: Proton launcher not found at '${PROTON_DIR}/proton'." >&2
    echo "Set CNA_DIRECT2D_PROTON_DIR or CNA_DIRECT2D_PROTON_STEAM_ROOT to the Steam install path." >&2
    exit 1
fi

# Publish the exact runtime identity. Without this a "Proton passed" line in plans/plan_direct2d.md names
# nothing testable; with it the artifact records the Proton build id, the bundled Wine build and the
# DXVK version that actually served the renderer's D3D11/DXGI presentation device.
IDENTITY_FILE="${CNA_DIRECT2D_PROTON_IDENTITY_FILE:-}"
emit_identity() {
    echo "ProtonDirectory=$(basename "${PROTON_DIR}")"
    echo "ProtonPath=${PROTON_DIR}"
    echo "ProtonPinFile=${PIN_FILE}"
    if [ -f "${PROTON_DIR}/version" ]; then
        echo "ProtonVersion=$(tr -d '\n' < "${PROTON_DIR}/version")"
    else
        echo "ProtonVersion=unknown (no version file)"
    fi
    if [ -f "${PROTON_DIR}/dist/version" ]; then
        echo "ProtonDistVersion=$(tr -d '\n' < "${PROTON_DIR}/dist/version")"
    elif [ -f "${PROTON_DIR}/files/version" ]; then
        echo "ProtonDistVersion=$(tr -d '\n' < "${PROTON_DIR}/files/version")"
    fi
    local dxvk_dll
    for dxvk_dll in "${PROTON_DIR}"/dist/lib64/wine/dxvk/d3d11.dll \
                    "${PROTON_DIR}"/files/lib64/wine/dxvk/d3d11.dll \
                    "${PROTON_DIR}"/dist/lib64/wine/x86_64-windows/d3d11.dll \
                    "${PROTON_DIR}"/files/lib64/wine/x86_64-windows/d3d11.dll; do
        if [ -f "${dxvk_dll}" ]; then
            echo "DxvkD3d11Path=${dxvk_dll}"
            if command -v strings >/dev/null 2>&1; then
                echo "DxvkVersion=$(strings -a "${dxvk_dll}" | grep -oE 'dxvk-v?[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n 1)"
            else
                echo "DxvkVersion=unknown (strings unavailable)"
            fi
            break
        fi
    done
}

if [ -n "${IDENTITY_FILE}" ]; then
    mkdir -p "$(dirname "${IDENTITY_FILE}")"
    emit_identity | tee "${IDENTITY_FILE}" >&2
else
    emit_identity >&2
fi

if [ "${CNA_DIRECT2D_PROTON_DRY_RUN:-0}" = "1" ]; then
    exit 0
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
