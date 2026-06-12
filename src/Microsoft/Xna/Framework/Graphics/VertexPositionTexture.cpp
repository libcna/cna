// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const VertexDeclaration& VertexPositionTexture::getVertexDeclarationStatic()
    {
        static const VertexDeclaration decl(
            static_cast<int>(sizeof(VertexPositionTexture)),
            {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,         0),
                VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            }
        );
        return decl;
    }

    std::string VertexPositionTexture::ToString() const
    {
        return "{Position: Vector3 TextureCoordinate: Vector2}";
    }
}
