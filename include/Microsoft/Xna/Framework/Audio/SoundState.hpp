// SPDX-License-Identifier: MS-PL
#pragma once


namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Specifies the current playback state of a sound effect instance. */
    enum class SoundState
    {
        /** @brief The sound is currently playing. */
        Playing,

        /** @brief The sound is currently paused. */
        Paused,

        /** @brief The sound is currently stopped. */
        Stopped,
    };
}
