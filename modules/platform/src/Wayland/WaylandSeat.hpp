// SPDX-License-Identifier: MS-PL
#pragma once

#include "WaylandProtocols.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace CNA::Platform::Wayland {

    /**
     * @brief One `wl_seat` (plans/plan_wayland.md WAYLAND-0050).
     *
     * A seat's capabilities change at run time -- a keyboard plugged in, a tablet mode switch
     * taking the pointer away -- and a compositor may offer several seats, or add and remove them.
     * This object owns only the `wl_seat`; the keyboard, pointer and touch proxies it creates are
     * handed to the input services, which own and release them, and taken back from them when the
     * capability goes away.
     */
    class WaylandSeat
    {
    public:
        /** @brief Where the seat's devices go. */
        struct Devices
        {
            /** @brief A keyboard appeared; the receiver owns it. */
            std::function<void(wl_keyboard*, wl_seat*)> keyboardAdded;
            /** @brief The seat's keyboard went away; the receiver releases it. */
            std::function<void(wl_keyboard*)> keyboardRemoved;
            /** @brief A pointer appeared. */
            std::function<void(wl_pointer*, wl_seat*)> pointerAdded;
            /** @brief The pointer went away. */
            std::function<void(wl_pointer*)> pointerRemoved;
            /** @brief A touch device appeared. */
            std::function<void(wl_touch*, wl_seat*)> touchAdded;
            /** @brief The touch device went away. */
            std::function<void(wl_touch*)> touchRemoved;
            /** @brief The seat now exists (for per-seat objects: data devices, text input). */
            std::function<void(WaylandSeat&)> seatReady;
        };

        /**
         * @brief Binds the seat.
         * @param registry The registry.
         * @param name The global's name.
         * @param version The negotiated version.
         * @param devices Where its devices go.
         */
        WaylandSeat(wl_registry* registry, std::uint32_t name, std::uint32_t version, Devices devices);

        /** @brief Hands every device back for release, then releases the seat. */
        ~WaylandSeat();

        WaylandSeat(const WaylandSeat&) = delete;
        WaylandSeat& operator=(const WaylandSeat&) = delete;

        /** @brief Gets the global name. @return The name. */
        [[nodiscard]] std::uint32_t GetGlobalName() const { return globalName_; }
        /** @brief Gets the proxy. @return The `wl_seat`. */
        [[nodiscard]] wl_seat* GetProxy() const { return seat_; }
        /** @brief Gets the seat's name (`seat0`). @return The name, or empty before v2. */
        [[nodiscard]] const std::string& GetName() const { return name_; }
        /** @brief Gets the capabilities. @return `wl_seat_capability` bits. */
        [[nodiscard]] std::uint32_t GetCapabilities() const { return capabilities_; }
        /** @brief Gets the keyboard, if any. @return The proxy or null. */
        [[nodiscard]] wl_keyboard* GetKeyboard() const { return keyboard_; }
        /** @brief Gets the pointer, if any. @return The proxy or null. */
        [[nodiscard]] wl_pointer* GetPointer() const { return pointer_; }

    private:
        static const wl_seat_listener kListener;

        void OnCapabilities(std::uint32_t capabilities);

        std::uint32_t globalName_ = 0;
        wl_seat* seat_ = nullptr;
        std::string name_;
        std::uint32_t capabilities_ = 0;
        wl_keyboard* keyboard_ = nullptr;
        wl_pointer* pointer_ = nullptr;
        wl_touch* touch_ = nullptr;
        Devices devices_;
        bool announced_ = false;
    };

} // namespace CNA::Platform::Wayland
