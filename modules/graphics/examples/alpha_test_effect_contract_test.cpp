// SPDX-License-Identifier: MS-PL
// SOFTWARE-111/303: renderer-neutral AlphaTestEffect fragment contract.
//
// This public-API fixture characterizes all eight CompareFunction modes on both sides of the
// byte reference, proves that texture/effect/vertex alpha are multiplied before comparison,
// covers Microsoft XNA's opaque-black null texture, and proves that discard does not write depth.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kReference = 128;
    const Color kBackground(0, 255, 0, 255);
    const Color kOpaqueRed(255, 0, 0, 255);

    struct FunctionCase
    {
        CompareFunction function;
        const char* name;
        std::array<bool, 3> expected;
    };

    constexpr std::array<float, 3> kAlphaValues{
        64.0f / 255.0f, 128.0f / 255.0f, 192.0f / 255.0f};
    constexpr std::array<const char*, 3> kAlphaNames{"below", "equal", "above"};
    constexpr std::array<FunctionCase, 8> kFunctions{{
        {CompareFunction::Always,       "Always",       {true,  true,  true}},
        {CompareFunction::Never,        "Never",        {false, false, false}},
        {CompareFunction::Less,         "Less",         {true,  false, false}},
        {CompareFunction::LessEqual,    "LessEqual",    {true,  true,  false}},
        {CompareFunction::Equal,        "Equal",        {false, true,  false}},
        {CompareFunction::NotEqual,     "NotEqual",     {true,  false, true}},
        {CompareFunction::GreaterEqual, "GreaterEqual", {false, true,  true}},
        {CompareFunction::Greater,      "Greater",      {false, false, true}},
    }};

    std::array<VertexPositionTexture, 6> TexturedQuad(float depth)
    {
        return {{
            {Vector3(-1.0f,  1.0f, depth), Vector2(0.5f, 0.5f)},
            {Vector3(-1.0f, -1.0f, depth), Vector2(0.5f, 0.5f)},
            {Vector3( 1.0f, -1.0f, depth), Vector2(0.5f, 0.5f)},
            {Vector3(-1.0f,  1.0f, depth), Vector2(0.5f, 0.5f)},
            {Vector3( 1.0f, -1.0f, depth), Vector2(0.5f, 0.5f)},
            {Vector3( 1.0f,  1.0f, depth), Vector2(0.5f, 0.5f)},
        }};
    }

    std::array<VertexPositionColorTexture, 6> ColoredTexturedQuad(const Color& color, float depth)
    {
        const auto positions = TexturedQuad(depth);
        std::array<VertexPositionColorTexture, 6> result{};
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = VertexPositionColorTexture(
                positions[i].Position, color, positions[i].TextureCoordinate);
        return result;
    }

    std::array<VertexPositionColor, 6> ColoredQuad(const Color& color, float depth)
    {
        const auto positions = TexturedQuad(depth);
        std::array<VertexPositionColor, 6> result{};
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = VertexPositionColor(positions[i].Position, color);
        return result;
    }
}

class AlphaTestEffectContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        ++total_;
        if (ok)
            ++passed_;
    }

    Color Center(GraphicsDevice& device)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color result;
        device.GetBackBufferData(&region, &result, 0, 1);
        return result;
    }

    bool IsDrawn(const Color& color) const
    {
        return color.getRProperty() > 8 && color.getGProperty() < 64;
    }

    void Prepare(GraphicsDevice& device, bool depth)
    {
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(depth ? DepthStencilState::Default
                                                  : DepthStencilState::None);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                     kBackground, 1.0f, 0);
    }

    Color RenderAlphaColor(GraphicsDevice& device, Texture2D* texture, float alpha,
                           CompareFunction function, int reference,
                           bool vertexColorEnabled = false, int vertexAlpha = 255)
    {
        Prepare(device, false);
        AlphaTestEffect effect(device);
        effect.setTextureProperty(texture);
        effect.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        effect.setAlphaProperty(alpha);
        effect.setAlphaFunctionProperty(function);
        effect.setReferenceAlphaProperty(reference);
        effect.setVertexColorEnabledProperty(vertexColorEnabled);
        effect.Apply();

        if (vertexColorEnabled)
        {
            const auto quad = ColoredTexturedQuad(Color(255, 255, 255, vertexAlpha), 0.25f);
            device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
        }
        else
        {
            const auto quad = TexturedQuad(0.25f);
            device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
        }
        return Center(device);
    }

    bool DrawAlpha(GraphicsDevice& device, Texture2D* texture, float alpha,
                   CompareFunction function, int reference,
                   bool vertexColorEnabled = false, int vertexAlpha = 255)
    {
        return IsDrawn(RenderAlphaColor(device, texture, alpha, function, reference,
                                        vertexColorEnabled, vertexAlpha));
    }

    void CheckFunctionSweep(GraphicsDevice& device, Texture2D& white)
    {
        for (const auto& function : kFunctions)
        {
            for (std::size_t value = 0; value < kAlphaValues.size(); ++value)
            {
                const bool drawn = DrawAlpha(device, &white, kAlphaValues[value],
                                             function.function, kReference);
                Check(drawn == function.expected[value],
                      std::string(function.name) + " at alpha " + kAlphaNames[value] +
                          " the byte reference is " + (function.expected[value] ? "drawn" : "discarded"));
            }
        }
    }

    void CheckAlphaSources(GraphicsDevice& device, Texture2D& white, Texture2D& halfAlpha)
    {
        const Color nullSample = RenderAlphaColor(
            device, nullptr, 1.0f, CompareFunction::Greater, 254);
        Check(nullSample == Color(0, 0, 0, 255),
              "a null AlphaTestEffect texture samples XNA opaque black and passes on alpha one");

        Check(DrawAlpha(device, &halfAlpha, 1.0f, CompareFunction::Greater, 100),
              "texture alpha alone participates in the comparison");
        Check(!DrawAlpha(device, &halfAlpha, 128.0f / 255.0f,
                         CompareFunction::Greater, 100),
              "texture alpha is multiplied by effect Alpha before comparison");

        Check(DrawAlpha(device, &white, 1.0f, CompareFunction::Greater, 200, false, 128),
              "vertex alpha is ignored while VertexColorEnabled is false");
        Check(!DrawAlpha(device, &white, 1.0f, CompareFunction::Greater, 200, true, 128),
              "enabled vertex alpha is multiplied before comparison");
    }

    void CheckDiscardDoesNotWriteDepth(GraphicsDevice& device, Texture2D& white)
    {
        Prepare(device, true);

        AlphaTestEffect rejected(device);
        rejected.setTextureProperty(&white);
        rejected.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        rejected.setAlphaFunctionProperty(CompareFunction::Never);
        rejected.Apply();
        const auto nearQuad = TexturedQuad(0.2f);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, nearQuad.data(), 0, 2);

        BasicEffect visible(device);
        visible.VertexColorEnabled = true;
        visible.Apply();
        const auto farQuad = ColoredQuad(Color(0, 0, 255, 255), 0.8f);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, farQuad.data(), 0, 2);

        const Color color = Center(device);
        Check(color.getBProperty() > 200 && color.getRProperty() < 32,
              "discarded near fragments do not write depth or block a farther draw");
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        Texture2D white(device, 1, 1);
        white.SetData(&kOpaqueRed, 1);
        const Color halfAlpha(255, 0, 0, 128);
        Texture2D half(device, 1, 1);
        half.SetData(&halfAlpha, 1);

        CheckFunctionSweep(device, white);
        CheckAlphaSources(device, white, half);
        CheckDiscardDoesNotWriteDepth(device, white);

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    AlphaTestEffectContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferredBackBufferWidthProperty(kSize);
        graphics_->setPreferredBackBufferHeightProperty(kSize);
        graphics_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24);
    }

    int Result() const { return result_; }
};

int main()
{
    AlphaTestEffectContractTest game;
    game.Run();
    return game.Result();
}
