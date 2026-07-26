// SPDX-License-Identifier: MS-PL
// REMED-GFX-102: WebGPU SpriteBatch must preserve and apply the BlendState selected by each
// Begin()/End() batch. This is a public-API regression: it uses only GraphicsDevice,
// RenderTarget2D, Texture2D, SpriteBatch, and BlendState, then checks GPU readback.
//
// Pre-fix, CreateSpriteResources() creates only two fixed pipelines and RenderSprites() chooses
// one from the frame-final blendEnabled_ value. The queued SpriteCommand has no BlendState or
// BlendFactor snapshot. Consequently distinct Begin() states collapse to the final live enabled
// bit, all enabled states use one hardcoded straight-alpha equation, and blend constants are
// frame-final rather than per batch.
//
// WebGPU deliberately prefers an sRGB swapchain format and its RenderTarget2D mirrors that format.
// The shader, clear values, and native blending operate in linear space; GetData() returns the
// encoded attachment bytes. Expected RGB values below are therefore analytically blended in
// linear space and explicitly sRGB-encoded before comparison. Alpha is not sRGB encoded.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRtW = 64;
    constexpr int kRtH = 64;
    const Color kDestination(36, 92, 173, 211);
    const Color kSource(189, 47, 113, 137);

    float Unit(int value)
    {
        return static_cast<float>(value) / 255.0f;
    }

    int SrgbByte(float linear)
    {
        linear = std::clamp(linear, 0.0f, 1.0f);
        const float encoded = linear <= 0.0031308f
            ? linear * 12.92f
            : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        return static_cast<int>(std::lround(encoded * 255.0f));
    }

    int LinearAlphaByte(float alpha)
    {
        return static_cast<int>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
    }

    int AbsI(int value)
    {
        return value < 0 ? -value : value;
    }
}

class WebGpuSpriteBatchBlendStateTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<RenderTarget2D> target_;
    Texture2D whiteTexture_;
    bool done_ = false;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (condition)
            ++passed_;
    }

    void CheckLinear(const Color& actual, float r, float g, float b, float a,
                     const std::string& label, int tolerance = 5)
    {
        const int er = SrgbByte(r);
        const int eg = SrgbByte(g);
        const int eb = SrgbByte(b);
        const int ea = LinearAlphaByte(a);
        const bool matches =
            AbsI(actual.getRProperty() - er) <= tolerance &&
            AbsI(actual.getGProperty() - eg) <= tolerance &&
            AbsI(actual.getBProperty() - eb) <= tolerance &&
            AbsI(actual.getAProperty() - ea) <= tolerance;
        Check(matches,
              label + " expected encoded RGBA(" + std::to_string(er) + "," +
              std::to_string(eg) + "," + std::to_string(eb) + "," +
              std::to_string(ea) + ") got (" +
              std::to_string(actual.getRProperty()) + "," +
              std::to_string(actual.getGProperty()) + "," +
              std::to_string(actual.getBProperty()) + "," +
              std::to_string(actual.getAProperty()) + ")");
    }

    void DrawSprite(GraphicsDevice& device, const Rectangle& destination, const Color& tint,
                    const BlendState& blendState)
    {
        SpriteBatch batch(device);
        SamplerState point = SamplerState::PointClamp;
        batch.Begin(SpriteSortMode::Deferred, blendState, &point, nullptr, nullptr);
        batch.Draw(whiteTexture_, destination, Rectangle(0, 0, 1, 1), tint);
        batch.End();
    }

    std::vector<Color> ReadTarget()
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kRtW) * kRtH,
                                  Color(0, 0, 0, 0));
        target_->GetData(0, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

    static Color At(const std::vector<Color>& pixels, int x, int y)
    {
        return pixels[static_cast<std::size_t>(y) * kRtW + x];
    }

    Color RenderSingle(GraphicsDevice& device, const BlendState& blendState,
                       const Color& source = kSource)
    {
        const Rectangle destination(8, 8, 48, 48);
        device.SetRenderTarget(target_.get());
        device.Clear(kDestination);
        DrawSprite(device, destination, source, blendState);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return At(ReadTarget(), 32, 32);
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        target_ = std::make_unique<RenderTarget2D>(
            device, kRtW, kRtH, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
        whiteTexture_ = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const float sr = Unit(kSource.getRProperty());
        const float sg = Unit(kSource.getGProperty());
        const float sb = Unit(kSource.getBProperty());
        const float sa = Unit(kSource.getAProperty());
        const float dr = Unit(kDestination.getRProperty());
        const float dg = Unit(kDestination.getGProperty());
        const float db = Unit(kDestination.getBProperty());
        const float da = Unit(kDestination.getAProperty());

        CheckLinear(RenderSingle(device, BlendState::Opaque),
                    sr, sg, sb, sa, "Opaque");

        CheckLinear(RenderSingle(device, BlendState::AlphaBlend),
                    sr + dr * (1.0f - sa),
                    sg + dg * (1.0f - sa),
                    sb + db * (1.0f - sa),
                    sa + da * (1.0f - sa),
                    "AlphaBlend (premultiplied convention)");

        CheckLinear(RenderSingle(device, BlendState::NonPremultiplied),
                    sr * sa + dr * (1.0f - sa),
                    sg * sa + dg * (1.0f - sa),
                    sb * sa + db * (1.0f - sa),
                    sa * sa + da * (1.0f - sa),
                    "NonPremultiplied");

        CheckLinear(RenderSingle(device, BlendState::Additive),
                    sr * sa + dr, sg * sa + dg, sb * sa + db,
                    sa * sa + da, "Additive");

        BlendState customColor;
        customColor.setColorSourceBlendProperty(Blend::One);
        customColor.setColorDestinationBlendProperty(Blend::One);
        customColor.setColorBlendFunctionProperty(BlendFunction::Subtract);
        customColor.setAlphaSourceBlendProperty(Blend::One);
        customColor.setAlphaDestinationBlendProperty(Blend::Zero);
        CheckLinear(RenderSingle(device, customColor),
                    sr - dr, sg - dg, sb - db, sa,
                    "custom colour Subtract equation");

        BlendState customAlpha;
        customAlpha.setColorSourceBlendProperty(Blend::One);
        customAlpha.setColorDestinationBlendProperty(Blend::Zero);
        customAlpha.setAlphaSourceBlendProperty(Blend::One);
        customAlpha.setAlphaDestinationBlendProperty(Blend::One);
        customAlpha.setAlphaBlendFunctionProperty(BlendFunction::ReverseSubtract);
        CheckLinear(RenderSingle(device, customAlpha),
                    sr, sg, sb, da - sa,
                    "custom alpha ReverseSubtract equation");

        BlendState constant;
        constant.setColorSourceBlendProperty(Blend::BlendFactor);
        constant.setColorDestinationBlendProperty(Blend::Zero);
        constant.setAlphaSourceBlendProperty(Blend::One);
        constant.setAlphaDestinationBlendProperty(Blend::Zero);
        constant.setBlendFactorProperty(Color(53, 181, 109, 239));
        CheckLinear(RenderSingle(device, constant, Color::White),
                    Unit(53), Unit(181), Unit(109), 1.0f,
                    "BlendFactor");

        constant.setColorSourceBlendProperty(Blend::InverseBlendFactor);
        CheckLinear(RenderSingle(device, constant, Color::White),
                    1.0f - Unit(53), 1.0f - Unit(181), 1.0f - Unit(109), 1.0f,
                    "InverseBlendFactor");

        // Static A -> B -> A in one target pass. Pre-fix, RenderSprites() reads only the final
        // live enabled bit (Opaque=false), so all three regions use Opaque and the middle Additive
        // result is visibly wrong.
        {
            const Rectangle a(4, 8, 16, 48);
            const Rectangle b(24, 8, 16, 48);
            const Rectangle aAgain(44, 8, 16, 48);
            device.SetRenderTarget(target_.get());
            device.Clear(kDestination);
            DrawSprite(device, a, kSource, BlendState::Opaque);
            DrawSprite(device, b, kSource, BlendState::Additive);
            DrawSprite(device, aAgain, kSource, BlendState::Opaque);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const auto pixels = ReadTarget();
            CheckLinear(At(pixels, 12, 32), sr, sg, sb, sa,
                        "Opaque -> Additive -> Opaque region A");
            CheckLinear(At(pixels, 32, 32),
                        sr * sa + dr, sg * sa + dg, sb * sa + db, sa * sa + da,
                        "Opaque -> Additive -> Opaque region B");
            CheckLinear(At(pixels, 52, 32), sr, sg, sb, sa,
                        "Opaque -> Additive -> Opaque region A again");
        }

        // Dynamic BlendFactor A -> B -> A in one target pass. The RGBA value is dynamic state and
        // must be captured per batch; it must not become pipeline identity.
        {
            BlendState red = constant;
            red.setColorSourceBlendProperty(Blend::BlendFactor);
            red.setBlendFactorProperty(Color(211, 31, 73, 255));
            BlendState green = red;
            green.setBlendFactorProperty(Color(29, 197, 83, 255));
            const Rectangle a(4, 8, 16, 48);
            const Rectangle b(24, 8, 16, 48);
            const Rectangle aAgain(44, 8, 16, 48);
            device.SetRenderTarget(target_.get());
            device.Clear(Color::Black);
            DrawSprite(device, a, Color::White, red);
            DrawSprite(device, b, Color::White, green);
            DrawSprite(device, aAgain, Color::White, red);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const auto pixels = ReadTarget();
            CheckLinear(At(pixels, 12, 32), Unit(211), Unit(31), Unit(73), 1.0f,
                        "BlendFactor A -> B -> A region A");
            CheckLinear(At(pixels, 32, 32), Unit(29), Unit(197), Unit(83), 1.0f,
                        "BlendFactor A -> B -> A region B");
            CheckLinear(At(pixels, 52, 32), Unit(211), Unit(31), Unit(73), 1.0f,
                        "BlendFactor A -> B -> A region A again");
        }

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        std::fflush(stdout);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    WebGpuSpriteBatchBlendStateTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(128);
        graphics_->setPreferredBackBufferHeightProperty(96);
    }

    int Result() const
    {
        return result_;
    }
};

int main()
{
    WebGpuSpriteBatchBlendStateTest game;
    game.Run();
    return game.Result();
}
