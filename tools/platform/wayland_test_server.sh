#!/bin/sh
# SPDX-License-Identifier: MS-PL
#
# plans/plan_wayland.md WAYLAND-0112/0113: run a command against a PRIVATE Wayland compositor.
#
# The Wayland backend's live suites need a real compositor, and the one a developer is sitting in
# front of is the one thing they must never touch: a test would open windows on the user's screen,
# take their clipboard, and -- on a locked session -- act on a desktop nobody is watching
# (plans/plan_wayland.md D-28). This starts a compositor of its own, in a runtime directory of its
# own, runs the command against it, and takes everything down again.
#
#   tools/platform/wayland_test_server.sh [--compositor weston|mutter] [--renderer pixman|gl]
#                                         [--scale N] [--layout XKB] <command> [args...]
#
#   weston  Weston's headless backend (default). No seat: windows, outputs, presentation, EGL with
#           --renderer gl, Vulkan. Found as `weston` on PATH, or the unpacked copy in ~/deps/weston.
#   mutter  gnome-shell --headless on a private session bus: GNOME's own compositor, strict in the
#           ways GNOME is, with a seat whose input comes from its org.gnome.Mutter.RemoteDesktop
#           API on that private bus -- never uinput, never the real desktop's input.
#
# The command sees:
#   WAYLAND_DISPLAY, CNA_WAYLAND_TEST_DISPLAY   the private compositor's socket name
#   XDG_RUNTIME_DIR                             the private runtime directory holding it
#   CNA_WAYLAND_TEST_COMPOSITOR                 weston or mutter
#   CNA_WAYLAND_TEST_SCALE                      the output scale asked for (weston)
#   CNA_WAYLAND_TEST_LAYOUT                     the keyboard layout asked for, if any
#   CNA_WAYLAND_TEST_BUS                        (mutter) the private session bus's address
#   DBUS_SESSION_BUS_ADDRESS                    a path that does not exist: no session bus
#
# A compositor that is not installed, or that does not come up, EXITS 77 -- ctest's skip code: a
# machine without one has not broken the backend, it just cannot exercise this part of it.

set -u

COMPOSITOR=weston
RENDERER=pixman
SCALE=1
LAYOUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --compositor) COMPOSITOR="$2"; shift 2 ;;
        --renderer) RENDERER="$2"; shift 2 ;;
        --scale) SCALE="$2"; shift 2 ;;
        --layout) LAYOUT="$2"; shift 2 ;;
        *) break ;;
    esac
done
if [ $# -eq 0 ]; then
    echo "usage: $0 [--compositor weston|mutter] [--renderer pixman|gl] [--scale N] [--layout XKB] <command> [args...]" >&2
    exit 2
fi

# A short private directory: a socket path is limited to 108 bytes.
PRIVATE=$(mktemp -d "${TMPDIR:-/tmp}/cna-wl-XXXXXX") || exit 77
chmod 700 "$PRIVATE"
mkdir -p "$PRIVATE/run" "$PRIVATE/config" "$PRIVATE/data" "$PRIVATE/cache" "$PRIVATE/state"
chmod 700 "$PRIVATE/run"

COMPOSITOR_PID=""
BUS_PID=""
cleanup() {
    for pid in $COMPOSITOR_PID $BUS_PID; do
        kill "$pid" 2>/dev/null
    done
    for pid in $COMPOSITOR_PID $BUS_PID; do
        # A compositor gets a few seconds to end its clients' connections cleanly, then no more.
        WAITED=0
        while kill -0 "$pid" 2>/dev/null && [ "$WAITED" -lt 50 ]; do
            sleep 0.1
            WAITED=$((WAITED + 1))
        done
        kill -9 "$pid" 2>/dev/null
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
            if [ -n "$LAYOUT" ]; then
                echo "[keyboard]"
                echo "keymap_layout=$LAYOUT"
            fi
        } > "$CONFIG"
        env -u WAYLAND_DISPLAY -u DISPLAY XDG_RUNTIME_DIR="$PRIVATE/run" \
            "$WESTON" --backend=headless --renderer="$RENDERER" --socket="$SOCKET" --config="$CONFIG" \
            --width=1920 --height=1080 --scale="$SCALE" --idle-time=0 >"$LOG" 2>&1 &
        COMPOSITOR_PID=$!
        ;;
    mutter)
        if ! command -v gnome-shell >/dev/null 2>&1 || ! command -v dbus-daemon >/dev/null 2>&1; then
            echo "SKIP: gnome-shell or dbus-daemon is not installed" >&2
            exit 77
        fi
        # A bus of its own with nothing to activate: nothing the compositor asks for can start a
        # portal, a keyring or a session manager -- or reach the desktop's.
        BUS_CONFIG="$PRIVATE/bus.conf"
        cat > "$BUS_CONFIG" <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:path=$PRIVATE/bus</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
