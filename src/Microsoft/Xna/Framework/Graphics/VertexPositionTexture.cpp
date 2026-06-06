#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const VertexDeclaration VertexPositionTexture::VertexDeclaration(
        static_cast<int>(sizeof(VertexPositionTexture)),
        {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,         0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        }
    );

    std::string VertexPositionTexture::ToString() const
    {
        return "{Position: Vector3 TextureCoordinate: Vector2}";
    }
}
