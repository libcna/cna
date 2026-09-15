// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/PlatformEvent.hpp"

#include "WaylandProtocols.hpp"

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Platform::Wayland {

    /**
     * @brief Translates an xkbcommon modifier state into CNA's modifier bitmask.
     *
     * Shift, Control, Alt (`Mod1`) and Gui (`Mod4`) as held; Caps Lock and Num Lock as latched;
     * Scroll Lock from its indicator; AltGr from the `LevelThree` virtual modifier where the
     * keymap names one, else the `Mod5` it conventionally maps to.
     *
     * @param state The xkb state.
     * @return A bitmask of `KeyModifier` values.
     */
    [[nodiscard]] std::uint16_t ModifiersFromXkbState(xkb_state* state);

    /**
     * @brief The characters of a committed string worth delivering: a lone control character --
     * what Backspace, Enter, Tab or Escape "type" -- is a key, not text (X11-0040, D-15).
     * @param utf8 The string xkbcommon produced.
     * @return True when it is text.
     */
    [[nodiscard]] bool IsCommittableText(const std::string& utf8);

    /**
     * @brief The keyboard through xkbcommon, on the keymap the compositor sends (WAYLAND-0051..0053).
     *
     * ### What Wayland gives and what it does not
     *
     * A compositor sends a keymap, key presses and releases (as evdev codes), the modifier state
     * it computed, and how fast keys should repeat. It does not send repeats, and a client cannot
     * ask which keys are down -- so both are this object's: held keys are accumulated from events
     * and seeded by `wl_keyboard.enter`, and repeats are generated from `repeat_info` (D-14).
     *
     * ### The two mappings, as under X11
     *
     * Physical `Scancode` from the keymap's key **names** through the XKB table X11 uses too
     * (src/Xkb/), so a key is the same key under both backends; logical `KeyCode` from the
     * active layout's level-0/level-1 keysyms through the same `BuildKeyCodeTable` rules. The
     * logical table is rebuilt when the layout (xkb "group") changes: switching from `us` to
     * `cz` must move `KeyCode`s and must not move `Scancode`s.
     *
     * ### Several seats
     *
     * Each seat's keyboard has its own keymap and state. The snapshot is their union: a key held
     * on either keyboard is held.
     */
    class WaylandKeyboard final : public IPlatformKeyboard
    {
    public:
        /** @brief What the keyboard needs from the platform. */
        struct Host
        {
            /** @brief Queues an event. */
            std::function<void(PlatformEvent)> post;
            /** @brief Whether text input is started for a window. */
            std::function<bool(WindowId)> textInputActive;
            /** @brief Records an input serial (for requests the compositor ties to input). */
            std::function<void(wl_seat*, std::uint32_t)> recordSerial;
            /** @brief Tells the window its keyboard focus changed. */
            std::function<void(WindowId, bool)> focusChanged;
        };

        /** @brief One seat's keyboard: its keymap, state and held keys. */
        struct SeatKeyboard;

        /**
         * @brief Creates the service.
         * @param host The platform.
         */
        explicit WaylandKeyboard(Host host);

        /** @brief Releases every seat keyboard and the xkb context. */
        ~WaylandKeyboard() override;

        WaylandKeyboard(const WaylandKeyboard&) = delete;
        WaylandKeyboard& operator=(const WaylandKeyboard&) = delete;

        /**
         * @brief Starts listening to a seat's new `wl_keyboard`.
         * @param keyboard The proxy.
         * @param seat Its seat.
         */
        void Attach(wl_keyboard* keyboard, wl_seat* seat);

        /**
         * @brief Stops listening and releases the proxy (the seat lost its keyboard, or went away).
         * @param keyboard The proxy.
         */
        void Detach(wl_keyboard* keyboard);

        /**
         * @brief Resolves a surface to its window for `enter`.
         * @param resolver The platform's surface map.
         */
        void SetSurfaceResolver(std::function<WindowId(wl_surface*)> resolver) { resolveSurface_ = std::move(resolver); }

        /**
         * @brief Produces the key repeats that are due, from the pump.
         * @param now The monotonic time.
         */
        void GenerateRepeats(std::chrono::steady_clock::time_point now);

        /**
         * @brief Forgets a window that went away: focus on it is dropped.
         * @param window The window.
         */
        void ForgetWindow(WindowId window);

        /**
         * @brief Stops the keyboard producing text of its own while an input method composes for
         * the focused window (text-input-v3 commits the text instead, WAYLAND-0054).
         * @param suppressed True while an input method is enabled for the focused window.
         */
        void SetComposeSuppressed(bool suppressed) { composeSuppressed_ = suppressed; }

        /** @brief Rebuilds the snapshot from the held keys and modifiers. */
        void Update() override;
        /** @brief Gets the snapshot. @return The state as of the last Update(). */
        [[nodiscard]] const KeyboardSnapshot& GetSnapshot() const override { return snapshot_; }
        /** @brief Gets whether a seat has a keyboard. @return True when one does. */
        [[nodiscard]] bool HasKeyboard() const override { return !keyboards_.empty(); }
        /**
         * @brief Resolves a physical key through the active layout.
         * @param scancode The key.
         * @return The key code, or None.
         */
        [[nodiscard]] KeyCode GetKeyFromScancode(Scancode scancode) const override;
        /**
         * @brief Gets the contract's stable name of a physical key, which no layout changes.
         * @param scancode The key.
         * @return The name, as `ToString(Scancode)` gives it.
         */
        [[nodiscard]] std::string GetScancodeName(Scancode scancode) const override;
        /**
         * @brief Resolves a physical key's stable name.
         * @param name The name.
         * @return The scancode, or Unknown.
         */
        [[nodiscard]] Scancode GetScancodeFromName(const std::string& name) const override;
        /**
         * @brief Gets the keycap label a physical key has on the active layout.
         * @param scancode The key.
         * @return The level-0 keysym's name, capitalised (`A`, `Space`, `Scaron`), or empty.
         */
        [[nodiscard]] std::string GetKeyName(Scancode scancode) const override;
        /**
         * @brief Resolves a key label or keysym name to a key code; the inverse of GetKeyName.
         * @param name The name.
         * @return The key code, or None.
         */
        [[nodiscard]] KeyCode GetKeyFromName(const std::string& name) const override;

        /**
         * @brief Gets the first seat keyboard, for tests and name queries.
         * @return The keyboard, or null.
         */
        [[nodiscard]] const SeatKeyboard* GetPrimary() const;

    private:
        static const wl_keyboard_listener kListener;

        SeatKeyboard* Find(wl_keyboard* keyboard) const;
        void OnKeymap(SeatKeyboard& keyboard, std::uint32_t format, int fd, std::uint32_t size);
        void OnEnter(SeatKeyboard& keyboard, std::uint32_t serial, wl_surface* surface, wl_array* keys);
        void OnLeave(SeatKeyboard& keyboard, std::uint32_t serial);
        void OnKey(SeatKeyboard& keyboard, std::uint32_t serial, std::uint32_t key, std::uint32_t state);
        void OnModifiers(SeatKeyboard& keyboard, std::uint32_t serial, std::uint32_t depressed, std::uint32_t latched,
                         std::uint32_t locked, std::uint32_t group);
        void OnRepeatInfo(SeatKeyboard& keyboard, std::int32_t rate, std::int32_t delay);
        void EmitKey(SeatKeyboard& keyboard, std::uint32_t xkbKeycode, bool pressed, bool repeat);
        void EmitText(SeatKeyboard& keyboard, std::uint32_t xkbKeycode);
        void RebuildKeyCodes(SeatKeyboard& keyboard);

        Host host_;
        std::function<WindowId(wl_surface*)> resolveSurface_;
        xkb_context* context_ = nullptr;
        xkb_compose_table* composeTable_ = nullptr;
        std::vector<std::unique_ptr<SeatKeyboard>> keyboards_;
        KeyboardSnapshot snapshot_;
        bool composeSuppressed_ = false;
    };

} // namespace CNA::Platform::Wayland
