#pragma once

namespace Microsoft::Xna::Framework::Audio
{
    /// Defines the channel layout of audio data.
    enum class AudioChannels : int
    {
        /// Single-channel audio.
        Mono = 1,

        /// Two-channel stereo audio.
        Stereo = 2
    };
}
