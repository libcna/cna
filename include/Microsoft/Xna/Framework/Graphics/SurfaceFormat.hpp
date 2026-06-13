// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines types of surface formats for textures, back buffers, and render targets. */
    enum class SurfaceFormat
    {
        /** @brief Unsigned 32-bit ARGB pixel format storing 8 bits per channel. */
        Color,
        /** @brief Unsigned 16-bit BGR pixel format: 5 bits blue, 6 bits green, 5 bits red. */
        Bgr565,
        /** @brief Unsigned 16-bit BGRA pixel format: 5 bits per color channel, 1 bit alpha. */
        Bgra5551,
        /** @brief Unsigned 16-bit BGRA pixel format storing 4 bits per channel. */
        Bgra4444,
        /** @brief DXT1 compressed texture format; surface dimensions must be a multiple of 4. */
        Dxt1,
        /** @brief DXT3 compressed texture format; surface dimensions must be a multiple of 4. */
        Dxt3,
        /** @brief DXT5 compressed texture format; surface dimensions must be a multiple of 4. */
        Dxt5,
        /** @brief Signed 16-bit bump-map format storing 8 bits each for u and v data. */
        NormalizedByte2,
        /** @brief Signed 16-bit bump-map format storing 8 bits per channel. */
        NormalizedByte4,
        /** @brief Unsigned 32-bit RGBA pixel format: 10 bits per color channel, 2 bits alpha. */
        Rgba1010102,
        /** @brief Unsigned 32-bit RG pixel format using 16 bits per channel. */
        Rg32,
        /** @brief Unsigned 64-bit RGBA pixel format using 16 bits per channel. */
        Rgba64,
        /** @brief Unsigned 8-bit alpha-only format. */
        Alpha8,
        /** @brief IEEE 32-bit single-precision float storing one channel (red). */
        Single,
        /** @brief IEEE 64-bit float format storing 32 bits per channel (RG). */
        Vector2,
        /** @brief IEEE 128-bit float format storing 32 bits per channel (RGBA). */
        Vector4,
        /** @brief 16-bit half-precision float storing one channel (red). */
        HalfSingle,
        /** @brief 32-bit half-precision float format storing 16 bits per channel (RG). */
        HalfVector2,
        /** @brief 64-bit half-precision float format storing 16 bits per channel (RGBA). */
        HalfVector4,
        /** @brief Float pixel format for high dynamic range data. */
        HdrBlendable,
        /** @brief Unsigned 32-bit ARGB sRGB pixel format storing 8 bits per channel; values are sRGB-encoded and read as linear in shaders. */
        ColorSrgb,
        /** @brief Unsigned 16-bit BGR sRGB pixel format: 5 bits blue, 6 bits green, 5 bits red; sRGB-encoded. */
        Bgr565Srgb,
        /** @brief Unsigned 16-bit BGRA sRGB pixel format: 5 bits per color, 1 bit alpha; sRGB-encoded. */
        Bgra5551Srgb,
        /** @brief Unsigned 16-bit BGRA sRGB pixel format storing 4 bits per channel; sRGB-encoded. */
        Bgra4444Srgb,
        /** @brief DXT1 compressed sRGB texture format; dimensions must be a multiple of 4; sRGB-encoded. */
        Dxt1Srgb,
        /** @brief DXT3 compressed sRGB texture format; dimensions must be a multiple of 4; sRGB-encoded. */
        Dxt3Srgb,
        /** @brief DXT5 compressed sRGB texture format; dimensions must be a multiple of 4; sRGB-encoded. */
        Dxt5Srgb
    };
}
