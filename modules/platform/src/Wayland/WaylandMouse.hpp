// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/PlatformEvent.hpp"

#include "WaylandProtocols.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace CNA::Platform::Wayland {

    class WaylandConnection;
    class WaylandShmBuffer;
    class WaylandCursorTheme;

    /**
     * @brief The contract's button number for a Linux input button code (`BTN_*`).
     *
     * 1 left, 2 middle, 3 right, 4 and 5 the side buttons (X1, X2) -- the numbering every CNA
     * backend uses. Back and forward are the side buttons under another name. Anything else (a
     * gaming mouse's thumb grid) has no contract button and is 0.
     *
     * @param button The `wl_pointer.button` code.
     * @return The button, or 0.
     */
    [[nodiscard]] std::uint8_t ButtonFromLinuxCode(std::uint32_t button);

    /** @brief One frame's scroll on one axis, in the forms a `wl_pointer` may have sent it. */
    struct AxisFrame
    {
        /** @brief The continuous value, in surface units (10 per wheel notch by convention). */
        double value = 0.0;
        /** @brief `axis_value120` (v8): exactly 120 per notch, fractions for high-resolution wheels. */
        std::int32_t value120 = 0;
        /** @brief `axis_discrete` (v5-v7): whole notches. */
        std::int32_t discrete = 0;
        /** @brief Whether a continuous value arrived. */
        bool hasValue = false;
        /** @brief Whether a value120 arrived. */
        bool hasValue120 = false;
        /** @brief Whether a discrete step arrived. */
        bool hasDiscrete = false;
    };

    /**
     * @brief Converts one frame's scroll on one axis to 120ths of a notch (D-17).
     *
     * value120 when there is one, discrete steps when there are, else the continuous value at the
     * 10-units-per-notch convention libinput and every compositor use -- so a touchpad's smooth
     * scroll becomes fractional notches rather than nothing.
     *
     * @param frame The axis frame.
     * @return The scroll in 120ths, in Wayland's direction (positive down or right).
     */
    [[nodiscard]] std::int32_t AxisTo120(const AxisFrame& frame);

    /**
     * @brief The pointer: position, buttons, wheel, relative motion and the cursor
     * (plans/plan_wayland.md WAYLAND-0055..0057).
     *
     * ### What is refused, and why
     *
     * Wayland gives an ordinary client neither the pointer's position outside its own surfaces
     * nor the power to move it (D-19): `SetCapture`, `TryGetGlobalPosition` and
     * `SetGlobalPosition` refuse naming `GlobalPointer`. `SetPosition` records the position the
     * snapshot reports and, while the pointer is locked, hands it to the compositor as the place
     * to leave the pointer on unlock -- the one form of warping the protocol has.
     *
     * ### Relative mode (D-18)
     *
     * `zwp_relative_pointer_v1` for motion -- unaccelerated, with the fractions carried between
     * reads so slow movement is not lost -- and a persistent `zwp_locked_pointer_v1` on the
     * window, so the pointer neither moves nor leaves while the game reads it. The lock belongs to
     * the compositor while it is active and to this object always: it is destroyed on disable, on
     * the window's destruction and on the pointer's removal, before its surface, so the desktop
     * can never be left trapped.
     */
    class WaylandMouse final : public IPlatformMouse
    {
    public:
        /** @brief What the pointer needs from the platform. */
        struct Host
        {
            /** @brief Queues an event. */
            std::function<void(PlatformEvent)> post;
            /** @brief The window a surface belongs to, or 0. */
            std::function<WindowId(wl_surface*)> resolveSurface;
            /** @brief A window's content surface, or null. */
            std::function<wl_surface*(WindowId)> surfaceOf;
            /** @brief Records an input serial. */
            std::function<void(wl_seat*, std::uint32_t)> recordSerial;
            /** @brief Commits a window's surface state (a position hint takes effect on commit). */
            std::function<void(WindowId)> commitSurface;
            /** @brief The connection, for the globals. */
            std::function<WaylandConnection&()> connection;
            /**
             * @brief Offers a pointer event on a surface that is not a window's content (the
             * built-in title bar); true when it was consumed.
             */
            std::function<bool(wl_surface*, int kind, double x, double y, std::uint32_t button, bool pressed,
                               std::uint32_t serial, wl_pointer* pointer)>
                frameEvent;
        };

        /** @brief One seat's pointer. */
        struct SeatPointer;

        /**
         * @brief Creates the service.
         * @param host The platform.
         */
        explicit WaylandMouse(Host host);

        /** @brief Releases every seat pointer, lock and cursor surface. */
        ~WaylandMouse() override;

        WaylandMouse(const WaylandMouse&) = delete;
        WaylandMouse& operator=(const WaylandMouse&) = delete;

        /**
         * @brief Starts listening to a seat's new `wl_pointer`.
         * @param pointer The proxy.
         * @param seat Its seat.
         */
        void Attach(wl_pointer* pointer, wl_seat* seat);

        /**
         * @brief Stops listening and releases the proxy, its lock and cursor device.
         * @param pointer The proxy.
         */
        void Detach(wl_pointer* pointer);

        /**
         * @brief Forgets a window before its surface is destroyed: its lock goes first.
         * @param window The window.
         */
        void ForgetWindow(WindowId window);

        /**
         * @brief Gets whether relative mode can work: both protocols bound.
         * @return True with relative-pointer and pointer-constraints.
         */
        [[nodiscard]] bool HasRelativeSupport() const;

        /**
         * @brief Gets whether a cursor theme or the cursor-shape protocol can show system shapes.
         * @return True when SetCursor(SystemCursor) can show something.
         */
        [[nodiscard]] bool HasSystemCursors() const;

        /** @brief Copies the per-seat state into the snapshot. */
        void Update() override;
        /** @brief Gets the snapshot. @return The state as of the last Update(). */
        [[nodiscard]] const MouseSnapshot& GetSnapshot() const override { return snapshot_; }
        /** @brief Returns and clears the relative motion accumulated in relative mode. @return The motion. */
        [[nodiscard]] MouseDelta ConsumeRelativeDelta() override;
        /**
         * @brief Records a position, and hints it to the compositor while locked (D-19).
         * @param window The window, or 0 to record only.
         * @param x Client x.
         * @param y Client y.
         */
        void SetPosition(WindowId window, int x, int y) override;
        /** @brief Shows or hides the cursor over this client's windows. @param visible Visibility. */
        void SetCursorVisible(bool visible) override;
        /** @brief Sets a system cursor shape. @param cursor The shape. */
        void SetCursor(SystemCursor cursor) override;
        /**
         * @brief Sets a custom cursor image.
         * @param cursor The image.
         * @throws PlatformException For a malformed image or when shared memory is unavailable.
         */
        void SetCursor(const CursorImage& cursor) override;
        /**
         * @brief Enters or leaves relative mode (D-18).
         * @param window The window whose surface is locked.
         * @param enabled Whether to lock.
         * @throws PlatformNotSupportedException Without both protocols.
         */
        void SetRelativeMode(WindowId window, bool enabled) override;
        /** @brief Gets whether relative mode is on. @return True while enabled. */
        [[nodiscard]] bool IsRelativeMode() const override { return relativeWindow_ != 0; }
        /** @brief Refuses: Wayland has no global pointer (D-19). @param enabled Ignored. @return Never. */
        bool SetCapture(bool enabled) override;
        /** @brief Refuses (D-19). @param x Untouched. @param y Untouched. @return Never. */
        [[nodiscard]] bool TryGetGlobalPosition(float& x, float& y) const override;
        /** @brief Refuses (D-19). @param x Ignored. @param y Ignored. @return Never. */
        bool SetGlobalPosition(float x, float y) override;

        /**
         * @brief Shows a cursor shape for one pointer, whatever the application's own cursor state
         * (the built-in frame's resize arrows).
         * @param pointer The pointer.
         * @param serial Its enter serial.
         * @param cursor The shape.
         */
        void ShowShapeFor(wl_pointer* pointer, std::uint32_t serial, SystemCursor cursor);

        /**
         * @brief Destroys everything made from the connection -- the custom cursor's surface and
         * buffer, the cursor theme's buffers -- before the connection closes. Called by the
         * platform as it disconnects; nothing of the connection is used afterwards.
         */
        void ReleaseConnectionObjects();

        /** @brief Gets whether a lock is currently active (the compositor confirmed it). @return True if locked. */
        [[nodiscard]] bool IsLockActive() const;

    private:
        static const wl_pointer_listener kListener;

        SeatPointer* Find(wl_pointer* pointer) const;
        void EnsureTheme() const;
        void ApplyCursor(SeatPointer& pointer);
        void CreateLock(SeatPointer& pointer);
        void DestroyLock(SeatPointer& pointer);
        void DestroyCustomCursor();
        void EmitFrame(SeatPointer& pointer);

        Host host_;
        std::vector<std::unique_ptr<SeatPointer>> pointers_;
        MouseSnapshot snapshot_;
        std::int64_t scroll120X_ = 0;
        std::int64_t scroll120Y_ = 0;
        std::uint8_t buttons_ = 0;
        WindowId lastWindow_ = 0;
        double lastX_ = 0.0;
        double lastY_ = 0.0;

        WindowId relativeWindow_ = 0;
        double relativeX_ = 0.0;
        double relativeY_ = 0.0;

        bool cursorVisible_ = true;
        SystemCursor systemCursor_ = SystemCursor::Arrow;
        wl_surface* customCursorSurface_ = nullptr;
        std::unique_ptr<WaylandShmBuffer> customCursorBuffer_;
        int customHotSpotX_ = 0;
        int customHotSpotY_ = 0;
        mutable std::unique_ptr<WaylandCursorTheme> theme_;
        mutable bool themeTried_ = false;

        // Double-click detection: Wayland, like X11, leaves it to the client.
        std::uint32_t lastClickTime_ = 0;
        std::uint8_t lastClickButton_ = 0;
        double lastClickX_ = 0.0;
        double lastClickY_ = 0.0;
        std::uint8_t clickCount_ = 0;
    };

} // namespace CNA::Platform::Wayland
