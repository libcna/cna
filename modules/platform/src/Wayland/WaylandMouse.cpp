// SPDX-License-Identifier: MS-PL

#include "WaylandMouse.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "WaylandConnection.hpp"
#include "WaylandCursorTheme.hpp"
#include "WaylandShm.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <linux/input-event-codes.h>

namespace CNA::Platform::Wayland {

    namespace {

        /// How close in time and space two presses must be to count as a double click -- the
        /// X11 backend's values, which every toolkit converges on.
        constexpr std::uint32_t kDoubleClickMilliseconds = 500;
        constexpr double kDoubleClickSlop = 4.0;

        /// The continuous scroll distance libinput and every compositor treat as one wheel notch.
        constexpr double kUnitsPerNotch = 10.0;

        /// Kinds of pointer event offered to the built-in title bar (Host::frameEvent).
        enum FrameEventKind
        {
            kFrameEnter = 0,
            kFrameLeave = 1,
            kFrameMotion = 2,
            kFrameButton = 3
        };

        std::uint8_t ButtonBit(const std::uint8_t button)
        {
            return button >= 1 && button <= 5 ? static_cast<std::uint8_t>(1u << (button - 1)) : 0;
        }

#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
        std::uint32_t ShapeOf(const SystemCursor cursor)
        {
            switch (cursor)
            {
                case SystemCursor::Arrow: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
                case SystemCursor::IBeam: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
                case SystemCursor::Wait: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT;
                case SystemCursor::Crosshair: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR;
                case SystemCursor::Move: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE;
                case SystemCursor::NotAllowed: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED;
                case SystemCursor::Pointer: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
                case SystemCursor::Progress: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS;
                case SystemCursor::NwseResize: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE;
                case SystemCursor::NeswResize: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE;
                case SystemCursor::EwResize: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE;
                case SystemCursor::NsResize: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE;
            }
            return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
        }
#endif

    } // namespace

    std::uint8_t ButtonFromLinuxCode(const std::uint32_t button)
    {
        switch (button)
        {
            case BTN_LEFT: return 1;
            case BTN_MIDDLE: return 2;
            case BTN_RIGHT: return 3;
            case BTN_SIDE:
            case BTN_BACK: return 4;
            case BTN_EXTRA:
            case BTN_FORWARD: return 5;
            default: return 0;
        }
    }

    std::int32_t AxisTo120(const AxisFrame& frame)
    {
        if (frame.hasValue120)
        {
            return frame.value120;
        }
        if (frame.hasDiscrete)
        {
            return frame.discrete * 120;
        }
        if (frame.hasValue)
        {
            return static_cast<std::int32_t>(std::lround(frame.value / kUnitsPerNotch * 120.0));
        }
        return 0;
    }

    struct WaylandMouse::SeatPointer
    {
        wl_pointer* proxy = nullptr;
        wl_seat* seat = nullptr;
        WindowId focus = 0;
        wl_surface* focusSurface = nullptr;
        bool onFrame = false;
        std::uint32_t enterSerial = 0;
        double x = 0.0;
        double y = 0.0;
        AxisFrame axis[2];
        std::uint8_t buttons = 0;
#if defined(CNA_WAYLAND_HAVE_RELATIVE_POINTER)
        zwp_relative_pointer_v1* relative = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
        zwp_locked_pointer_v1* locked = nullptr;
        bool lockActive = false;
#endif
#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
        wp_cursor_shape_device_v1* shapeDevice = nullptr;
#endif
        wl_surface* themeSurface = nullptr;
    };

