// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines the primitive topology for drawing operations.
    enum class PrimitiveType
    {
        /// A list of isolated triangles.
        TriangleList = 0,
        /// A strip of connected triangles.
        TriangleStrip = 1,
        /// A list of isolated line segments.
        LineList = 2,
        /// A strip of connected line segments.
        LineStrip = 3
    };
}
