// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformTextInput.hpp"

#include "WaylandProtocols.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Platform::Wayland {

    /**
     * @brief Counts the code points in the first `bytes` bytes of a UTF-8 string -- the
     * contract's composition cursor is in characters, text-input-v3's in bytes.
     * @param text The string.
     * @param bytes A byte offset into it (clamped to its length).
     * @return The number of code points before that offset.
     */
    [[nodiscard]] int CodePointsBefore(std::string_view text, std::int32_t bytes);

    /**
     * @brief Text entry and input-method composition (plans/plan_wayland.md WAYLAND-0053/0054).
     *
     * Committed text from the keyboard comes through xkbcommon (WaylandKeyboard, D-15) while text
     * input is started for the focused window, on every compositor. Where the compositor offers
     * `zwp_text_input_manager_v3`, each seat also gets a text-input object, enabled on a window's
     * surface while text input is started there: an input method then composes -- preedit as
     * `TextEditingEvent`, commit as `TextInputEvent` -- and the keys it consumes never reach the
     * client as keys, so nothing is typed twice. `delete_surrounding_text` has no counterpart in
     * the contract (the platform does not know the application's text) and is not applied; the
     * candidate list is the input method's own window, as under XIM (D-16).
     */
    class WaylandTextInput final : public IPlatformTextInput
    {
    public:
        /** @brief What text input needs from the platform. */
        struct Host
        {
            /** @brief Queues an event. */
            std::function<void(PlatformEvent)> post;
            /** @brief The window a surface belongs to, or 0. */
            std::function<WindowId(wl_surface*)> resolveSurface;
            /** @brief A window's content surface, or null. */
            std::function<wl_surface*(WindowId)> surfaceOf;
            /** @brief Whether a window exists. */
            std::function<bool(WindowId)> windowExists;
            /** @brief Flushes requests. */
            std::function<void()> flush;
        };

        /**
         * @brief Creates the service.
         * @param host The platform.
         */
        explicit WaylandTextInput(Host host);

        /** @brief Destroys every seat's text-input object. */
        ~WaylandTextInput() override;

        WaylandTextInput(const WaylandTextInput&) = delete;
        WaylandTextInput& operator=(const WaylandTextInput&) = delete;

        /**
         * @brief Gives a seat a text-input object (text-input-v3 only).
         * @param manager The `zwp_text_input_manager_v3`, or null.
         * @param seat The seat.
         */
        void AttachSeat(void* manager, wl_seat* seat);

        /**
         * @brief Destroys a seat's text-input object (the seat went away).
         * @param seat The seat.
         */
        void DetachSeat(wl_seat* seat);

        /**
         * @brief Forgets a window.
         * @param window The window.
         */
        void ForgetWindow(WindowId window);

        /** @brief Gets whether composition events can be delivered. @return True with text-input-v3 seats. */
        [[nodiscard]] bool HasInputMethod() const { return !seats_.empty(); }

        /**
         * @brief Starts text input for a window.
         * @param window The window.
         * @param type The kind of text.
         * @throws PlatformException For a window that does not exist.
         */
        void Start(WindowId window, TextInputType type) override;
        /** @brief Stops text input for a window. @param window The window. */
        void Stop(WindowId window) override;
        /** @brief Gets whether text input is started for a window. @param window The window. @return True if so. */
        [[nodiscard]] bool IsActive(WindowId window) const override;
        /**
         * @brief Gets whether an on-screen keyboard is shown: Wayland does not tell a client.
         * @param window The window.
         * @return False.
         */
        [[nodiscard]] bool IsScreenKeyboardShown(WindowId window) const override;
        /**
         * @brief Tells the input method where the caret is (its candidate window goes there).
         * @param window The window.
         * @param area The edited area.
         */
        void SetInputArea(WindowId window, const TextInputArea& area) override;

        /** @brief One seat's text-input object and the event group it is receiving. */
        struct SeatInput;

    private:
        void Enable(SeatInput& input);
        void Disable(SeatInput& input);

        Host host_;
        std::vector<std::unique_ptr<SeatInput>> seats_;
        std::map<WindowId, TextInputType> active_;
        std::map<WindowId, TextInputArea> areas_;
    };

} // namespace CNA::Platform::Wayland
