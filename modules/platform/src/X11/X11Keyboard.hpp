// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/KeyCode.hpp"
#include "CNA/Platform/Input/Scancode.hpp"

#include "X11Headers.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace CNA::Platform::X11 {

    class X11Connection;

    /**
     * @brief Translates an XKB key name into a physical CNA scancode.
     *
     * ### Why names and not keycodes
     *
     * The tempting mapping is arithmetic: on Linux with the `evdev` ruleset, an X keycode is the
     * kernel's evdev code plus 8. That is true, and it is still the wrong mapping, twice over.
     * The offset holds only for that one ruleset — `xfree86`, and the layouts the BSDs ship, use
     * a different numbering — and evdev codes are not USB HID usage IDs, which is what
     * `Scancode` actually is, so the arithmetic would need a second translation table anyway.
     *
     * XKB key names are the stable thing. `AD01` is the key in the position where a US keyboard
     * has `Q`, on every ruleset, under every layout, on every X server that ships
     * `xkeyboard-config` — which is all of them. The name is exactly the "physical position"
     * concept `Scancode` documents, so the mapping is one table with no arithmetic in it.
     *
     * @param name The four-character XKB key name, not necessarily NUL-terminated.
     * @return The matching scancode, or `Scancode::Unknown`.
     */
    [[nodiscard]] Scancode ScancodeFromXkbKeyName(const char name[4]);

    /**
     * @brief Translates an X keysym into a layout-dependent CNA key code.
     *
     * `KeyCode` values are Windows virtual-key codes, which name the *unshifted* identity of a
     * key: there is a `VK_A` but no `VK_a`, and punctuation has `VK_OEM_*` names rather than
     * character ones. So the caller passes the keysym from group 0, shift level 0 — what the key
     * produces with no modifiers on the current layout — and gets back the virtual key for it.
     *
     * @param keysym The X keysym.
     * @return The matching key code, or `KeyCode::None` when X names no virtual key for it.
     */
    [[nodiscard]] KeyCode KeyCodeFromKeysym(KeySym keysym);

    /**
     * @brief Translates an X event state mask into CNA's modifier bitmask.
     *
     * @param state The `state` field of an X key, button or motion event.
     * @param modeSwitchMask The modifier mask bit the current layout uses for AltGr, or zero.
     * @return A bitmask of `KeyModifier` values.
     */
    [[nodiscard]] std::uint16_t ModifiersFromXState(unsigned int state,
                                                    unsigned int modeSwitchMask);

    /**
     * @brief Reads the keyboard through XKB, and answers CNA's keyboard queries.
     *
     * ### The two mappings this owns
     *
     * A `keycode -> Scancode` table, rebuilt whenever the server reports a new keyboard, and a
     * `keycode -> KeyCode` mapping that is re-read on every layout (group) change. They are
     * genuinely different things — the first is the physical key and must not move when the user
     * switches to AZERTY, the second is what that key now means and must.
     *
     * ### Held state
     *
     * `Update()` reads the server's own key vector with `XQueryKeymap` rather than accumulating
     * press and release events. That is what makes the snapshot correct after the application
     * misses events — coming back from a grab, from another workspace, or from a window that
     * was unmapped while a key was down.
     */
    class X11Keyboard final : public IPlatformKeyboard
    {
    public:
        /**
         * @brief Builds the keyboard for one connection.
         *
         * @param connection The connection to read the keyboard description from.
         */
        explicit X11Keyboard(X11Connection& connection);

        /** @brief Re-reads the held-key vector and the modifier state from the server. */
        void Update() override;

        /** @brief Gets the most recent snapshot. @return The snapshot. */
        [[nodiscard]] const KeyboardSnapshot& GetSnapshot() const override { return snapshot_; }

        /** @brief Gets whether a keyboard is attached. @return True; X11 always has a core keyboard. */
        [[nodiscard]] bool HasKeyboard() const override { return true; }

        /**
         * @brief Gets the key a physical position currently produces.
         * @param scancode The physical key.
         * @return The virtual key on the current layout, or `KeyCode::None`.
         */
        [[nodiscard]] KeyCode GetKeyFromScancode(Scancode scancode) const override;

        /**
         * @brief Gets the stable, layout-independent name of a physical key.
         * @param scancode The physical key.
         * @return The contract's own scancode name.
         */
        [[nodiscard]] std::string GetScancodeName(Scancode scancode) const override;

        /**
         * @brief Gets the physical key a stable name refers to.
         * @param name The scancode name.
         * @return The scancode, or `Scancode::Unknown`.
         */
        [[nodiscard]] Scancode GetScancodeFromName(const std::string& name) const override;

        /**
         * @brief Gets the label the current layout puts on a physical key.
         * @param scancode The physical key.
         * @return The keysym's name on the current layout, or an empty string.
         */
        [[nodiscard]] std::string GetKeyName(Scancode scancode) const override;

        /**
         * @brief Gets the key a layout-dependent name refers to.
         * @param name The key name, as `GetKeyName` would produce.
         * @return The key code, or `KeyCode::None`.
         */
        [[nodiscard]] KeyCode GetKeyFromName(const std::string& name) const override;

        // --- used by the event mapper ----------------------------------------------------------

        /**
         * @brief Gets the physical key an X keycode refers to.
         * @param keycode The X keycode.
         * @return The scancode, or `Scancode::Unknown`.
         */
        [[nodiscard]] Scancode GetScancode(unsigned int keycode) const;

        /**
         * @brief Gets the virtual key an X keycode currently produces.
         * @param keycode The X keycode.
         * @return The key code, or `KeyCode::None`.
         */
        [[nodiscard]] KeyCode GetKeyCode(unsigned int keycode) const;

        /** @brief Re-reads the keycode tables after a keyboard or layout change. */
        void RefreshKeyboardMapping();

        /**
         * @brief Gets the modifier mask bit the current layout uses for AltGr.
         *
         * @return The mask, or zero when the layout has no Mode_switch.
         */
        [[nodiscard]] unsigned int GetModeSwitchMask() const { return modeSwitchMask_; }

        /**
         * @brief Records that a key was pressed or released, for repeat detection.
         *
         * @param keycode The X keycode.
         * @param pressed True for a press.
         * @return True when this press repeats a key that was already held.
         */
        bool TrackKeyState(unsigned int keycode, bool pressed);

        /** @brief Clears every held key, for use when a window loses focus. */
        void ReleaseAllKeys();

        /**
         * @brief Gets whether a key is currently held, according to this backend's own tracking.
         *
         * @param keycode The X keycode.
         * @return True when held.
         */
        [[nodiscard]] bool IsKeyHeld(unsigned int keycode) const;

    private:
        static constexpr int kMinKeycode = 8;
        static constexpr int kMaxKeycode = 255;
        static constexpr std::size_t kKeycodeCount = kMaxKeycode + 1;

        X11Connection& connection_;
        std::array<Scancode, kKeycodeCount> scancodes_{};
        std::array<KeyCode, kKeycodeCount> keycodes_{};
        std::array<bool, kKeycodeCount> held_{};
        unsigned int modeSwitchMask_ = 0;
        KeyboardSnapshot snapshot_;
    };

} // namespace CNA::Platform::X11
