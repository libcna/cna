#pragma once

namespace Microsoft::Xna::Framework::Audio
{
    /// Defines the current state of a microphone.
    enum class MicrophoneState
    {
        /// The microphone is not capturing samples.
        Stopped,

        /// The microphone is capturing samples.
        Started
    };
}
