#!/usr/bin/env bash
# RTR-P14-9: run a 32-bit Windows GLIDE build under Wine with an explicitly selected
# caller-supplied Glide 3.x runtime. OpenGlide is the validated Wine route; dgVoodoo2 targets
# Windows and its 2.87.3 Glide DLL crashes in ResolveSubresource under both WineD3D and DXVK.
#
# Usage:
#   CNA_GLIDE3X_DLL=/absolute/path/to/glide3x.dll \
#     scripts/run-wine-glide.sh <path-to.exe> [args...]
#
# Set CNA_GLIDE_WINEPREFIX to select a prefix; the default is ~/.wine. The wrapper asserts that
# CNA loaded the requested runtime and selected the GLIDE renderer, so a passing process cannot be
# mistaken for a different backend or a failed DLL lookup.
set -uo pipefail

launchDirectory="$(pwd -P)"

if [ "$#" -lt 1 ]; then
    echo "usage: CNA_GLIDE3X_DLL=/path/to/glide3x.dll $0 <path-to.exe> [args...]" >&2
    exit 2
fi

application="$1"
shift
if [[ ! "$application" =~ ^[A-Za-z]:[\\/].* ]]; then
    if [ ! -f "$application" ]; then
        echo "error: Windows executable '$application' does not exist." >&2
        exit 2
    fi
    application="$(readlink -f -- "$application")"
    applicationDirectory="$(dirname -- "$application")"
    applicationName="$(basename -- "$application")"
    cd "$applicationDirectory" || exit 1
    application="./$applicationName"
fi

runtime="${CNA_GLIDE3X_DLL:-}"
if [ -z "$runtime" ]; then
    echo "error: CNA_GLIDE3X_DLL must name the caller-supplied 32-bit Glide 3.x runtime." >&2
    exit 2
fi

if [[ "$runtime" =~ ^[A-Za-z]:[\\/].* ]]; then
    runtimeWine="${runtime//\//\\}"
else
    if [[ "$runtime" != /* ]]; then
        runtime="$launchDirectory/$runtime"
    fi
    if [ ! -f "$runtime" ]; then
        echo "error: Glide runtime '$runtime' does not exist." >&2
        exit 2
    fi
    runtime="$(readlink -f -- "$runtime")"
    runtimeWine="Z:${runtime//\//\\}"
fi

export WINEPREFIX="${CNA_GLIDE_WINEPREFIX:-$HOME/.wine}"
export WINEDEBUG="${WINEDEBUG:--all}"
export CNA_GLIDE_DIAGNOSTICS="${CNA_GLIDE_DIAGNOSTICS:-1}"
export CNA_GLIDE3X_DLL="$runtimeWine"

if [ ! -f "${WINEPREFIX}/system.reg" ]; then
    echo "error: WINEPREFIX '${WINEPREFIX}' is not an initialized Wine prefix." >&2
    echo "Set it up first: WINEPREFIX=${WINEPREFIX} wineboot --init" >&2
    exit 1
fi

logFile="$(mktemp "${TMPDIR:-/tmp}/cna-glide-wine-log.XXXXXX")"
trap 'rm -f "$logFile"' EXIT

wine "$application" "$@" 2>&1 | tee "$logFile"
wineExit="${PIPESTATUS[0]}"

if [ "$wineExit" -ne 0 ]; then
    exit "$wineExit"
fi

if ! grep -Fq "[CNA GLIDE] runtime=${runtimeWine}" "$logFile"; then
    echo "error: GLIDE runtime-identity gate failed; CNA did not report '${runtimeWine}'." >&2
    exit 3
fi

if ! grep -Fq "[INFO][RENDER] CNA: graphics renderer: GLIDE" "$logFile"; then
    echo "error: GLIDE renderer-identity gate failed." >&2
    exit 3
fi

exit 0
