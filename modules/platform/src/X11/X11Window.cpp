// SPDX-License-Identifier: MS-PL

#include "X11Window.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "X11Display.hpp"
#include "X11Error.hpp"

#include <algorithm>
#include <cstring>
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
            if (visual_ == nullptr)
            {
                visual_ = attributes.visual;
                depth_ = attributes.depth;
            }
        }
    }

    X11Window::~X11Window()
    {
        // Order matters and is not interchangeable: an XIC holds a reference to the window inside
        // the input method, and destroying the window first leaves the IM with a dangling client
        // window that it will use on the next XFilterEvent.
        if (inputContext_ != nullptr)
        {
            XDestroyIC(inputContext_);
            inputContext_ = nullptr;
        }
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
        const WindowBounds bounds = GetClientBounds();
        return {bounds.width, bounds.height};
    }

    void X11Window::SetSize(const int width, const int height)
    {
        if (width <= 0 || height <= 0)
        {
            throw PlatformException("X11Window::SetSize",
                                    "a window size must be positive; X11 rejects a zero extent");
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

        if (mode == WindowFullscreenMode::ExclusiveFullscreen)
        {
            // plans/plan_x11.md design decision 11. Genuine exclusive mode means an XRandR mode
            // switch that changes the user's desktop resolution, with all the restore-on-crash
            // obligations that carries. It is not implemented, so it refuses rather than silently
            // giving the caller borderless fullscreen under an "exclusive" name -- which would
            // make GetFullscreenMode() lie about what the display is doing.
            throw PlatformNotSupportedException(PlatformCapability::BorderlessFullscreen,
                                                "X11 (exclusive fullscreen is not implemented; "
                                                "BorderlessFullscreen is available)");
        }

        if (mode == WindowFullscreenMode::BorderlessFullscreen &&
            !connection_.SupportsEwmhHint(atoms.netWmStateFullscreen))
        {
            throw PlatformNotSupportedException(
                PlatformCapability::BorderlessFullscreen,
                connection_.HasWindowManager()
                    ? "X11 (this window manager does not advertise _NET_WM_STATE_FULLSCREEN)"
                    : "X11 (no EWMH window manager is running on this display)");
        }

        const bool enable = mode == WindowFullscreenMode::BorderlessFullscreen;
        if (!SetNetWmState(atoms.netWmStateFullscreen, kNone, enable))
        {
            throw PlatformException("X11Window::SetFullscreenMode",
                                    "the window manager did not accept the _NET_WM_STATE request");
        }
        fullscreenMode_ = mode;
        XFlush(connection_.GetDisplay());
    }

    WindowFullscreenMode X11Window::GetFullscreenMode() const
    {
        // Read from the server rather than from the cached field: the window manager, the user or
        // another client can leave fullscreen, and a cached answer would then be wrong in exactly
        // the situation a caller asks about.
        if (HasNetWmState(connection_.GetAtoms().netWmStateFullscreen))
        {
            return WindowFullscreenMode::BorderlessFullscreen;
        }
        return WindowFullscreenMode::Windowed;
    }

    void X11Window::Show()
    {
        Display* display = connection_.GetDisplay();
        XMapWindow(display, window_);
        XFlush(display);
    }

    void X11Window::Hide()
    {
        Display* display = connection_.GetDisplay();
        XUnmapWindow(display, window_);
        XFlush(display);
    }

    void X11Window::Minimize()
    {
        Display* display = connection_.GetDisplay();
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
        if (IsMinimized() || !mapped_)
        {
            XMapRaised(display, window_);
        }
        SetNetWmState(atoms.netWmStateMaximizedVert, atoms.netWmStateMaximizedHorz, false);
        XFlush(display);
    }

    void X11Window::Sync()
    {
        // The contract's reason for this method exactly: X11 window state changes are requests to
        // the server and, for anything a window manager mediates, requests to another process.
        // XSync round-trips the connection so the server has certainly processed everything sent.
        XSync(connection_.GetDisplay(), False);
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

    std::string X11Window::GetDisplayName() const
    {
        // The connection's display string, which is what identifies "the screen this window is
        // on" in X11 terms. A per-monitor name would be a RandR output name and belongs to the
        // display service, which is where GraphicsAdapter gets it.
        return connection_.GetDisplayName();
    }

    void X11Window::SetInputContext(const XIC context)
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
    }

    void X11Window::MarkDestroyedByServer()
    {
        if (inputContext_ != nullptr)
        {
            // The input method holds this window as its client window. Destroying the context now
            // rather than in the destructor keeps the IM from using an XID the server has freed.
            XDestroyIC(inputContext_);
            inputContext_ = nullptr;
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

        // Before the window is mapped, a _NET_WM_STATE client message has nobody to act on it:
        // the window manager only starts managing the window at MapRequest. So an unmapped window
        // gets the property written directly, which is the ICCCM-sanctioned way to ask for an
        // initial state, and a mapped one gets the client message.
        if (!mapped_)
        {
            std::vector<unsigned char> data;
            int format = 0;
            std::vector<long> states;
            if (connection_.ReadProperty(window_, atoms.netWmState, XA_ATOM, format, data) &&
                format == 32)
            {
                const std::size_t count = data.size() / sizeof(long);
                states.resize(count);
                std::memcpy(states.data(), data.data(), count * sizeof(long));
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
