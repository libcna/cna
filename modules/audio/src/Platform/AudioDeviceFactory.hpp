// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Audio/Platform/IAudioDevice.hpp"
#include "CNA/Audio/Platform/IAudioRecordingDevice.hpp"

#include <memory>

namespace CNA::Audio::Platform {

    /** @brief Constructs the playback device selected by `CNA_AUDIO_PLATFORM`. */
    [[nodiscard]] std::unique_ptr<IAudioDevice> CreateSelectedAudioDevice();

    /** @brief Constructs the selected recording provider, or null when unsupported. */
    [[nodiscard]] std::unique_ptr<IAudioRecordingDeviceProvider>
    CreateSelectedAudioRecordingDeviceProvider();

} // namespace CNA::Audio::Platform
