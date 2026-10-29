// SPDX-License-Identifier: MS-PL
// The capture device provider of the selected audio platform, which Microphone uses.
//
// Link boundary: apart from the playback factory (AudioDeviceFactory.cpp), so that playing a
// sound does not link audio capture.
#include "Platform/AudioDeviceFactory.hpp"

#if defined(CNA_AUDIO_PLATFORM_SDL3)
#include "Platform/Sdl3/Sdl3AudioRecordingDevice.hpp"
#elif defined(CNA_AUDIO_PLATFORM_ALSA)
#include "Platform/Alsa/AlsaAudioRecordingDevice.hpp"
#endif

#include <memory>

namespace CNA::Audio::Platform {

    std::unique_ptr<IAudioRecordingDeviceProvider>
    CreateSelectedAudioRecordingDeviceProvider()
    {
#if defined(CNA_AUDIO_PLATFORM_SDL3)
        return std::make_unique<Sdl3::Sdl3AudioRecordingDeviceProvider>();
#elif defined(CNA_AUDIO_PLATFORM_ALSA)
        // X11-0162: capture through ALSA.
        return std::make_unique<Alsa::AlsaAudioRecordingDeviceProvider>();
#elif defined(CNA_AUDIO_PLATFORM_NULL)
        return nullptr;
#else
#error "CNA audio platform selection did not define an implementation"
#endif
    }

} // namespace CNA::Audio::Platform
