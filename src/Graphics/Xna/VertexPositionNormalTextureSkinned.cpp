// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"

#include <cstddef>

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const VertexDeclaration& VertexPositionNormalTextureSkinned::getVertexDeclarationStatic()
    {
        using Stream = CNA::Internal::Graphics::PositionNormalTextureSkinnedStream;
        static const VertexDeclaration decl(
            static_cast<int>(sizeof(Stream)),
            {
                VertexElement(static_cast<int>(offsetof(Stream, x)),
                              VertexElementFormat::Vector3, VertexElementUsage::Position,          0),
                VertexElement(static_cast<int>(offsetof(Stream, nx)),
                              VertexElementFormat::Vector3, VertexElementUsage::Normal,            0),
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

    std::string VertexPositionNormalTextureSkinned::ToString() const
    {
        return "{{Position:" + Position.ToString()
             + " Normal:" + Normal.ToString()
             + " TextureCoordinate:" + TextureCoordinate.ToString()
             + " BlendWeight:" + BlendWeight.ToString()
             + " BlendIndices:{" + std::to_string(BlendIndices[0]) + " " + std::to_string(BlendIndices[1])
             + " " + std::to_string(BlendIndices[2]) + " " + std::to_string(BlendIndices[3]) + "}"
             + "}}";
    }
}
