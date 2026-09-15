// SPDX-License-Identifier: MS-PL

#include "X11Window.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "X11Display.hpp"
#include "X11Error.hpp"
#include "X11ModeSwitch.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <thread>
#include <vector>

namespace CNA::Platform::X11 {

    namespace {

        /// The `_MOTIF_WM_HINTS` property layout. Motif's own header is not a dependency any
        /// modern system carries, and the property is five `long`s whose meaning has not changed
        /// since 1989 -- so it is declared here rather than pulling in Motif for five words.
        struct MotifWmHints
        {
            unsigned long flags;
            unsigned long functions;
            unsigned long decorations;
            long inputMode;
            unsigned long status;
        };

        constexpr unsigned long kMotifHintsDecorations = 1uL << 1;

        constexpr long kNetWmStateRemove = 0;
        constexpr long kNetWmStateAdd = 1;

        /// How long after a display-mode change a focus loss is held before it is believed.
        /// Changing a monitor's mode makes some window managers move focus about while they lay
        /// the screen out again; SDL holds focus changes for the same 400 ms after a mode switch.
        constexpr std::chrono::milliseconds kModeChangeFocusGrace{400};

    } // namespace

    X11Window::X11Window(X11Connection& connection, const ::Window window, const WindowId id,
                         const Colormap colormap, Visual* visual, const int depth,
                         const bool ownsWindow)
        : connection_(connection), window_(window), id_(id), colormap_(colormap), visual_(visual),
          depth_(depth), ownsWindow_(ownsWindow)
    {
        XWindowAttributes attributes{};
        if (XGetWindowAttributes(connection_.GetDisplay(), window_, &attributes) != 0)
        {
            cachedWidth_ = attributes.width;
            cachedHeight_ = attributes.height;
            mapped_ = attributes.map_state != IsUnmapped;
            // An adopted window that is already on screen was shown by whoever created it.
            shown_ = mapped_;
            if (visual_ == nullptr)
            {
                visual_ = attributes.visual;
                depth_ = attributes.depth;
            }
        }
    }

    X11Window::~X11Window()
    {
        // The platform first, while this window is still whole. Its registry and its services
        // hold raw pointers to this object; left in place, the next PollEvents walks freed memory
        // and a later focus change unsets the input context of a window that no longer exists
        // (plans/plan_native_platform_validation.md NPV-0101).
        if (host_ != nullptr)
        {
            X11WindowHost* host = host_;
            host_ = nullptr;
            host->OnWindowDestroyed(*this);
        }

        // The display mode first, while the window it was taken for still exists: a game that
        // destroys its window without leaving fullscreen still gives the monitor back.
        LeaveExclusive();

        // Order matters and is not interchangeable: an XIC holds a reference to the window inside
        // the input method, and destroying the window first leaves the IM with a dangling client
        // window that it will use on the next XFilterEvent.
        if (inputContext_ != nullptr)
        {
            XDestroyIC(inputContext_);
            inputContext_ = nullptr;
        }
        inputContextData_.reset();
        if (ownsWindow_ && window_ != kNone)
        {
            XDestroyWindow(connection_.GetDisplay(), window_);
        }
        // The colormap is freed after the window, because a colormap still installed in a live
        // window is in use. An adopted window's colormap belongs to whoever created it, which is
        // why colormap_ is None for those.
        if (colormap_ != kNone)
        {
            XFreeColormap(connection_.GetDisplay(), colormap_);
            colormap_ = kNone;
        }
        window_ = kNone;
    }

    std::uintptr_t X11Window::GetWindowHandle() const
    {
        return static_cast<std::uintptr_t>(window_);
    }

    NativeWindowHandle X11Window::GetNativeHandle() const
    {
        NativeWindowHandle handle;
        handle.system = NativeWindowSystem::X11;
        handle.display = connection_.GetDisplay();
        // The XID goes in windowId, never in `window`. See NativeWindowHandle.hpp: an XID is a
        // server resource identifier, not an address, and carrying it in a pointer field makes a
        // null check meaningless because XID 0 (None) is a legitimate-looking null pointer.
        handle.windowId = static_cast<std::uint64_t>(window_);
        return handle;
    }

