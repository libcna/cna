// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

#include "WaylandProtocols.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace CNA::Platform::Wayland {

    /**
     * @brief Touch points from `wl_touch` (plans/plan_wayland.md WAYLAND-0058).
     *
     * A finger's id is the compositor's for as long as the finger is down, and is reported with
     * the seat's global name in its upper half so two seats' touchscreens never share an id.
     * Coordinates are normalised to the window's logical client size, as the contract's touch
     * events are everywhere. Wayland reports no pressure; a finger that is down reports 1.
     */
    class WaylandTouch
    {
    public:
        /** @brief What touch needs from the platform. */
        struct Host
        {
            /** @brief Queues an event. */
            std::function<void(PlatformEvent)> post;
            /** @brief The window a surface belongs to, or 0. */
            std::function<WindowId(wl_surface*)> resolveSurface;
            /** @brief A window's logical client size. */
            std::function<void(WindowId, int&, int&)> clientSize;
            /** @brief Records an input serial. */
            std::function<void(wl_seat*, std::uint32_t)> recordSerial;
        };

        /**
         * @brief Creates the service.
         * @param host The platform.
         */
        explicit WaylandTouch(Host host) : host_(std::move(host)) {}

        /** @brief Releases every touch device. */
        ~WaylandTouch();

        WaylandTouch(const WaylandTouch&) = delete;
        WaylandTouch& operator=(const WaylandTouch&) = delete;

        /**
         * @brief Starts listening to a seat's `wl_touch`.
         * @param touch The proxy.
         * @param seat Its seat.
         * @param seatName The seat's global name, the upper half of its fingers' ids.
         */
        void Attach(wl_touch* touch, wl_seat* seat, std::uint32_t seatName);

        /**
         * @brief Releases a touch device; its fingers still down are cancelled.
         * @param touch The proxy.
         */
        void Detach(wl_touch* touch);

        /**
         * @brief Forgets a window; its fingers are cancelled.
         * @param window The window.
         */
        void ForgetWindow(WindowId window);

        /** @brief Gets whether any touch device is attached. @return True when one is. */
        [[nodiscard]] bool HasTouch() const { return !devices_.empty(); }

    private:
        struct Finger
        {
            std::int32_t id = 0;
            WindowId window = 0;
            float x = 0.0f;
            float y = 0.0f;
            int width = 1;
            int height = 1;
        };

        struct Device
        {
            wl_touch* proxy = nullptr;
            wl_seat* seat = nullptr;
            std::uint32_t seatName = 0;
            std::vector<Finger> fingers;
        };

        static const wl_touch_listener kListener;

        Device* Find(wl_touch* touch);
        void Emit(const Device& device, const Finger& finger, TouchEventKind kind, float deltaX, float deltaY);
        void CancelAll(Device& device);

        Host host_;
        std::vector<Device> devices_;
    };

} // namespace CNA::Platform::Wayland
