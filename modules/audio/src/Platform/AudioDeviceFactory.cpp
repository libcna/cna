// SPDX-License-Identifier: MS-PL

#include "Platform/AudioDeviceFactory.hpp"
#include "Platform/Null/NullAudioDevice.hpp"

#if defined(CNA_AUDIO_PLATFORM_SDL3)
#include "Platform/Sdl3/Sdl3AudioDevice.hpp"
#include "Platform/Sdl3/Sdl3AudioRecordingDevice.hpp"
#elif defined(CNA_AUDIO_PLATFORM_SDL2)
#include "Platform/Sdl2/Sdl2AudioDevice.hpp"
#elif defined(CNA_AUDIO_PLATFORM_ALSA)
#include "Platform/Alsa/AlsaAudioDevice.hpp"
#endif

#include <memory>

namespace CNA::Audio::Platform {

    std::unique_ptr<IAudioDevice> CreateSelectedAudioDevice()
    {
#if defined(CNA_AUDIO_PLATFORM_SDL3)
        return std::make_unique<Sdl3::Sdl3AudioDevice>();
#elif defined(CNA_AUDIO_PLATFORM_SDL2)
        return std::make_unique<Sdl2::Sdl2AudioDevice>();
#elif defined(CNA_AUDIO_PLATFORM_ALSA)
        return std::make_unique<Alsa::AlsaAudioDevice>();
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
#elif defined(CNA_AUDIO_PLATFORM_SDL2)
        // Playback is available through SDL2.  Capture has no equivalent implementation yet,
        // so preserve the platform contract's explicit "unsupported" representation.
        return nullptr;
#elif defined(CNA_AUDIO_PLATFORM_ALSA)
        // Capture through ALSA is not implemented: Microphone reports no devices.
        return nullptr;
#elif defined(CNA_AUDIO_PLATFORM_NULL)
        return nullptr;
#else
#error "CNA audio platform selection did not define an implementation"
#endif
    }

} // namespace CNA::Audio::Platform
