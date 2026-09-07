// SPDX-License-Identifier: MS-PL
// SOFTWARE-112: renderer-neutral classic stock-effect fog contract.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include "common/ViewSpaceFogRef.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
namespace FogRef = CNA::Examples::VSFogRef;

namespace
{
    constexpr int kSize = 32;
    const Color kBlack(0, 0, 0, 255);
    const Color kWhite(255, 255, 255, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kGreen(0, 255, 0, 255);

    struct FogCase
    {
        const char* label;
        Matrix world;
        Matrix view;
        float objectZ;
        bool enabled;
        float start;
        float end;
        float expectedKeep;
    };

    struct SkinnedVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedVertex) == 52);

    template<typename TEffect>
    void ConfigureFog(TEffect& effect, const FogCase& testCase)
    {
        effect.setWorldProperty(testCase.world);
        effect.setViewProperty(testCase.view);
        effect.setProjectionProperty(FogRef::Ortho());
        effect.setFogEnabledProperty(testCase.enabled);
        effect.setFogColorProperty(FogRef::kFogRGB);
        effect.setFogStartProperty(testCase.start);
        effect.setFogEndProperty(testCase.end);
    }

    bool CloseChannel(int actual, int expected)
    {
        return std::abs(actual - expected) <= 2;
    }

    bool CloseColor(const Color& actual, const Color& expected)
    {
        return CloseChannel(actual.getRProperty(), expected.getRProperty()) &&
               CloseChannel(actual.getGProperty(), expected.getGProperty()) &&
               CloseChannel(actual.getBProperty(), expected.getBProperty()) &&
               CloseChannel(actual.getAProperty(), expected.getAProperty());
    }

    std::string ColorText(const Color& color)
    {
        return "(" + std::to_string(static_cast<int>(color.getRProperty())) + "," +
               std::to_string(static_cast<int>(color.getGProperty())) + "," +
               std::to_string(static_cast<int>(color.getBProperty())) + "," +
               std::to_string(static_cast<int>(color.getAProperty())) + ")";
    }
}

class StockEffectFogContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(const std::string& family, const FogCase& testCase, const Color& actual)
    {
        const Color expected = FogRef::ColorFor(testCase.expectedKeep);
        Check(family + " " + testCase.label, actual, expected);
    }

    void Check(const std::string& label, const Color& actual, const Color& expected)
    {
        const bool ok = CloseColor(actual, expected);
        std::printf("[%s] %s: got=%s expected=%s\n", ok ? "PASS" : "FAIL",
                    label.c_str(), ColorText(actual).c_str(),
                    ColorText(expected).c_str());
        ++total_;
        if (ok)
            ++passed_;
    }

    void Begin(GraphicsDevice& device, RenderTarget2D& target)
    {
        device.SetRenderTarget(&target);
        device.setViewportProperty(Viewport(0, 0, kSize, kSize));
        device.Clear(kBlack);
    }

    Color Finish(GraphicsDevice& device, RenderTarget2D& target)
    {
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const Rectangle center(kSize / 2, kSize / 2, 1, 1);
        Color pixel;
        target.GetData(0, &center, &pixel, 0, 1);
        return pixel;
    }

    Color RenderBasic(GraphicsDevice& device, RenderTarget2D& target,
                      const FogCase& testCase, float alpha = 1.0f)
    {
        Begin(device, target);
        BasicEffect effect(device);
        ConfigureFog(effect, testCase);
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.VertexColorEnabled = false;
        effect.setDiffuseColorProperty(FogRef::kGeomRGB);
        effect.setAlphaProperty(alpha);
        effect.Apply();
        const Color ignored(0, 255, 0, 255);
        const float z = testCase.objectZ;
        const VertexPositionColor vertices[6] = {
            {Vector3(-1,  1, z), ignored}, {Vector3(-1, -1, z), ignored},
            {Vector3( 1, -1, z), ignored}, {Vector3(-1,  1, z), ignored},
            {Vector3( 1, -1, z), ignored}, {Vector3( 1,  1, z), ignored},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
        return Finish(device, target);
    }

    Color RenderAlpha(GraphicsDevice& device, RenderTarget2D& target,
                      const FogCase& testCase)
    {
        Begin(device, target);
        AlphaTestEffect effect(device);
        ConfigureFog(effect, testCase);
        effect.setVertexColorEnabledProperty(false);
        effect.setDiffuseColorProperty(FogRef::kGeomRGB);
        effect.Apply();
        const Color ignored(0, 255, 0, 255);
        const float z = testCase.objectZ;
        const VertexPositionColor vertices[6] = {
            {Vector3(-1,  1, z), ignored}, {Vector3(-1, -1, z), ignored},
            {Vector3( 1, -1, z), ignored}, {Vector3(-1,  1, z), ignored},
            {Vector3( 1, -1, z), ignored}, {Vector3( 1,  1, z), ignored},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
        return Finish(device, target);
    }

    Color RenderDual(GraphicsDevice& device, RenderTarget2D& target,
                     Texture2D& white, Texture2D& gray, const FogCase& testCase)
    {
        Begin(device, target);
        DualTextureEffect effect(device);
        ConfigureFog(effect, testCase);
        effect.setTextureProperty(&white);
        effect.setTexture2Property(&gray);
        effect.setVertexColorEnabledProperty(false);
        effect.setDiffuseColorProperty(FogRef::kGeomRGB);
        effect.Apply();
        const float z = testCase.objectZ;
        const VertexPositionTexture vertices[6] = {
            {Vector3(-1,  1, z), Vector2(0, 0)}, {Vector3(-1, -1, z), Vector2(0, 1)},
            {Vector3( 1, -1, z), Vector2(1, 1)}, {Vector3(-1,  1, z), Vector2(0, 0)},
            {Vector3( 1, -1, z), Vector2(1, 1)}, {Vector3( 1,  1, z), Vector2(1, 0)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
        return Finish(device, target);
    }

    Color RenderEnvironment(GraphicsDevice& device, RenderTarget2D& target,
                            Texture2D& blue, TextureCube& cube, const FogCase& testCase,
                            float environmentAmount = 0.0f)
    {
        Begin(device, target);
        EnvironmentMapEffect effect(device);
        ConfigureFog(effect, testCase);
        effect.setTextureProperty(&blue);
        effect.setEnvironmentMapProperty(&cube);
        effect.setEnvironmentMapAmountProperty(environmentAmount);
        effect.setEnvironmentMapSpecularProperty(Vector3::Zero);
        effect.setFresnelFactorProperty(0.0f);
        effect.setDiffuseColorProperty(Vector3::One);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setEmissiveColorProperty(Vector3::One);
        effect.DirectionalLight0.setEnabledProperty(false);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
        effect.Apply();
        const Vector3 normal(0, 0, 1);
        const float z = testCase.objectZ;
        const VertexPositionNormalTexture vertices[6] = {
            {Vector3(-1,  1, z), normal, Vector2(0, 0)},
            {Vector3(-1, -1, z), normal, Vector2(0, 1)},
            {Vector3( 1, -1, z), normal, Vector2(1, 1)},
            {Vector3(-1,  1, z), normal, Vector2(0, 0)},
            {Vector3( 1, -1, z), normal, Vector2(1, 1)},
            {Vector3( 1,  1, z), normal, Vector2(1, 0)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
        return Finish(device, target);
    }

    Color RenderSkinned(GraphicsDevice& device, RenderTarget2D& target, Texture2D& blue,
                        const FogCase& testCase, float boneZ = 0.0f)
    {
        Begin(device, target);
        SkinnedEffect effect(device);
        ConfigureFog(effect, testCase);
        effect.setTextureProperty(&blue);
        effect.setDiffuseColorProperty(Vector3::One);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setEmissiveColorProperty(Vector3::One);
        effect.DirectionalLight0.setEnabledProperty(false);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
        effect.setWeightsPerVertexProperty(1);
        effect.SetBoneTransforms({Matrix::CreateTranslation(0, 0, boneZ)});
        effect.Apply();

        const float z = testCase.objectZ;
        const SkinnedVertex vertices[6] = {
            {-1,  1, z, 0,0,1, 0,0, 1,0,0,0, 0,0,0,0},
            {-1, -1, z, 0,0,1, 0,1, 1,0,0,0, 0,0,0,0},
            { 1, -1, z, 0,0,1, 1,1, 1,0,0,0, 0,0,0,0},
            {-1,  1, z, 0,0,1, 0,0, 1,0,0,0, 0,0,0,0},
            { 1, -1, z, 0,0,1, 1,1, 1,0,0,0, 0,0,0,0},
            { 1,  1, z, 0,0,1, 1,0, 1,0,0,0, 0,0,0,0},
        };
        const VertexDeclaration declaration(52, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(48, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
        });
        VertexBuffer buffer(device, declaration, 6, BufferUsage::None);
        buffer.SetData(vertices, 6);
        device.SetVertexBuffer(&buffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        return Finish(device, target);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        Texture2D white(device, 1, 1);
        white.SetData(&kWhite, 1);
        const Color grayPixel(128, 128, 128, 255);
        Texture2D gray(device, 1, 1);
        gray.SetData(&grayPixel, 1);
        Texture2D blue(device, 1, 1);
        blue.SetData(&kBlue, 1);
        TextureCube cube(device, 1, false, SurfaceFormat::Color);
        for (CubeMapFace face : {CubeMapFace::PositiveX, CubeMapFace::NegativeX,
                                CubeMapFace::PositiveY, CubeMapFace::NegativeY,
                                CubeMapFace::PositiveZ, CubeMapFace::NegativeZ})
            cube.SetData(face, &kGreen, 1);

        const Matrix identity = Matrix::getIdentityProperty();
        const std::vector<FogCase> cases = {
            {"disabled", identity, Matrix::CreateTranslation(0, 0, -4), 0, false,
                FogRef::kFogStart, FogRef::kFogEnd, 1.0f},
            {"start boundary", identity, Matrix::CreateTranslation(0, 0, -2), 0, true,
                FogRef::kFogStart, FogRef::kFogEnd, 1.0f},
            {"identity midpoint", identity, identity, -4, true,
                FogRef::kFogStart, FogRef::kFogEnd, 0.5f},
            {"view-space midpoint", identity, Matrix::CreateTranslation(0, 0, -4), 0, true,
                FogRef::kFogStart, FogRef::kFogEnd, 0.5f},
            {"world-space end boundary", Matrix::CreateTranslation(0, 0, -6), identity, 0, true,
                FogRef::kFogStart, FogRef::kFogEnd, 0.0f},
            {"degenerate start=end", identity, Matrix::CreateTranslation(0, 0, -4), 0, true,
                4.0f, 4.0f, 0.0f},
        };

        for (const FogCase& testCase : cases)
        {
            Check("BasicEffect", testCase, RenderBasic(device, target, testCase));
            Check("AlphaTestEffect", testCase, RenderAlpha(device, target, testCase));
            Check("DualTextureEffect", testCase,
                  RenderDual(device, target, white, gray, testCase));
            Check("EnvironmentMapEffect", testCase,
                  RenderEnvironment(device, target, blue, cube, testCase));
            Check("SkinnedEffect", testCase, RenderSkinned(device, target, blue, testCase));
        }

        const FogCase postSkin{"post-skin midpoint", identity,
            Matrix::CreateTranslation(0, 0, -2), 0, true,
            FogRef::kFogStart, FogRef::kFogEnd, 0.5f};
        Check("SkinnedEffect", postSkin,
              RenderSkinned(device, target, blue, postSkin, -2.0f));

        // Fog is the final stock-effect RGB stage: it must blend the completed environment-map
        // contribution, not the base texture that the cube subsequently replaces.
        const FogCase finalStage{"final-stage midpoint", identity,
            Matrix::CreateTranslation(0, 0, -4), 0, true,
            FogRef::kFogStart, FogRef::kFogEnd, 0.5f};
        Check("EnvironmentMapEffect final-stage ordering",
              RenderEnvironment(device, target, blue, cube, finalStage, 1.0f),
              FogRef::ColorFor(0.5f, Vector3(0, 1, 0), FogRef::kFogRGB));

        // FNA Common.fxh applies fog toward FogColor * the completed output alpha. This is
        // observably different from mixing toward the unscaled fog colour on an opaque target.
        const float transparentAlpha = 0.25f;
        const auto scaledChannel = [&](float geometry, float fog)
        {
            return static_cast<int>(std::lround(
                transparentAlpha * (fog * 0.5f + geometry * 0.5f) * 255.0f));
        };
        Check("BasicEffect transparent fog premultiplies FogColor by output alpha",
              RenderBasic(device, target, finalStage, transparentAlpha),
              Color(scaledChannel(FogRef::kGeomRGB.X, FogRef::kFogRGB.X),
                    scaledChannel(FogRef::kGeomRGB.Y, FogRef::kFogRGB.Y),
                    scaledChannel(FogRef::kGeomRGB.Z, FogRef::kFogRGB.Z),
                    static_cast<int>(std::lround(transparentAlpha * 255.0f))));

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    StockEffectFogContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(64);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int Result() const { return result_; }
};

int main()
{
    StockEffectFogContractTest game;
    game.Run();
    return game.Result();
}