    std::string X11Window::GetTitle() const
    {
        const X11Atoms& atoms = connection_.GetAtoms();
        std::vector<unsigned char> data;
        int format = 0;
        if (connection_.ReadProperty(window_, atoms.netWmName, atoms.utf8String, format, data) &&
            format == 8)
        {
            return std::string(reinterpret_cast<const char*>(data.data()), data.size());
        }
        // Falling back to WM_NAME rather than returning empty: a window adopted from another
        // toolkit may only have set the legacy property.
        if (connection_.ReadProperty(window_, atoms.wmName, AnyPropertyType, format, data) &&
            format == 8)
        {
            return std::string(reinterpret_cast<const char*>(data.data()), data.size());
        }
        return {};
    }

    void X11Window::SetTitle(const std::string& title)
    {
        Display* display = connection_.GetDisplay();
        const X11Atoms& atoms = connection_.GetAtoms();
        const auto* bytes = reinterpret_cast<const unsigned char*>(title.c_str());

        // Both properties are set. _NET_WM_NAME is the UTF-8 one every modern window manager
        // reads; WM_NAME is what a pager, a window list or an older tool still reads, and a
        // window that sets only the first shows up with no name in those.
        XChangeProperty(display, window_, atoms.netWmName, atoms.utf8String, 8, PropModeReplace,
                        bytes, static_cast<int>(title.size()));
        XChangeProperty(display, window_, atoms.wmName, XA_STRING, 8, PropModeReplace, bytes,
                        static_cast<int>(title.size()));
        XFlush(display);
    }

    WindowBounds X11Window::GetClientBounds() const
    {
        WindowBounds bounds;
        Display* display = connection_.GetDisplay();

        XWindowAttributes attributes{};
        if (XGetWindowAttributes(display, window_, &attributes) == 0)
        {
            return {cachedX_, cachedY_, cachedWidth_, cachedHeight_};
        }
        bounds.width = attributes.width;
        bounds.height = attributes.height;

        // A reparenting window manager makes the window a child of a decoration frame, so
        // attributes.x/y are relative to that frame rather than to the root. Translating a (0,0)
        // client point to root space is the only way to get the position a caller means by
        // "where is my window", and it is correct with or without a window manager.
        int rootX = 0;
        int rootY = 0;
        ::Window child = kNone;
        if (XTranslateCoordinates(display, window_, connection_.GetRoot(), 0, 0, &rootX, &rootY,
                                  &child) != 0)
        {
            bounds.x = rootX;
            bounds.y = rootY;
        }
        else
        {
            bounds.x = attributes.x;
            bounds.y = attributes.y;
        }
        return bounds;
    }

    WindowSize X11Window::GetPixelSize() const
    {
        // X11 has no separate logical and physical window geometry: a window's size is always in
        // device pixels, and a scaled session scales fonts and toolkits rather than the server's
        // coordinate space. So the drawable size IS the client size here, and reporting anything
        // else would make a renderer size its swapchain wrongly.
        //
        // Deliberately NOT delegating to GetClientBounds(): that one also translates the origin to
        // root coordinates, which is a second round trip for a position this caller does not
        // want. A surface presenter reads the target size once per frame, so halving its server
        // traffic is worth four lines.
        XWindowAttributes attributes{};
        if (XGetWindowAttributes(connection_.GetDisplay(), window_, &attributes) == 0)
        {
            return {cachedWidth_, cachedHeight_};
        }
        return {attributes.width, attributes.height};
    }

    void X11Window::SetSize(const int width, const int height)
    {
        if (width <= 0 || height <= 0)
        {
            throw PlatformException("X11Window::SetSize",
                                    "a window size must be positive; X11 rejects a zero extent");
        }
        if (exclusive_)
        {
            // SDL's rule, and what XNA's GraphicsDevice relies on after IsFullScreen: in exclusive
            // fullscreen a new size asks for a different DISPLAY MODE, not a different window --
            // the window manager keeps a fullscreen window the size of its monitor.
            EnterExclusive(width, height);
            if (exclusive_)
            {
                return;
            }
            // No mode for the new size. The window stays fullscreen on the desktop's own mode --
            // borderless, which GetFullscreenMode now reports -- and takes the size request the
            // way a borderless fullscreen window does, below.
        }
        Display* display = connection_.GetDisplay();

        // The size hints have to move with the request. A non-resizable window pins min == max ==
        // the current size, so resizing one without updating the hints first produces a window
        // the window manager immediately snaps back.
        if (!resizable_)
        {
            minimumWidth_ = maximumWidth_ = width;
            minimumHeight_ = maximumHeight_ = height;
            ApplyNormalHints();
        }
        XResizeWindow(display, window_, static_cast<unsigned int>(width),
                      static_cast<unsigned int>(height));
        XFlush(display);
        // Recorded so Sync() knows what it is waiting for. See Sync() for why an XSync alone is
        // not enough under a window manager.
        pendingWidth_ = width;
        pendingHeight_ = height;
    }

