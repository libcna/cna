// SPDX-License-Identifier: MS-PL
#pragma once

#include "X11Headers.hpp"

#include <string>
#include <vector>

namespace CNA::Platform::X11 {

    /**
     * @brief Every atom this backend interns, cached once per connection.
     *
     * Atom interning is a server round trip. Doing it per request would put a synchronous
     * round trip inside the event pump and the window-state setters, which is precisely the
     * per-frame platform cost the performance contract forbids. They are interned once when the
     * connection opens instead.
     */
    struct X11Atoms
    {
        /** @brief `WM_PROTOCOLS`: the ICCCM protocol-participation property. */
        Atom wmProtocols = kNone;
        /** @brief `WM_DELETE_WINDOW`: the close request the application must answer. */
        Atom wmDeleteWindow = kNone;
        /** @brief `WM_STATE`: carries the ICCCM iconic/normal/withdrawn state. */
        Atom wmState = kNone;
        /** @brief `WM_NAME`: the legacy Latin-1 title property. */
        Atom wmName = kNone;
        /** @brief `WM_CHANGE_STATE`: the client message that asks for iconification. */
        Atom wmChangeState = kNone;
        /** @brief `_NET_WM_NAME`: the EWMH UTF-8 title property. */
        Atom netWmName = kNone;
        /** @brief `_NET_WM_STATE`: the EWMH window-state property. */
        Atom netWmState = kNone;
        /** @brief `_NET_WM_STATE_FULLSCREEN`. */
        Atom netWmStateFullscreen = kNone;
        /** @brief `_NET_WM_STATE_MAXIMIZED_VERT`. */
        Atom netWmStateMaximizedVert = kNone;
        /** @brief `_NET_WM_STATE_MAXIMIZED_HORZ`. */
        Atom netWmStateMaximizedHorz = kNone;
        /** @brief `_NET_WM_STATE_HIDDEN`. */
        Atom netWmStateHidden = kNone;
        /** @brief `_NET_ACTIVE_WINDOW`: the EWMH focus request. */
        Atom netActiveWindow = kNone;
        /** @brief `_NET_SUPPORTED`: what the running window manager implements. */
        Atom netSupported = kNone;
        /** @brief `_NET_SUPPORTING_WM_CHECK`: proves an EWMH window manager is actually running. */
        Atom netSupportingWmCheck = kNone;
        /** @brief `_NET_WM_PID`: the owning process, for the window manager's task list. */
        Atom netWmPid = kNone;
        /** @brief `_MOTIF_WM_HINTS`: the only broadly honoured way to remove decorations. */
        Atom motifWmHints = kNone;
        /** @brief `UTF8_STRING`: the type of every UTF-8 text property and selection target. */
        Atom utf8String = kNone;
        /** @brief `CLIPBOARD`: the selection a copy/paste actually uses. */
        Atom clipboard = kNone;
        /** @brief `PRIMARY`, interned for completeness of the selection vocabulary. */
        Atom primary = kNone;
        /** @brief `TARGETS`: the selection meta-target listing what a selection owner can convert to. */
        Atom targets = kNone;
        /** @brief `TEXT`: the polymorphic ICCCM text target. */
        Atom text = kNone;
        /** @brief `TIMESTAMP`: the selection target reporting when ownership was taken. */
        Atom timestamp = kNone;
        /** @brief `INCR`: marks an incremental (chunked) selection transfer. */
        Atom incr = kNone;
        /** @brief `CNA_SELECTION`: this backend's own property for receiving selection data. */
        Atom cnaSelection = kNone;
        /** @brief `XdndAware`, interned so a future drag-and-drop task has the vocabulary. */
        Atom xdndAware = kNone;
    };

    /**
     * @brief Owns one `Display*` connection and everything cached per connection.
     *
     * ### One connection per platform instance
     *
     * Not per process. The conformance suite constructs two platform instances in one process,
     * and a shared connection would let one instance's `XCloseDisplay` invalidate the other's
     * windows. The connection opens on the first `Video` acquisition and closes on the last
     * release, matching the contract's refcounted subsystem rules.
     *
     * ### What "cached per connection" means
     *
     * Atoms, the EWMH support list, the XKB availability flag and the display-scale reading are
     * all properties of a connection rather than of a window, and all of them cost a server round
     * trip to obtain. They are read once here so no per-frame path has to.
     */
    class X11Connection
    {
    public:
        /**
         * @brief Opens a connection to the X server named by `$DISPLAY`.
         *
         * @throws PlatformException If no server could be reached, with `$DISPLAY` in the message.
         */
        X11Connection();

        /** @brief Closes the connection and restores the process error handler when last. */
        ~X11Connection();

        X11Connection(const X11Connection&) = delete;
        X11Connection& operator=(const X11Connection&) = delete;

        /** @brief Gets the raw connection. @return The `Display*`; never null while alive. */
        [[nodiscard]] Display* GetDisplay() const { return display_; }

        /** @brief Gets the default screen number. @return The screen index. */
        [[nodiscard]] int GetScreen() const { return screen_; }

        /** @brief Gets the root window of the default screen. @return The root window XID. */
        [[nodiscard]] ::Window GetRoot() const { return root_; }

