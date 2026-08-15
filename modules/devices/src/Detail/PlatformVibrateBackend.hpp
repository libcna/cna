// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Devices/Detail/IVibrateBackend.hpp"

#include <cstdint>
#include <optional>

namespace CNA::Platform
{
    class IPlatform;
    class IPlatformHaptics;
    struct HapticInfo;
}

namespace Microsoft::Devices::Detail
{
    /** @brief Adapts VibrateController to the selected platform's haptic service. */
    class PlatformVibrateBackend final : public IVibrateBackend
    {
    public:
        /** @brief Creates a lazy backend; no subsystem or device is touched yet. */
        PlatformVibrateBackend() = default;

        /** @brief Releases the haptic-subsystem hold when native shutdown has not begun. */
        ~PlatformVibrateBackend() override;

        PlatformVibrateBackend(const PlatformVibrateBackend&) = delete;
        PlatformVibrateBackend& operator=(const PlatformVibrateBackend&) = delete;

        void Start(const System::TimeSpan& duration, float intensity) override;
        void Stop() override;
        [[nodiscard]] bool IsSupported() override;
        [[nodiscard]] std::string GetDeviceName() override;
        void StartLeftRight(float largeMotor, float smallMotor,
                            const System::TimeSpan& duration) override;

    private:
        [[nodiscard]] bool EnsureService();
        void RefreshDevice();
        void ReleaseService();
        [[nodiscard]] static std::uint32_t DurationMilliseconds(const System::TimeSpan& duration);

        CNA::Platform::IPlatform* platform_ = nullptr;
        CNA::Platform::IPlatformHaptics* haptics_ = nullptr;
        std::optional<std::uint64_t> deviceId_;
        bool subsystemHeld_ = false;
    };
}
