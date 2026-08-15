// SPDX-License-Identifier: MS-PL
#pragma once

// Shared test scaffolding for a complete mapped-gamepad service. Tests publish whole snapshots
// through Update(), while metadata, optional inputs and actuator results are explicitly scripted.

#include "CNA/Platform/Input/IPlatformGamepad.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

#include <array>

namespace CNA::Platform::Testing {

    /** @brief An IPlatformGamepad whose four slots are controlled by a test. */
    class CannedGamepad final : public IPlatformGamepad
    {
    public:
        /** @brief Sets one slot's state for the next Update(). */
        void SetPendingSnapshot(const int slot, const GamepadSnapshot& snapshot)
        {
            if (IsValidSlot(slot))
            {
                pendingSnapshots_[static_cast<std::size_t>(slot)] = snapshot;
            }
        }

        /** @brief Sets one slot's cached capabilities. */
        void SetCapabilities(const int slot, const GamepadCapabilities& capabilities)
        {
            if (IsValidSlot(slot))
            {
                capabilities_[static_cast<std::size_t>(slot)] = capabilities;
            }
        }

        /** @brief Sets one slot's cached identity. */
        void SetInfo(const int slot, const GamepadInfo& info)
        {
            if (IsValidSlot(slot))
            {
                infos_[static_cast<std::size_t>(slot)] = info;
            }
        }

        /** @brief Publishes all four pending snapshots atomically. */
        void Update() override
        {
            snapshots_ = pendingSnapshots_;
            ++updateCount_;
        }

        /** @brief Always exposes the four XNA player slots. */
        [[nodiscard]] int GetCount() const override { return GamepadSlotCount; }

        /** @brief Gets one published snapshot or an empty disconnected value. */
        [[nodiscard]] const GamepadSnapshot& GetSnapshot(const int index) const override
        {
            static const GamepadSnapshot empty;
            return IsValidSlot(index) ? snapshots_[static_cast<std::size_t>(index)] : empty;
        }

        /** @brief Gets the scripted name. */
        [[nodiscard]] std::string GetName(const int index) const override
        {
            return GetInfo(index).name;
        }

        /** @brief Gets scripted capabilities or an empty value. */
        [[nodiscard]] const GamepadCapabilities& GetCapabilities(const int index) const override
        {
            static const GamepadCapabilities empty;
            return IsValidSlot(index) ? capabilities_[static_cast<std::size_t>(index)] : empty;
        }

        /** @brief Gets scripted identity or an empty value. */
        [[nodiscard]] const GamepadInfo& GetInfo(const int index) const override
        {
            static const GamepadInfo empty;
            return IsValidSlot(index) ? infos_[static_cast<std::size_t>(index)] : empty;
        }

        /** @brief Records ordinary rumble and returns the scripted result. */
        bool SetRumble(const int index, const float low, const float high,
                       const std::uint32_t duration) override
        {
            lastSlot_ = index;
            lastLeft_ = low;
            lastRight_ = high;
            lastDuration_ = duration;
            ++rumbleCalls_;
            return IsValidSlot(index) && rumbleResult_;
        }

        /** @brief Records trigger rumble and returns the scripted result. */
        bool SetTriggerRumble(const int index, const float left, const float right,
                              const std::uint32_t duration) override
        {
            lastSlot_ = index;
            lastLeft_ = left;
            lastRight_ = right;
            lastDuration_ = duration;
            ++triggerRumbleCalls_;
            return IsValidSlot(index) && triggerRumbleResult_;
        }

        /** @brief Records an RGB command and returns the scripted result. */
        bool SetLightBar(const int index, const std::uint8_t red, const std::uint8_t green,
                         const std::uint8_t blue) override
        {
            lastSlot_ = index;
            lastRed_ = red;
            lastGreen_ = green;
            lastBlue_ = blue;
            ++lightBarCalls_;
            return IsValidSlot(index) && lightBarResult_;
        }