    float X11Window::GetDisplayScale() const
    {
        return connection_.GetDisplayScale();
    }

    void X11Window::SetResizable(const bool resizable)
    {
        if (resizable_ == resizable)
        {
            return;
        }
        resizable_ = resizable;
        if (!resizable_)
        {
            const WindowBounds bounds = GetClientBounds();
            minimumWidth_ = maximumWidth_ = bounds.width;
            minimumHeight_ = maximumHeight_ = bounds.height;
        }
        else
        {
            // Restoring "unconstrained" rather than the previous explicit constraints: the
            // contract's minimum/maximum are creation-time parameters, and a window that was made
            // non-resizable and then resizable again has no recorded intent beyond "let the user
            // resize it".
            minimumWidth_ = minimumHeight_ = 0;
            maximumWidth_ = maximumHeight_ = 0;
        }
        ApplyNormalHints();
        XFlush(connection_.GetDisplay());
    }

    void X11Window::SetBorderless(const bool borderless)
    {
        if (borderless_ == borderless)
        {
            return;
        }
        borderless_ = borderless;
        ApplyMotifDecorations(!borderless);
        XFlush(connection_.GetDisplay());
    }

    void X11Window::SetFullscreenMode(const WindowFullscreenMode mode)
    {
        const X11Atoms& atoms = connection_.GetAtoms();

        // plans/plan_x11.md design decision 11. Both kinds of fullscreen are the window manager's
        // to perform -- exclusive is borderless plus a display mode of its own -- so both refuse
        // when the window manager does not advertise _NET_WM_STATE_FULLSCREEN, rather than
        // reporting a state nothing will put the window in.
        if (mode != WindowFullscreenMode::Windowed &&
            !connection_.SupportsEwmhHint(atoms.netWmStateFullscreen))
        {
            throw PlatformNotSupportedException(
                PlatformCapability::BorderlessFullscreen,
                connection_.HasWindowManager()
                    ? "X11 (this window manager does not advertise _NET_WM_STATE_FULLSCREEN)"
                    : "X11 (no EWMH window manager is running on this display)");
        }

        const bool wasFullscreen = HasNetWmState(atoms.netWmStateFullscreen);
        if (mode == WindowFullscreenMode::ExclusiveFullscreen)
        {
            // The size the display mode is chosen for. SDL's rule: the window's size as it is
            // now, which is the back buffer's for an XNA game. A window already exclusive keeps
            // the size its mode was chosen for -- its own size is now the monitor's.
            const WindowBounds bounds = GetClientBounds();
            EnterExclusive(exclusive_ ? exclusiveWidth_ : bounds.width,
                           exclusive_ ? exclusiveHeight_ : bounds.height);
        }
        else
        {
            LeaveExclusive();
        }

        const bool enable = mode != WindowFullscreenMode::Windowed;
        if (!SetNetWmState(atoms.netWmStateFullscreen, kNone, enable))
        {
            LeaveExclusive();
            throw PlatformException("X11Window::SetFullscreenMode",
                                    "the window manager did not accept the _NET_WM_STATE request");
        }
        fullscreenConfirmed_ = enable && wasFullscreen;
        fullscreenMode_ = mode;
        XFlush(connection_.GetDisplay());
    }

    WindowFullscreenMode X11Window::GetFullscreenMode() const
    {
        // Read from the server rather than from the cached field: the window manager, the user or
        // another client can leave fullscreen, and a cached answer would then be wrong in exactly
        // the situation a caller asks about. Exclusive is fullscreen with a display mode of the
        // window's own -- reported while the window has one, including while that mode is given
        // back because the window is hidden or minimised, as SDL reports it.
        if (HasNetWmState(connection_.GetAtoms().netWmStateFullscreen))
        {
            return exclusive_ ? WindowFullscreenMode::ExclusiveFullscreen
                              : WindowFullscreenMode::BorderlessFullscreen;
        }
        return WindowFullscreenMode::Windowed;
    }