    const wl_pointer_listener WaylandMouse::kListener = {
        .enter = [](void* data, wl_pointer* proxy, const std::uint32_t serial, wl_surface* surface,
                    const wl_fixed_t sx, const wl_fixed_t sy) {
            auto* self = static_cast<WaylandMouse*>(data);
            SeatPointer* pointer = self->Find(proxy);
            if (pointer == nullptr || surface == nullptr)
            {
                return;
            }
            pointer->enterSerial = serial;
            pointer->focusSurface = surface;
            pointer->x = wl_fixed_to_double(sx);
            pointer->y = wl_fixed_to_double(sy);
            if (self->host_.recordSerial) { self->host_.recordSerial(pointer->seat, serial); }
            // A surface that is not a window's content -- the built-in title bar -- is the
            // frame's, and the game never sees the pointer over it.
            pointer->onFrame = self->host_.frameEvent &&
                               self->host_.frameEvent(surface, kFrameEnter, pointer->x, pointer->y, 0, false, serial,
                                                      proxy);
            pointer->focus = pointer->onFrame ? 0 : (self->host_.resolveSurface ? self->host_.resolveSurface(surface) : 0);
            if (!pointer->onFrame)
            {
                self->lastWindow_ = pointer->focus;
                self->lastX_ = pointer->x;
                self->lastY_ = pointer->y;
                self->ApplyCursor(*pointer);
            }
            self->Update();
        },
        .leave = [](void* data, wl_pointer* proxy, std::uint32_t, wl_surface* surface) {
            auto* self = static_cast<WaylandMouse*>(data);
            SeatPointer* pointer = self->Find(proxy);
            if (pointer == nullptr)
            {
                return;
            }
            if (pointer->onFrame && self->host_.frameEvent)
            {
                (void) self->host_.frameEvent(surface, kFrameLeave, 0, 0, 0, false, 0, proxy);
            }
            pointer->onFrame = false;
            pointer->focus = 0;
            pointer->focusSurface = nullptr;
            // Buttons held when the pointer left are released by the compositor on the surface it
            // goes to, not here; without this the snapshot would hold them down.
            pointer->buttons = 0;
            self->Update();
        },
        .motion = [](void* data, wl_pointer* proxy, std::uint32_t, const wl_fixed_t sx, const wl_fixed_t sy) {
            auto* self = static_cast<WaylandMouse*>(data);
            SeatPointer* pointer = self->Find(proxy);
            if (pointer == nullptr)
            {
                return;
            }
            const double x = wl_fixed_to_double(sx);
            const double y = wl_fixed_to_double(sy);
            if (pointer->onFrame)
            {
                pointer->x = x;
                pointer->y = y;
                (void) self->host_.frameEvent(pointer->focusSurface, kFrameMotion, x, y, 0, false, 0, proxy);
                return;
            }
            MouseMotionEvent motion;
            motion.window = pointer->focus;
            motion.x = static_cast<float>(x);
            motion.y = static_cast<float>(y);
            motion.deltaX = static_cast<float>(x - pointer->x);
            motion.deltaY = static_cast<float>(y - pointer->y);
            pointer->x = x;
            pointer->y = y;
            self->lastWindow_ = pointer->focus;
            self->lastX_ = x;
            self->lastY_ = y;
            if (self->host_.post) { self->host_.post(motion); }
            self->Update();
        },
        .button = [](void* data, wl_pointer* proxy, const std::uint32_t serial, const std::uint32_t time,
                     const std::uint32_t code, const std::uint32_t state) {
            auto* self = static_cast<WaylandMouse*>(data);
            SeatPointer* pointer = self->Find(proxy);
            if (pointer == nullptr)
            {
                return;
            }
            if (self->host_.recordSerial) { self->host_.recordSerial(pointer->seat, serial); }
            const bool pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
            if (pointer->onFrame)
            {
                (void) self->host_.frameEvent(pointer->focusSurface, kFrameButton, pointer->x, pointer->y, code,
                                              pressed, serial, proxy);
                return;
            }
            const std::uint8_t button = ButtonFromLinuxCode(code);
            if (button == 0)
            {
                return;
            }
            if (pressed)
            {
                pointer->buttons |= ButtonBit(button);
            }
            else
            {
                pointer->buttons &= static_cast<std::uint8_t>(~ButtonBit(button));
            }
            std::uint8_t clicks = 1;
            if (pressed)
            {
                const bool again = button == self->lastClickButton_ && time - self->lastClickTime_ <= kDoubleClickMilliseconds &&
                                   std::abs(pointer->x - self->lastClickX_) <= kDoubleClickSlop &&
                                   std::abs(pointer->y - self->lastClickY_) <= kDoubleClickSlop;
                self->clickCount_ = again ? static_cast<std::uint8_t>(self->clickCount_ + 1) : std::uint8_t{1};
                clicks = self->clickCount_;
                self->lastClickTime_ = time;
                self->lastClickButton_ = button;
                self->lastClickX_ = pointer->x;
                self->lastClickY_ = pointer->y;
            }
            MouseButtonEvent event;
            event.window = pointer->focus;
            event.button = button;
            event.pressed = pressed;
            event.clicks = clicks;
            event.x = static_cast<float>(pointer->x);
            event.y = static_cast<float>(pointer->y);
            if (self->host_.post) { self->host_.post(event); }
            self->Update();
        },
        .axis = [](void* data, wl_pointer* proxy, std::uint32_t, const std::uint32_t axis, const wl_fixed_t value) {
            auto* self = static_cast<WaylandMouse*>(data);
            SeatPointer* pointer = self->Find(proxy);
            if (pointer == nullptr || axis > 1)
            {
                return;
            }
            pointer->axis[axis].value += wl_fixed_to_double(value);
            pointer->axis[axis].hasValue = true;
            // Before v5 there are no frames: each axis event is its own.
            if (wl_pointer_get_version(proxy) < WL_POINTER_FRAME_SINCE_VERSION)
            {
                self->EmitFrame(*pointer);
            }
        },
        .frame = [](void* data, wl_pointer* proxy) {
            auto* self = static_cast<WaylandMouse*>(data);
            if (SeatPointer* pointer = self->Find(proxy))
            {
                self->EmitFrame(*pointer);
            }
        },
        .axis_source = [](void*, wl_pointer*, std::uint32_t) {},
        .axis_stop = [](void*, wl_pointer*, std::uint32_t, std::uint32_t) {},
        .axis_discrete = [](void* data, wl_pointer* proxy, const std::uint32_t axis, const std::int32_t discrete) {
            auto* self = static_cast<WaylandMouse*>(data);
            SeatPointer* pointer = self->Find(proxy);
            if (pointer == nullptr || axis > 1)
            {
                return;
            }
            pointer->axis[axis].discrete += discrete;
            pointer->axis[axis].hasDiscrete = true;
        },
        .axis_value120 = [](void* data, wl_pointer* proxy, const std::uint32_t axis, const std::int32_t value120) {
            auto* self = static_cast<WaylandMouse*>(data);
            SeatPointer* pointer = self->Find(proxy);
            if (pointer == nullptr || axis > 1)
            {
                return;
            }
            pointer->axis[axis].value120 += value120;
            pointer->axis[axis].hasValue120 = true;
        },
        .axis_relative_direction = [](void*, wl_pointer*, std::uint32_t, std::uint32_t) {
            // Whether the value is already inverted for "natural" scrolling. The contract's wheel
            // values are after the host's direction preference, which is what the compositor sent.
        },
    };

