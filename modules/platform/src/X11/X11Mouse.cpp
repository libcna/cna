// SPDX-License-Identifier: MS-PL

#include "X11Mouse.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "X11Display.hpp"
#include "X11Error.hpp"
#include "X11Window.hpp"

#include <X11/cursorfont.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace CNA::Platform::X11 {

    namespace {

        /// CNA shape -> X cursor-font glyph.
        ///
        /// The cursor font is core X11, present on every server, and needs no extension. Four of
        /// CNA's twelve shapes have no cursor-font equivalent (`NotAllowed`, `Progress`,
        /// `NwseResize`, `NeswResize`); each falls back to the nearest shape that genuinely
        /// exists rather than to an arbitrary one, and the choice is named in the row.
        unsigned int ToCursorFontShape(const SystemCursor cursor)
        {
            switch (cursor)
            {
                case SystemCursor::Arrow: return XC_left_ptr;
                case SystemCursor::IBeam: return XC_xterm;
                case SystemCursor::Wait: return XC_watch;
                case SystemCursor::Crosshair: return XC_crosshair;
                case SystemCursor::Move: return XC_fleur;
                // X's pirate glyph is what the cursor font offers for "you cannot do that here".
                case SystemCursor::NotAllowed: return XC_pirate;
                case SystemCursor::Pointer: return XC_hand2;
                // The cursor font has no busy-with-arrow glyph, so this is the plain busy one.
                case SystemCursor::Progress: return XC_watch;
                // The font has only the four axis-aligned corner glyphs, not the two diagonal
                // double-headed arrows; the matching corner is closer than a generic resize.
                case SystemCursor::NwseResize: return XC_bottom_right_corner;
                case SystemCursor::NeswResize: return XC_bottom_left_corner;
                case SystemCursor::EwResize: return XC_sb_h_double_arrow;
                case SystemCursor::NsResize: return XC_sb_v_double_arrow;
            }
            return XC_left_ptr;
        }

    } // namespace

    X11Mouse::X11Mouse(X11Connection& connection) : connection_(connection) {}

    X11Mouse::~X11Mouse()
    {
        Display* display = connection_.GetDisplay();
        if (relativeWindow_ != 0 || captured_)
        {
            XUngrabPointer(display, kCurrentTime);
        }
        for (const auto& [shape, cursor] : fontCursors_)
        {
            (void) shape;
            XFreeCursor(display, cursor);
        }
        if (hiddenCursor_ != 0) { XFreeCursor(display, hiddenCursor_); }
        if (customCursor_ != 0) { XFreeCursor(display, customCursor_); }
    }

    bool X11Mouse::HasRawMotion() const
    {
#if defined(CNA_X11_HAVE_XI)
        return connection_.GetXInput2Opcode() >= 0;
#else
        return false;
#endif
    }

    void X11Mouse::RegisterWindow(const WindowId id, X11Window* window)
    {
        if (window == nullptr)
        {
            windows_.erase(id);
            if (relativeWindow_ == id)
            {
                // The window the pointer was locked to is going away. Leaving the grab in place
                // would lock the user's pointer to a destroyed window until the connection dies.
                ReleaseRelativeMode();
            }
            return;
        }
        windows_[id] = window;
    }

    X11Window* X11Mouse::FindWindow(const WindowId id) const
    {
        const auto found = windows_.find(id);
        return found != windows_.end() ? found->second : nullptr;
    }

    void X11Mouse::Update()
    {
        Display* display = connection_.GetDisplay();
        ::Window root = kNone;
        ::Window child = kNone;
        int rootX = 0;
        int rootY = 0;
        int windowX = 0;
        int windowY = 0;
        unsigned int mask = 0;

        // Querying through the window the snapshot already refers to keeps the reported position
        // in that window's client coordinates. With no window yet, the root is the only thing
        // that can be asked, and the position is desktop coordinates -- which is honest, because
        // there is no client area for it to be relative to.
        ::Window queryWindow = connection_.GetRoot();
        if (X11Window* window = FindWindow(snapshot_.window))
        {
            queryWindow = window->GetXWindow();
        }

        if (XQueryPointer(display, queryWindow, &root, &child, &rootX, &rootY, &windowX, &windowY,
                          &mask) == True)
        {
            snapshot_.x = windowX;
            snapshot_.y = windowY;
        }

        // Bits 0-2 come from the server's own pointer mask, which is what a level query needs:
        // accumulating press and release events loses a button that went down while the
        // application was not listening.
        //
        // Bits 3-4 (X1, X2) do NOT. `Button4Mask` and `Button5Mask` are X's WHEEL buttons, not the
        // side buttons -- the core protocol has exactly five mask bits and spends two of them on
        // scrolling. Copying them into the X1/X2 bits would report a button held for the duration
        // of every wheel notch. The core protocol has no mask bit for a real button 8 or 9 at all,
        // so the only source for those is the press/release events the pump already tracks, which
        // is why they are carried across rather than rebuilt here.
        constexpr std::uint8_t kExtraButtonBits = 0x18;  // bits 3 and 4
        std::uint8_t buttons = static_cast<std::uint8_t>(snapshot_.buttons & kExtraButtonBits);
        if ((mask & Button1Mask) != 0) { buttons |= 1u << 0; }
        if ((mask & Button2Mask) != 0) { buttons |= 1u << 1; }
        if ((mask & Button3Mask) != 0) { buttons |= 1u << 2; }
        snapshot_.buttons = buttons;
    }

    MouseDelta X11Mouse::ConsumeRelativeDelta()
    {
        const MouseDelta delta{relativeX_, relativeY_};
        relativeX_ = 0;
        relativeY_ = 0;
        return delta;
    }

    void X11Mouse::SetPosition(const WindowId window, const int x, const int y)
    {
        X11Window* target = FindWindow(window);
        if (target == nullptr)
        {
            throw PlatformException("X11Mouse::SetPosition", "unknown window id");
        }
        Display* display = connection_.GetDisplay();
        XWarpPointer(display, kNone, target->GetXWindow(), 0, 0, 0, 0, x, y);
        XFlush(display);
    }

    void X11Mouse::SetCursorVisible(const bool visible)
    {
        if (cursorVisible_ == visible)
        {
            return;
        }
        cursorVisible_ = visible;
        ApplyCursor(visible ? activeCursor_ : GetHiddenCursor());
    }

    void X11Mouse::SetCursor(const SystemCursor cursor)
    {
        Display* display = connection_.GetDisplay();
        const unsigned int shape = ToCursorFontShape(cursor);

        // Cached per shape: XCreateFontCursor is a server round trip, and a UI that sets the
        // I-beam on every pointer motion over a text field would otherwise pay for it repeatedly.
        const auto found = fontCursors_.find(static_cast<int>(shape));
        ::Cursor handle = 0;
        if (found != fontCursors_.end())
        {
            handle = found->second;
        }
        else
        {
            handle = XCreateFontCursor(display, shape);
            if (handle == 0)
            {
                throw PlatformException("X11Mouse::SetCursor", "the X cursor font is unavailable");
            }
            fontCursors_[static_cast<int>(shape)] = handle;
        }
        activeCursor_ = handle;
        if (cursorVisible_)
        {
            ApplyCursor(handle);
        }
    }

    void X11Mouse::SetCursor(const CursorImage& cursor)
    {
#if defined(CNA_X11_HAVE_XCURSOR)
        if (cursor.width <= 0 || cursor.height <= 0 ||
            cursor.rgba.size() <
                static_cast<std::size_t>(cursor.width) * static_cast<std::size_t>(cursor.height))
        {
            throw PlatformException("X11Mouse::SetCursor",
                                    "the cursor image is smaller than its stated dimensions");
        }

        XcursorImage* image = XcursorImageCreate(cursor.width, cursor.height);
        if (image == nullptr)
        {
            throw PlatformException("X11Mouse::SetCursor", "XcursorImageCreate failed");
        }
        image->xhot = static_cast<XcursorDim>(std::max(0, cursor.hotSpotX));
        image->yhot = static_cast<XcursorDim>(std::max(0, cursor.hotSpotY));

        // The contract's pixels are 0xAABBGGRR (red in the low byte); Xcursor wants premultiplied
        // 0xAARRGGBB. Both halves of that conversion are real: getting the channel order right
        // and forgetting the premultiplication produces a cursor with a bright halo.
        const std::size_t count =
            static_cast<std::size_t>(cursor.width) * static_cast<std::size_t>(cursor.height);
        for (std::size_t index = 0; index < count; ++index)
        {
            const std::uint32_t source = cursor.rgba[index];
            const std::uint32_t alpha = (source >> 24) & 0xFFu;
            const std::uint32_t blue = (source >> 16) & 0xFFu;
            const std::uint32_t green = (source >> 8) & 0xFFu;
            const std::uint32_t red = source & 0xFFu;
            const auto premultiply = [alpha](const std::uint32_t channel) {
                return (channel * alpha + 127u) / 255u;
            };
            image->pixels[index] = (alpha << 24) | (premultiply(red) << 16) |
                                   (premultiply(green) << 8) | premultiply(blue);
        }

        ::Cursor handle = XcursorImageLoadCursor(connection_.GetDisplay(), image);
        XcursorImageDestroy(image);
        if (handle == 0)
        {
            throw PlatformException("X11Mouse::SetCursor", "XcursorImageLoadCursor failed");
        }
        if (customCursor_ != 0)
        {
            XFreeCursor(connection_.GetDisplay(), customCursor_);
        }
        customCursor_ = handle;
        activeCursor_ = handle;
        if (cursorVisible_)
        {
            ApplyCursor(handle);
        }
#else
        (void) cursor;
        // Core X11 can only make a two-colour cursor from a pair of bitmaps. Rendering an ARGB
        // image down to 1-bit-per-pixel-plus-mask would produce something visibly different from
        // what the caller handed over, so this refuses instead -- the shaped cursors from the
        // cursor font remain available and CursorShapes stays true.
        throw PlatformNotSupportedException(
            PlatformCapability::CursorShapes,
            "X11 (a custom ARGB cursor needs libXcursor, which was not available at build time; "
            "the standard cursor shapes are still supported)");
#endif
    }

    void X11Mouse::SetRelativeMode(const WindowId window, const bool enabled)
    {
        if (!enabled)
        {
            ReleaseRelativeMode();
            return;
        }

        if (!HasRawMotion())
        {
            throw PlatformNotSupportedException(
                PlatformCapability::RelativeMouse,
                "X11 (relative mouse needs XInput2 raw motion, which this server or build does "
                "not provide; a warp-based imitation would report the warps as input)");
        }

        X11Window* target = FindWindow(window);
        if (target == nullptr)
        {
            throw PlatformException("X11Mouse::SetRelativeMode", "unknown window id");
        }

#if defined(CNA_X11_HAVE_XI)
        Display* display = connection_.GetDisplay();

        // Raw events are delivered per client, not per window, and only the root window can be
        // selected on for them -- so the window argument decides the grab, not the selection.
        unsigned char mask[XIMaskLen(XI_LASTEVENT)] = {};
        XISetMask(mask, XI_RawMotion);
        XIEventMask eventMask{};
        eventMask.deviceid = XIAllMasterDevices;
        eventMask.mask_len = sizeof(mask);
        eventMask.mask = mask;
        XISelectEvents(display, connection_.GetRoot(), &eventMask, 1);

        // The grab is what makes the mode deterministic: the pointer cannot leave the window, a
        // click cannot reach another application, and the cursor is hidden for the duration. The
        // raw deltas keep arriving regardless of where the confined pointer sits, which is why
        // edge clipping does not lose motion the way a warp-based scheme does.
        const int grab = XGrabPointer(display, target->GetXWindow(), True,
                                      ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                      GrabModeAsync, GrabModeAsync, target->GetXWindow(),
                                      GetHiddenCursor(), kCurrentTime);
        if (grab != GrabSuccess)
        {
            XISelectEvents(display, connection_.GetRoot(), &eventMask, 0);
            throw PlatformException("X11Mouse::SetRelativeMode",
                                    "another client holds the pointer grab");
        }
        relativeWindow_ = window;
        relativeX_ = 0;
        relativeY_ = 0;
        XFlush(display);
#endif
    }

    void X11Mouse::ReleaseRelativeMode()
    {
        if (relativeWindow_ == 0)
        {
            return;
        }
        Display* display = connection_.GetDisplay();
#if defined(CNA_X11_HAVE_XI)
        unsigned char mask[XIMaskLen(XI_LASTEVENT)] = {};
        XIEventMask eventMask{};
        eventMask.deviceid = XIAllMasterDevices;
        eventMask.mask_len = sizeof(mask);
        eventMask.mask = mask;
        XISelectEvents(display, connection_.GetRoot(), &eventMask, 1);
#endif
        if (!captured_)
        {
            XUngrabPointer(display, kCurrentTime);
        }
        relativeWindow_ = 0;
        relativeX_ = 0;
        relativeY_ = 0;
        XFlush(display);
    }

    bool X11Mouse::SetCapture(const bool enabled)
    {
        Display* display = connection_.GetDisplay();
        if (!enabled)
        {
            if (!captured_)
            {
                return true;
            }
            captured_ = false;
            if (relativeWindow_ == 0)
            {
                XUngrabPointer(display, kCurrentTime);
                XFlush(display);
            }
            return true;
        }

        if (captured_)
        {
            return true;
        }
        X11Window* target = FindWindow(snapshot_.window);
        if (target == nullptr && !windows_.empty())
        {
            target = windows_.begin()->second;
        }
        if (target == nullptr)
        {
            return false;
        }

        // confine_to is None here, deliberately: capture means "keep delivering events to me even
        // when the pointer leaves my window", which is what a drag needs. Confining the pointer is
        // relative mode's job, and doing it here would break dragging past the window edge.
        const int grab = XGrabPointer(display, target->GetXWindow(), True,
                                      ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                      GrabModeAsync, GrabModeAsync, kNone, kNone, kCurrentTime);
        if (grab != GrabSuccess)
        {
            return false;
        }
        captured_ = true;
        XFlush(display);
        return true;
    }

    bool X11Mouse::TryGetGlobalPosition(float& x, float& y) const
    {
        Display* display = connection_.GetDisplay();
        ::Window root = kNone;
        ::Window child = kNone;
        int rootX = 0;
        int rootY = 0;
        int windowX = 0;
        int windowY = 0;
        unsigned int mask = 0;
        if (XQueryPointer(display, connection_.GetRoot(), &root, &child, &rootX, &rootY, &windowX,
                          &windowY, &mask) != True)
        {
            // False here means the pointer is on a different screen of the same display, which is
            // a real answer rather than a failure -- but it is not a position on this screen, so
            // reporting one would be wrong.
            return false;
        }
        x = static_cast<float>(rootX);
        y = static_cast<float>(rootY);
        return true;
    }

    bool X11Mouse::SetGlobalPosition(const float x, const float y)
    {
        Display* display = connection_.GetDisplay();
        XWarpPointer(display, kNone, connection_.GetRoot(), 0, 0, 0, 0, static_cast<int>(x),
                     static_cast<int>(y));
        XFlush(display);
        return true;
    }

    void X11Mouse::AccumulateRawMotion(const double deltaX, const double deltaY)
    {
        relativeX_ += static_cast<int>(deltaX);
        relativeY_ += static_cast<int>(deltaY);
    }

    void X11Mouse::SetLastPosition(const WindowId window, const int x, const int y)
    {
        snapshot_.window = window;
        snapshot_.x = x;
        snapshot_.y = y;
    }

    void X11Mouse::SetButtonState(const unsigned int button, const bool pressed)
    {
        if (button == 0 || button > 8)
        {
            return;
        }
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << (button - 1));
        if (pressed)
        {
            snapshot_.buttons |= bit;
        }
        else
        {
            snapshot_.buttons = static_cast<std::uint8_t>(snapshot_.buttons & ~bit);
        }
    }

    void X11Mouse::AccumulateScroll(const int x, const int y)
    {
        // The snapshot's scroll fields are in XNA units -- 120 per whole notch, which is what
        // `Mouse::GetState().ScrollWheelValue` reports and what every XNA game divides by. X
        // delivers one button press per notch, so the conversion happens here rather than leaving
        // a backend that counts in notches and one that counts in XNA units disagreeing by 120x.
        constexpr long long kUnitsPerNotch = 120;
        const auto accumulate = [](int& total, const int notches) {
            const long long next =
                static_cast<long long>(total) + static_cast<long long>(notches) * kUnitsPerNotch;
            total = static_cast<int>(std::clamp(
                next, static_cast<long long>(std::numeric_limits<int>::min()),
                static_cast<long long>(std::numeric_limits<int>::max())));
        };
        accumulate(snapshot_.scrollX, x);
        accumulate(snapshot_.scrollY, y);
    }

    void X11Mouse::ApplyCursor(const ::Cursor cursor)
    {
        Display* display = connection_.GetDisplay();
        for (const auto& [id, window] : windows_)
        {
            (void) id;
            if (window == nullptr)
            {
                continue;
            }
            // XDefineCursor with None restores the parent's cursor, which is exactly what
            // "back to the default" means in X11 -- there is no separate "undefine to arrow".
            XDefineCursor(display, window->GetXWindow(), cursor);
        }
        XFlush(display);
    }

    ::Cursor X11Mouse::GetHiddenCursor()
    {
        if (hiddenCursor_ != 0)
        {
            return hiddenCursor_;
        }
        Display* display = connection_.GetDisplay();

        // A 1x1 fully transparent cursor. XFixesHideCursor exists and is simpler, but it hides
        // the cursor for the whole screen rather than for this application's windows, which is
        // not what SetCursorVisible(false) promises.
        char zero[8] = {};
        Pixmap pixmap = XCreateBitmapFromData(display, connection_.GetRoot(), zero, 1, 1);
        XColor black{};
        hiddenCursor_ = XCreatePixmapCursor(display, pixmap, pixmap, &black, &black, 0, 0);
        XFreePixmap(display, pixmap);
        return hiddenCursor_;
    }

} // namespace CNA::Platform::X11
