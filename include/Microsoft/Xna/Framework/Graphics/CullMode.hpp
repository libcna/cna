// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines a culling mode for faces in the rasterization process.
    enum class CullMode
    {
        None,
        CullClockwiseFace,
        CullCounterClockwiseFace,
    };
}
