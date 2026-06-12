// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Audio
{
    /// Specifies how a playing cue or category should be stopped.
    enum class AudioStopOptions
    {
        /// Stop after all authored release phases complete.
        AsAuthored,

        /// Stop immediately, cutting off release tails.
        Immediate
    };
}