    void X11Window::EnterExclusive(const int width, const int height)
    {
        const bool wasExclusive = exclusive_;
        const int previousWidth = exclusiveWidth_;
        const int previousHeight = exclusiveHeight_;

        std::string whyNot;
        if (!connection_.GetModeSwitcher().Resolve(this, GetClientBounds(), width, height, whyNot))
        {
            // No display mode for this size -- larger than every mode the monitor has, a server
            // without RandR, a scaled monitor. Fullscreen then stays on the desktop's own mode,
            // and GetFullscreenMode says BorderlessFullscreen, because that is what the display
            // is doing. SDL makes the same substitution and reports it the same way.
            LeaveExclusive();
            return;
        }
        exclusive_ = true;
        exclusiveWidth_ = width;
        exclusiveHeight_ = height;
        try
        {
            (void) UpdateExclusiveMode(true);
        }
        catch (...)
        {
            // The server refused the mode. The switcher kept whatever mode was in effect before,
            // so the window goes back to describing that.
            exclusive_ = wasExclusive;
            exclusiveWidth_ = previousWidth;
            exclusiveHeight_ = previousHeight;
            throw;
        }
    }

    void X11Window::LeaveExclusive()
    {
        exclusive_ = false;
        exclusiveSuspended_ = false;
        focusLossPending_ = false;
        X11ModeSwitcher& switcher = connection_.GetModeSwitcher();
        if (switcher.IsApplied(this))
        {
            switcher.Release(this);
            modeChangedAt_ = std::chrono::steady_clock::now();
        }
    }

    bool X11Window::UpdateExclusiveMode(const bool throwOnRefusal)
    {
        X11ModeSwitcher& switcher = connection_.GetModeSwitcher();
        const bool wanted = exclusive_ && shown_ && !exclusiveSuspended_ && window_ != kNone;
        if (!wanted)
        {
            if (switcher.IsApplied(this))
            {
                switcher.Release(this);
                modeChangedAt_ = std::chrono::steady_clock::now();
            }
            return true;
        }

        std::string whyNot;
        std::optional<X11AppliedMode> applied;
        try
        {
            applied = switcher.Apply(this, GetClientBounds(), exclusiveWidth_, exclusiveHeight_,
                                     whyNot);
        }
        catch (const PlatformException& refusal)
        {
            if (throwOnRefusal)
            {
                throw;
            }
            // Taking the mode back after focus returned, or at Show(): there is no caller to hand
            // a refusal to. The window stays fullscreen on the desktop's mode, and says so.
            std::fprintf(stderr, "CNA X11: exclusive fullscreen fell back to borderless: %s\n",
                         refusal.what());
            std::fflush(stderr);
            exclusive_ = false;
            return false;
        }
        if (!applied)
        {
            // The monitor changed under the window since the mode was chosen.
            exclusive_ = false;
            return false;
        }
        modeChangedAt_ = std::chrono::steady_clock::now();
        // The window manager fits a fullscreen window to its monitor, whose size this now is.
        // Sync() waits for that, which is what lets GraphicsDevice read the new size at once.
        pendingWidth_ = applied->width;
        pendingHeight_ = applied->height;
        return true;
    }

    void X11Window::SuspendExclusive()
    {
        // Minimised first, so the game's window is not seen stretched over the desktop's mode for
        // the moment between the two.
        exclusiveSuspended_ = true;
        focusLossPending_ = false;
        XIconifyWindow(connection_.GetDisplay(), window_, connection_.GetScreen());
        (void) UpdateExclusiveMode(false);
        XFlush(connection_.GetDisplay());
    }

    void X11Window::OnFocusChanged(const bool gained)
    {
        if (!exclusive_)
        {
            return;
        }
        if (gained)
        {
            focusLossPending_ = false;
            if (exclusiveSuspended_)
            {
                exclusiveSuspended_ = false;
                (void) UpdateExclusiveMode(false);
                XFlush(connection_.GetDisplay());
            }
            return;
        }
        // Under Xwayland the mode is an emulation shown only in this game's own window; the
        // desktop never left its mode, so there is nothing to give back (SDL skips it there too).
        if (!connection_.GetModeSwitcher().IsApplied(this) || connection_.IsXwayland())
        {
            return;
        }
        if (std::chrono::steady_clock::now() - modeChangedAt_ < kModeChangeFocusGrace)
        {
            focusLossPending_ = true;
            return;
        }
        SuspendExclusive();
    }

