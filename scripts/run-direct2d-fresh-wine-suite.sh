#!/usr/bin/env bash
# plans/plan_direct2d.md D2D-121: run the whole Direct2D CTest label against a Wine prefix created from
# nothing, so a pass can never depend on a developer's accumulated ~/.wine state (installed
# runtimes, DLL overrides, registry tweaks, leftover Proton upgrades).
#
# Usage: scripts/run-direct2d-fresh-wine-suite.sh <build-dir>
#
# Environment:
#   CNA_DIRECT2D_FRESH_PREFIX_ROOT  parent directory for the throwaway prefix (default: mktemp -d)
#   CNA_DIRECT2D_KEEP_PREFIX=1      keep the prefix after the run for post-mortem inspection
#   CNA_DIRECT2D_CTEST_ARGS         extra ctest arguments (appended verbatim)
#
# The prefix receives exactly one declared dependency: `wineboot --init`, i.e. Wine's own default
# prefix contents. No winetricks package, no native DLL override and no DXVK install is performed
# -- Direct2D's supported contract is that Wine's built-in d2d1/d3d11/dxgi implementations are what
# gets exercised. If a future dependency becomes genuinely necessary, add it here explicitly so the
# requirement stays visible instead of hiding in a developer's prefix.
set -uo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <build-dir> [extra ctest args...]" >&2
    exit 2
fi

BUILD_DIR="$1"
shift

if [ ! -d "${BUILD_DIR}" ]; then
    echo "error: build directory '${BUILD_DIR}' does not exist; configure and build the Direct2D targets first." >&2
    exit 1
fi

for tool in wine wineboot ctest; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "error: required tool '${tool}' not found on PATH." >&2
        exit 1
    fi
done

if [ -n "${CNA_DIRECT2D_FRESH_PREFIX_ROOT:-}" ]; then
    mkdir -p "${CNA_DIRECT2D_FRESH_PREFIX_ROOT}"
    PREFIX="$(mktemp -d "${CNA_DIRECT2D_FRESH_PREFIX_ROOT}/cna-direct2d-prefix.XXXXXXXX")"
else
    PREFIX="$(mktemp -d "${TMPDIR:-/tmp}/cna-direct2d-prefix.XXXXXXXX")"
fi

case "${PREFIX}" in
    "${HOME}/.wine"|"${HOME}/.wine/"*)
        echo "error: refusing to treat the developer's ~/.wine as a throwaway prefix." >&2
        exit 1
        ;;
esac

cleanup() {
    if [ "${CNA_DIRECT2D_KEEP_PREFIX:-0}" = "1" ]; then
        echo "Direct2D fresh-prefix run kept its prefix at ${PREFIX}" >&2
        return
    fi
    rm -rf "${PREFIX}"
}
trap cleanup EXIT INT TERM

echo "Direct2D fresh-prefix run: initializing ${PREFIX}"
# wineboot writes the prefix into WINEPREFIX; the empty directory mktemp created is replaced by a
# complete default prefix. WINEDLLOVERRIDES stays unset on purpose (see the header).
if ! WINEPREFIX="${PREFIX}" WINEDEBUG="${WINEDEBUG:--all}" wineboot --init; then
    echo "error: wineboot --init failed for the throwaway prefix." >&2
    exit 1
fi
if command -v wineserver >/dev/null 2>&1; then
    WINEPREFIX="${PREFIX}" wineserver -w
fi

echo "Direct2D fresh-prefix run: executing the Direct2D CTest label"
# Every registered Direct2D test re-enters run-wine-direct2d.sh, which honours
# CNA_DIRECT2D_WINEPREFIX and the canonical virtual display. Running ctest itself under that
# display keeps one server for the whole label instead of one per test.
CNA_DIRECT2D_WINEPREFIX="${PREFIX}" \
    "$(dirname "$0")/run-direct2d-virtual-display.sh" \
    ctest --test-dir "${BUILD_DIR}" -L Direct2D --output-on-failure "$@" \
    ${CNA_DIRECT2D_CTEST_ARGS:-}
status=$?

if [ "${status}" -eq 0 ]; then
    echo "Direct2D fresh-prefix run: the Direct2D label passed on a prefix with no user state."
else
    echo "Direct2D fresh-prefix run: FAILED (ctest exit ${status})." >&2
fi
exit "${status}"
