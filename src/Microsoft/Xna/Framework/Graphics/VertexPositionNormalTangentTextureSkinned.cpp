// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"

#include <cstddef>

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const VertexDeclaration& VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic()
    {
        using Stream = CNA::Internal::Graphics::PositionNormalTangentTextureSkinnedStream;
        static const VertexDeclaration decl(
            static_cast<int>(sizeof(Stream)),
            {
                VertexElement(static_cast<int>(offsetof(Stream, x)),
                              VertexElementFormat::Vector3, VertexElementUsage::Position,          0),
                VertexElement(static_cast<int>(offsetof(Stream, nx)),
                              VertexElementFormat::Vector3, VertexElementUsage::Normal,            0),
                VertexElement(static_cast<int>(offsetof(Stream, tx)),
                              VertexElementFormat::Vector4, VertexElementUsage::Tangent,           0),
                VertexElement(static_cast<int>(offsetof(Stream, u)),
                              VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
                VertexElement(static_cast<int>(offsetof(Stream, w0)),
                              VertexElementFormat::Vector4, VertexElementUsage::BlendWeight,       0),
                VertexElement(static_cast<int>(offsetof(Stream, i0)),
                              VertexElementFormat::Byte4,   VertexElementUsage::BlendIndices,      0),
            }
        );
        return decl;
    }

    std::string VertexPositionNormalTangentTextureSkinned::ToString() const
    {
        return "{{Position:" + Position.ToString()
             + " Normal:" + Normal.ToString()
             + " Tangent:" + Tangent.ToString()
             + " TextureCoordinate:" + TextureCoordinate.ToString()
             + " BlendWeight:" + BlendWeight.ToString()
             + " BlendIndices:{" + std::to_string(BlendIndices[0]) + " " + std::to_string(BlendIndices[1])
             + " " + std::to_string(BlendIndices[2]) + " " + std::to_string(BlendIndices[3]) + "}"
             + "}}";
    }
}
