// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Media
{
    /// Defines the type of sounds in a video.
    enum class VideoSoundtrackType
    {
        /// This video contains only music.
        Music,

        /// This video contains only dialog.
        Dialog,

        /// This video contains music and dialog.
        MusicAndDialog
    };
}
