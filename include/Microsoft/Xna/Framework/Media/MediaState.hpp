#pragma once

namespace Microsoft::Xna::Framework::Media
{
    /// Defines the current playback state of the media player.
    enum class MediaState
    {
        /// Media playback is stopped.
        Stopped,

        /// Media playback is playing.
        Playing,

        /// Media playback is paused.
        Paused
    };
}