    void X11Window::CheckPendingFocusLoss()
    {
        if (!focusLossPending_ ||
            std::chrono::steady_clock::now() - modeChangedAt_ < kModeChangeFocusGrace)
        {
            return;
        }
        focusLossPending_ = false;
        if (!focused_ && exclusive_ && connection_.GetModeSwitcher().IsApplied(this))
        {
            SuspendExclusive();
        }
    }

    void X11Window::OnNetWmStateChanged()
    {
        if (!exclusive_ || window_ == kNone)
        {
            return;
        }
        if (HasNetWmState(connection_.GetAtoms().netWmStateFullscreen))
        {
            fullscreenConfirmed_ = true;
            return;
        }
        if (!fullscreenConfirmed_)
        {
            // The request to enter fullscreen is still on its way; other state changes arrive
            // first.
            return;
        }
        // Taken out of fullscreen by someone else. The display gets its mode back, and the
        // window is windowed now -- which GetFullscreenMode reports.
        fullscreenConfirmed_ = false;
        LeaveExclusive();
        fullscreenMode_ = WindowFullscreenMode::Windowed;
    }

    void X11Window::Show()
    {
        Display* display = connection_.GetDisplay();
        shown_ = true;
        if (exclusive_)
        {
            // Before the map, so the window manager maps the window straight onto its monitor in
            // the new mode rather than fitting it twice.
            (void) UpdateExclusiveMode(false);
        }
        XMapWindow(display, window_);
        XFlush(display);
    }

    void X11Window::Hide()
    {
        Display* display = connection_.GetDisplay();
        shown_ = false;
        XUnmapWindow(display, window_);
        if (exclusive_)
        {
            // A hidden window has no claim on the monitor's mode; Show() takes it again.
            (void) UpdateExclusiveMode(false);
        }
        XFlush(display);
    }

    void X11Window::Minimize()
    {
        Display* display = connection_.GetDisplay();
        if (exclusive_)
        {
            // Minimised is not visible, and the desktop gets its mode back until the window is
            // restored or focused again.
            SuspendExclusive();
            return;
        }
        // XIconifyWindow sends WM_CHANGE_STATE with IconicState, which is the ICCCM way and works
        // with every window manager rather than only EWMH ones.
        XIconifyWindow(display, window_, connection_.GetScreen());
        XFlush(display);
    }

    void X11Window::Maximize()
    {
        const X11Atoms& atoms = connection_.GetAtoms();
        if (!connection_.SupportsEwmhHint(atoms.netWmStateMaximizedVert) &&
            !connection_.SupportsEwmhHint(atoms.netWmStateMaximizedHorz))
        {
            // Maximising is purely a window-manager service; there is no core-X11 equivalent and
            // resizing to the screen by hand would be a different thing wearing its name. Doing
            // nothing here is the documented graceful degradation, not a silent success claim:
            // GetFullscreenMode and the state events keep reporting the truth.
            return;
        }
        SetNetWmState(atoms.netWmStateMaximizedVert, atoms.netWmStateMaximizedHorz, true);
        XFlush(connection_.GetDisplay());
    }

