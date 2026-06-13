// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines whether the previous render-target content is preserved when it is set on the graphics device. */
    enum class RenderTargetUsage
    {
        /** @brief The render target content will not be preserved. */
        DiscardContents,
        /** @brief The render target content will be preserved even if it is slow or requires extra memory. */
        PreserveContents,
        /** @brief The render target content may be preserved if the platform can do so without a performance or memory penalty. */
        PlatformContents
    };
}
