// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Audio/Platform/IAudioDevice.hpp"

#include <memory>

namespace CNA::Audio::Platform {

    /** @brief Constructs the playback device selected by `CNA_AUDIO_PLATFORM`. */
    [[nodiscard]] std::unique_ptr<IAudioDevice> CreateSelectedAudioDevice();

} // namespace CNA::Audio::Platform
