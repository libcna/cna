// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines pixel formats for textures, back buffers and render targets.
    enum class SurfaceFormat
    {
        Color,
        Bgr565,
        Bgra5551,
        Bgra4444,
        Dxt1,
        Dxt3,
        Dxt5,
        NormalizedByte2,
        NormalizedByte4,
        Rgba1010102,
        Rg32,
        Rgba64,
        Alpha8,
        Single,
        Vector2,
        Vector4,
        HalfSingle,
        HalfVector2,
        HalfVector4,
        HdrBlendable,
        ColorSrgb,
        Bgr565Srgb,
        Bgra5551Srgb,
        Bgra4444Srgb,
        Dxt1Srgb,
        Dxt3Srgb,
        Dxt5Srgb
    };
}
