// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines modes for addressing texels using texture coordinates that fall outside the range 0.0 to 1.0. */
    enum class TextureAddressMode
    {
        /** @brief Texels outside the range tile at every integer junction. */
        Wrap,
        /** @brief Texels outside the range are clamped to the texel at 0.0 or 1.0. */
        Clamp,
        /** @brief Same as Wrap but tiles are also flipped at every integer junction. */
        Mirror,
    };
}
