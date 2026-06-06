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
    struct VertexPositionTexture : public IVertexType
    {
        Microsoft::Xna::Framework::Vector3 Position;
        Microsoft::Xna::Framework::Vector2 TextureCoordinate;

        VertexPositionTexture() = default;

        VertexPositionTexture(const Microsoft::Xna::Framework::Vector3& position,
                              const Microsoft::Xna::Framework::Vector2& textureCoordinate)
            : Position(position), TextureCoordinate(textureCoordinate)
        {
        }

        [[nodiscard]] static const Graphics::VertexDeclaration& getVertexDeclarationStatic();

        [[nodiscard]] const Graphics::VertexDeclaration& getVertexDeclarationProperty() const override
        {
            return getVertexDeclarationStatic();
        }

        bool operator==(const VertexPositionTexture& o) const
        {
            return Position == o.Position && TextureCoordinate == o.TextureCoordinate;
        }
        bool operator!=(const VertexPositionTexture& o) const { return !(*this == o); }
        [[nodiscard]] std::string ToString() const;
    };
}
