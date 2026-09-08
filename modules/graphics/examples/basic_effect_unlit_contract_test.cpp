// SPDX-License-Identifier: MS-PL
// SOFTWARE-153: renderer-neutral BasicEffect unlit material and vertex-output contract.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kTolerance = 3;
    const Vector3 kDiffuse(0.4f, 0.2f, 0.6f);
    const Vector3 kEmissive(0.2f, 0.25f, 0.2f);
    constexpr float kAlpha = 0.5f;

    int ToByte(float value)
    {
        return std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255);
    }

    float Channel(const Color& value, int channel)
    {
        switch (channel)
        {
            case 0: return value.getRProperty() / 255.0f;
            case 1: return value.getGProperty() / 255.0f;
            case 2: return value.getBProperty() / 255.0f;
            default: return value.getAProperty() / 255.0f;
        }
    }

    float FoldedMaterial(int channel)
    {
        if (channel == 3)
            return kAlpha;
        const float diffuse[3] = {kDiffuse.X, kDiffuse.Y, kDiffuse.Z};
        const float emissive[3] = {kEmissive.X, kEmissive.Y, kEmissive.Z};
        return (diffuse[channel] + emissive[channel]) * kAlpha;
    }

    Color Expected(const Color& vertex, const Color& texture, bool vertexColorEnabled)
    {
        int components[4] = {};
        for (int channel = 0; channel < 4; ++channel)
        {
            const float vertexValue = vertexColorEnabled ? Channel(vertex, channel) : 1.0f;
            // FNA's stock shader places DiffuseColor*vertex colour in a D3D9 COLOR output.
            // That output is saturated before interpolation; texture sampling happens afterward.
            const float vertexOutput = std::clamp(
                vertexValue * FoldedMaterial(channel), 0.0f, 1.0f);
            components[channel] = ToByte(vertexOutput * Channel(texture, channel));
        }
        return Color(components[0], components[1], components[2], components[3]);
    }
}

class BasicEffectUnlitContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<RenderTarget2D> target_;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(const Color& actual, const Color& expected, const std::string& label,
               int tolerance = kTolerance)
    {
        const bool ok =
            std::abs(static_cast<int>(actual.getRProperty()) - expected.getRProperty()) <= tolerance &&
            std::abs(static_cast<int>(actual.getGProperty()) - expected.getGProperty()) <= tolerance &&
            std::abs(static_cast<int>(actual.getBProperty()) - expected.getBProperty()) <= tolerance &&
            std::abs(static_cast<int>(actual.getAProperty()) - expected.getAProperty()) <= tolerance;
        std::printf("[%s] %s: got=(%d,%d,%d,%d) expected=(%d,%d,%d,%d) tolerance=%d\n",
                    ok ? "PASS" : "FAIL", label.c_str(),
                    actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                    actual.getAProperty(), expected.getRProperty(), expected.getGProperty(),
                    expected.getBProperty(), expected.getAProperty(), tolerance);
        ++total_;
        if (ok)
            ++passed_;
    }

    Color Render(GraphicsDevice& device, const Color& left, const Color& right,
                 bool vertexColorEnabled, bool textureEnabled, Texture2D* texture,
                 const Vector3& diffuse = kDiffuse, const Vector3& emissive = kEmissive,
                 float alpha = kAlpha)
    {
        device.SetRenderTarget(target_.get());
        device.setViewportProperty(Viewport(0, 0, kSize, kSize));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(Color(0, 0, 0, 0));

        BasicEffect effect(device);
        effect.setLightingEnabledProperty(false);
        effect.VertexColorEnabled = vertexColorEnabled;
        effect.setTextureEnabledProperty(textureEnabled);
        effect.setTextureProperty(texture);
        effect.setDiffuseColorProperty(diffuse);
        effect.setEmissiveColorProperty(emissive);
        effect.setAlphaProperty(alpha);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();

        const VertexPositionColorTexture quad[6] = {
            {Vector3(-1,  1, 0), left,  Vector2(0, 0)},
            {Vector3(-1, -1, 0), left,  Vector2(0, 1)},
            {Vector3( 1, -1, 0), right, Vector2(1, 1)},
            {Vector3(-1,  1, 0), left,  Vector2(0, 0)},
            {Vector3( 1, -1, 0), right, Vector2(1, 1)},
            {Vector3( 1,  1, 0), right, Vector2(1, 0)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Rectangle center(kSize / 2, kSize / 2, 1, 1);
        Color pixel;
        target_->GetData(0, &center, &pixel, 0, 1);
        return pixel;
    }

    void BeginTarget(GraphicsDevice& device)
    {
        device.SetRenderTarget(target_.get());
        device.setViewportProperty(Viewport(0, 0, kSize, kSize));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(Color(0, 0, 0, 0));
    }

    Color FinishTarget(GraphicsDevice& device)
    {
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const Rectangle center(kSize / 2, kSize / 2, 1, 1);
        Color pixel;
        target_->GetData(0, &center, &pixel, 0, 1);
        return pixel;
    }

    Color RenderAlphaTestSaturation(GraphicsDevice& device, Texture2D& texture)
    {
        BeginTarget(device);
        AlphaTestEffect effect(device);
        effect.setTextureProperty(&texture);
        effect.setVertexColorEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(2, 2, 2));
        effect.setAlphaProperty(1.0f);
        effect.setAlphaFunctionProperty(CompareFunction::Always);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();

        const VertexPositionTexture quad[6] = {
            {Vector3(-1,  1, 0), Vector2(0, 0)}, {Vector3(-1, -1, 0), Vector2(0, 1)},
            {Vector3( 1, -1, 0), Vector2(1, 1)}, {Vector3(-1,  1, 0), Vector2(0, 0)},
            {Vector3( 1, -1, 0), Vector2(1, 1)}, {Vector3( 1,  1, 0), Vector2(1, 0)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        return FinishTarget(device);
    }

    Color RenderDualTextureSaturation(GraphicsDevice& device, Texture2D& texture,
                                      Texture2D& overlay)
    {
        BeginTarget(device);
        DualTextureEffect effect(device);
        effect.setTextureProperty(&texture);
        effect.setTexture2Property(&overlay);
        effect.setVertexColorEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(2, 2, 2));
        effect.setAlphaProperty(1.0f);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();

        const VertexPositionTexture quad[6] = {
            {Vector3(-1,  1, 0), Vector2(0, 0)}, {Vector3(-1, -1, 0), Vector2(0, 1)},
            {Vector3( 1, -1, 0), Vector2(1, 1)}, {Vector3(-1,  1, 0), Vector2(0, 0)},
            {Vector3( 1, -1, 0), Vector2(1, 1)}, {Vector3( 1,  1, 0), Vector2(1, 0)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        return FinishTarget(device);
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        target_ = std::make_unique<RenderTarget2D>(
            device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        const Color vertex(128, 192, 64, 128);
        const Color textureValue(128, 64, 192, 128);
        const Color white(255, 255, 255, 255);

        Texture2D texture(device, 1, 1);
        texture.SetData(&textureValue, 1);

        Check(Render(device, vertex, vertex, true, false, &texture),
              Expected(vertex, white, true),
              "TextureEnabled=false ignores a non-null Texture");
        Check(Render(device, vertex, vertex, true, true, nullptr),
              Expected(vertex, white, true),
              "TextureEnabled=true with null Texture samples opaque white");
        Check(Render(device, vertex, vertex, true, true, &texture),
              Expected(vertex, textureValue, true),
              "texture * vertex * (diffuse+emissive) * alpha, including output alpha");
        Check(Render(device, vertex, vertex, false, true, &texture),
              Expected(vertex, textureValue, false),
              "VertexColorEnabled=false ignores the declared vertex colour");

        const Color grey(102, 102, 102, 255);
        Texture2D greyTexture(device, 1, 1);
        greyTexture.SetData(&grey, 1);
        const Color constantClamped = Render(
            device, white, white, false, true, &greyTexture,
            Vector3(1, 1, 1), Vector3(1, 1, 1), 1.0f);
        Check(constantClamped, grey,
              "constant diffuse+emissive saturates before texture multiplication");

        const Color black(0, 0, 0, 255);
        const Color gradient = Render(
            device, white, black, true, false, nullptr,
            Vector3(1, 0, 0), Vector3(1, 0, 0), 1.0f);
        // At pixel-center x=32 of a 64-wide target the saturated 1->0 vertex output is 126/255.
        // Multiplying after interpolation instead produces approximately 251 and is far outside
        // this tight raster-rounding allowance.
        Check(gradient, Color(126, 0, 0, 255),
              "D3D9 COLOR output saturates before interpolation", 6);

        // AlphaTestEffect and DualTextureEffect use the same FNA Common.fxh vertex output.
        // Keep direct public probes here so the shared renderer correction cannot regress them
        // while BasicEffect's own combinations remain green.
        Check(RenderAlphaTestSaturation(device, greyTexture), grey,
              "AlphaTestEffect saturates DiffuseColor before texture multiplication");

        const Color darkGrey(51, 51, 51, 255);
        Texture2D darkGreyTexture(device, 1, 1);
        darkGreyTexture.SetData(&darkGrey, 1);
        Texture2D whiteTexture(device, 1, 1);
        whiteTexture.SetData(&white, 1);
        Check(RenderDualTextureSaturation(device, darkGreyTexture, whiteTexture),
              Color(102, 102, 102, 255),
              "DualTextureEffect saturates DiffuseColor before its two-texture pixel shader");

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    BasicEffectUnlitContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kSize);
        graphics_->setPreferredBackBufferHeightProperty(kSize);
    }

    int Result() const { return result_; }
};

int main()
{
    BasicEffectUnlitContractTest game;
    game.Run();
    return game.Result();
}