    WaylandMouse::WaylandMouse(Host host) : host_(std::move(host))
    {
        // Nothing of the connection is touched here: the pointer service exists before the
        // connection does (seats are announced during its startup), and the cursor theme is loaded
        // the first time it is needed (EnsureTheme).
    }

    void WaylandMouse::EnsureTheme() const
    {
        if (themeTried_)
        {
            return;
        }
        themeTried_ = true;
        const WaylandGlobals& globals = host_.connection().GetGlobals();
#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
        if (globals.cursorShapeManager != nullptr)
        {
            return;
        }
#endif
        theme_ = WaylandCursorTheme::Load(globals.shm);
    }

    WaylandMouse::~WaylandMouse()
    {
        ReleaseConnectionObjects();
    }

    void WaylandMouse::ReleaseConnectionObjects()
    {
        while (!pointers_.empty())
        {
            Detach(pointers_.back()->proxy);
        }
        DestroyCustomCursor();
        // The theme's buffers are wl_buffers of this connection: they go with it, and no theme is
        // loaded again from a connection that is closing.
        theme_.reset();
        themeTried_ = true;
    }

    WaylandMouse::SeatPointer* WaylandMouse::Find(wl_pointer* pointer) const
    {
        for (const auto& candidate : pointers_)
        {
            if (candidate->proxy == pointer)
            {
                return candidate.get();
            }
        }
        return nullptr;
    }

