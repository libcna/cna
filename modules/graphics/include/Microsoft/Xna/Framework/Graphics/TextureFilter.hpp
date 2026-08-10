// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines filtering types for the texture sampler. */
    enum class TextureFilter
    {
        /** @brief Use linear filtering. */
        Linear,
        /** @brief Use point filtering. */
        Point,
        /** @brief Use anisotropic filtering. */
        Anisotropic,
        /** @brief Use linear filtering to shrink or expand, and point filtering between mipmap levels. */
        LinearMipPoint,
        /** @brief Use point filtering to shrink or expand, and linear filtering between mipmap levels. */
        PointMipLinear,
        /** @brief Use linear filtering to shrink, point filtering to expand, and linear filtering between mipmap levels. */
        MinLinearMagPointMipLinear,
        /** @brief Use linear filtering to shrink, point filtering to expand, and point filtering between mipmap levels. */
        MinLinearMagPointMipPoint,
        /** @brief Use point filtering to shrink, linear filtering to expand, and linear filtering between mipmap levels. */
        MinPointMagLinearMipLinear,
        /** @brief Use point filtering to shrink, linear filtering to expand, and point filtering between mipmap levels. */
        MinPointMagLinearMipPoint,
    };
}
