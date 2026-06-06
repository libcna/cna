#pragma once

#include <string>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/IVertexType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    struct VertexPositionNormalTexture : public IVertexType
    {
        Vector3 Position;
        Vector3 Normal;
        Vector2 TextureCoordinate;

        VertexPositionNormalTexture() = default;

        VertexPositionNormalTexture(const Vector3& position,
                                    const Vector3& normal,
                                    const Vector2& textureCoordinate)
            : Position(position), Normal(normal), TextureCoordinate(textureCoordinate)
        {
        }

        static const VertexDeclaration VertexDeclaration;

        [[nodiscard]] const Graphics::VertexDeclaration& getVertexDeclarationProperty() const override
        {
            return VertexDeclaration;
        }

        bool operator==(const VertexPositionNormalTexture& o) const
        {
            return Position == o.Position && Normal == o.Normal && TextureCoordinate == o.TextureCoordinate;
        }
        bool operator!=(const VertexPositionNormalTexture& o) const { return !(*this == o); }
        [[nodiscard]] std::string ToString() const;
    };
}
