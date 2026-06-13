// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Media
{
    /** @brief Defines the current playback state of the media player. */
    enum class MediaState
    {
        /** @brief Media playback is stopped. */
        Stopped,

        /** @brief Media playback is playing. */
        Playing,

        /** @brief Media playback is paused. */
        Paused
    };
}
