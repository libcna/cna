// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-035: DualTextureEffect formula, semantic, sampler-slot,
// null-texture, fog, and stock-effect transition coverage.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 48;

    struct DualVertex
    {
        Vector3 position;
        std::uint32_t color;
        Vector2 textureCoordinate0;
        Vector2 textureCoordinate1;
    };

    static_assert(std::is_trivially_copyable_v<DualVertex>);
    static_assert(sizeof(DualVertex) == 32);
    static_assert(offsetof(DualVertex, color) == 12);
    static_assert(offsetof(DualVertex, textureCoordinate0) == 16);
    static_assert(offsetof(DualVertex, textureCoordinate1) == 24);

    const VertexDeclaration kDualDeclaration(
        32,
        {
            VertexElement(0, VertexElementFormat::Vector3,
                          VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,
                          VertexElementUsage::Color, 0),
            VertexElement(16, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(24, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 1),
        });

    [[nodiscard]] std::array<DualVertex, 6> Quad(
        const float z, const Color& color,
        const Vector2& textureCoordinate0, const Vector2& textureCoordinate1)
    {
        const std::uint32_t packedColor = color.getPackedValueProperty();
        return {{
            {Vector3(-1.0f,  1.0f, z), packedColor,
             textureCoordinate0, textureCoordinate1},
            {Vector3(-1.0f, -1.0f, z), packedColor,
             textureCoordinate0, textureCoordinate1},
            {Vector3( 1.0f, -1.0f, z), packedColor,
             textureCoordinate0, textureCoordinate1},
            {Vector3(-1.0f,  1.0f, z), packedColor,
             textureCoordinate0, textureCoordinate1},
            {Vector3( 1.0f, -1.0f, z), packedColor,
             textureCoordinate0, textureCoordinate1},
            {Vector3( 1.0f,  1.0f, z), packedColor,
             textureCoordinate0, textureCoordinate1},
        }};
    }
}

class RlglDualTextureEffectTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglDualTextureEffectTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferredBackBufferWidthProperty(kWidth);
        graphics_->setPreferredBackBufferHeightProperty(kHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;

        const Rectangle center(kWidth / 2, kHeight / 2, 1, 1);
        const Matrix identity = Matrix::getIdentityProperty();

        Texture2D texture0(device, 2, 1);
        const std::array<Color, 2> texture0Pixels{
            Color(100, 100, 100, 255), Color(0, 128, 0, 255)};
        texture0.SetData(texture0Pixels.data(), static_cast<int>(texture0Pixels.size()));

        Texture2D texture1(device, 2, 1);
        const std::array<Color, 2> texture1Pixels{
            Color(0, 0, 255, 255), Color::White};
        texture1.SetData(texture1Pixels.data(), static_cast<int>(texture1Pixels.size()));

        VertexBuffer vertexBuffer(
            device, kDualDeclaration, 6, BufferUsage::None);
        device.SetVertexBuffer(&vertexBuffer);

        DualTextureEffect effect(device);
        effect.setWorldProperty(identity);
        effect.setViewProperty(identity);
        effect.setProjectionProperty(identity);
        effect.setTextureProperty(&texture0);
        effect.setTexture2Property(&texture1);

        const auto draw = [&](const float z, const Color& color,
                              const Vector2& uv0, const Vector2& uv1)
        {
            const auto vertices = Quad(z, color, uv0, uv1);
            vertexBuffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
            device.Clear(Color::Black);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        };

        draw(0.0f, Color::White, Vector2(0.25f, 0.5f), Vector2(0.75f, 0.5f));
        ExpectPixel(
            "DualTextureEffect uses TextureCoordinate0 and TextureCoordinate1 independently",
            center, Color(200, 200, 200, 255), 2);

        device.getSamplerStatesProperty()[0] = SamplerState::PointWrap;
        device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
        draw(0.0f, Color::White, Vector2(1.25f, 0.5f), Vector2(1.25f, 0.5f));
        ExpectPixel(
            "texture slots keep independent wrap and clamp samplers",
            center, Color(200, 200, 200, 255), 2);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        effect.setTextureProperty(nullptr);
        draw(0.0f, Color::White, Vector2::Zero, Vector2(0.75f, 0.5f));
        ExpectPixel(
            "a null first texture uses rlgl's default white texture",
            center, Color::White);

        effect.setTextureProperty(&texture0);
        effect.setTexture2Property(nullptr);
        draw(0.0f, Color::White, Vector2(0.25f, 0.5f), Vector2::Zero);
        ExpectPixel(
            "a null second texture uses rlgl's default white texture",
            center, Color(200, 200, 200, 255), 2);

        effect.setTexture2Property(&texture1);
        effect.setVertexColorEnabledProperty(true);
        effect.setDiffuseColorProperty(Vector3(1.0f, 0.5f, 1.0f));
        effect.setAlphaProperty(0.5f);
        draw(
            0.0f, Color(128, 255, 128, 128),
            Vector2(0.25f, 0.5f), Vector2(0.75f, 0.5f));
        ExpectPixel(
            "vertex color, diffuse color, and alpha multiply the two textures",
            center, Color(50, 50, 50, 64), 2);
        Color alphaPixel = Color::Black;
        device.GetBackBufferData(&center, &alphaPixel, 0, 1);
        Check(
            std::abs(static_cast<int>(alphaPixel.getAProperty()) - 64) <= 2,
            "DualTextureEffect preserves multiplied alpha under opaque blending");

        effect.setVertexColorEnabledProperty(false);
        draw(
            0.0f, Color(128, 255, 128, 128),
            Vector2(0.25f, 0.5f), Vector2(0.75f, 0.5f));
        ExpectPixel(
            "disabling vertex color removes its contribution on the next draw",
            center, Color(100, 50, 100, 128), 2);

        effect.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        effect.setAlphaProperty(1.0f);
        effect.setFogEnabledProperty(true);
        effect.setFogStartProperty(0.0f);
        effect.setFogEndProperty(1.0f);
        effect.setFogColorProperty(Vector3(0.0f, 0.0f, 1.0f));
        draw(
            -0.5f, Color::White,
            Vector2(0.25f, 0.5f), Vector2(0.75f, 0.5f));
        ExpectPixel(
            "DualTextureEffect fog uses the forwarded view-space Z vector",
            center, Color(100, 0, 128, 255), 2);

        const std::array<VertexPositionTexture, 3> missingSecondCoordinates{
            VertexPositionTexture(Vector3(-0.75f, -0.75f, 0.0f), Vector2::Zero),
            VertexPositionTexture(Vector3(0.75f, -0.75f, 0.0f), Vector2::Zero),
            VertexPositionTexture(Vector3(0.0f, 0.75f, 0.0f), Vector2::Zero)};
        bool missingSemanticRejected = false;
        effect.Apply();
        try
        {
            device.DrawUserPrimitives(
                PrimitiveType::TriangleList,
                missingSecondCoordinates.data(), 0, 1);
        }
        catch (const System::NotSupportedException&)
        {
            missingSemanticRejected = true;
        }
        Check(
            missingSemanticRejected,
            "DualTextureEffect rejects a declaration without TextureCoordinate1");

        BasicEffect basic(device);
        basic.setWorldProperty(identity);
        basic.setViewProperty(identity);
        basic.setProjectionProperty(identity);
        basic.setTextureEnabledProperty(false);
        basic.setVertexColorEnabledProperty(false);
        basic.setDiffuseColorProperty(Vector3(0.0f, 1.0f, 0.0f));
        const std::array<VertexPositionTexture, 3> basicTriangle{
            VertexPositionTexture(Vector3(-0.75f, -0.75f, 0.0f), Vector2::Zero),
            VertexPositionTexture(Vector3(0.75f, -0.75f, 0.0f), Vector2::Zero),
            VertexPositionTexture(Vector3(0.0f, 0.75f, 0.0f), Vector2::Zero)};
        device.Clear(Color::Black);
        basic.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, basicTriangle.data(), 0, 1);
        ExpectPixel(
            "transition to BasicEffect clears the dual-texture shader state",
            center, Color(0, 255, 0, 255));
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    return CNA::Examples::RunPixelTest<RlglDualTextureEffectTest>();
}
