#!/bin/sh
# SPDX-License-Identifier: MS-PL
#
# plans/plan_x11.md X11-0101/X11-0102: run a command against a private X server.
#
# The X11 backend's integration tests need a real X connection, and a build machine usually has
# no display. This starts an isolated `Xvfb`, optionally a window manager, runs the command, and
# takes both down again -- so the tests neither depend on a developer's own session nor leave
# anything behind on it.
#
# Two deliberate choices:
#
#   * The display number is SEARCHED FOR, not hardcoded. A fixed `:99` collides with a parallel
#     ctest job, with a developer's own scratch server, and with CI runners that already use it --
#     and the collision looks like a flaky test rather than like what it is.
#   * A missing Xvfb EXITS 77, ctest's skip code, rather than failing. A machine with no virtual
#     X server has not broken the backend; it simply cannot exercise this part of it, and the
#     suite records that rather than reporting red.
#
# Usage:
#   tools/platform/x11_test_server.sh [--require-window-manager] <command> [args...]
#
# `--require-window-manager` CHECKS for a window manager binary and skips without one; it does not
# start one. Starting it here would be the obvious thing and is wrong: the window-manager suite's
# own fixture starts and stops `openbox` per test, because a test that needs a window manager also
# needs to know when it became ready. Two instances raced -- the fixture's `openbox --replace`
# displaced the launcher's mid-run and a window left fullscreen stopped being noticed -- which is
# exactly the kind of failure that reads as flakiness. So the launcher owns the server and the
# fixture owns the window manager, with no overlap.

set -u

REQUIRE_WINDOW_MANAGER=0
if [ "${1:-}" = "--require-window-manager" ]; then
    REQUIRE_WINDOW_MANAGER=1
    shift
fi

if [ $# -eq 0 ]; then
    echo "usage: $0 [--require-window-manager] <command> [args...]" >&2
    exit 2
fi

if ! command -v Xvfb >/dev/null 2>&1; then
    echo "SKIP: Xvfb is not installed; the X11 integration tests need a virtual X server" >&2
    exit 77
fi

if [ "$REQUIRE_WINDOW_MANAGER" -eq 1 ] && ! command -v openbox >/dev/null 2>&1; then
    echo "SKIP: openbox is not installed; EWMH window-state transitions have no window manager" >&2
    exit 77
fi

DISPLAY_NUMBER=""
N=90
while [ "$N" -lt 130 ]; do
    if [ ! -e "/tmp/.X11-unix/X$N" ] && [ ! -e "/tmp/.X$N-lock" ]; then
        DISPLAY_NUMBER="$N"
        break
    fi
    N=$((N + 1))
done
if [ -z "$DISPLAY_NUMBER" ]; then
    echo "SKIP: no free X display number between :90 and :129" >&2
    exit 77
fi

# -nolisten tcp: this server is for one test run on this machine and must not be reachable from
# anywhere else.
Xvfb ":$DISPLAY_NUMBER" -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!

cleanup() {
    kill "$XVFB_PID" 2>/dev/null
    wait "$XVFB_PID" 2>/dev/null
    rm -f "/tmp/.X$DISPLAY_NUMBER-lock"
}
trap cleanup EXIT INT TERM

# Wait for the socket rather than sleeping a fixed amount: a loaded machine takes longer, and a
# fixed sleep is a race that only shows up there.
WAITED=0
while [ "$WAITED" -lt 200 ]; do
    if [ -e "/tmp/.X11-unix/X$DISPLAY_NUMBER" ]; then
        break
    fi
    sleep 0.05
    WAITED=$((WAITED + 1))
done
if [ ! -e "/tmp/.X11-unix/X$DISPLAY_NUMBER" ]; then
    echo "SKIP: Xvfb did not come up on :$DISPLAY_NUMBER" >&2
    exit 77
fi

DISPLAY=":$DISPLAY_NUMBER"
export DISPLAY

"$@"
