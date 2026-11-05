// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include <cstddef>

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "VertexValueHash.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    std::size_t VertexPositionColorTexture::GetHashCode() const
    {
        using CNA::Internal::Graphics::SmartVertexHash;
        using CNA::Internal::Graphics::VertexFloatWord;
        return SmartVertexHash(VertexFloatWord(Position.X),
                               VertexFloatWord(Position.Y),
                               VertexFloatWord(Position.Z),
                               Color.getPackedValueProperty(),
                               VertexFloatWord(TextureCoordinate.X),
                               VertexFloatWord(TextureCoordinate.Y));
    }

    const VertexDeclaration& VertexPositionColorTexture::getVertexDeclarationStatic()
    {
        using Stream = CNA::Internal::Graphics::PositionColorTextureStream;
        static const VertexDeclaration decl = [] {
            VertexDeclaration result(
                static_cast<int>(sizeof(Stream)),
                {
                    VertexElement(static_cast<int>(offsetof(Stream, x)),
                                  VertexElementFormat::Vector3, VertexElementUsage::Position,          0),
                    VertexElement(static_cast<int>(offsetof(Stream, r)),
                                  VertexElementFormat::Color,   VertexElementUsage::Color,             0),
                    VertexElement(static_cast<int>(offsetof(Stream, u)),
                                  VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
                }
            );
            // XNA Refresh VertexPositionColorTexture::.cctor IL_0050-005b sets Name.
            result.setNameProperty("VertexPositionColorTexture.VertexDeclaration");
            return result;
        }();
        return decl;
    }

    // Refresh VertexPositionColorTexture.ToString IL_0000-0041 emits one outer brace pair;
    // FNA instead concatenates double literal braces.
    std::string VertexPositionColorTexture::ToString() const
    {
        return "{Position:" + Position.ToString()
             + " Color:" + Color.ToString()
             + " TextureCoordinate:" + TextureCoordinate.ToString()
             + "}";
    }
}
