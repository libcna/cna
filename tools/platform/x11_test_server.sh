#!/bin/sh
# SPDX-License-Identifier: MS-PL
#
# plans/plan_x11.md X11-0101/X11-0102: run a command against a private X server.
#
# A test that talks to X11 -- SDL3's x11 video driver, its clipboard and primary selection, the
# Xlib error-handler regression -- needs a real X connection, and a build machine usually has no
# display. This starts an isolated `Xvfb`, runs the command, and takes it down again -- so the
# tests neither depend on a developer's own session nor leave anything behind on it.
#
# Two deliberate choices:
#
#   * The display number is SEARCHED FOR, not hardcoded. A fixed `:99` collides with a parallel
#     ctest job, with a developer's own scratch server, and with CI runners that already use it --
#     and the collision looks like a flaky test rather than like what it is.
#   * A missing Xvfb EXITS 77, ctest's skip code, rather than failing. A machine with no virtual
#     X server has not broken anything; it simply cannot exercise this part of it, and the suite
#     records that rather than reporting red.
#
# Usage:
#   tools/platform/x11_test_server.sh <command> [args...]

set -u

if [ $# -eq 0 ]; then
    echo "usage: $0 <command> [args...]" >&2
    exit 2
fi

if ! command -v Xvfb >/dev/null 2>&1; then
    echo "SKIP: Xvfb is not installed; the X11 integration tests need a virtual X server" >&2
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
#
# -noreset: without it an X server regenerates itself every time its LAST client disconnects,
# and a connection arriving during that regeneration is reset (XOpenDisplay fails with
# ECONNRESET). A window test closes its connection in TearDown and the next test opens a new one
# moments later, so on a loaded machine alternate tests were skipping as "cannot reach the X
# server" -- which ctest reports as a pass (plans/plan_native_platform_validation.md NPV-0108).
Xvfb ":$DISPLAY_NUMBER" -screen 0 1280x1024x24 -nolisten tcp -noreset >/dev/null 2>&1 &
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
# Tells a test that this server is its own, started for this run and discarded after it -- so it
# may change server-wide state (the keymap, say) that it must never touch on a developer's desktop.
CNA_X11_PRIVATE_TEST_SERVER=1
export CNA_X11_PRIVATE_TEST_SERVER

# plans/plan_x11.md X11-0169: a platform asks the session bus for the desktop portal (SDL3's file
# dialogs do), and a file chooser opened there would open on the desktop of whoever runs the tests.
# The command gets no session bus at all.
DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent/cna-x11-test-no-session-bus"
export DBUS_SESSION_BUS_ADDRESS

"$@"
