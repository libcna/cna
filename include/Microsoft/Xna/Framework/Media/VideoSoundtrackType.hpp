// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Media
{
    /** @brief Defines the type of audio content in a video. */
    enum class VideoSoundtrackType
    {
        /** @brief The video contains music only. */
        Music,

        /** @brief The video contains dialog only. */
        Dialog,

        /** @brief The video contains both music and dialog. */
        MusicAndDialog
    };
}