    bool WaylandMouse::HasRelativeSupport() const
    {
#if defined(CNA_WAYLAND_HAVE_RELATIVE_POINTER) && defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
        const WaylandGlobals& globals = host_.connection().GetGlobals();
        return globals.relativePointerManager != nullptr && globals.pointerConstraints != nullptr;
#else
        return false;
#endif
    }

    bool WaylandMouse::HasSystemCursors() const
    {
#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
        if (host_.connection().GetGlobals().cursorShapeManager != nullptr)
        {
            return true;
        }
#endif
        EnsureTheme();
        return theme_ != nullptr;
    }

    void WaylandMouse::Attach(wl_pointer* proxy, wl_seat* seat)
    {
        auto pointer = std::make_unique<SeatPointer>();
        pointer->proxy = proxy;
        pointer->seat = seat;
        wl_pointer_add_listener(proxy, &kListener, this);
        SeatPointer& added = *pointer;
        pointers_.push_back(std::move(pointer));
        // A pointer that appears while relative mode is on is locked like the others.
        if (relativeWindow_ != 0)
        {
            CreateLock(added);
        }
    }

    void WaylandMouse::Detach(wl_pointer* proxy)
    {
        const auto found = std::find_if(pointers_.begin(), pointers_.end(),
                                        [proxy](const auto& candidate) { return candidate->proxy == proxy; });
        if (found == pointers_.end())
        {
            return;
        }
        SeatPointer& pointer = **found;
        DestroyLock(pointer);
#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
        if (pointer.shapeDevice != nullptr)
        {
            wp_cursor_shape_device_v1_destroy(pointer.shapeDevice);
            pointer.shapeDevice = nullptr;
        }
#endif
        if (pointer.themeSurface != nullptr)
        {
            wl_surface_destroy(pointer.themeSurface);
            pointer.themeSurface = nullptr;
        }
        if (wl_pointer_get_version(proxy) >= WL_POINTER_RELEASE_SINCE_VERSION)
        {
            wl_pointer_release(proxy);
        }
        else
        {
            wl_pointer_destroy(proxy);
        }
        pointers_.erase(found);
        Update();
    }

    void WaylandMouse::ForgetWindow(const WindowId window)
    {
        if (relativeWindow_ == window)
        {
            // The lock must go before the surface it holds the pointer on.
            for (const auto& pointer : pointers_)
            {
                DestroyLock(*pointer);
            }
            relativeWindow_ = 0;
            relativeX_ = 0.0;
            relativeY_ = 0.0;
        }
        for (const auto& pointer : pointers_)
        {
            if (pointer->focus == window)
            {
                pointer->focus = 0;
                pointer->focusSurface = nullptr;
                pointer->buttons = 0;
            }
        }
        if (lastWindow_ == window)
        {
            lastWindow_ = 0;
        }
        Update();
    }

    void WaylandMouse::EmitFrame(SeatPointer& pointer)
    {
        const std::int32_t vertical = AxisTo120(pointer.axis[WL_POINTER_AXIS_VERTICAL_SCROLL]);
        const std::int32_t horizontal = AxisTo120(pointer.axis[WL_POINTER_AXIS_HORIZONTAL_SCROLL]);
        pointer.axis[0] = {};
        pointer.axis[1] = {};
        if ((vertical == 0 && horizontal == 0) || pointer.onFrame)
        {
            return;
        }
        // Wayland scrolls positive downward and rightward; the contract (as SDL and X11's buttons
        // 4-7) is positive away from the user and rightward.
        scroll120Y_ -= vertical;
        scroll120X_ += horizontal;
        MouseWheelEvent wheel;
        wheel.window = pointer.focus;
        wheel.x = static_cast<float>(horizontal) / 120.0f;
        wheel.y = static_cast<float>(-vertical) / 120.0f;
        if (host_.post) { host_.post(wheel); }
        Update();
    }

