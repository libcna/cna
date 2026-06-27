// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const VertexDeclaration& VertexPositionNormalTexture::getVertexDeclarationStatic()
    {
        static const VertexDeclaration decl(
            static_cast<int>(sizeof(VertexPositionNormalTexture)),
            {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,         0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal,            0),
                VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
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