    void X11Window::Restore()
    {
        Display* display = connection_.GetDisplay();
        const X11Atoms& atoms = connection_.GetAtoms();

        // Restore has to undo both of the states that are not "normal", and the order matters:
        // un-maximising an iconified window does nothing visible until it is de-iconified, so the
        // map comes first.
        const bool minimized = IsMinimized();
        if (minimized || !mapped_)
        {
            XMapRaised(display, window_);
            shown_ = true;
        }
        if (minimized && connection_.SupportsEwmhHint(atoms.netActiveWindow))
        {
            // Mapping is how ICCCM de-iconifies, and it only works with a window manager that
            // UNMAPS iconified windows (openbox, xfwm4): the map becomes a MapRequest it acts on.
            // A compositing window manager that keeps them mapped -- mutter, i.e. GNOME -- never
            // sees a request for an already-mapped window, so the window stayed minimised there.
            // Activation is EWMH's way to bring a window back, and what pagers and taskbars use
            // (plans/plan_native_platform_validation.md NPV-0113). Source indication 1: an
            // application acting on its own window.
            connection_.SendRootClientMessage(window_, atoms.netActiveWindow, 1,
                                              static_cast<long>(kCurrentTime), 0);
        }
        // Only when it is actually maximised. Sending an un-maximise to a window that is not
        // maximised is a second window-manager round trip that does nothing, and doing it in the
        // same call as a de-iconify gives the two requests something to race over.
        if (HasNetWmState(atoms.netWmStateMaximizedVert) ||
            HasNetWmState(atoms.netWmStateMaximizedHorz))
        {
            SetNetWmState(atoms.netWmStateMaximizedVert, atoms.netWmStateMaximizedHorz, false);
        }
        if (exclusive_)
        {
            // Back on screen: the window takes its display mode again.
            exclusiveSuspended_ = false;
            (void) UpdateExclusiveMode(false);
        }
        XFlush(display);
    }

