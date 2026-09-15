#!/bin/sh
# SPDX-License-Identifier: MS-PL
#
# plans/plan_x11.md X11-0153: an XNA game in exclusive fullscreen, end to end.
#
# Runs under tools/platform/x11_test_server.sh, whose private Xvfb is the only kind of server this
# may change the display mode of:
#
#   tools/platform/x11_test_server.sh --require-window-manager \
#       sh tools/platform/x11_exclusive_fullscreen_game.sh <cna_house3d_demo>
#
# The game asks the XNA way -- IsFullScreen and an 800x600 back buffer on its GraphicsDeviceManager
# -- and everything is checked against the server's own account (xrandr), never the game's:
#
#   1. the monitor switches to 800x600 while the game runs, and the game's window covers it;
#   2. a normal exit gives the desktop its mode back;
#   3. a game killed with SIGKILL -- no destructor, no atexit, nothing of it runs -- has the mode
#      given back all the same, by its mode guardian.

set -u

if [ $# -ne 1 ]; then
    echo "usage: $0 <cna_house3d_demo>" >&2
    exit 2
fi
GAME="$1"

if [ "${CNA_X11_PRIVATE_TEST_SERVER:-}" != "1" ]; then
    echo "SKIP: this changes the display mode; run it under tools/platform/x11_test_server.sh" >&2
    exit 77
fi
if ! command -v xrandr >/dev/null 2>&1 || ! command -v xprop >/dev/null 2>&1; then
    echo "SKIP: xrandr or xprop is not installed (x11-xserver-utils, x11-utils)" >&2
    exit 77
fi

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

# The lit output's geometry, WIDTHxHEIGHT, as the server reports it.
monitor() {
    xrandr 2>/dev/null | awk '/ connected/ {
        for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+x[0-9]+\+/) { split($i, g, "+"); print g[1]; exit }
    }'
}

# The game's window -- WM_CLASS "CNA" -- as WIDTHxHEIGHT+X+Y in root coordinates.
game_window() {
    xwininfo -root -tree 2>/dev/null | awk '/"CNA"\)/ {
        for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+x[0-9]+[+-]/) { size = $i; sub(/[+-].*/, "", size) }
        print size $NF; exit
    }'
}

await_game_window() {
    WANTED="$1"
    TENTHS="$2"
    while [ "$TENTHS" -gt 0 ]; do
        [ "$(game_window)" = "$WANTED" ] && return 0
        sleep 0.1
        TENTHS=$((TENTHS - 1))
    done
    return 1
}

await_monitor() {
    WANTED="$1"
    TENTHS="$2"
    while [ "$TENTHS" -gt 0 ]; do
        [ "$(monitor)" = "$WANTED" ] && return 0
        sleep 0.1
        TENTHS=$((TENTHS - 1))
    done
    return 1
}

WM_PID=""
GAME_PID=""
cleanup() {
    [ -n "$GAME_PID" ] && kill -9 "$GAME_PID" 2>/dev/null
    [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT INT TERM

# The window manager fullscreen needs. This script owns it, as the window-manager suite's fixture
# owns its own.
openbox >/dev/null 2>&1 &
WM_PID=$!
WAITED=0
until xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q "window id"; do
    WAITED=$((WAITED + 1))
    [ "$WAITED" -gt 100 ] && { echo "SKIP: openbox did not become the window manager" >&2; exit 77; }
    sleep 0.1
done

# Something to switch to: Xvfb's one output starts with one mode, the screen's own.
OUTPUT=$(xrandr 2>/dev/null | awk '/ connected/ { print $1; exit }')
[ -n "$OUTPUT" ] || fail "the server reports no connected output"
xrandr --newmode cna-800x600 38.25 800 832 912 1024 600 603 607 624 -hsync +vsync 2>/dev/null
xrandr --addmode "$OUTPUT" cna-800x600 || fail "could not give $OUTPUT an 800x600 mode"
DESKTOP=$(monitor)
[ "$DESKTOP" != "800x600" ] || fail "the desktop already runs at 800x600; nothing would be proven"

echo "desktop $DESKTOP; running the game in exclusive fullscreen at 800x600"
"$GAME" --fullscreen 800x600 --smoke 240 &
GAME_PID=$!
await_monitor 800x600 200 || fail "the game's IsFullScreen never switched the monitor to 800x600 (it is $(monitor))"
echo "  the monitor is 800x600 while the game runs"
# And the window covers it: a mode switch under a window the window manager never made fullscreen
# is a decorated window on a smaller desktop, not a fullscreen game.
await_game_window 800x600+0+0 100 || fail "the game's window does not cover the monitor; it is $(game_window)"
echo "  the game's window covers it"
wait "$GAME_PID"
STATUS=$?
GAME_PID=""
[ "$STATUS" -eq 0 ] || fail "the game exited with status $STATUS"
[ "$(monitor)" = "$DESKTOP" ] || fail "the game exited and left the monitor at $(monitor)"
echo "  a normal exit gave back $DESKTOP"

"$GAME" --fullscreen 800x600 --smoke 1000000 &
GAME_PID=$!
await_monitor 800x600 200 || fail "the second run never switched the monitor (it is $(monitor))"
kill -9 "$GAME_PID"
wait "$GAME_PID" 2>/dev/null
GAME_PID=""
await_monitor "$DESKTOP" 50 || fail "the game was killed and the monitor stayed at $(monitor)"
echo "  a SIGKILLed game's guardian gave back $DESKTOP"
exit 0
