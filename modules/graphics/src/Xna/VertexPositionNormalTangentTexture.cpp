// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"

#include <cstddef>

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const VertexDeclaration& VertexPositionNormalTangentTexture::getVertexDeclarationStatic()
    {
        using Stream = CNA::Internal::Graphics::PositionNormalTangentTextureStream;
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
            }
        );
        return decl;
    }

    std::string VertexPositionNormalTangentTexture::ToString() const
    {
        return "{{Position:" + Position.ToString()
             + " Normal:" + Normal.ToString()
             + " Tangent:" + Tangent.ToString()
             + " TextureCoordinate:" + TextureCoordinate.ToString()
             + "}}";
    }
}
