// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformGamepad.hpp"
#include "CNA/Platform/Input/IPlatformKeyboard.hpp"

namespace CNA::Platform
{
    /** @brief Process-wide opt-in keyboard source for the game's first gamepad slot. */
    class KeyboardGamepad
    {
    public:
        /**
         * @brief Gets whether the software gamepad is enabled.
         * @return True when keyboard emulation is enabled.
         */
        [[nodiscard]] static bool IsEnabled();
        /**
         * @brief Changes mode and clears held software input on a mode change.
         * @param enabled True to enable the software gamepad.
         */
        static void SetEnabled(bool enabled);
        /**
         * @brief Publishes one keyboard frame; an unfocused game supplies neutral input.
         * @param keyboard The current keyboard snapshot.
         * @param focused True when the game accepts keyboard input.
         */
        static void Update(const KeyboardSnapshot& keyboard, bool focused);
        /**
         * @brief Gets the software state independently of any physical controller.
         * @return The last published keyboard state, disconnected when disabled.
         */
        [[nodiscard]] static GamepadSnapshot GetSnapshot();
        /**
         * @brief Gets mapped held buttons even when game focus suppresses the software state.
         * @return Raw keyboard button flags for the Guide dismissal release barrier.
         */
        [[nodiscard]] static std::uint32_t GetHeldButtons();
        /**
         * @brief Merges keyboard buttons and nonzero axes into a physical snapshot.
         * @param physical The first slot's physical snapshot, or a disconnected value.
         * @return The combined connected state, or physical unchanged when disabled.
         */
        [[nodiscard]] static GamepadSnapshot MergeSnapshot(const GamepadSnapshot& physical);
        /**
         * @brief Adds the mapped keyboard controls to the physical capabilities.
         * @param physical The first slot's physical capabilities, or an empty value.
         * @return Combined capabilities, or physical unchanged when disabled.
         */
        [[nodiscard]] static GamepadCapabilities MergeCapabilities(const GamepadCapabilities& physical);
    };
}
