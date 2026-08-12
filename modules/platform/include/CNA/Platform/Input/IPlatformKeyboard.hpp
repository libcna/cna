// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <vector>

namespace CNA::Platform {

    /**
     * @brief A whole-keyboard snapshot taken at one instant.
     *
     * XNA's `Keyboard::GetState()` is a level query — "is this key held right now" — so the
     * platform produces one snapshot per frame and the game reads a local structure. That is
     * `cnaplatform.md`'s input-snapshot rule made concrete: thousands of `IsKeyDown` calls must
     * not become thousands of platform calls.
     */
    struct KeyboardSnapshot
    {
        /** @brief Virtual key codes currently held, matching `Microsoft::Xna::Framework::Input::Keys`. */
        std::vector<std::uint32_t> pressedKeys;
        /** @brief Bitmask of modifier keys held. */
        std::uint16_t modifiers = 0;
    };

    /**
     * @brief Reads keyboard state.
     *
     * ### Exactness is a capability, not an assumption
     *
     * `ExactKeyboardState` reports whether every press has a real matching release. It is true on
     * a windowing platform, and false where releases must be synthesised from repeat timing —
     * a terminal without the Kitty keyboard protocol being the concrete case. A game that needs
     * precise held-key behaviour can then branch instead of silently misbehaving.
     */
    class IPlatformKeyboard
    {
    public:
        /** @brief Destroys the service. */
        virtual ~IPlatformKeyboard() = default;

        /**
         * @brief Updates the snapshot from the platform's current state.
         *
         * Called once per frame, before the game reads state.
         */
        virtual void Update() = 0;

        /**
         * @brief Gets the most recent snapshot.
         *
         * @return The state as of the last Update().
         */
        [[nodiscard]] virtual const KeyboardSnapshot& GetSnapshot() const = 0;

        /**
         * @brief Gets whether a keyboard is present.
         *
         * @return True if at least one keyboard is connected.
         */
        [[nodiscard]] virtual bool HasKeyboard() const = 0;
    };

} // namespace CNA::Platform
