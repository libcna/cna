#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const VertexDeclaration VertexPositionNormalTexture::VertexDeclaration(
        static_cast<int>(sizeof(VertexPositionNormalTexture)),
        {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,         0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal,            0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        }
    );

    std::string VertexPositionNormalTexture::ToString() const
    {
        return "{Position: Vector3 Normal: Vector3 TextureCoordinate: Vector2}";
    }
}
