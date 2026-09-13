// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstddef>

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "VertexValueHash.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    std::size_t VertexPositionTexture::GetHashCode() const
    {
        using CNA::Internal::Graphics::SmartVertexHash;
        using CNA::Internal::Graphics::VertexFloatWord;
        return SmartVertexHash(VertexFloatWord(Position.X),
                               VertexFloatWord(Position.Y),
                               VertexFloatWord(Position.Z),
                               VertexFloatWord(TextureCoordinate.X),
                               VertexFloatWord(TextureCoordinate.Y));
    }

    const VertexDeclaration& VertexPositionTexture::getVertexDeclarationStatic()
    {
        using Stream = CNA::Internal::Graphics::PositionTextureStream;
        static const VertexDeclaration decl(
            static_cast<int>(sizeof(Stream)),
            {
                VertexElement(static_cast<int>(offsetof(Stream, x)),
                              VertexElementFormat::Vector3, VertexElementUsage::Position,          0),
                VertexElement(static_cast<int>(offsetof(Stream, u)),
                              VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            }
        );
        return decl;
    }

    std::string VertexPositionTexture::ToString() const
    {
        return "{{Position:" + Position.ToString()
             + " TextureCoordinate:" + TextureCoordinate.ToString()
             + "}}";
    }
}
