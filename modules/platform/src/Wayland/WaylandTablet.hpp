// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

#include "WaylandProtocols.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Platform::Wayland {

    /**
     * @brief Graphics tablets through `zwp_tablet_v2` (plans/plan_wayland.md WAYLAND-0059).
     *
     * The contract has no tablet service, and the X11 backend already decided what a pen is in
     * CNA's terms (X11-0155): **a pen whose tip is down is a touch with the pressure it reports,
     * and a hovering pen is nothing.** This does the same with the tablet protocol, so a game that
     * reads `TouchPanel` sees a pen under both backends.
     *
     * Each tool gets a stable finger id (the seat's global name in the upper half, so two seats'
     * tablets never share one), and each stroke is `Down`, `Motion` with its delta, `Up`; a tool
     * that leaves proximity or a tablet that is unplugged mid-stroke `Cancelled`s it, so no contact
     * stays down. A tool's events arrive in frames, as the protocol requires: position, pressure
     * and tip state are applied together when `frame` arrives.
     *
     * Not delivered, deliberately: tilt, rotation, slider, the barrel buttons and the pad -- the
     * contract carries none of them, and inventing events for them would be a private API. An
     * eraser touching is a touch like the tip, as under X11.
     */
    class WaylandTablet
    {
    public:
        /** @brief What the tablets need from the platform. */
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

        /** @brief One tool (a pen, an eraser, an airbrush) of one tablet seat. */
        struct Tool;

        /**
         * @brief Creates the service.
         * @param host The platform.
         */
        explicit WaylandTablet(Host host);

        /** @brief Releases every tablet seat and tool; strokes in progress are cancelled. */
        ~WaylandTablet();

        WaylandTablet(const WaylandTablet&) = delete;
        WaylandTablet& operator=(const WaylandTablet&) = delete;

        /**
         * @brief Asks a seat's tablets for their tools, where the compositor offers the protocol.
         * @param manager The `zwp_tablet_manager_v2`, or null.
         * @param seat The seat.
         * @param seatName The seat's global name, the upper half of its tools' ids.
         */
        void AttachSeat(void* manager, wl_seat* seat, std::uint32_t seatName);

        /**
         * @brief Releases a seat's tablet objects; strokes in progress are cancelled.
         * @param seat The seat.
         */
        void DetachSeat(wl_seat* seat);

        /**
         * @brief Forgets a window; a stroke on it is cancelled.
         * @param window The window.
         */
        void ForgetWindow(WindowId window);

        /** @brief Gets whether any tool is known. @return True when one is. */
        [[nodiscard]] bool HasTablet() const { return !tools_.empty(); }

        /** @brief Gets the names of the tools known, for input-device enumeration. */
        [[nodiscard]] std::vector<std::string> GetToolNames() const;

    private:
        struct Seat;

        void Cancel(Tool& tool);

        Host host_;
        std::vector<std::unique_ptr<Seat>> seats_;
        std::vector<std::unique_ptr<Tool>> tools_;
    };

} // namespace CNA::Platform::Wayland
