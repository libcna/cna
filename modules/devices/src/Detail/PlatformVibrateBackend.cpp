// SPDX-License-Identifier: MS-PL

#include "PlatformVibrateBackend.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/Input/IPlatformHaptics.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Devices/Detail/DevicesShutdownCoordinator.hpp"

namespace Microsoft::Devices::Detail
{
    PlatformVibrateBackend::~PlatformVibrateBackend()
    {
        if (!DevicesShutdownCoordinator::IsShutdown())
        {
            ReleaseService();
        }
    }

    std::uint32_t PlatformVibrateBackend::DurationMilliseconds(const System::TimeSpan& duration)
    {
        return static_cast<std::uint32_t>(duration.getTotalMillisecondsProperty());
    }

    void PlatformVibrateBackend::ReleaseService()
    {
        // A platform pointer captured earlier is only safe to call through while that same platform
        // is still the installed one. The DevicesShutdownCoordinator flag alone does not guarantee
        // that: it is process-global and a test can reset it (ResetForTesting), after which a
        // backend destroyed at process exit -- VibrateController's function-local static is
        // destroyed by __run_exit_handlers, long after the platform is gone -- called through a
        // freed vtable and took the process down with SIGSEGV at address 0. Reproduced
        // deterministically with `--gtest_filter=*Instanc*`, on every renderer and with
        // CNA_CNAEXT off, so it is neither renderer- nor engine-layer-specific.
        if (subsystemHeld_ && platform_ != nullptr && CNA::Platform::HasCurrentPlatform() &&
            &CNA::Platform::GetCurrentPlatform() == platform_)
        {
            platform_->ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Haptic);
        }
        subsystemHeld_ = false;
        deviceId_.reset();
        haptics_ = nullptr;
        platform_ = nullptr;
    }

    bool PlatformVibrateBackend::EnsureService()
    {
        CNA::Platform::IPlatform& current = CNA::Platform::GetCurrentPlatform();
        if (platform_ != nullptr && platform_ != &current)
        {
            ReleaseService();
        }
        if (subsystemHeld_)
        {
            return haptics_ != nullptr;
        }

        CNA::Platform::IPlatformHaptics* haptics = current.GetHaptics();
        if (haptics == nullptr)
        {
            return false;
        }
        try
        {
            current.AcquireSubsystem(CNA::Platform::PlatformSubsystem::Haptic);
        }
        catch (const CNA::Platform::PlatformException&)
        {
            return false;
        }

        platform_ = &current;
        haptics_ = haptics;
        subsystemHeld_ = true;
        return true;
    }

    void PlatformVibrateBackend::RefreshDevice()
    {
        deviceId_.reset();
        if (haptics_ == nullptr)
        {
            return;
        }
        const std::optional<CNA::Platform::HapticInfo> device =
            haptics_->GetDefaultVibrationDevice();
        if (device.has_value())
        {
            deviceId_ = device->id;
        }
    }

    void PlatformVibrateBackend::Start(const System::TimeSpan& duration, const float intensity)
    {
        if (!EnsureService())
        {
            return;
        }
        RefreshDevice();
        if (deviceId_.has_value())
        {
            (void)haptics_->PlayRumble(*deviceId_, intensity, DurationMilliseconds(duration));
        }
    }

    void PlatformVibrateBackend::Stop()
    {
        if (haptics_ != nullptr && deviceId_.has_value())
        {
            (void)haptics_->StopAll(*deviceId_);
        }
    }

    bool PlatformVibrateBackend::IsSupported()
    {
        if (!EnsureService())
        {
            return false;
        }
        RefreshDevice();
        return deviceId_.has_value() && haptics_->SupportsRumble(*deviceId_);
    }

    std::string PlatformVibrateBackend::GetDeviceName()
    {
        if (!EnsureService())
        {
            return {};
        }
        const std::optional<CNA::Platform::HapticInfo> device =
            haptics_->GetDefaultVibrationDevice();
        if (!device.has_value())
        {
            deviceId_.reset();
            return {};
        }
        deviceId_ = device->id;
        return device->name;
    }

    void PlatformVibrateBackend::StartLeftRight(const float largeMotor, const float smallMotor,
                                                const System::TimeSpan& duration)
    {
        if (!EnsureService())
        {
            return;
        }
        RefreshDevice();
        if (deviceId_.has_value())
        {
            (void)haptics_->PlayLeftRight(*deviceId_, largeMotor, smallMotor,
                                          DurationMilliseconds(duration));
        }
    }
}