        /** @brief Returns one scripted sensor reading. */
        [[nodiscard]] bool TryGetSensor(const int index, const GamepadSensor sensor,
                                        GamepadSensorReading& reading) override
        {
            reading = {};
            if (!IsValidSlot(index))
            {
                return false;
            }
            const std::size_t sensorIndex = sensor == GamepadSensor::Gyroscope ? 0u : 1u;
            if (!sensorAvailable_[sensorIndex])
            {
                return false;
            }
            reading = sensorReadings_[sensorIndex];
            return true;
        }

        /** @brief Gets the scripted player indicator. */
        [[nodiscard]] int GetPlayerIndex(const int index) const override
        {
            return IsValidSlot(index) ? playerIndices_[static_cast<std::size_t>(index)] : -1;
        }

        /** @brief Sets the scripted player indicator when allowed. */
        bool SetPlayerIndex(const int index, const int playerIndex) override
        {
            if (!IsValidSlot(index) || !playerIndexResult_)
            {
                return false;
            }
            playerIndices_[static_cast<std::size_t>(index)] = playerIndex;
            return true;
        }

        /** @brief Gets scripted battery state. */
        [[nodiscard]] GamepadPowerInfo GetPowerInfo(const int index) const override
        {
            return IsValidSlot(index) ? power_[static_cast<std::size_t>(index)]
                                      : GamepadPowerInfo{GamepadPowerState::Error, -1};
        }

        /** @brief Gets the one scripted button label and records the requested button. */
        [[nodiscard]] GamepadButtonLabel GetButtonLabel(const int index,
                                                        const GamepadButton button) const override
        {
            lastLabelButton_ = button;
            return IsValidSlot(index) ? buttonLabels_[static_cast<std::size_t>(index)]
                                      : GamepadButtonLabel::Unknown;
        }

        /** @brief Gets a scripted touchpad count. */
        [[nodiscard]] int GetTouchpadCount(const int index) const override
        {
            return IsValidSlot(index) ? touchpadCounts_[static_cast<std::size_t>(index)] : 0;
        }

        /** @brief Gets the scripted finger capacity for touchpad zero. */
        [[nodiscard]] int GetTouchpadFingerCount(const int index, const int touchpad) const override
        {
            return IsValidSlot(index) && touchpad == 0
                ? touchpadFingerCounts_[static_cast<std::size_t>(index)] : 0;
        }

        /** @brief Returns one scripted contact at touchpad/finger zero. */
        [[nodiscard]] bool TryGetTouchpadFinger(const int index, const int touchpad,
                                                const int fingerIndex,
                                                GamepadTouchpadFinger& finger) const override
        {
            finger = {};
            if (!IsValidSlot(index) || touchpad != 0 || fingerIndex != 0
                || !touchpadFingerAvailable_[static_cast<std::size_t>(index)])
            {
                return false;
            }
            finger = touchpadFingers_[static_cast<std::size_t>(index)];
            return true;
        }

        void SetRumbleResult(const bool value) { rumbleResult_ = value; }
        void SetTriggerRumbleResult(const bool value) { triggerRumbleResult_ = value; }
        void SetLightBarResult(const bool value) { lightBarResult_ = value; }
        void SetPlayerIndexResult(const bool value) { playerIndexResult_ = value; }
        void SetPlayerIndexValue(const int slot, const int value)
        {
            if (IsValidSlot(slot)) { playerIndices_[static_cast<std::size_t>(slot)] = value; }
        }
        void SetPowerInfo(const int slot, const GamepadPowerInfo value)
        {
            if (IsValidSlot(slot)) { power_[static_cast<std::size_t>(slot)] = value; }
        }
        void SetButtonLabel(const int slot, const GamepadButtonLabel value)
        {
            if (IsValidSlot(slot)) { buttonLabels_[static_cast<std::size_t>(slot)] = value; }
        }
        void SetSensor(const GamepadSensor sensor, const GamepadSensorReading value,
                       const bool available = true)
        {
            const std::size_t index = sensor == GamepadSensor::Gyroscope ? 0u : 1u;
            sensorReadings_[index] = value;
            sensorAvailable_[index] = available;
        }
        void SetTouchpad(const int slot, const int count, const int fingers,
                         const GamepadTouchpadFinger value, const bool available = true)
        {
            if (!IsValidSlot(slot)) { return; }
            const std::size_t index = static_cast<std::size_t>(slot);
            touchpadCounts_[index] = count;
            touchpadFingerCounts_[index] = fingers;
            touchpadFingers_[index] = value;
            touchpadFingerAvailable_[index] = available;
        }

