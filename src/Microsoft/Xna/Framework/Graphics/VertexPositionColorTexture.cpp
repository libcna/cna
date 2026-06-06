#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const VertexDeclaration& VertexPositionColorTexture::getVertexDeclarationStatic()
    {
        static const VertexDeclaration decl(
            static_cast<int>(sizeof(VertexPositionColorTexture)),
            {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,         0),
                VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,             0),
                VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            }
        );
        return decl;
    }

    std::string VertexPositionColorTexture::ToString() const
    {
        return "{Position: Vector3 Color: Color TextureCoordinate: Vector2}";
    }
}
