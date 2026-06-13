// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines how GraphicsDevice::Present updates the game window relative to the display refresh. */
    enum class PresentInterval
    {
        /** @brief Equivalent to PresentInterval::One. */
        Default,
        /** @brief Wait for one vertical retrace period before updating; frame rate is capped to the refresh rate. */
        One,
        /** @brief Wait for two vertical retrace periods before updating; frame rate is capped to half the refresh rate. */
        Two,
        /** @brief Update the window immediately without waiting for vertical retrace; no frame rate limit. */
        Immediate
    };
}
