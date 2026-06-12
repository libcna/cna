// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines the semantic usage of a vertex element within a vertex declaration.
    enum class VertexElementUsage
    {
        /// The element contains a position.
        Position = 0,
        /// The element contains a color.
        Color = 1,
        /// The element contains texture coordinates.
        TextureCoordinate = 2,
        /// The element contains a surface normal.
        Normal = 3,
        /// The element contains a binormal (bitangent) vector.
        Binormal = 4,
        /// The element contains a tangent vector.
        Tangent = 5,
        /// The element contains blend indices for skinning.
        BlendIndices = 6,
        /// The element contains blend weights for skinning.
        BlendWeight = 7,
        /// The element contains depth information.
        Depth = 8,
        /// The element contains a fog factor.
        Fog = 9,
        /// The element contains a point size.
        PointSize = 10,
        /// The element contains a sample index.
        Sample = 11,
        /// The element contains a tessellation factor.
        TessellateFactor = 12,
    };
}
