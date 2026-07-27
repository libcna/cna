// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstddef>

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const VertexDeclaration& VertexPositionColor::getVertexDeclarationStatic()
    {
        using Stream = CNA::Internal::Graphics::PositionColorStream;
        static const VertexDeclaration decl(
            static_cast<int>(sizeof(Stream)),
            {
                VertexElement(static_cast<int>(offsetof(Stream, x)),
                              VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(static_cast<int>(offsetof(Stream, r)),
                              VertexElementFormat::Color, VertexElementUsage::Color, 0),
            }
        );
        return decl;
    }

    std::string VertexPositionColor::ToString() const
    {
        return "{{Position:" + Position.ToString()
             + " Color:" + Color.ToString()
             + "}}";
    }
}
