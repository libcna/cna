// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/24/25.
//

#pragma once


namespace Microsoft::Xna::Framework::Audio
{
    /// Specifies the playback state of a sound.
    enum class SoundState
    {
        /// The sound is currently playing.
        Playing,

        /// The sound is currently paused.
        Paused,

        /// The sound is currently stopped.
        Stopped,
    };
}
