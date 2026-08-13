// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/CannedJoystick.hpp"
#include "CNA/Platform/Input/IPlatformHaptics.hpp"

#include <map>
#include <utility>

namespace CNA::Platform::Testing {

    /** @brief Deterministic standalone-haptics service for public input tests. */
    class CannedHaptics final : public IPlatformHaptics
    {
    public:
        /** @brief Adds or replaces a connected device. */
        void Connect(HapticInfo info) { devices_[info.id] = std::move(info); }

        /** @brief Removes a connected device. */
        void Disconnect(const DeviceId id) { devices_.erase(id); }

        /** @brief Gets descriptors in ascending id order. */
        [[nodiscard]] std::vector<HapticInfo> GetHaptics() const override
        {
            std::vector<HapticInfo> result;
            result.reserve(devices_.size());
            for (const auto& [id, info] : devices_)
            {
                (void)id;
                result.push_back(info);
            }
            return result;
        }

        /** @brief Gets whether the id is connected. */
        [[nodiscard]] bool IsConnected(const DeviceId id) const override
        {
            return devices_.contains(id);
        }

        /** @brief Gets the canned simple-rumble capability. */
        [[nodiscard]] bool SupportsRumble(const DeviceId id) const override
        {
            ++supportsCalls;
            const auto item = devices_.find(id);
            return item != devices_.end() && item->second.rumbleSupported;
        }

        /** @brief Records initialization and returns the configured capability. */
        bool InitializeRumble(const DeviceId id) override
        {
            ++initializeCalls;
            lastId = id;
            return IsConnected(id) && SupportsRumble(id) && initializeResult;
        }

        /** @brief Records a simple-rumble request and returns the configured answer. */
        bool PlayRumble(const DeviceId id, const float strength,
                        const std::uint32_t durationMilliseconds) override
        {
            ++playCalls;
            lastId = id;
            lastStrength = strength;
            lastDurationMilliseconds = durationMilliseconds;
            return IsConnected(id) && SupportsRumble(id) && playResult;
        }

        /** @brief Records a stop request and returns the configured answer. */
        bool StopRumble(const DeviceId id) override
        {
            ++stopCalls;
            lastId = id;
            return IsConnected(id) && stopResult;
        }

        /** @brief Answer returned after connection/capability validation. */
        bool playResult = true;
        /** @brief Initialization result after connection/capability validation. */
        bool initializeResult = true;
        /** @brief Answer returned after connection validation. */
        bool stopResult = true;
        /** @brief Number of capability queries. */
        mutable int supportsCalls = 0;
        /** @brief Number of play calls. */
        int playCalls = 0;
        /** @brief Number of initialization calls. */
        int initializeCalls = 0;
        /** @brief Number of stop calls. */
        int stopCalls = 0;
        /** @brief Last addressed id. */
        DeviceId lastId = 0;
        /** @brief Last requested strength. */
        float lastStrength = 0.0f;
        /** @brief Last requested duration. */
        std::uint32_t lastDurationMilliseconds = 0;

    private:
        std::map<DeviceId, HapticInfo> devices_;
    };

    /** @brief Test platform exposing canned standalone haptics and raw joysticks. */
    class CannedHapticsPlatform final : public PlatformTestDecorator
    {
    public:
        /** @brief Tracks and forwards subsystem acquisition. */
        void AcquireSubsystem(const PlatformSubsystem subsystem) override
        {
            PlatformTestDecorator::AcquireSubsystem(subsystem);
            if (subsystem == PlatformSubsystem::Haptic)
            {
                ++hapticSubsystemBalance;
            }
        }

        /** @brief Tracks and forwards subsystem release. */
        void ReleaseSubsystem(const PlatformSubsystem subsystem) override
        {
            PlatformTestDecorator::ReleaseSubsystem(subsystem);
            if (subsystem == PlatformSubsystem::Haptic)
            {
                --hapticSubsystemBalance;
            }
        }

        /** @brief Gets the canned haptics service. */
        [[nodiscard]] IPlatformHaptics* GetHaptics() override { return &haptics; }
        /** @brief Gets the canned joystick service used by haptic correlation tests. */
        [[nodiscard]] IPlatformJoystick* GetJoystick() override { return &joystick; }

        /** @brief Reports both canned service capabilities. */
        [[nodiscard]] PlatformCapabilities GetCapabilities() const override
        {
            PlatformCapabilities capabilities = PlatformTestDecorator::GetCapabilities();
            capabilities.haptics = true;
            capabilities.joystick = true;
            return capabilities;
        }

        /** @brief Mutable deterministic haptics service. */
        CannedHaptics haptics;
        /** @brief Mutable deterministic joystick service. */
        CannedJoystick joystick;
        /** @brief Outstanding haptic subsystem references owned by public haptic objects. */
        int hapticSubsystemBalance = 0;
    };

} // namespace CNA::Platform::Testing
