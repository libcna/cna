// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const VertexDeclaration& VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic()
    {
        static const VertexDeclaration decl(
            static_cast<int>(sizeof(VertexPositionNormalTangentTextureSkinned)),
            {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,         0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal,            0),
                VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent,           0),
                VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
                VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight,        0),
                VertexElement(64, VertexElementFormat::Byte4,   VertexElementUsage::BlendIndices,       0),
            }
        );
        return decl;
    }

    std::string VertexPositionNormalTangentTextureSkinned::ToString() const
    {
        return "{{Position:" + Position.ToString()
             + " Normal:" + Normal.ToString()
             + " Tangent:" + Tangent.ToString()
             + " TextureCoordinate:" + TextureCoordinate.ToString()
             + " BlendWeight:" + BlendWeight.ToString()
             + " BlendIndices:{" + std::to_string(BlendIndices[0]) + " " + std::to_string(BlendIndices[1])
             + " " + std::to_string(BlendIndices[2]) + " " + std::to_string(BlendIndices[3]) + "}"
             + "}}";
    }
}
