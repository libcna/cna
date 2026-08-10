// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Defines the current capture state of a microphone device. */
    enum class MicrophoneState
    {
        /** @brief The microphone is actively capturing samples. */
        Started,

        /** @brief The microphone is not capturing samples. */
        Stopped
    };
}