    void WaylandMouse::Update()
    {
        buttons_ = 0;
        for (const auto& pointer : pointers_)
        {
            buttons_ |= pointer->buttons;
        }
        snapshot_.window = lastWindow_;
        snapshot_.x = static_cast<int>(std::floor(lastX_));
        snapshot_.y = static_cast<int>(std::floor(lastY_));
        snapshot_.buttons = buttons_;
        // XNA units, 120 per notch (X11's D-6: once accumulated in notches, off by 120).
        snapshot_.scrollX = static_cast<int>(scroll120X_);
        snapshot_.scrollY = static_cast<int>(scroll120Y_);
    }

    MouseDelta WaylandMouse::ConsumeRelativeDelta()
    {
        if (relativeWindow_ == 0)
        {
            return {};
        }
        // Whole units out, the fraction kept for the next read: slow movement adds up rather than
        // truncating to nothing every frame.
        MouseDelta delta;
        delta.x = static_cast<int>(relativeX_);
        delta.y = static_cast<int>(relativeY_);
        relativeX_ -= delta.x;
        relativeY_ -= delta.y;
        return delta;
    }

    void WaylandMouse::SetPosition(const WindowId window, const int x, const int y)
    {
        // Wayland does not let a client move the pointer (D-19). The position is what the snapshot
        // reports until the pointer next moves, which is what a game that re-centres each frame
        // reads back.
        lastX_ = x;
        lastY_ = y;
        if (window != 0)
        {
            lastWindow_ = window;
        }
        Update();
#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
        // While locked, the compositor takes it as where to leave the pointer on unlock; the hint
        // is surface state and takes effect on the next commit.
        if (window != 0 && window == relativeWindow_)
        {
            bool hinted = false;
            for (const auto& pointer : pointers_)
            {
                if (pointer->locked != nullptr)
                {
                    zwp_locked_pointer_v1_set_cursor_position_hint(pointer->locked, wl_fixed_from_int(x),
                                                                   wl_fixed_from_int(y));
                    hinted = true;
                }
            }
            if (hinted && host_.commitSurface)
            {
                host_.commitSurface(window);
            }
        }
#endif
    }

    void WaylandMouse::SetCursorVisible(const bool visible)
    {
        cursorVisible_ = visible;
        for (const auto& pointer : pointers_)
        {
            if (pointer->focus != 0)
            {
                ApplyCursor(*pointer);
            }
        }
    }

    void WaylandMouse::SetCursor(const SystemCursor cursor)
    {
        if (!HasSystemCursors())
        {
            throw PlatformNotSupportedException(PlatformCapability::CursorShapes,
                                                "Wayland (no wp_cursor_shape_manager_v1 and no cursor theme)");
        }
        DestroyCustomCursor();
        systemCursor_ = cursor;
        for (const auto& pointer : pointers_)
        {
            if (pointer->focus != 0)
            {
                ApplyCursor(*pointer);
            }
        }
    }

