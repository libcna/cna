// SPDX-License-Identifier: MS-PL

#include "Platform/AudioDeviceFactory.hpp"
#include "Platform/Null/NullAudioDevice.hpp"

#if defined(CNA_AUDIO_PLATFORM_SDL3)
#include "Platform/Sdl3/Sdl3AudioDevice.hpp"
#include "Platform/Sdl3/Sdl3AudioRecordingDevice.hpp"
#endif

#include <memory>

namespace CNA::Audio::Platform {

    std::unique_ptr<IAudioDevice> CreateSelectedAudioDevice()
    {
#if defined(CNA_AUDIO_PLATFORM_SDL3)
        return std::make_unique<Sdl3::Sdl3AudioDevice>();
#elif defined(CNA_AUDIO_PLATFORM_NULL)
        return std::make_unique<Null::NullAudioDevice>();
#else
#error "CNA audio platform selection did not define an implementation"
#endif
    }

    std::unique_ptr<IAudioRecordingDeviceProvider>
    CreateSelectedAudioRecordingDeviceProvider()
    {
#if defined(CNA_AUDIO_PLATFORM_SDL3)
        return std::make_unique<Sdl3::Sdl3AudioRecordingDeviceProvider>();
#elif defined(CNA_AUDIO_PLATFORM_NULL)
        return nullptr;
#else
#error "CNA audio platform selection did not define an implementation"
#endif
    }

} // namespace CNA::Audio::Platform
