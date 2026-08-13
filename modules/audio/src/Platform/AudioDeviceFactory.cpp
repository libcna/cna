// SPDX-License-Identifier: MS-PL

#include "Platform/AudioDeviceFactory.hpp"

#if defined(CNA_AUDIO_PLATFORM_SDL3)
#include "Platform/Sdl3/Sdl3AudioDevice.hpp"
#endif

#include <memory>
#include <stdexcept>

namespace CNA::Audio::Platform {

    std::unique_ptr<IAudioDevice> CreateSelectedAudioDevice()
    {
#if defined(CNA_AUDIO_PLATFORM_SDL3)
        return std::make_unique<Sdl3::Sdl3AudioDevice>();
#elif defined(CNA_AUDIO_PLATFORM_NULL)
        // Selection is already a hard identity, but its real implementation deliberately lands
        // in PLAT-99. Refuse here instead of manufacturing an SDL3 fallback under a NULL build.
        throw std::runtime_error(
            "CNA_AUDIO_PLATFORM=NULL playback is not implemented until PLAT-99");
#else
#error "CNA audio platform selection did not define an implementation"
#endif
    }

} // namespace CNA::Audio::Platform