    void WaylandMouse::SetCursor(const CursorImage& cursor)
    {
        if (cursor.width <= 0 || cursor.height <= 0 ||
            cursor.rgba.size() != static_cast<std::size_t>(cursor.width) * static_cast<std::size_t>(cursor.height) ||
            cursor.hotSpotX < 0 || cursor.hotSpotY < 0 || cursor.hotSpotX >= cursor.width ||
            cursor.hotSpotY >= cursor.height)
        {
            throw PlatformException("WaylandMouse::SetCursor", "the cursor image is malformed");
        }
        WaylandConnection& connection = host_.connection();
        const WaylandGlobals& globals = connection.GetGlobals();
        auto buffer = WaylandShmBuffer::Create(globals.shm, cursor.width, cursor.height, WL_SHM_FORMAT_ARGB8888);
        if (buffer == nullptr)
        {
            throw PlatformException("WaylandMouse::SetCursor", "no shared memory for the cursor image");
        }
        // 0xAABBGGRR in, premultiplied ARGB8888 (0xAARRGGBB in the machine's word) out: wl_shm's
        // ARGB is premultiplied, and an unpremultiplied edge would show as a bright fringe.
        for (int y = 0; y < cursor.height; ++y)
        {
            auto* row = reinterpret_cast<std::uint32_t*>(buffer->GetPixels() + static_cast<std::size_t>(y) *
                                                                                 static_cast<std::size_t>(buffer->GetStride()));
            for (int x = 0; x < cursor.width; ++x)
            {
                const std::uint32_t pixel = cursor.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(cursor.width) +
                                                        static_cast<std::size_t>(x)];
                const std::uint32_t alpha = pixel >> 24;
                const std::uint32_t red = (pixel & 0xFFu) * alpha / 255u;
                const std::uint32_t green = ((pixel >> 8) & 0xFFu) * alpha / 255u;
                const std::uint32_t blue = ((pixel >> 16) & 0xFFu) * alpha / 255u;
                row[x] = (alpha << 24) | (red << 16) | (green << 8) | blue;
            }
        }
        DestroyCustomCursor();
        customCursorSurface_ = wl_compositor_create_surface(globals.compositor);
        wl_surface_attach(customCursorSurface_, buffer->GetBuffer(), 0, 0);
        if (globals.compositorVersion >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION)
        {
            wl_surface_damage_buffer(customCursorSurface_, 0, 0, cursor.width, cursor.height);
        }
        else
        {
            wl_surface_damage(customCursorSurface_, 0, 0, cursor.width, cursor.height);
        }
        wl_surface_commit(customCursorSurface_);
        buffer->MarkAttached();
        customCursorBuffer_ = std::move(buffer);
        customHotSpotX_ = cursor.hotSpotX;
        customHotSpotY_ = cursor.hotSpotY;
        for (const auto& pointer : pointers_)
        {
            if (pointer->focus != 0)
            {
                ApplyCursor(*pointer);
            }
        }
        connection.Flush();
    }

    void WaylandMouse::DestroyCustomCursor()
    {
        if (customCursorSurface_ != nullptr)
        {
            wl_surface_destroy(customCursorSurface_);
            customCursorSurface_ = nullptr;
        }
        customCursorBuffer_.reset();
    }

    void WaylandMouse::ApplyCursor(SeatPointer& pointer)
    {
        if (pointer.enterSerial == 0)
        {
            return;
        }
        WaylandConnection& connection = host_.connection();
        const bool hidden = !cursorVisible_
#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
                            || pointer.locked != nullptr
#endif
            ;
        if (hidden)
        {
            // A null surface hides the cursor over this client's surfaces.
            wl_pointer_set_cursor(pointer.proxy, pointer.enterSerial, nullptr, 0, 0);
            connection.Flush();
            return;
        }
        if (customCursorSurface_ != nullptr)
        {
            wl_pointer_set_cursor(pointer.proxy, pointer.enterSerial, customCursorSurface_, customHotSpotX_,
                                  customHotSpotY_);
            connection.Flush();
            return;
        }
#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
        const WaylandGlobals& globals = connection.GetGlobals();
        if (globals.cursorShapeManager != nullptr)
        {
            if (pointer.shapeDevice == nullptr)
            {
                pointer.shapeDevice = wp_cursor_shape_manager_v1_get_pointer(globals.cursorShapeManager, pointer.proxy);
            }
            wp_cursor_shape_device_v1_set_shape(pointer.shapeDevice, pointer.enterSerial, ShapeOf(systemCursor_));
            connection.Flush();
            return;
        }
#endif
        EnsureTheme();
        WaylandCursorImage image;
        if (theme_ != nullptr && theme_->Get(systemCursor_, image))
        {
            if (pointer.themeSurface == nullptr)
            {
                pointer.themeSurface = wl_compositor_create_surface(connection.GetGlobals().compositor);
            }
            wl_surface_attach(pointer.themeSurface, image.buffer, 0, 0);
            wl_surface_damage(pointer.themeSurface, 0, 0, image.width, image.height);
            wl_surface_commit(pointer.themeSurface);
            wl_pointer_set_cursor(pointer.proxy, pointer.enterSerial, pointer.themeSurface, image.hotSpotX,
                                  image.hotSpotY);
            connection.Flush();
        }
    }

    void WaylandMouse::ShowShapeFor(wl_pointer* proxy, const std::uint32_t serial, const SystemCursor cursor)
    {
        SeatPointer* pointer = Find(proxy);
        if (pointer == nullptr || serial == 0)
        {
            return;
        }
        WaylandConnection& connection = host_.connection();
#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
        const WaylandGlobals& globals = connection.GetGlobals();
        if (globals.cursorShapeManager != nullptr)
        {
            if (pointer->shapeDevice == nullptr)
            {
                pointer->shapeDevice = wp_cursor_shape_manager_v1_get_pointer(globals.cursorShapeManager, pointer->proxy);
            }
            wp_cursor_shape_device_v1_set_shape(pointer->shapeDevice, serial, ShapeOf(cursor));
            connection.Flush();
            return;
        }
#endif
        EnsureTheme();
        WaylandCursorImage image;
        if (theme_ != nullptr && theme_->Get(cursor, image))
        {
            if (pointer->themeSurface == nullptr)
            {
                pointer->themeSurface = wl_compositor_create_surface(connection.GetGlobals().compositor);
            }
            wl_surface_attach(pointer->themeSurface, image.buffer, 0, 0);
            wl_surface_damage(pointer->themeSurface, 0, 0, image.width, image.height);
            wl_surface_commit(pointer->themeSurface);
            wl_pointer_set_cursor(pointer->proxy, serial, pointer->themeSurface, image.hotSpotX, image.hotSpotY);
            connection.Flush();
        }
    }

    void WaylandMouse::SetRelativeMode(const WindowId window, const bool enabled)
    {
        if (!HasRelativeSupport())
        {
            throw PlatformNotSupportedException(
                PlatformCapability::RelativeMouse,
                "Wayland (the compositor offers no zwp_relative_pointer_manager_v1 and zwp_pointer_constraints_v1)");
        }
        if (!enabled)
        {
            if (relativeWindow_ == 0)
            {
                return;
            }
            for (const auto& pointer : pointers_)
            {
                DestroyLock(*pointer);
                if (pointer->focus != 0)
                {
                    ApplyCursor(*pointer);
                }
            }
            relativeWindow_ = 0;
            relativeX_ = 0.0;
            relativeY_ = 0.0;
            host_.connection().Flush();
            return;
        }
        if (window == 0)
        {
            return;
        }
        if (relativeWindow_ == window)
        {
            return;
        }
        if (relativeWindow_ != 0)
        {
            for (const auto& pointer : pointers_)
            {
                DestroyLock(*pointer);
            }
        }
        relativeWindow_ = window;
        relativeX_ = 0.0;
        relativeY_ = 0.0;
        for (const auto& pointer : pointers_)
        {
            CreateLock(*pointer);
            if (pointer->focus != 0)
            {
                ApplyCursor(*pointer);
            }
        }
        host_.connection().Flush();
    }

    void WaylandMouse::CreateLock(SeatPointer& pointer)
    {
#if defined(CNA_WAYLAND_HAVE_RELATIVE_POINTER) && defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
        const WaylandGlobals& globals = host_.connection().GetGlobals();
        wl_surface* surface = host_.surfaceOf ? host_.surfaceOf(relativeWindow_) : nullptr;
        if (surface == nullptr || globals.relativePointerManager == nullptr || globals.pointerConstraints == nullptr)
        {
            return;
        }
        if (pointer.relative == nullptr)
        {
            static const zwp_relative_pointer_v1_listener relativeListener = {
                .relative_motion = [](void* data, zwp_relative_pointer_v1* relative, std::uint32_t, std::uint32_t,
                                      wl_fixed_t, wl_fixed_t, const wl_fixed_t dxUnaccelerated,
                                      const wl_fixed_t dyUnaccelerated) {
                    auto* self = static_cast<WaylandMouse*>(data);
                    if (self->relativeWindow_ == 0)
                    {
                        return;
                    }
                    SeatPointer* owner = nullptr;
                    for (const auto& candidate : self->pointers_)
                    {
                        if (candidate->relative == relative)
                        {
                            owner = candidate.get();
                        }
                    }
                    // Only motion over the locked window counts: the relative pointer reports the
                    // device's motion wherever the pointer is, and a game must not turn while the
                    // user moves the pointer over another window.
                    if (owner == nullptr || owner->focus != self->relativeWindow_)
                    {
                        return;
                    }
                    // Unaccelerated, as the X11 backend's XInput2 raw motion is (D-18).
                    const double dx = wl_fixed_to_double(dxUnaccelerated);
                    const double dy = wl_fixed_to_double(dyUnaccelerated);
                    self->relativeX_ += dx;
                    self->relativeY_ += dy;
                    MouseMotionEvent motion;
                    motion.window = self->relativeWindow_;
                    motion.x = static_cast<float>(owner->x);
                    motion.y = static_cast<float>(owner->y);
                    motion.deltaX = static_cast<float>(dx);
                    motion.deltaY = static_cast<float>(dy);
                    if (self->host_.post) { self->host_.post(motion); }
                },
            };
            pointer.relative =
                zwp_relative_pointer_manager_v1_get_relative_pointer(globals.relativePointerManager, pointer.proxy);
            zwp_relative_pointer_v1_add_listener(pointer.relative, &relativeListener, this);
        }
        if (pointer.locked == nullptr)
        {
            static const zwp_locked_pointer_v1_listener lockListener = {
                .locked = [](void* data, zwp_locked_pointer_v1* locked) {
                    auto* self = static_cast<WaylandMouse*>(data);
                    for (const auto& candidate : self->pointers_)
                    {
                        if (candidate->locked == locked) { candidate->lockActive = true; }
                    }
                },
                .unlocked = [](void* data, zwp_locked_pointer_v1* locked) {
                    // The compositor deactivated the lock -- focus went elsewhere. A persistent
                    // lock comes back by itself when the window has focus again.
                    auto* self = static_cast<WaylandMouse*>(data);
                    for (const auto& candidate : self->pointers_)
                    {
                        if (candidate->locked == locked) { candidate->lockActive = false; }
                    }
                },
            };
            pointer.locked = zwp_pointer_constraints_v1_lock_pointer(globals.pointerConstraints, surface, pointer.proxy,
                                                                     nullptr,
                                                                     ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
            zwp_locked_pointer_v1_add_listener(pointer.locked, &lockListener, this);
            pointer.lockActive = false;
        }
#else
        (void) pointer;
#endif
    }

    void WaylandMouse::DestroyLock(SeatPointer& pointer)
    {
#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
        if (pointer.locked != nullptr)
        {
            zwp_locked_pointer_v1_destroy(pointer.locked);
            pointer.locked = nullptr;
            pointer.lockActive = false;
        }
#endif
#if defined(CNA_WAYLAND_HAVE_RELATIVE_POINTER)
        if (pointer.relative != nullptr)
        {
            zwp_relative_pointer_v1_destroy(pointer.relative);
            pointer.relative = nullptr;
        }
#endif
        (void) pointer;
    }

    bool WaylandMouse::IsLockActive() const
    {
#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
        for (const auto& pointer : pointers_)
        {
            if (pointer->lockActive)
            {
                return true;
            }
        }
#endif
        return false;
    }

    bool WaylandMouse::SetCapture(const bool enabled)
    {
        (void) enabled;
        throw PlatformNotSupportedException(PlatformCapability::GlobalPointer,
                                            "Wayland (clients are not given the pointer outside their windows)");
    }

    bool WaylandMouse::TryGetGlobalPosition(float& x, float& y) const
    {
        (void) x;
        (void) y;
        throw PlatformNotSupportedException(PlatformCapability::GlobalPointer,
                                            "Wayland (clients are not given desktop coordinates)");
    }

    bool WaylandMouse::SetGlobalPosition(const float x, const float y)
    {
        (void) x;
        (void) y;
        throw PlatformNotSupportedException(PlatformCapability::GlobalPointer,
                                            "Wayland (clients cannot move the pointer)");
    }

} // namespace CNA::Platform::Wayland
