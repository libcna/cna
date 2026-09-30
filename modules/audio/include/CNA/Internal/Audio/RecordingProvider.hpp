// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Audio/Platform/IAudioRecordingDevice.hpp"

namespace CNA::Internal::Audio
{
    /**
     * @brief The selected audio platform's recording provider, shared by XNA's Microphone and by
     * CNA's own capture users (network voice).
     *
     * Each user opens its own session from it, so network voice never takes bytes a game's
     * Microphone was going to read.
     *
     * @return The provider, or null when this build or platform has no capture.
     */
    CNA::Audio::Platform::IAudioRecordingDeviceProvider* RecordingProvider();
}
