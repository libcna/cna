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
#   tools/platform/x11_test_server.sh [--require-window-manager] [--with-ibus] <command> [args...]
#
# `--with-ibus` also starts a private ibus input method with its XIM server on the private display
# (plans/plan_x11.md X11-0152): its own socket, its own config and cache directories, the plain
# US-English engine, nothing shared with a developer's own ibus -- whose address is keyed by the
# display, so it is never reached. The command sees XMODIFIERS=@im=ibus and IBUS_ADDRESS, and it
# skips (77) where ibus or its XIM server is not installed.
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
WITH_IBUS=0
while [ $# -gt 0 ]; do
    case "$1" in
        --require-window-manager) REQUIRE_WINDOW_MANAGER=1; shift ;;
        --with-ibus) WITH_IBUS=1; shift ;;
        *) break ;;
    esac
done

if [ $# -eq 0 ]; then
    echo "usage: $0 [--require-window-manager] [--with-ibus] <command> [args...]" >&2
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

if [ "$WITH_IBUS" -eq 1 ]; then
    IBUS_XIM_SERVER=""
    for candidate in /usr/libexec/ibus-x11 /usr/lib/ibus/ibus-x11 /usr/lib64/ibus/ibus-x11; do
        if [ -x "$candidate" ]; then
            IBUS_XIM_SERVER="$candidate"
            break
        fi
    done
    if ! command -v ibus-daemon >/dev/null 2>&1 || ! command -v ibus >/dev/null 2>&1 \
        || ! command -v dbus-run-session >/dev/null 2>&1 || [ -z "$IBUS_XIM_SERVER" ]; then
        echo "SKIP: ibus, its XIM server or dbus-run-session is not installed" >&2
        exit 77
    fi
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
# ECONNRESET). Every X11Live test closes its connection in TearDown and the next test opens a new
# one moments later, so on a loaded machine alternate tests were skipping as "cannot reach the X
# server" -- which ctest reports as a pass (plans/plan_native_platform_validation.md NPV-0108).
Xvfb ":$DISPLAY_NUMBER" -screen 0 1280x1024x24 -nolisten tcp -noreset >/dev/null 2>&1 &
XVFB_PID=$!

IBUS_DIRECTORY=""
cleanup() {
    if [ -n "$IBUS_DIRECTORY" ]; then
        # Only the daemon listening on this run's own socket: its XIM server and its bus follow it.
        pkill -f "address unix:path=$IBUS_DIRECTORY/" 2>/dev/null
        rm -rf "$IBUS_DIRECTORY"
    fi
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

if [ "$WITH_IBUS" -eq 1 ]; then
    # A short path: a Unix socket's path is limited to 108 bytes, and ibus reports a longer one as
    # "address already in use".
    IBUS_DIRECTORY="${XDG_RUNTIME_DIR:-/tmp}/cna-ibus-$$"
    mkdir -p "$IBUS_DIRECTORY/config" "$IBUS_DIRECTORY/cache"
    IBUS_ADDRESS="unix:path=$IBUS_DIRECTORY/ibus.sock"
    export IBUS_ADDRESS
    XDG_CONFIG_HOME="$IBUS_DIRECTORY/config" XDG_CACHE_HOME="$IBUS_DIRECTORY/cache" \
        dbus-run-session -- ibus-daemon --xim --single --panel disable --emoji-extension disable \
        --config disable --cache none --address "$IBUS_ADDRESS" >/dev/null 2>&1 &
    WAITED=0
    while [ "$WAITED" -lt 100 ]; do
        if xprop -root XIM_SERVERS 2>/dev/null | grep -q "@server=ibus"; then
            break
        fi
        sleep 0.1
        WAITED=$((WAITED + 1))
    done
    if ! xprop -root XIM_SERVERS 2>/dev/null | grep -q "@server=ibus"; then
        echo "SKIP: the private ibus did not register its XIM server on :$DISPLAY_NUMBER" >&2
        exit 77
    fi
    XDG_CONFIG_HOME="$IBUS_DIRECTORY/config" ibus engine xkb:us::eng >/dev/null 2>&1
    XMODIFIERS="@im=ibus"
    export XMODIFIERS
    CNA_X11_TEST_IBUS=1
    export CNA_X11_TEST_IBUS
fi

"$@"
