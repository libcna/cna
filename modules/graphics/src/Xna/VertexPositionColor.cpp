// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstddef>

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "VertexValueHash.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    std::size_t VertexPositionColor::GetHashCode() const
    {
        using CNA::Internal::Graphics::SmartVertexHash;
        using CNA::Internal::Graphics::VertexFloatWord;
        return SmartVertexHash(VertexFloatWord(Position.X),
                               VertexFloatWord(Position.Y),
                               VertexFloatWord(Position.Z),
                               Color.getPackedValueProperty());
    }

    const VertexDeclaration& VertexPositionColor::getVertexDeclarationStatic()
    {
        using Stream = CNA::Internal::Graphics::PositionColorStream;
        static const VertexDeclaration decl = [] {
            VertexDeclaration result(
                static_cast<int>(sizeof(Stream)),
                {
                    VertexElement(static_cast<int>(offsetof(Stream, x)),
                                  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                    VertexElement(static_cast<int>(offsetof(Stream, r)),
                                  VertexElementFormat::Color, VertexElementUsage::Color, 0),
                }
            );
            // XNA Refresh VertexPositionColor::.cctor IL_003a-0045 sets Name.
            result.setNameProperty("VertexPositionColor.VertexDeclaration");
            return result;
        }();
        return decl;
    }

    // Refresh VertexPositionColor.ToString IL_0000-0033 emits one outer brace pair;
    // FNA instead concatenates double literal braces.
    std::string VertexPositionColor::ToString() const
    {
        return "{Position:" + Position.ToString()
             + " Color:" + Color.ToString()
             + "}";
    }
}