    void X11Window::Sync()
    {
        Display* display = connection_.GetDisplay();

        // The contract's reason for this method exactly: X11 window state changes are requests to
        // the server and, for anything a window manager mediates, requests to another process.
        XSync(display, kXFalse);

        if (pendingWidth_ <= 0 || pendingHeight_ <= 0)
        {
            return;
        }

        // XSync alone is NOT enough for a resize. On a managed window `XResizeWindow` is a
        // *redirected* request: the server does not apply it, it sends a ConfigureRequest to the
        // window manager, which then decides and issues the real configure. XSync guarantees only
        // that our request reached the server -- so `SetSize(); Sync(); GetClientBounds()` read
        // the OLD size whenever a window manager was running, and passed under a bare Xvfb where
        // there is none. The cross-implementation conformance suite caught exactly that.
        //
        // Polling the geometry rather than waiting on the ConfigureNotify, because consuming that
        // event here would take it from PollEvents and the application would never see the
        // Resized it is entitled to.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < deadline)
        {
            XWindowAttributes attributes{};
            if (XGetWindowAttributes(display, window_, &attributes) != 0 &&
                attributes.width == pendingWidth_ && attributes.height == pendingHeight_)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // Cleared either way. A window manager may legitimately refuse a size -- size increments,
        // a maximised window, a screen too small -- and blocking again on the next Sync() for a
        // request that will never be honoured would turn one refusal into a permanent stall.
        pendingWidth_ = 0;
        pendingHeight_ = 0;
    }

    bool X11Window::IsMinimized() const
    {
        const X11Atoms& atoms = connection_.GetAtoms();

        // _NET_WM_STATE_HIDDEN is the EWMH answer and the accurate one, because a window can be
        // "hidden" by a pager without being iconic. WM_STATE is the ICCCM fallback for a window
        // manager that implements no EWMH at all.
        if (HasNetWmState(atoms.netWmStateHidden))
        {
            return true;
        }

        std::vector<unsigned char> data;
        int format = 0;
        if (connection_.ReadProperty(window_, atoms.wmState, atoms.wmState, format, data) &&
            format == 32 && data.size() >= sizeof(long))
        {
            long state = 0;
            std::memcpy(&state, data.data(), sizeof(long));
            return state == IconicState;
        }
        return false;
    }

    void X11Window::ReadStateFlags(bool& minimized, bool& maximized) const
    {
        minimized = false;
        maximized = false;
        if (window_ == kNone)
        {
            return;
        }
        const X11Atoms& atoms = connection_.GetAtoms();

        std::vector<unsigned char> data;
        int format = 0;
        bool sawNetState = false;
        bool maximizedVertically = false;
        bool maximizedHorizontally = false;

        if (connection_.ReadProperty(window_, atoms.netWmState, XA_ATOM, format, data) &&
            format == 32)
        {
            sawNetState = true;
            const std::size_t count = data.size() / sizeof(long);
            for (std::size_t index = 0; index < count; ++index)
            {
                long value = 0;
                std::memcpy(&value, data.data() + index * sizeof(long), sizeof(long));
                const auto state = static_cast<Atom>(value);
                if (state == atoms.netWmStateHidden) { minimized = true; }
                else if (state == atoms.netWmStateMaximizedVert) { maximizedVertically = true; }
                else if (state == atoms.netWmStateMaximizedHorz) { maximizedHorizontally = true; }
            }
        }
        // Maximised means both axes. A window maximised vertically only -- which several window
        // managers offer on a double-click of the title bar edge -- is not "maximised" in the
        // sense a game asking about it means.
        maximized = maximizedVertically && maximizedHorizontally;

        if (!sawNetState || !minimized)
        {
            // The ICCCM fallback, and the correction for an EWMH window manager that iconifies
            // without setting _NET_WM_STATE_HIDDEN.
            std::vector<unsigned char> iccm;
            int iccmFormat = 0;
            if (connection_.ReadProperty(window_, atoms.wmState, atoms.wmState, iccmFormat, iccm) &&
                iccmFormat == 32 && iccm.size() >= sizeof(long))
            {
                long state = 0;
                std::memcpy(&state, iccm.data(), sizeof(long));
                minimized = minimized || state == IconicState;
            }
        }
    }

    std::string X11Window::GetDisplayName() const
    {
        // The connection's display string, which is what identifies "the screen this window is
        // on" in X11 terms. A per-monitor name would be a RandR output name and belongs to the
        // display service, which is where GraphicsAdapter gets it.
        return connection_.GetDisplayName();
    }

    void X11Window::SetInputContext(const XIC context, std::shared_ptr<void> contextData)
    {
        if (inputContext_ == context)
        {
            return;
        }
        if (inputContext_ != nullptr)
        {
            XDestroyIC(inputContext_);
        }
        inputContext_ = context;
        // Only now: the old context's callbacks can run inside XDestroyIC above.
        inputContextData_ = std::move(contextData);
    }

    void X11Window::MarkDestroyedByServer()
    {
        // Another client destroyed the window -- or killed this one's connection to it. The
        // monitor's mode was taken for this window, and goes back with it.
        LeaveExclusive();
        if (inputContext_ != nullptr)
        {
            // The input method holds this window as its client window. Destroying the context now
            // rather than in the destructor keeps the IM from using an XID the server has freed.
            XDestroyIC(inputContext_);
            inputContext_ = nullptr;
            inputContextData_.reset();
        }
        window_ = kNone;
        ownsWindow_ = false;
        mapped_ = false;
        focused_ = false;
    }

    void X11Window::SetCachedSize(const int width, const int height)
    {
        cachedWidth_ = width;
        cachedHeight_ = height;
    }

    void X11Window::SetCachedPosition(const int x, const int y)
    {
        cachedX_ = x;
        cachedY_ = y;
    }

    void X11Window::ApplySizeConstraints(const int minimumWidth, const int minimumHeight,
                                         const int maximumWidth, const int maximumHeight)
    {
        minimumWidth_ = minimumWidth;
        minimumHeight_ = minimumHeight;
        maximumWidth_ = maximumWidth;
        maximumHeight_ = maximumHeight;
        if (!resizable_)
        {
            const WindowBounds bounds = GetClientBounds();
            minimumWidth_ = maximumWidth_ = bounds.width;
            minimumHeight_ = maximumHeight_ = bounds.height;
        }
        ApplyNormalHints();
    }

    void X11Window::ApplyNormalHints()
    {
        XSizeHints* hints = XAllocSizeHints();
        if (hints == nullptr)
        {
            return;
        }
        hints->flags = 0;
        if (minimumWidth_ > 0 || minimumHeight_ > 0)
        {
            hints->flags |= PMinSize;
            hints->min_width = minimumWidth_ > 0 ? minimumWidth_ : 1;
            hints->min_height = minimumHeight_ > 0 ? minimumHeight_ : 1;
        }
        if (maximumWidth_ > 0 || maximumHeight_ > 0)
        {
            hints->flags |= PMaxSize;
            // A zero in one axis means "unconstrained in that axis", and X has no way to say that
            // in a PMaxSize hint -- so the axis is set to the largest value the 16-bit protocol
            // field can carry rather than to zero, which would pin the window to nothing.
            hints->max_width = maximumWidth_ > 0 ? maximumWidth_ : 32767;
            hints->max_height = maximumHeight_ > 0 ? maximumHeight_ : 32767;
        }
        XSetWMNormalHints(connection_.GetDisplay(), window_, hints);
        XFree(hints);
    }

    void X11Window::ApplyMotifDecorations(const bool decorated)
    {
        const X11Atoms& atoms = connection_.GetAtoms();
        if (atoms.motifWmHints == kNone)
        {
            return;
        }
        MotifWmHints hints{};
        hints.flags = kMotifHintsDecorations;
        hints.decorations = decorated ? 1 : 0;
        XChangeProperty(connection_.GetDisplay(), window_, atoms.motifWmHints, atoms.motifWmHints,
                        32, PropModeReplace, reinterpret_cast<const unsigned char*>(&hints),
                        sizeof(MotifWmHints) / sizeof(long));
    }

    bool X11Window::SetNetWmState(const Atom first, const Atom second, const bool enabled)
    {
        if (first == kNone)
        {
            return false;
        }
        const X11Atoms& atoms = connection_.GetAtoms();

        // Before the window has ever been asked to map, a _NET_WM_STATE client message has
        // nobody to act on it: the window manager only starts managing the window at MapRequest.
        // So such a window gets the property written directly, which is the sanctioned way to ask
        // for an initial state (EWMH: "the Client can set _NET_WM_STATE directly" while
        // Withdrawn).
        //
        // The condition is "never asked to map", NOT "not mapped right now". An iconified window
        // is unmapped and still managed, and writing the property directly under the window
        // manager's feet -- which is what the first version did -- raced its own map request:
        // Restore() de-iconified and then overwrote the state the window manager was in the
        // middle of updating, so the window intermittently stayed iconic. It failed roughly one
        // run in three inside the full suite while passing every time in isolation.
        //
        // Nor is it "no MapNotify seen yet", which is what came next: a game shows its window and
        // asks for fullscreen before it has pumped a single event, the window manager has managed
        // the window by then, and a property written onto a managed window is ignored -- the
        // window stayed windowed (plans/plan_x11.md X11-0153). Once Show() has sent the map
        // request, the client message is right even if the window manager has not processed that
        // request yet: the MapRequest reaches it first.
        if (!everMapped_ && !shown_)
        {
            std::vector<unsigned char> data;
            int format = 0;
            std::vector<long> states;
            if (connection_.ReadProperty(window_, atoms.netWmState, XA_ATOM, format, data) &&
                format == 32)
            {
                const std::size_t count = data.size() / sizeof(long);
                // An existing but EMPTY _NET_WM_STATE (a window that has left every state) is
                // common, and memcpy's pointers must be valid even for a zero length: both are
                // null here, which UBSan reports and an optimiser is entitled to exploit.
                if (count > 0)
                {
                    states.resize(count);
                    std::memcpy(states.data(), data.data(), count * sizeof(long));
                }
            }

            const auto apply = [&states, enabled](const Atom atom) {
                if (atom == kNone) { return; }
                const auto value = static_cast<long>(atom);
                const auto found = std::find(states.begin(), states.end(), value);
                if (enabled && found == states.end())
                {
                    states.push_back(value);
                }
                else if (!enabled && found != states.end())
                {
                    states.erase(found);
                }
            };
            apply(first);
            apply(second);

            XChangeProperty(connection_.GetDisplay(), window_, atoms.netWmState, XA_ATOM, 32,
                            PropModeReplace,
                            reinterpret_cast<const unsigned char*>(states.data()),
                            static_cast<int>(states.size()));
            return true;
        }

        return connection_.SendRootClientMessage(window_, atoms.netWmState,
                                                 enabled ? kNetWmStateAdd : kNetWmStateRemove,
                                                 static_cast<long>(first),
                                                 static_cast<long>(second));
    }

    bool X11Window::HasNetWmState(const Atom state) const
    {
        if (state == kNone)
        {
            return false;
        }
        std::vector<unsigned char> data;
        int format = 0;
        if (!connection_.ReadProperty(window_, connection_.GetAtoms().netWmState, XA_ATOM, format,
                                      data) ||
            format != 32)
        {
            return false;
        }
        const std::size_t count = data.size() / sizeof(long);
        for (std::size_t index = 0; index < count; ++index)
        {
            long value = 0;
            std::memcpy(&value, data.data() + index * sizeof(long), sizeof(long));
            if (static_cast<Atom>(value) == state)
            {
                return true;
            }
        }
        return false;
    }

} // namespace CNA::Platform::X11
