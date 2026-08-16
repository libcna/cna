// SPDX-License-Identifier: MS-PL
#include "CNA/Input/Haptics.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/Input/IPlatformHaptics.hpp"
#include "CNA/Platform/Input/IPlatformJoystick.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <limits>
#include <utility>

namespace CNA::Input
{
    namespace
    {
        class ScopedHapticSubsystem
        {
        public:
            explicit ScopedHapticSubsystem(CNA::Platform::IPlatform& platform)
                : platform_(&platform)
            {
                try
                {
                    platform_->AcquireSubsystem(CNA::Platform::PlatformSubsystem::Haptic);
                    acquired_ = true;
                }
                catch (const CNA::Platform::PlatformException&)
                {
                    acquired_ = false;
                }
            }

            ~ScopedHapticSubsystem()
            {
                if (acquired_)
                {
                    platform_->ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Haptic);
                }
            }

            [[nodiscard]] bool IsAcquired() const { return acquired_; }
            void TransferOwnership() { acquired_ = false; }

        private:
            CNA::Platform::IPlatform* platform_;
            bool acquired_ = false;
        };
    }

    std::vector<HapticInfoEXT> Haptics::GetHapticsEXT()
    {
        std::vector<HapticInfoEXT> result;
        CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
        CNA::Platform::IPlatformHaptics* service =
            platform.GetHaptics();
        if (service == nullptr)
        {
            return result;
        }
        ScopedHapticSubsystem subsystem(platform);
        if (!subsystem.IsAcquired())
        {
            return result;
        }
        for (const CNA::Platform::HapticInfo& info : service->GetHaptics())
        {
            if (info.id <= std::numeric_limits<std::uint32_t>::max())
            {
                result.push_back({static_cast<std::uint32_t>(info.id), info.name});
            }
        }
        return result;
    }

    HapticDevice Haptics::OpenEXT(const std::uint32_t id)
    {
        CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
        CNA::Platform::IPlatformHaptics* service =
            platform.GetHaptics();
        if (service == nullptr)
        {
            return {};
        }
        ScopedHapticSubsystem subsystem(platform);
        if (!subsystem.IsAcquired())
        {
            return {};
        }

        std::string name;
        bool connected = false;
        for (const CNA::Platform::HapticInfo& info : service->GetHaptics())
        {
            if (info.id == id)
            {
                name = info.name;
                connected = true;
                break;
            }
        }
        if (!connected)
        {
            return {};
        }
        std::unique_ptr<CNA::Platform::IPlatformHapticDevice> device = service->Open(id);
        subsystem.TransferOwnership();
        return HapticDevice(std::move(device), &platform, id, std::move(name));
    }

    HapticDevice Haptics::OpenFromJoystickEXT(const std::uint32_t joystickId)
    {
        CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
        CNA::Platform::IPlatformJoystick* joysticks = platform.GetJoystick();
        if (platform.GetHaptics() == nullptr || joysticks == nullptr
            || !joysticks->IsConnected(joystickId))
            return {};
        ScopedHapticSubsystem subsystem(platform);
        if (!subsystem.IsAcquired())
            return {};
        std::unique_ptr<CNA::Platform::IPlatformHapticDevice> device =
            platform.GetHaptics()->OpenFromJoystick(joystickId);
        if (device == nullptr)
            return {};
        subsystem.TransferOwnership();
        return HapticDevice(std::move(device), &platform, std::nullopt, {});
    }

    HapticDevice Haptics::OpenFromMouseEXT()
    {
        CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
        if (platform.GetHaptics() == nullptr)
            return {};
        ScopedHapticSubsystem subsystem(platform);
        if (!subsystem.IsAcquired())
            return {};
        std::unique_ptr<CNA::Platform::IPlatformHapticDevice> device =
            platform.GetHaptics()->OpenFromMouse();
        if (device == nullptr)
            return {};
        subsystem.TransferOwnership();
        return HapticDevice(std::move(device), &platform, std::nullopt, {});
    }

    bool Haptics::IsJoystickHapticEXT(const std::uint32_t joystickId)
    {
        CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
        CNA::Platform::IPlatformJoystick* joysticks = platform.GetJoystick();
        if (platform.GetHaptics() == nullptr || joysticks == nullptr
            || !joysticks->IsConnected(joystickId))
            return false;
        ScopedHapticSubsystem subsystem(platform);
        return subsystem.IsAcquired()
            && platform.GetHaptics()->IsJoystickHaptic(joystickId);
    }

    bool Haptics::IsMouseHapticEXT()
    {
        CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
        if (platform.GetHaptics() == nullptr)
            return false;
        ScopedHapticSubsystem subsystem(platform);
        return subsystem.IsAcquired() && platform.GetHaptics()->IsMouseHaptic();
    }
}
