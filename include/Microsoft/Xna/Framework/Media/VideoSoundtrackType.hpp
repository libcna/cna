// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Media
{
    /**
     * @brief Defines the type of audio content in a video.
     *
     * Metadata only, on both FNA and CNA: confirmed by reading every FNA Video source file that
     * nothing in playback logic ever branches on this value to affect volume or ducking. Not a
     * gap -- do not add new ducking/muting behavior FNA itself never had.
     */
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
