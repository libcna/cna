// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformHaptics.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace CNA::Internal::Input::test_support
{
    struct FakeHapticConfig
    {
        std::string name;
        std::uint32_t features = 0;
        int axisCount = 0;
        int maxEffects = 0;
        int maxEffectsPlaying = 0;
        bool rumbleSupported = false;
        bool effectSupported = true;
        bool openFails = false;
    };

    /** @brief Recording platform haptics factory with no native dependency. */
    class RecordingHapticBackend final
    {
        struct DeviceState
        {
            FakeHapticConfig config;
            int nextEffectId = 0;
            std::set<int> liveEffects;
            std::set<int> playingEffects;
        };

        class Device final : public CNA::Platform::IPlatformHapticDevice
        {
        public:
            Device(RecordingHapticBackend& owner, FakeHapticConfig config)
                : owner_(owner), state_{std::move(config)}
            {
                ++owner_.openCount;
            }

            ~Device() override { ++owner_.closeCount; }

            CNA::Platform::HapticDeviceCapabilities GetCapabilities() const override
            {
                return {state_.config.name, state_.config.features, state_.config.axisCount,
                        state_.config.maxEffects, state_.config.maxEffectsPlaying,
                        state_.config.rumbleSupported};
            }

            bool IsEffectSupported(const CNA::Platform::HapticEffect&) const override
            {
                return state_.config.effectSupported;
            }

            bool InitializeRumble() override
            {
                ++owner_.initRumbleCalls;
                return state_.config.rumbleSupported;
            }

            bool PlayRumble(const float strength, const std::uint32_t lengthMs) override
            {
                ++owner_.playRumbleCalls;
                owner_.lastRumbleStrength = strength;
                owner_.lastRumbleLengthMs = lengthMs;
                return state_.config.rumbleSupported;
            }

            bool StopRumble() override
            {
                ++owner_.stopRumbleCalls;
                return true;
            }

            int CreateEffect(const CNA::Platform::HapticEffect& effect) override
            {
                ++owner_.createEffectCalls;
                owner_.lastCreatedEffect = effect;
                const int id = state_.nextEffectId++;
                state_.liveEffects.insert(id);
                return id;
            }

            bool UpdateEffect(const int id, const CNA::Platform::HapticEffect& effect) override
            {
                owner_.lastUpdatedEffect = effect;
                return state_.liveEffects.contains(id);
            }

            bool RunEffect(const int id, const std::uint32_t) override
            {
                if (!state_.liveEffects.contains(id))
                {
                    return false;
                }
                state_.playingEffects.insert(id);
                return true;
            }

            bool StopEffect(const int id) override
            {
                if (!state_.liveEffects.contains(id))
                {
                    return false;
                }
                state_.playingEffects.erase(id);
                return true;
            }

            void DestroyEffect(const int id) override
            {
                ++owner_.destroyEffectCalls;
                state_.liveEffects.erase(id);
                state_.playingEffects.erase(id);
            }

            bool GetEffectStatus(const int id) const override
            {
                return state_.playingEffects.contains(id);
            }

            bool StopAllEffects() override
            {
                state_.playingEffects.clear();
                return true;
            }

            bool SetGain(const int gain) override
            {
                owner_.lastGain = gain;
                return true;
            }

            bool SetAutocenter(const int autocenter) override
            {
                owner_.lastAutocenter = autocenter;
                return true;
            }

            bool Pause() override
            {
                ++owner_.pauseCalls;
                return true;
            }

            bool Resume() override
            {
                ++owner_.resumeCalls;
                return true;
            }

        private:
            RecordingHapticBackend& owner_;
            DeviceState state_;
        };

    public:
        void Register(const CNA::Platform::DeviceId id, FakeHapticConfig config)
        {
            if (onRegister)
            {
                onRegister(id, config);
            }
            registered_[id] = std::move(config);
        }

        std::unique_ptr<CNA::Platform::IPlatformHapticDevice> Open(
            const CNA::Platform::DeviceId id)
        {
            const auto found = registered_.find(id);
            return found == registered_.end() ? nullptr : MakeDevice(found->second);
        }

        std::unique_ptr<CNA::Platform::IPlatformHapticDevice> OpenFromJoystick(
            const CNA::Platform::DeviceId id)
        {
            lastJoystickId = id;
            return id == 0 ? nullptr : MakeDevice(joystickConfig);
        }

        std::unique_ptr<CNA::Platform::IPlatformHapticDevice> OpenFromMouse()
        {
            return MakeDevice(mouseConfig);
        }

        bool IsJoystickHaptic(const CNA::Platform::DeviceId id)
        {
            lastJoystickId = id;
            return id != 0 && joystickIsHaptic;
        }

        [[nodiscard]] bool IsMouseHaptic() const { return mouseIsHaptic; }

        std::function<void(CNA::Platform::DeviceId, const FakeHapticConfig&)> onRegister;
        FakeHapticConfig joystickConfig;
        bool joystickIsHaptic = false;
        FakeHapticConfig mouseConfig;
        bool mouseIsHaptic = false;
        int openCount = 0;
        int closeCount = 0;
        int initRumbleCalls = 0;
        int playRumbleCalls = 0;
        int stopRumbleCalls = 0;
        float lastRumbleStrength = 0.0f;
        std::uint32_t lastRumbleLengthMs = 0;
        int createEffectCalls = 0;
        int destroyEffectCalls = 0;
        int lastGain = -1;
        int lastAutocenter = -1;
        int pauseCalls = 0;
        int resumeCalls = 0;
        CNA::Platform::DeviceId lastJoystickId = 0;
        CNA::Platform::HapticEffect lastCreatedEffect{};
        CNA::Platform::HapticEffect lastUpdatedEffect{};

    private:
        std::unique_ptr<CNA::Platform::IPlatformHapticDevice> MakeDevice(
            const FakeHapticConfig& config)
        {
            return config.openFails ? nullptr : std::make_unique<Device>(*this, config);
        }

        std::map<CNA::Platform::DeviceId, FakeHapticConfig> registered_;
    };
}
