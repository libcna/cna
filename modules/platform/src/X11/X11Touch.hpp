// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"
#include "X11Headers.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace CNA::Platform::X11 {

    class X11Connection;
    class X11Mouse;
    class X11Window;

    /**
     * @brief A touch contact's identity across devices: the XI2 touch id is unique only within
     * its source device, so the device goes into the upper half.
     *
     * @param sourceDevice The XI2 slave device the touch came from.
     * @param touchId The XI2 touch id (the event's `detail`).
     * @return The contract's finger id.
     */
    [[nodiscard]] constexpr std::uint64_t MakeFingerId(const int sourceDevice,
                                                       const int touchId) noexcept
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(sourceDevice)) << 32) |
               static_cast<std::uint32_t>(touchId);
    }

    /**
     * @brief Builds the contract's touch event from a position in window coordinates.
     *
     * The position is normalised against the client size -- the same size the event carries and
     * the input bridge multiplies by again, so the round trip is exact.
     *
     * @param window The window.
     * @param finger The finger id.
     * @param kind What happened.
     * @param x The position in window coordinates.
     * @param y The position in window coordinates.
     * @param clientWidth The window's client width.
     * @param clientHeight The window's client height.
     * @param previous The finger's previous normalised position, for a motion's delta.
     * @return The event; pressure is 1 for every step, as SDL3's X11 backend reports it.
     */
    [[nodiscard]] TouchEvent MakeTouchEvent(WindowId window, std::uint64_t finger,
                                            TouchEventKind kind, double x, double y,
                                            int clientWidth, int clientHeight,
                                            std::optional<std::pair<float, float>> previous);

    /**
     * @brief A pressure reading as the contract's normalised pressure.
     *
     * @param value The valuator's value.
     * @param minimum The valuator's minimum.
     * @param maximum The valuator's maximum.
     * @return The pressure in [0, 1]; 1 for a valuator with no range to normalise against.
     */
    [[nodiscard]] float NormalisePressure(double value, double minimum, double maximum) noexcept;

    /**
     * @brief Touchscreen contacts through XInput 2.2 touch events.
     *
     * Every window CNA creates selects `XI_TouchBegin`/`Update`/`End` from the master devices
     * when the server speaks XInput 2.2. Each contact becomes the contract's `TouchEvent` -- down,
     * motion with its delta, up -- in the window it began in (the server grabs a touch for the
     * window it started on). A window that goes away mid-touch has its contacts cancelled on the
     * next pump, so `TouchPanel` is not left holding a finger that will never lift.
     *
     * A window that selects touch events no longer receives the pointer events the server
     * emulates from touches, so the contact the server marks as emulating the pointer drives the
     * mouse here instead -- a press, motion and a release of the left button, and the mouse
     * snapshot with them -- which is what keeps a mouse-driven XNA game working on a touchscreen,
     * as SDL3 does by synthesising mouse events from touches.
     *
     * ### Pens
     *
     * A pen -- SDL3's rule: an enabled slave pointer with an "Abs Pressure" valuator -- is a touch
     * while its tip is down: `Down` when it touches, `Motion` while it moves touching, `Up` when it
     * lifts, with the pressure it reports. That is what SDL3 does by default (its pen touch events),
     * and it is what makes `TouchPanel` work with a stylus. A hovering pen is not a touch. The
     * pen's own XI2 events are selected per device, so the core pointer events it drives -- the
     * mouse -- reach the window as they always did; pens are found at start-up and again whenever
     * the device hierarchy changes.
     */
    class X11Touch
    {
    public:
        /**
         * @brief Creates the service for one connection.
         *
         * @param connection The connection.
         * @param mouse The mouse the pointer-emulating contact drives, or null.
         */
        X11Touch(X11Connection& connection, X11Mouse* mouse);

        /**
         * @brief Makes a window receive touch and pen events.
         *
         * @param window A window this platform created.
         */
        void AttachWindow(const X11Window& window);

        /**
         * @brief Finds the pens again -- at start-up, and after `XI_HierarchyChanged` -- and
         * selects their events on every attached window.
         */
        void RefreshPens();

        /**
         * @brief Translates one XI2 pointer event, if it is a known pen's.
         *
         * @param event The event, its data fetched.
         * @param window The window it was delivered to.
         * @param destination Receives the events.
         * @return True when the event was a pen's.
         */
        bool HandlePenEvent(const XIDeviceEvent& event, X11Window& window,
                            std::vector<PlatformEvent>& destination);

        /**
         * @brief Translates one XI2 touch event.
         *
         * @param event The event, its data fetched.
         * @param window The window it was delivered to.
         * @param destination Receives the events.
         */
        void HandleEvent(const XIDeviceEvent& event, X11Window& window,
                         std::vector<PlatformEvent>& destination);

        /**
         * @brief Cancels the contacts of a window that is going away, on the next pump.
         *
         * @param window The window's id.
         * @param xid The window's XID, or zero when the server has already destroyed it.
         */
        void ForgetWindow(WindowId window, ::Window xid);

        /**
         * @brief Delivers the cancellations queued by @ref ForgetWindow.
         *
         * @param destination Receives the events.
         */
        void TakePendingEvents(std::vector<PlatformEvent>& destination);

    private:
        struct Contact
        {
            WindowId window = 0;
            float x = 0.0f;
            float y = 0.0f;
            int clientWidth = 1;
            int clientHeight = 1;
            float pressure = 1.0f;
            bool emulating = false;
        };

        struct Pen
        {
            int pressureValuator = -1;
            double pressureMinimum = 0.0;
            double pressureMaximum = 0.0;
            float pressure = 0.0f;
            bool touching = false;
        };

        void SelectPens(::Window window) const;
        void EmitContact(std::uint64_t finger, TouchEventKind kind, double x, double y,
                         float pressure, bool emulating, X11Window& window,
                         std::vector<PlatformEvent>& destination);

        X11Connection& connection_;
        X11Mouse* mouse_ = nullptr;
        std::map<std::uint64_t, Contact> contacts_;
        std::vector<PlatformEvent> pending_;
        std::vector<::Window> windows_;
        std::map<int, Pen> pens_;
    };

} // namespace CNA::Platform::X11
