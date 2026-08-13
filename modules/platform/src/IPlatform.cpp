// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/IPlatform.hpp"

#include "CNA/Platform/PlatformException.hpp"

namespace CNA::Platform {

    std::unique_ptr<IPlatformWindow> IPlatform::AdoptWindow(const WindowId windowId)
    {
        throw PlatformException(
            "AdoptWindow(" + std::to_string(windowId) + ")",
            "external window adoption is unsupported by " + GetName());
    }

    const std::string& ToString(const PlatformSubsystem subsystem)
    {
        static const std::string video = "Video";
        static const std::string audio = "Audio";
        static const std::string gamepad = "Gamepad";
        static const std::string haptic = "Haptic";
        static const std::string sensor = "Sensor";

        switch (subsystem)
        {
            case PlatformSubsystem::Video:   return video;
            case PlatformSubsystem::Audio:   return audio;
            case PlatformSubsystem::Gamepad: return gamepad;
            case PlatformSubsystem::Haptic:  return haptic;
            case PlatformSubsystem::Sensor:  return sensor;
        }
        return video;
    }

} // namespace CNA::Platform