EOF
        dbus-daemon --config-file="$BUS_CONFIG" --nofork --nopidfile >/dev/null 2>&1 &
        BUS_PID=$!
        WAITED=0
        while [ ! -S "$PRIVATE/bus" ] && [ "$WAITED" -lt 100 ]; do
            sleep 0.05
            WAITED=$((WAITED + 1))
        done
        if [ ! -S "$PRIVATE/bus" ]; then
            echo "SKIP: the private session bus did not come up" >&2
            exit 77
        fi
        BUS="unix:path=$PRIVATE/bus"
        # GNOME's settings from a keyfile in the private config directory, never dconf: the
        # desktop's settings are neither read nor written.
        SETTINGS="$PRIVATE/config/glib-2.0/settings"
        mkdir -p "$SETTINGS"
        {
            echo "[org/gnome/desktop/interface]"
            echo "enable-animations=false"
            # A virtual pointer that starts at, or is parked against, the top-left corner must
            # not open the Activities overview, which takes the keyboard from every window.
            echo "enable-hot-corners=false"
            echo "[org/gnome/desktop/session]"
            echo "idle-delay=uint32 0"
            # A fresh profile gets the "Welcome to GNOME" tour dialog, which is modal: no window
            # gets the keyboard, or a click, until it is dismissed.
            echo "[org/gnome/shell]"
            echo "welcome-dialog-last-shown-version='9999'"
            if [ -n "$LAYOUT" ]; then
                SOURCES=""
                OLDIFS="$IFS"
                IFS=","
                for layout in $LAYOUT; do
                    SOURCES="$SOURCES${SOURCES:+, }('xkb', '$layout')"
                done
                IFS="$OLDIFS"
                echo "[org/gnome/desktop/input-sources]"
                echo "sources=[$SOURCES]"
            fi
        } > "$SETTINGS/keyfile"
        env -u WAYLAND_DISPLAY -u DISPLAY -u XDG_SESSION_TYPE -u XDG_CURRENT_DESKTOP -u GNOME_SETUP_DISPLAY \
            XDG_RUNTIME_DIR="$PRIVATE/run" XDG_CONFIG_HOME="$PRIVATE/config" XDG_DATA_HOME="$PRIVATE/data" \
            XDG_CACHE_HOME="$PRIVATE/cache" XDG_STATE_HOME="$PRIVATE/state" GSETTINGS_BACKEND=keyfile \
            DBUS_SESSION_BUS_ADDRESS="$BUS" NO_AT_BRIDGE=1 GTK_A11Y=none \
            gnome-shell --headless --wayland --no-x11 --sm-disable --virtual-monitor 1920x1080 \
            --wayland-display "$SOCKET" >"$LOG" 2>&1 &
        COMPOSITOR_PID=$!
        CNA_WAYLAND_TEST_BUS="$BUS"
        export CNA_WAYLAND_TEST_BUS
        ;;
    *)
        echo "unknown compositor '$COMPOSITOR' (weston or mutter)" >&2
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
if [ "$COMPOSITOR" = mutter ]; then
    # The socket appears long before the shell is ready: its startup ends by showing the
    # Activities overview, which takes the keyboard from any window already shown. Ready is when
    # the shell says it has started (its own log line, the only signal a headless shell gives).
    WAITED=0
    while [ "$WAITED" -lt 300 ]; do
        if grep -q "GNOME Shell started" "$LOG" 2>/dev/null; then
            break
        fi
        if ! kill -0 "$COMPOSITOR_PID" 2>/dev/null; then
            break
        fi
        sleep 0.1
        WAITED=$((WAITED + 1))
    done
    if ! grep -q "GNOME Shell started" "$LOG" 2>/dev/null; then
        echo "SKIP: gnome-shell did not finish starting; its log:" >&2
        tail -20 "$LOG" >&2
        exit 77
    fi
    # The shell starts in the Activities overview, where a new window is a thumbnail that gets
    # neither the keyboard nor the pointer. Closed through the shell's own property, on the
    # private bus -- deterministic, where pressing Escape races the startup.
    DBUS_SESSION_BUS_ADDRESS="$BUS" gdbus call --session --dest org.gnome.Shell --object-path /org/gnome/Shell \
        --method org.freedesktop.DBus.Properties.Set org.gnome.Shell OverviewActive "<false>" >/dev/null 2>&1
    WAITED=0
    while [ "$WAITED" -lt 50 ]; do
        if DBUS_SESSION_BUS_ADDRESS="$BUS" gdbus call --session --dest org.gnome.Shell --object-path /org/gnome/Shell \
            --method org.freedesktop.DBus.Properties.Get org.gnome.Shell OverviewActive 2>/dev/null | grep -q false; then
            break
        fi
        sleep 0.1
        WAITED=$((WAITED + 1))
    done
fi

XDG_RUNTIME_DIR="$PRIVATE/run"
WAYLAND_DISPLAY="$SOCKET"
CNA_WAYLAND_TEST_DISPLAY="$SOCKET"
CNA_WAYLAND_TEST_COMPOSITOR="$COMPOSITOR"
CNA_WAYLAND_TEST_SCALE="$SCALE"
CNA_WAYLAND_TEST_LAYOUT="$LAYOUT"
DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent/cna-wayland-test-no-session-bus"
export XDG_RUNTIME_DIR WAYLAND_DISPLAY CNA_WAYLAND_TEST_DISPLAY CNA_WAYLAND_TEST_COMPOSITOR CNA_WAYLAND_TEST_SCALE
export CNA_WAYLAND_TEST_LAYOUT
export DBUS_SESSION_BUS_ADDRESS
unset DISPLAY

"$@"
STATUS=$?
if [ "$STATUS" -ne 0 ] && [ "$STATUS" -ne 77 ]; then
    echo "--- $COMPOSITOR log (last 30 lines) ---" >&2
    tail -30 "$LOG" >&2
fi
exit "$STATUS"
