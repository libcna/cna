// SPDX-License-Identifier: MS-PL
// SAMPLE-002: real EasyGL regression for the vertex layout used by Microsoft's
// Primitives3D sample. Position+Normal is intentionally 24 bytes, the same stride as
// VertexPositionColorTexture; the declaration must select BasicEffect lighting.

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    struct VertexPositionNormal
    {
        Vector3 Position;
        Vector3 Normal;
    };

    static_assert(sizeof(VertexPositionNormal) == 24);
    static_assert(std::is_trivially_copyable_v<VertexPositionNormal>);

    VertexDeclaration PositionNormalDeclaration()
    {
        return VertexDeclaration(
            24,
            {
                VertexElement(0, VertexElementFormat::Vector3,
                              VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3,
                              VertexElementUsage::Normal, 0),
            });
    }
}

class BasicEffectPositionNormalTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto declaration = PositionNormalDeclaration();
        VertexBuffer vertices(device, declaration, 4, BufferUsage::None);
        IndexBuffer indices(
            device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        const std::array<std::uint16_t, 6> indexData{0, 1, 2, 0, 2, 3};
        indices.SetData(indexData.data(), static_cast<int>(indexData.size()));

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(true);
        effect.setPreferPerPixelLightingProperty(false);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        effect.setEmissiveColorProperty(Vector3::Zero);
        effect.setSpecularColorProperty(Vector3::Zero);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        effect.DirectionalLight0.setSpecularColorProperty(Vector3::Zero);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);

        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetDepthTestEnabled(false);
        device.SetVertexBuffer(&vertices);
        device.setIndicesProperty(&indices);

        const auto drawNormal = [&](const Vector3& normal)
        {
            const std::array<VertexPositionNormal, 4> vertexData{{
                {Vector3(-1.0f,  1.0f, 0.0f), normal},
                {Vector3(-1.0f, -1.0f, 0.0f), normal},
                {Vector3( 1.0f, -1.0f, 0.0f), normal},
                {Vector3( 1.0f,  1.0f, 0.0f), normal},
            }};
            vertices.SetData(vertexData.data(), static_cast<int>(vertexData.size()));
            device.Clear(Color(3, 7, 11, 255));
            effect.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);

            Color result(0, 0, 0, 0);
            const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
            device.GetBackBufferData(&centre, &result, 0, 1);
            return result;
        };

        const Color lit = drawNormal(Vector3(0.0f, 0.0f, 1.0f));
        const Color unlit = drawNormal(Vector3(0.0f, 0.0f, -1.0f));
        Check(lit.getRProperty() >= 245 && lit.getGProperty() <= 8 &&
                  lit.getBProperty() <= 8,
              "Position+Normal declaration produces the expected lit red surface");
        Check(unlit.getRProperty() <= 8 && unlit.getGProperty() <= 8 &&
                  unlit.getBProperty() <= 8,
              "reversing the custom normal removes the directional contribution");
    }

public:
    BasicEffectPositionNormalTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kSize);
        graphics_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<BasicEffectPositionNormalTest>();
}
