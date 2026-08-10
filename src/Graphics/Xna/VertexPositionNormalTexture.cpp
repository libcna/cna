// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstddef>

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const VertexDeclaration& VertexPositionNormalTexture::getVertexDeclarationStatic()
    {
        using Stream = CNA::Internal::Graphics::PositionNormalTextureStream;
        static const VertexDeclaration decl(
            static_cast<int>(sizeof(Stream)),
            {
                VertexElement(static_cast<int>(offsetof(Stream, x)),
                              VertexElementFormat::Vector3, VertexElementUsage::Position,          0),
                VertexElement(static_cast<int>(offsetof(Stream, nx)),
                              VertexElementFormat::Vector3, VertexElementUsage::Normal,            0),
                VertexElement(static_cast<int>(offsetof(Stream, u)),
                              VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            }
        );
        return decl;
    }

    std::string VertexPositionNormalTexture::ToString() const
    {
        return "{{Position:" + Position.ToString()
             + " Normal:" + Normal.ToString()
             + " TextureCoordinate:" + TextureCoordinate.ToString()
             + "}}";
    }
}
