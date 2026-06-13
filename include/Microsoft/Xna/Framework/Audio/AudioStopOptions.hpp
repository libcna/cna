// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Specifies how a playing cue or category should be stopped. */
    enum class AudioStopOptions
    {
        /** @brief Stop after all authored release phases complete. */
        AsAuthored,

        /** @brief Stop immediately, cutting off release tails. */
        Immediate
    };
}
