// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/KeyCode.hpp"

#include <cstdint>
#include <vector>

namespace CNA::Platform {

    /**
     * @brief Modifier keys, as bits in `KeyboardSnapshot::modifiers`.
     *
     * CNA's own layout, not any implementation's. The distinction matters more here than
     * elsewhere in the contract: a bare `std::uint16_t` with no stated meaning invites an
     * implementation to pass its native mask straight through, which compiles, runs, and is
     * silently wrong on the second implementation — the values simply mean something else.
     *
     * Shift, Control, Alt and Gui do **not** distinguish left from right. XNA has no notion of
     * sided modifiers and neither does any consumer in CNA, so reporting the side would be
     * information nothing can use and every implementation would have to invent bits for.
     */
    enum class KeyModifier : std::uint16_t
    {
        /** @brief No modifier is active. */
        None = 0,
        /** @brief Either Shift key is held. */
        Shift = 1u << 0,
        /** @brief Either Control key is held. */
        Control = 1u << 1,
        /** @brief Either Alt key is held. */
        Alt = 1u << 2,
        /** @brief Either GUI key (Windows, Command, Super) is held. */
        Gui = 1u << 3,
        /** @brief Caps Lock is on. A latched state, not a held key. */
        CapsLock = 1u << 4,
        /** @brief Num Lock is on. A latched state, not a held key. */
        NumLock = 1u << 5,
        /** @brief Scroll Lock is on. A latched state, not a held key. */
        ScrollLock = 1u << 6,
        /** @brief The AltGr / Mode key is held. */
        Mode = 1u << 7
    };

    /**
     * @brief Tests whether a modifier is set in a mask.
     *
     * @param modifiers The mask, as taken from `KeyboardSnapshot::modifiers`.
     * @param modifier The modifier to test for.
     * @return True if the modifier is set.
     */
    [[nodiscard]] constexpr bool HasModifier(const std::uint16_t modifiers,
                                             const KeyModifier modifier)
    {
        return (modifiers & static_cast<std::uint16_t>(modifier)) != 0;
    }

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
        /** @brief Virtual key codes currently held; never `KeyCode::None`. */
        std::vector<KeyCode> pressedKeys;
        /**
         * @brief Modifier keys held or latched, as a bitmask of `KeyModifier` values.
         *
         * An implementation must translate into this layout rather than passing its own mask
         * through — see KeyModifier.
         */
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