        /** @brief Gets the interned atoms. @return The atom cache. */
        [[nodiscard]] const X11Atoms& GetAtoms() const { return atoms_; }

        /**
         * @brief Gets the display name this connection was opened with.
         *
         * @return The `$DISPLAY` value, or the server's own idea of it.
         */
        [[nodiscard]] const std::string& GetDisplayName() const { return displayName_; }

        /**
         * @brief Reports whether the running window manager advertises a hint.
         *
         * EWMH is a convention, not a requirement, and a window manager may implement any subset
         * of it — or there may be no window manager at all, which is the normal state of a bare
         * `Xvfb`. Every EWMH request in this backend asks this first and degrades rather than
         * sending a message nothing will act on.
         *
         * @param hint The atom to look for in `_NET_SUPPORTED`.
         * @return True when the window manager listed it.
         */
        [[nodiscard]] bool SupportsEwmhHint(Atom hint) const;

        /**
         * @brief Reports whether an EWMH-compliant window manager is running at all.
         *
         * @return True when `_NET_SUPPORTING_WM_CHECK` resolves to a live window.
         */
        [[nodiscard]] bool HasWindowManager() const { return hasWindowManager_; }

        /**
         * @brief Re-reads `_NET_SUPPORTED` and the window-manager check.
         *
         * A window manager can be started, replaced or stopped while an application runs. This is
         * called when the root window's property set changes, never per frame.
         */
        void RefreshWindowManagerState();

        /**
         * @brief Gets the display scale this connection reports.
         *
         * See plans/plan_x11.md design decision 10 for the policy: `Xft.dpi` from the resource
         * database divided by 96, clamped to a sane range, and exactly 1.0 when the session does
         * not state one. X11 has no authoritative scale, and a value derived from a monitor's
         * claimed physical size is frequently fiction.
         *
         * @return The scale, where 1.0 means one logical unit per physical pixel.
         */
        [[nodiscard]] float GetDisplayScale() const { return displayScale_; }

        /** @brief Gets whether the XKB extension is usable on this connection. */
        [[nodiscard]] bool HasXkb() const { return hasXkb_; }

        /**
         * @brief Gets whether the server granted detectable auto-repeat.
         *
         * When true, a held key produces `KeyPress` events with no intervening `KeyRelease`, and
         * the backend can report `KeyEvent::repeat` without guessing. When false it falls back to
         * coalescing the release/press pair the server sends instead.
         */
        [[nodiscard]] bool HasDetectableAutoRepeat() const { return detectableAutoRepeat_; }

        /**
         * @brief Gets the XKB event base, for recognising XKB events in the pump.
         *
         * @return The first XKB event type, or -1 when XKB is unavailable.
         */
        [[nodiscard]] int GetXkbEventBase() const { return xkbEventBase_; }

        /**
         * @brief Gets the XInput2 opcode, for recognising XI2 generic events.
         *
         * @return The major opcode, or -1 when XInput2 is unavailable.
         */
        [[nodiscard]] int GetXInput2Opcode() const { return xi2Opcode_; }

        /**
         * @brief Gets the XRandR event base, for recognising screen-change notifications.
         *
         * @return The first RandR event type, or -1 when RandR is unavailable.
         */
        [[nodiscard]] int GetRandrEventBase() const { return randrEventBase_; }

        /** @brief Gets whether XRandR 1.2 or newer is usable on this connection. */
        [[nodiscard]] bool HasRandr() const { return randrEventBase_ >= 0; }

        /**
         * @brief Reads a whole window property.
         *
         * Handles the multi-request loop `XGetWindowProperty` requires for properties longer than
         * the requested length, which is the part that is easy to get wrong and silently
         * truncating.
         *
         * @param window The window to read from.
         * @param property The property atom.
         * @param type The expected type, or `AnyPropertyType`.
         * @param format Receives the property's format (8, 16 or 32); untouched on failure.
         * @param data Receives the raw bytes.
         * @return True when the property existed and was read.
         */
        [[nodiscard]] bool ReadProperty(::Window window, Atom property, Atom type, int& format,
                                        std::vector<unsigned char>& data) const;

        /**
         * @brief Sends an EWMH client message to the root window.
         *
         * @param window The window the message is about.
         * @param type The message type atom.
         * @param data0 First data word.
         * @param data1 Second data word.
         * @param data2 Third data word.
         * @param data3 Fourth data word.
         * @return True when the message was sent.
         */
        bool SendRootClientMessage(::Window window, Atom type, long data0, long data1 = 0,
                                   long data2 = 0, long data3 = 0) const;

    private:
        void InternAtoms();
        void DetectExtensions();
        void ReadDisplayScale();

        Display* display_ = nullptr;
        int screen_ = 0;
        ::Window root_ = kNone;
        std::string displayName_;
        X11Atoms atoms_;
        std::vector<Atom> supportedHints_;
        bool hasWindowManager_ = false;
        bool hasXkb_ = false;
        bool detectableAutoRepeat_ = false;
        int xkbEventBase_ = -1;
        int xi2Opcode_ = -1;
        int randrEventBase_ = -1;
        float displayScale_ = 1.0f;
    };

} // namespace CNA::Platform::X11
