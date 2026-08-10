// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines formats for the depth-stencil buffer. */
    enum class DepthFormat
    {
        /** @brief No depth-stencil buffer will be created. */
        None,
        /** @brief 16-bit depth buffer. */
        Depth16,
        /** @brief 24-bit depth buffer. */
        Depth24,
        /** @brief 32-bit depth-stencil buffer: 24 bits for depth and 8 bits for stencil. */
        Depth24Stencil8
    };
}
