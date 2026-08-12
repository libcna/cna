// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/CannedGamepad.hpp"
#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace CNA::Internal::Input::Testing {

    /**
     * @brief Drives public GamePad tests through whole IPlatformGamepad snapshots.
     *
     * Mutators mirror the old event-store test vocabulary, but every change is published through
     * the platform contract. Packet numbers advance only when a connected value actually changes.
     */
    class CannedGamepadStateDriver
    {
    public:
        CannedGamepadStateDriver()
            : installed_(platform_)
        {
        }

        void Reset()
        {
            snapshots_ = {};
            PublishAll();
        }

        void SetGamePadConnection(const Microsoft::Xna::Framework::PlayerIndex playerIndex,
                                  const bool connected)
        {
            const int slot = ToSlot(playerIndex);
            if (slot < 0)
            {
                return;
            }

            auto& snapshot = snapshots_[static_cast<std::size_t>(slot)];
            if (!connected)
            {
                snapshot = {};
            }
            else if (!snapshot.connected)
            {
                snapshot.connected = true;
                ++snapshot.packetNumber;
            }
            Publish(slot);
        }

        void SetGamePadButtonState(
            const Microsoft::Xna::Framework::PlayerIndex playerIndex,
            const CNA::Platform::GamepadButton button,
            const Microsoft::Xna::Framework::Input::ButtonState state)
        {
            const int slot = ToSlot(playerIndex);
            if (slot < 0)
            {
                return;
            }

            auto& snapshot = snapshots_[static_cast<std::size_t>(slot)];
            const std::uint32_t before = snapshot.buttons;
            const std::uint32_t flag = static_cast<std::uint32_t>(button);
            if (state == Microsoft::Xna::Framework::Input::ButtonState::Pressed)
            {
                snapshot.buttons |= flag;
            }
            else
            {
                snapshot.buttons &= ~flag;
            }
            if (snapshot.buttons != before)
            {
                ++snapshot.packetNumber;
            }
            Publish(slot);
        }

        void SetGamePadAxisValue(const Microsoft::Xna::Framework::PlayerIndex playerIndex,
                                 const CNA::Platform::GamepadAxis axis, const float value)
        {
            const int slot = ToSlot(playerIndex);
            if (slot < 0)
            {
                return;
            }

            auto& snapshot = snapshots_[static_cast<std::size_t>(slot)];
            const std::size_t axisIndex = static_cast<std::size_t>(axis);
            const bool trigger = axis == CNA::Platform::GamepadAxis::LeftTrigger
                || axis == CNA::Platform::GamepadAxis::RightTrigger;
            const float normalized = trigger ? std::clamp(value, 0.0f, 1.0f)
                                             : std::clamp(value, -1.0f, 1.0f);
            if (snapshot.axes[axisIndex] != normalized)
            {
                snapshot.axes[axisIndex] = normalized;
                ++snapshot.packetNumber;
            }
            Publish(slot);
        }

        [[nodiscard]] CNA::Platform::Testing::CannedGamepadPlatform& Platform()
        {
            return platform_;
        }

    private:
        static int ToSlot(const Microsoft::Xna::Framework::PlayerIndex playerIndex)
        {
            const int slot = static_cast<int>(playerIndex);
            return slot >= 0 && slot < CNA::Platform::GamepadSlotCount ? slot : -1;
        }

        void Publish(const int slot)
        {
            platform_.Canned().SetPendingSnapshot(
                slot, snapshots_[static_cast<std::size_t>(slot)]);
            platform_.Canned().Update();
        }

        void PublishAll()
        {
            for (int slot = 0; slot < CNA::Platform::GamepadSlotCount; ++slot)
            {
                platform_.Canned().SetPendingSnapshot(
                    slot, snapshots_[static_cast<std::size_t>(slot)]);
            }
            platform_.Canned().Update();
        }

        CNA::Platform::Testing::CannedGamepadPlatform platform_;
        CNA::Platform::Testing::ScopedCurrentPlatform installed_;
        std::array<CNA::Platform::GamepadSnapshot, CNA::Platform::GamepadSlotCount> snapshots_{};
    };

} // namespace CNA::Internal::Input::Testing
