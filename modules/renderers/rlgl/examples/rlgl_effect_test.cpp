// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-033: focused unlit BasicEffect, AlphaTestEffect, texture,
// fog, and state-transition evidence. Exit 77 means no usable GL context.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/NotSupportedException.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 48;

    [[nodiscard]] std::array<VertexPositionTexture, 3> TexturedTriangle(
        const float z, const Vector2& textureCoordinate)
    {
        return {
            VertexPositionTexture(Vector3(-0.75f, -0.75f, z), textureCoordinate),
            VertexPositionTexture(Vector3(0.75f, -0.75f, z), textureCoordinate),
            VertexPositionTexture(Vector3(0.0f, 0.75f, z), textureCoordinate)};
    }

    [[nodiscard]] std::array<VertexPositionColor, 3> ColoredTriangle(
        const float z, const Color& color)
    {
        return {
            VertexPositionColor(Vector3(-0.75f, -0.75f, z), color),
            VertexPositionColor(Vector3(0.75f, -0.75f, z), color),
            VertexPositionColor(Vector3(0.0f, 0.75f, z), color)};
    }
}

class RlglEffectTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglEffectTest()
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
        const Rectangle center(kWidth / 2, kHeight / 2, 1, 1);
        const Matrix identity = Matrix::getIdentityProperty();

        Texture2D pattern(device, 2, 2);
        const std::array<Color, 4> patternPixels{
            Color(255, 0, 0, 255), Color(0, 255, 0, 255),
            Color(0, 0, 255, 255), Color(255, 255, 255, 255)};
        pattern.SetData(patternPixels.data(), static_cast<int>(patternPixels.size()));

        BasicEffect basic(device);
        basic.setWorldProperty(identity);
        basic.setViewProperty(identity);
        basic.setProjectionProperty(identity);
        basic.setTextureProperty(&pattern);
        basic.setTextureEnabledProperty(true);
        basic.setVertexColorEnabledProperty(false);
        const auto textured = TexturedTriangle(0.0f, Vector2(0.25f, 0.25f));
        device.Clear(Color::Black);
        basic.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, textured.data(), 0, 1);
        ExpectPixel(
            "BasicEffect samples TextureCoordinate0 with XNA row ordering",
            center, Color::Red);

        Texture2D white(device, 1, 1);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        basic.setTextureProperty(&white);
        basic.setDiffuseColorProperty(Vector3(0.25f, 0.5f, 0.75f));
        basic.setEmissiveColorProperty(Vector3(0.25f, 0.0f, 0.0f));
        basic.setAlphaProperty(0.5f);
        basic.setVertexColorEnabledProperty(true);
        const std::array<VertexPositionColorTexture, 3> coloredTextured{
            VertexPositionColorTexture(
                Vector3(-0.75f, -0.75f, 0.0f), Color(128, 255, 128, 255), Vector2::Zero),
            VertexPositionColorTexture(
                Vector3(0.75f, -0.75f, 0.0f), Color(128, 255, 128, 255), Vector2::Zero),
            VertexPositionColorTexture(
                Vector3(0.0f, 0.75f, 0.0f), Color(128, 255, 128, 255), Vector2::Zero)};
        device.Clear(Color::Black);
        basic.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, coloredTextured.data(), 0, 1);
        ExpectPixel(
            "BasicEffect combines texture, vertex color, diffuse, emissive, and alpha",
            center, Color(32, 64, 48, 255), 2);

        basic.setTextureProperty(nullptr);
        basic.setDiffuseColorProperty(Vector3(0.0f, 1.0f, 0.0f));
        basic.setEmissiveColorProperty(Vector3::Zero);
        basic.setAlphaProperty(1.0f);
        basic.setVertexColorEnabledProperty(false);
        device.Clear(Color::Black);
        basic.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, textured.data(), 0, 1);
        ExpectPixel(
            "a null BasicEffect texture uses rlgl's default white texture",
            center, Color(0, 255, 0, 255));

        basic.setTextureProperty(&pattern);
        basic.setTextureEnabledProperty(false);
        basic.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f));
        const auto whiteColored = ColoredTriangle(0.0f, Color::White);
        device.Clear(Color::Black);
        basic.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, whiteColored.data(), 0, 1);
        ExpectPixel(
            "textured-to-untextured transition disables sampling deterministically",
            center, Color::Blue);

        Texture2D alphaTexture(device, 1, 1);
        const Color halfAlphaRed(255, 0, 0, 128);
        alphaTexture.SetData(&halfAlphaRed, 1);
        AlphaTestEffect alphaTest(device);
        alphaTest.setWorldProperty(identity);
        alphaTest.setViewProperty(identity);
        alphaTest.setProjectionProperty(identity);
        alphaTest.setTextureProperty(&alphaTexture);
        alphaTest.setVertexColorEnabledProperty(false);
        alphaTest.setReferenceAlphaProperty(128);
        const std::array<CompareFunction, 8> comparisons{
            CompareFunction::Always, CompareFunction::Never,
            CompareFunction::Less, CompareFunction::LessEqual,
            CompareFunction::Equal, CompareFunction::GreaterEqual,
            CompareFunction::Greater, CompareFunction::NotEqual};
        const std::array<bool, 8> shouldPass{
            true, false, false, true, true, true, false, false};
        const std::array<const char*, 8> comparisonLabels{
            "AlphaTest Always passes", "AlphaTest Never rejects",
            "AlphaTest Less rejects equality", "AlphaTest LessEqual passes equality",
            "AlphaTest Equal passes equality", "AlphaTest GreaterEqual passes equality",
            "AlphaTest Greater rejects equality", "AlphaTest NotEqual rejects equality"};
        for (std::size_t index = 0; index < comparisons.size(); ++index)
        {
            alphaTest.setAlphaFunctionProperty(comparisons[index]);
            device.Clear(Color::Black);
            alphaTest.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, textured.data(), 0, 1);
            ExpectPixel(
                comparisonLabels[index], center,
                shouldPass[index] ? Color::Red : Color::Black);
        }

        alphaTest.setTextureProperty(nullptr);
        alphaTest.setVertexColorEnabledProperty(true);
        alphaTest.setAlphaFunctionProperty(CompareFunction::Always);
        alphaTest.setDiffuseColorProperty(Vector3(1.0f, 0.5f, 0.0f));
        alphaTest.setFogEnabledProperty(true);
        alphaTest.setFogStartProperty(0.0f);
        alphaTest.setFogEndProperty(1.0f);
        alphaTest.setFogColorProperty(Vector3(0.0f, 0.0f, 1.0f));
        const auto alphaFogged = ColoredTriangle(
            -0.5f, Color(128, 255, 255, 255));
        device.Clear(Color::Black);
        alphaTest.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, alphaFogged.data(), 0, 1);
        ExpectPixel(
            "untextured AlphaTestEffect combines diffuse, vertex color, and fog",
            center, Color(64, 64, 128, 255), 2);

        basic.setTextureEnabledProperty(false);
        basic.setVertexColorEnabledProperty(false);
        basic.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        basic.setFogEnabledProperty(true);
        basic.setFogStartProperty(0.0f);
        basic.setFogEndProperty(1.0f);
        basic.setFogColorProperty(Vector3(0.0f, 0.0f, 1.0f));
        const auto fogged = ColoredTriangle(-0.5f, Color::White);
        device.Clear(Color::Black);
        basic.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, fogged.data(), 0, 1);
        ExpectPixel(
            "BasicEffect fog uses the forwarded view-space Z vector",
            center, Color(128, 0, 128, 255), 2);

        basic.setFogEnabledProperty(false);
        basic.setLightingEnabledProperty(true);
        basic.Apply();
        bool lightingRejected = false;
        try
        {
            device.DrawUserPrimitives(
                PrimitiveType::TriangleList, textured.data(), 0, 1);
        }
        catch (const System::NotSupportedException&)
        {
            lightingRejected = true;
        }
        Check(lightingRejected,
              "lit BasicEffect remains an explicit RLGL-034 failure");
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    return CNA::Examples::RunPixelTest<RlglEffectTest>();
}
