// SPDX-License-Identifier: MS-PL

// The playback device of the selected audio platform. Capture has its own factory
// (AudioRecordingDeviceFactory.cpp): only Microphone asks for it, and a game that only plays
// sound must not link the capture backend.
#include "Platform/AudioDeviceFactory.hpp"
#include "Platform/Null/NullAudioDevice.hpp"

#if defined(CNA_AUDIO_PLATFORM_SDL3)
#include "Platform/Sdl3/Sdl3AudioDevice.hpp"
#elif defined(CNA_AUDIO_PLATFORM_ALSA)
#include "Platform/Alsa/AlsaAudioDevice.hpp"
#endif

#include <memory>

namespace CNA::Audio::Platform {

    std::unique_ptr<IAudioDevice> CreateSelectedAudioDevice()
    {
#if defined(CNA_AUDIO_PLATFORM_SDL3)
        return std::make_unique<Sdl3::Sdl3AudioDevice>();
#elif defined(CNA_AUDIO_PLATFORM_ALSA)
        return std::make_unique<Alsa::AlsaAudioDevice>();
#elif defined(CNA_AUDIO_PLATFORM_NULL)
        return std::make_unique<Null::NullAudioDevice>();
#else
#error "CNA audio platform selection did not define an implementation"
#endif
    }

} // namespace CNA::Audio::Platform