        [[nodiscard]] int UpdateCount() const { return updateCount_; }
        [[nodiscard]] int RumbleCalls() const { return rumbleCalls_; }
        [[nodiscard]] int TriggerRumbleCalls() const { return triggerRumbleCalls_; }
        [[nodiscard]] int LightBarCalls() const { return lightBarCalls_; }
        [[nodiscard]] int LastSlot() const { return lastSlot_; }
        [[nodiscard]] float LastLeft() const { return lastLeft_; }
        [[nodiscard]] float LastRight() const { return lastRight_; }
        [[nodiscard]] std::uint32_t LastDuration() const { return lastDuration_; }
        [[nodiscard]] std::uint8_t LastRed() const { return lastRed_; }
        [[nodiscard]] std::uint8_t LastGreen() const { return lastGreen_; }
        [[nodiscard]] std::uint8_t LastBlue() const { return lastBlue_; }
        [[nodiscard]] GamepadButton LastLabelButton() const { return lastLabelButton_; }

    private:
        [[nodiscard]] static bool IsValidSlot(const int slot)
        {
            return slot >= 0 && slot < GamepadSlotCount;
        }

        std::array<GamepadSnapshot, GamepadSlotCount> pendingSnapshots_{};
        std::array<GamepadSnapshot, GamepadSlotCount> snapshots_{};
        std::array<GamepadCapabilities, GamepadSlotCount> capabilities_{};
        std::array<GamepadInfo, GamepadSlotCount> infos_{};
        std::array<int, GamepadSlotCount> playerIndices_{{-1, -1, -1, -1}};
        std::array<GamepadPowerInfo, GamepadSlotCount> power_{};
        std::array<GamepadButtonLabel, GamepadSlotCount> buttonLabels_{};
        std::array<int, GamepadSlotCount> touchpadCounts_{};
        std::array<int, GamepadSlotCount> touchpadFingerCounts_{};
        std::array<GamepadTouchpadFinger, GamepadSlotCount> touchpadFingers_{};
        std::array<bool, GamepadSlotCount> touchpadFingerAvailable_{};
        std::array<GamepadSensorReading, 2> sensorReadings_{};
        std::array<bool, 2> sensorAvailable_{};
        bool rumbleResult_ = false;
        bool triggerRumbleResult_ = false;
        bool lightBarResult_ = false;
        bool playerIndexResult_ = false;
        int updateCount_ = 0;
        int rumbleCalls_ = 0;
        int triggerRumbleCalls_ = 0;
        int lightBarCalls_ = 0;
        int lastSlot_ = -1;
        float lastLeft_ = 0.0f;
        float lastRight_ = 0.0f;
        std::uint32_t lastDuration_ = 0;
        std::uint8_t lastRed_ = 0;
        std::uint8_t lastGreen_ = 0;
        std::uint8_t lastBlue_ = 0;
        mutable GamepadButton lastLabelButton_ = GamepadButton::A;
    };

    /** @brief A real platform decorated with a scripted gamepad service. */
    class CannedGamepadPlatform final : public PlatformTestDecorator
    {
    public:
        [[nodiscard]] IPlatformGamepad* GetGamepad() override
        {
            return gamepadAvailable_ ? &gamepad_ : nullptr;
        }
        [[nodiscard]] CannedGamepad& Canned() { return gamepad_; }
        void SetGamepadAvailable(const bool available) { gamepadAvailable_ = available; }

    private:
        CannedGamepad gamepad_;
        bool gamepadAvailable_ = true;
    };

} // namespace CNA::Platform::Testing
