// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines stencil buffer operations.
    enum class StencilOperation
    {
        Keep,
        Zero,
        Replace,
        Increment,
        Decrement,
        IncrementSaturation,
        DecrementSaturation,
        Invert,
    };
}
