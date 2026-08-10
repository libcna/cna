// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Defines the channel layout of audio data. */
    enum class AudioChannels : int
    {
        /** @brief Single-channel audio. */
        Mono = 1,

        /** @brief Two-channel stereo audio. */
        Stereo = 2
    };
}
