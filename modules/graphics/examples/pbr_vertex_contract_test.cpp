// SPDX-License-Identifier: MS-PL
// Museum rv-shading: PBR semantics must not depend on whether a mesh stores a
// tangent.
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class PbrVertexContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    int result_ = 1;
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, 64, 64, false, SurfaceFormat::Color, DepthFormat::None);
        Texture2D white(device, 1, 1);
        const Color pixel = Color::White;
        white.SetData(&pixel, 1);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        const std::array<Vector3, 6> positions{
            {{-1, 1, 0}, {1, 1, 0}, {1, -1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}}};
        std::array<VertexPositionNormalTexture, 6> plain;
        struct PackedTangentVertex
        {
            Vector3 position, normal;
            Vector4 tangent;
            Vector2 uv;
        };
        static_assert(sizeof(PackedTangentVertex) == 48);
        std::array<PackedTangentVertex, 6> tangent;
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            plain[i] = VertexPositionNormalTexture(positions[i], Vector3(0, 0, 1), Vector2(0, 0));
            tangent[i] = {positions[i], Vector3(0, 0, 1), Vector4(1, 0, 0, 1), Vector2(0, 0)};
        }
        // Same total stride as the canonical tangent record, but reordered and
        // without one. A prefix vertex and prefix index expose offset mistakes
        // during normalization.
        struct PaddedVertex
        {
            float padding;
            Vector2 uv;
            float unused;
            Vector3 normal, position;
            float tail[2];
        };
        static_assert(sizeof(PaddedVertex) == 48);
        std::array<PaddedVertex, 7> padded{};
        for (std::size_t i = 0; i < 6; ++i)
            padded[i + 1] = {123, Vector2::Zero, 456, Vector3(0, 0, 1), positions[i], {789, 789}};
        const VertexDeclaration declaration(
            48, {{28, VertexElementFormat::Vector3, VertexElementUsage::Position, 0},
                 {4, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0},
                 {16, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0}});
        VertexBuffer buffer(device, declaration, 7, BufferUsage::None);
        buffer.SetDataRaw(padded.data(), 7, 48);
        IndexBuffer indices(device, 7);
        const std::uint16_t indexData[]{6, 0, 1, 2, 3, 4, 5};
        indices.SetData(indexData, 7);
        struct ColoredTangentVertex
        {
            PackedTangentVertex vertex;
            Vector2 uv1;
            std::uint32_t color;
        };
        struct ColoredPlainVertex
        {
            Vector3 position, normal;
            Vector2 uv;
            std::uint32_t color;
        };
        static_assert(sizeof(ColoredTangentVertex) == 60);
        static_assert(sizeof(ColoredPlainVertex) == 36);
        std::array<ColoredTangentVertex, 6> coloredTangent;
        std::array<ColoredPlainVertex, 6> coloredPlain;
        for (std::size_t i = 0; i < 6; ++i)
        {
            constexpr std::uint32_t color = 0xff80c040u;
            coloredTangent[i] = {tangent[i], Vector2::Zero, color};
            coloredPlain[i] = {positions[i], Vector3(0, 0, 1), Vector2::Zero, color};
        }
        const VertexDeclaration coloredTangentDeclaration(
            60, {{0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0},
                 {12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0},
                 {24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0},
                 {40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0},
                 {48, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1},
                 {56, VertexElementFormat::Color, VertexElementUsage::Color, 0}});
        const VertexDeclaration coloredPlainDeclaration(
            36, {{0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0},
                 {12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0},
                 {24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0},
                 {32, VertexElementFormat::Color, VertexElementUsage::Color, 0}});
        const auto render = [&](int route, float metallic, float roughness, Vector3 base, Vector3 emission,
                                bool light, bool vertexColor = true) {
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            PbrEffect effect(device);
            effect.setLightingEnabledProperty(true);
            effect.setAmbientLightColorProperty(Vector3::Zero);
            effect.setDiffuseColorProperty(base);
            effect.setMetallicFactorProperty(metallic);
            effect.setRoughnessFactorProperty(roughness);
            effect.setEmissiveFactorProperty(emission);
            effect.setTextureProperty(&white);
            effect.VertexColorEnabledEXT = route >= 5 && vertexColor;
            effect.DirectionalLight0.setEnabledProperty(light);
            effect.DirectionalLight0.setDirectionProperty(Vector3(0, 0, -1));
            effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
            effect.DirectionalLight0.setSpecularColorProperty(Vector3::One);
            effect.DirectionalLight1.setEnabledProperty(false);
            effect.DirectionalLight2.setEnabledProperty(false);
            effect.setViewProperty(Matrix::CreateLookAt(Vector3(0, 0, 2), Vector3::Zero, Vector3::Up));
            effect.setProjectionProperty(
                Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1, 0.1f, 10));
            effect.Apply();
            if (route == 0)
                device.DrawUserPrimitives(PrimitiveType::TriangleList, tangent.data(), 0, 2,
                                          VertexPositionNormalTangentTexture::getVertexDeclarationStatic());
            else if (route == 1)
                device.DrawUserPrimitives(PrimitiveType::TriangleList, plain.data(), 0, 2);
            else if (route == 2)
                device.DrawUserPrimitives(PrimitiveType::TriangleList, padded.data(), 1, 2, declaration);
            else if (route == 5)
                device.DrawUserPrimitives(PrimitiveType::TriangleList, coloredTangent.data(), 0, 2,
                                          coloredTangentDeclaration);
            else if (route == 6)
                device.DrawUserPrimitives(PrimitiveType::TriangleList, coloredPlain.data(), 0, 2,
                                          coloredPlainDeclaration);
            else
            {
                device.SetVertexBuffer(&buffer);
                if (route == 3)
                    device.DrawPrimitives(PrimitiveType::TriangleList, 1, 2);
                else
                {
                    device.setIndicesProperty(&indices);
                    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 1, 0, 6, 1, 2);
                    device.setIndicesProperty(nullptr);
                }
                device.SetVertexBuffer(nullptr);
            }
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const Rectangle center(32, 32, 1, 1);
            Color out;
            target.GetData(0, &center, &out, 0, 1);
            return out;
        };
        bool pass = true;
        // Exact equality is appropriate for two layouts supplying the same shader
        // inputs.
        std::array<Color, 5> reference{};
        for (int scene = 0; scene < 5; ++scene)
        {
            const float metallic = scene == 1 ? 1.0f : 0.0f;
            const float roughness = scene == 2 ? 1.0f : 0.5f;
            const Vector3 base = scene == 3 ? Vector3::Zero : Vector3(0.8f, 0.8f, 0.8f);
            const Vector3 emission = scene == 3 ? Vector3(0, 1, 0) : Vector3::Zero;
            const Color expected = render(0, metallic, roughness, base, emission, scene != 4);
            reference[scene] = expected;
            for (int route = 1; route <= 4; ++route)
            {
                const Color actual = render(route, metallic, roughness, base, emission, scene != 4);
                const bool equal = actual == expected;
                std::printf("scene%d route%d tangent=%u,%u,%u actual=%u,%u,%u %s\n", scene, route,
                            expected.getRProperty(), expected.getGProperty(), expected.getBProperty(),
                            actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                            equal ? "PASS" : "FAIL");
                pass &= equal;
                if (scene == 4)
                    pass &= actual.getRProperty() == 0 && actual.getGProperty() == 0 &&
                            actual.getBProperty() == 0;
            }
            const Color coloredExpected = render(5, metallic, roughness, base, emission, scene != 4);
            const Color coloredActual = render(6, metallic, roughness, base, emission, scene != 4);
            const bool colorEqual = coloredActual == coloredExpected;
            std::printf(
                "scene%d COLOR0 tangent=%u,%u,%u actual=%u,%u,%u %s\n", scene, coloredExpected.getRProperty(),
                coloredExpected.getGProperty(), coloredExpected.getBProperty(), coloredActual.getRProperty(),
                coloredActual.getGProperty(), coloredActual.getBProperty(), colorEqual ? "PASS" : "FAIL");
            pass &= colorEqual;
            if (scene == 0)
            {
                pass &= coloredExpected != expected &&
                        coloredExpected.getGProperty() > coloredExpected.getRProperty();
                pass &= render(6, metallic, roughness, base, emission, true, false) == expected;
            }
        }
        pass &= std::abs(reference[0].getRProperty() - reference[1].getRProperty()) > 10;
        pass &= reference[0] != reference[2];
        pass &= reference[3].getGProperty() > 240;
        std::printf("PBR_VERTEX_RESULT comparisons=25 failures=%d\n", pass ? 0 : 1);
        result_ = pass ? 0 : 1;
        Exit();
    }

  public:
    PbrVertexContractTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }
    int Result() const
    {
        return result_;
    }
};
int main()
{
    try
    {
        PbrVertexContractTest game;
        game.Run();
        return game.Result();
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 2;
    }
}
