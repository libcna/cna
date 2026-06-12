// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines texture filtering modes during texture sampling.
    enum class TextureFilter
    {
        Linear,
        Point,
        Anisotropic,
        LinearMipPoint,
        PointMipLinear,
        MinLinearMagPointMipLinear,
        MinLinearMagPointMipPoint,
        MinPointMagLinearMipLinear,
        MinPointMagLinearMipPoint,
    };
}
