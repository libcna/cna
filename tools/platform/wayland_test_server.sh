#!/bin/sh
# SPDX-License-Identifier: MS-PL
#
# plans/plan_wayland.md WAYLAND-0112/0113: run a command against a PRIVATE Wayland compositor.
#
# A test that talks to Wayland -- SDL3's wayland video driver, a GPU test through Xwayland
# (tools/platform/run_gpu_tests_private.sh) -- needs a real compositor, and the one a developer is
# sitting in front of is the one thing it must never touch: a test would open windows on the
# user's screen, take their clipboard, and -- on a locked session -- act on a desktop nobody is
# watching (plans/plan_wayland.md D-28). This starts a compositor of its own, in a runtime
# directory of its own, runs the command against it, and takes everything down again.
#
#   tools/platform/wayland_test_server.sh [--compositor weston] [--renderer pixman|gl] [--scale N]
#                                         <command> [args...]
#
#   weston  Weston's headless backend (the only one). No seat: windows, outputs, presentation, EGL
#           with --renderer gl, Vulkan. Found as `weston` on PATH, or the unpacked copy in
#           ~/deps/weston.
#
# The command sees:
#   WAYLAND_DISPLAY, CNA_WAYLAND_TEST_DISPLAY   the private compositor's socket name
#   XDG_RUNTIME_DIR                             the private runtime directory holding it
#   CNA_WAYLAND_TEST_COMPOSITOR                 weston
#   CNA_WAYLAND_TEST_SCALE                      the output scale asked for
#   DBUS_SESSION_BUS_ADDRESS                    a path that does not exist: no session bus
#
# A compositor that is not installed, or that does not come up, EXITS 77 -- ctest's skip code: a
# machine without one has not broken anything, it just cannot exercise this part of it.

set -u

COMPOSITOR=weston
RENDERER=pixman
SCALE=1
while [ $# -gt 0 ]; do
    case "$1" in
        --compositor) COMPOSITOR="$2"; shift 2 ;;
        --renderer) RENDERER="$2"; shift 2 ;;
        --scale) SCALE="$2"; shift 2 ;;
        *) break ;;
    esac
done
if [ $# -eq 0 ]; then
    echo "usage: $0 [--compositor weston] [--renderer pixman|gl] [--scale N] <command> [args...]" >&2
    exit 2
fi

# A short private directory: a socket path is limited to 108 bytes.
PRIVATE=$(mktemp -d "${TMPDIR:-/tmp}/cna-wl-XXXXXX") || exit 77
chmod 700 "$PRIVATE"
mkdir -p "$PRIVATE/run" "$PRIVATE/config" "$PRIVATE/data" "$PRIVATE/cache" "$PRIVATE/state"
chmod 700 "$PRIVATE/run"

COMPOSITOR_PID=""
cleanup() {
    # The compositor leads a process group of its own (setsid below), so what it started goes
    # with it: Weston's shell client above all. A compositor killed before it finished starting
    # leaves children that do not notice their parent went.
    for pid in $COMPOSITOR_PID; do
        kill -TERM "-$pid" 2>/dev/null
    done
    for pid in $COMPOSITOR_PID; do
        # A compositor gets a few seconds to end its clients' connections cleanly, then no more.
        WAITED=0
        while kill -0 "-$pid" 2>/dev/null && [ "$WAITED" -lt 50 ]; do
            sleep 0.1
            WAITED=$((WAITED + 1))
        done
        kill -KILL "-$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
    done
    # CNA_WAYLAND_TEST_KEEP_LOG=<file> keeps the compositor's log for a person debugging a run.
    if [ -n "${CNA_WAYLAND_TEST_KEEP_LOG:-}" ] && [ -f "$PRIVATE/compositor.log" ]; then
        cp "$PRIVATE/compositor.log" "$CNA_WAYLAND_TEST_KEEP_LOG"
    fi
    rm -rf "$PRIVATE"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

SOCKET="cna-$COMPOSITOR-$$"
LOG="$PRIVATE/compositor.log"

case "$COMPOSITOR" in
    weston)
        WESTON=$(command -v weston 2>/dev/null)
        if [ -z "$WESTON" ] && [ -x "$HOME/deps/weston/bin/weston" ]; then
            WESTON="$HOME/deps/weston/bin/weston"
        fi
        if [ -z "$WESTON" ]; then
            echo "SKIP: weston is not installed; the Wayland live suites need a headless compositor" >&2
            exit 77
        fi
        # An unpacked Weston (no package manager on the machine) keeps its helper clients beside
        # itself, not in /usr/libexec.
        SHELL_CLIENT=""
        case "$WESTON" in
            "$HOME"/deps/weston/*) SHELL_CLIENT="$HOME/deps/weston/usr/libexec/weston-desktop-shell" ;;
        esac
        CONFIG="$PRIVATE/weston.ini"
        {
            echo "[core]"
            echo "idle-time=0"
            echo "require-input=false"
            # No on-screen keyboard client: nothing but the command under test connects.
            echo "[input-method]"
            echo "path="
            echo "[shell]"
            if [ -n "$SHELL_CLIENT" ]; then
                echo "client=$SHELL_CLIENT"
            fi
            echo "panel-position=none"
            echo "background-color=0xff202020"
            echo "locking=false"
            echo "animation=none"
            echo "startup-animation=none"
            echo "close-animation=none"
            echo "focus-animation=none"
        } > "$CONFIG"
        env -u WAYLAND_DISPLAY -u DISPLAY XDG_RUNTIME_DIR="$PRIVATE/run" \
            setsid "$WESTON" --backend=headless --renderer="$RENDERER" --socket="$SOCKET" --config="$CONFIG" \
            --width=1920 --height=1080 --scale="$SCALE" --idle-time=0 >"$LOG" 2>&1 &
        COMPOSITOR_PID=$!
        ;;
    *)
        echo "unknown compositor '$COMPOSITOR' (weston is the only one)" >&2
        exit 2
        ;;
esac

# Wait for the socket rather than sleeping: a loaded machine takes longer, and a fixed sleep is a
# race that shows up only there.
WAITED=0
while [ ! -S "$PRIVATE/run/$SOCKET" ] && [ "$WAITED" -lt 300 ]; do
    if ! kill -0 "$COMPOSITOR_PID" 2>/dev/null; then
        break
    fi
    sleep 0.05
    WAITED=$((WAITED + 1))
done
if [ ! -S "$PRIVATE/run/$SOCKET" ]; then
    echo "SKIP: $COMPOSITOR did not come up; its log:" >&2
    tail -20 "$LOG" >&2
    exit 77
fi
XDG_RUNTIME_DIR="$PRIVATE/run"
WAYLAND_DISPLAY="$SOCKET"
CNA_WAYLAND_TEST_DISPLAY="$SOCKET"
CNA_WAYLAND_TEST_COMPOSITOR="$COMPOSITOR"
CNA_WAYLAND_TEST_SCALE="$SCALE"
DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent/cna-wayland-test-no-session-bus"
export XDG_RUNTIME_DIR WAYLAND_DISPLAY CNA_WAYLAND_TEST_DISPLAY CNA_WAYLAND_TEST_COMPOSITOR CNA_WAYLAND_TEST_SCALE
export DBUS_SESSION_BUS_ADDRESS
unset DISPLAY

"$@"
STATUS=$?
if [ "$STATUS" -ne 0 ] && [ "$STATUS" -ne 77 ]; then
    echo "--- $COMPOSITOR log (last 30 lines) ---" >&2
    tail -30 "$LOG" >&2
fi
exit "$STATUS"
