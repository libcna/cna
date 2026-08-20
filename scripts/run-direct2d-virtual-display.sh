#!/usr/bin/env bash
# plans/plan_direct2d.md D2D-120: the one canonical virtual-display runner for Direct2D tests.
#
# Direct2D always presents to a real HWND, so its tests need a display even when they only read
# pixels back. Ad-hoc `xvfb-run -a ...` invocations scattered across docs and CI differ in screen
# geometry and depth, leave their X authority file behind, and silently inherit whatever DISPLAY a
# developer's session happened to export. This script is the single place that decides all three.
#
# Usage: scripts/run-direct2d-virtual-display.sh <command> [args...]
#
# Environment:
#   CNA_DIRECT2D_XVFB_SCREEN       screen geometry, default 1280x800x24 (WIDTHxHEIGHTxDEPTH)
#   CNA_DIRECT2D_USE_HOST_DISPLAY  set to 1 to run on the caller's existing DISPLAY instead
#
# The runner is reentrant: a nested invocation inside a display this script already created reuses
# it (CNA_DIRECT2D_VIRTUAL_DISPLAY) instead of stacking a second Xvfb.
set -uo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <command> [args...]" >&2
    exit 2
fi

if [ "${CNA_DIRECT2D_USE_HOST_DISPLAY:-0}" = "1" ]; then
    exec "$@"
fi

# Already inside a display this runner owns (e.g. ctest launched under it, and each registered test
# re-enters through run-wine-direct2d.sh): reuse it rather than nesting another server.
if [ "${CNA_DIRECT2D_VIRTUAL_DISPLAY:-0}" = "1" ] && [ -n "${DISPLAY:-}" ]; then
    exec "$@"
fi

if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "error: xvfb-run not found; install Xvfb or set CNA_DIRECT2D_USE_HOST_DISPLAY=1." >&2
    exit 1
fi

SCREEN="${CNA_DIRECT2D_XVFB_SCREEN:-1280x800x24}"
if ! echo "${SCREEN}" | grep -qE '^[1-9][0-9]*x[1-9][0-9]*x(8|15|16|24|30)$'; then
    echo "error: CNA_DIRECT2D_XVFB_SCREEN must be WIDTHxHEIGHTxDEPTH (e.g. 1280x800x24), got '${SCREEN}'." >&2
    exit 2
fi

# A private authority file per run, removed on every exit path, so parallel runs cannot collide and
# nothing is left in $HOME. mktemp -d also gives Xvfb's own scratch state a bounded lifetime.
RUN_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cna-direct2d-xvfb.XXXXXXXX")"
cleanup() {
    rm -rf "${RUN_DIR}"
}
trap cleanup EXIT INT TERM

export CNA_DIRECT2D_VIRTUAL_DISPLAY=1
# -a picks a free display number; -nolisten tcp keeps the server local to this machine. Some
# xvfb-run versions append -s to their own default server arguments and some replace them, so the
# flag is passed explicitly even though the default already carries it -- a duplicate is harmless,
# a silently dropped one would not be.
xvfb-run -a -f "${RUN_DIR}/Xauthority" -s "-screen 0 ${SCREEN} -nolisten tcp" "$@"
status=$?
exit "${status}"
