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
// REMED-GFX-131: SurfaceFormat::Color is a plain 8-bit UNORM byte format on every renderer, so the
// shader output, the clear value, the native blend result and the byte GetData() returns are all
// the same value. Expected RGBA values below are therefore analytically blended and converted with
// one plain UNORM rounding, for colour and alpha alike. This file previously sRGB-encoded its RGB
// expectations because WebGPU gave its render targets the swapchain's *UnormSrgb format.
//
// Permanent coverage also records cache cardinality, proves dynamic constants and object
// identities are not keys, exercises RT -> backbuffer -> RT reuse, checks post-Begin mutation,
// independent channel masks, and verifies MultiSampleMask on the renderer's supported 4x target.
// The MSAA capability path runs WebGPU's existing validation error scope; the final native
// uncaptured-error count must remain zero.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

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

    /**
     * @brief The byte a unit colour component must be stored as.
     *
     * REMED-GFX-131: this used to apply a linear-to-sRGB encode, because WebGPU gave its render
     * targets the swapchain's *UnormSrgb format. SurfaceFormat::Color is a plain 8-bit UNORM byte
     * format, so the shader's linear output IS the stored byte and the encode was reproducing a
     * defect in the oracle. Colour and alpha now share one conversion, as they always should have.
     */
    int UnormByte(float value)
    {
        return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
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
    Texture2D alternateWhiteTexture_;
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
        const int er = UnormByte(r);
        const int eg = UnormByte(g);
        const int eb = UnormByte(b);
        const int ea = UnormByte(a);
        const bool matches =
            AbsI(actual.getRProperty() - er) <= tolerance &&
            AbsI(actual.getGProperty() - eg) <= tolerance &&
            AbsI(actual.getBProperty() - eb) <= tolerance &&
            AbsI(actual.getAProperty() - ea) <= tolerance;
        Check(matches,
              label + " expected stored RGBA(" + std::to_string(er) + "," +
              std::to_string(eg) + "," + std::to_string(eb) + "," +
              std::to_string(ea) + ") got (" +
              std::to_string(actual.getRProperty()) + "," +
              std::to_string(actual.getGProperty()) + "," +
              std::to_string(actual.getBProperty()) + "," +
              std::to_string(actual.getAProperty()) + ")");
    }

    void DrawSprite(GraphicsDevice& device, const Rectangle& destination, const Color& tint,
                    const BlendState& blendState, const Texture2D* texture = nullptr)
    {
        SpriteBatch batch(device);
        SamplerState point = SamplerState::PointClamp;
        batch.Begin(SpriteSortMode::Deferred, blendState, &point, nullptr, nullptr);
        batch.Draw(texture != nullptr ? *texture : whiteTexture_,
                   destination, Rectangle(0, 0, 1, 1), tint);
        batch.End();
    }

    std::vector<Color> ReadTarget(RenderTarget2D& target)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kRtW) * kRtH,
                                  Color(0, 0, 0, 0));
        target.GetData(0, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

    std::vector<Color> ReadTarget()
    {
        return ReadTarget(*target_);
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
        alternateWhiteTexture_ = Texture2D::CreateFromPixels(
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
        auto& renderer = static_cast<WebGPURenderer&>(device.GetRenderer());

        Check(renderer.GetSpritePipelineCacheSizeEXT() == 0,
              "SpriteBatch pipeline cache begins empty (lazy creation)");
        Check(renderer.GetUncapturedErrorCountEXT() == 0,
              "WebGPU validation callback begins with zero uncaptured errors");

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

        // Sixty-four distinct dynamic RGBA constants, two texture objects, and sixty-four sprite
        // identities all reuse the existing BlendFactor pipeline. Only the final overwrite is
        // visible, so it also proves the last command received its own captured dynamic value.
        {
            BlendState factorState;
            factorState.setColorSourceBlendProperty(Blend::BlendFactor);
            factorState.setColorDestinationBlendProperty(Blend::Zero);
            factorState.setAlphaSourceBlendProperty(Blend::One);
            factorState.setAlphaDestinationBlendProperty(Blend::Zero);
            const std::size_t before = renderer.GetSpritePipelineCacheSizeEXT();
            Color finalFactor(0, 0, 0, 255);
            device.SetRenderTarget(target_.get());
            device.Clear(Color::Black);
            for (int i = 0; i < 64; ++i)
            {
                finalFactor = Color((i * 37 + 17) & 255, (i * 73 + 29) & 255,
                                    (i * 109 + 43) & 255, 255);
                BlendState varying = factorState;
                varying.setBlendFactorProperty(finalFactor);
                DrawSprite(device, Rectangle(8, 8, 48, 48), Color::White, varying,
                           (i & 1) != 0 ? &alternateWhiteTexture_ : &whiteTexture_);
            }
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const Color actual = At(ReadTarget(), 32, 32);
            const std::size_t after = renderer.GetSpritePipelineCacheSizeEXT();
            CheckLinear(actual, Unit(finalFactor.getRProperty()), Unit(finalFactor.getGProperty()),
                        Unit(finalFactor.getBProperty()), 1.0f,
                        "64 BlendFactor RGBA changes preserve the final per-batch value");
            Check(before == after,
                  "64 BlendFactor values, two textures, and 64 sprites do not grow pipeline cache");
            std::printf("[INFO] BlendFactor cache cardinality: before=%zu after=%zu\n",
                        before, after);
        }

        // Capture happens at Begin(), not at End()/render time. Mutating both the caller's source
        // BlendState object and GraphicsDevice.BlendFactor after Begin must not alter this batch.
        {
            BlendState captured;
            captured.setColorSourceBlendProperty(Blend::BlendFactor);
            captured.setColorDestinationBlendProperty(Blend::Zero);
            captured.setAlphaSourceBlendProperty(Blend::One);
            captured.setAlphaDestinationBlendProperty(Blend::Zero);
            captured.setBlendFactorProperty(Color(173, 61, 219, 255));

            device.SetRenderTarget(target_.get());
            device.Clear(Color::Black);
            SpriteBatch batch(device);
            SamplerState point = SamplerState::PointClamp;
            batch.Begin(SpriteSortMode::Deferred, captured, &point, nullptr, nullptr);
            batch.Draw(whiteTexture_, Rectangle(8, 8, 48, 48),
                       Rectangle(0, 0, 1, 1), Color::White);
            captured.setColorSourceBlendProperty(Blend::InverseBlendFactor);
            captured.setColorWriteChannelsProperty(ColorWriteChannels::None);
            captured.setBlendFactorProperty(Color(7, 241, 19, 255));
            device.setBlendFactorProperty(Color(7, 241, 19, 255));
            batch.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            CheckLinear(At(ReadTarget(), 32, 32), Unit(173), Unit(61), Unit(219), 1.0f,
                        "source BlendState and GraphicsDevice.BlendFactor mutation after Begin "
                        "cannot alter queued batch");
        }

        // Attachment write masking is independently observable: enabled channels take the
        // source, disabled channels preserve the distinct destination (including alpha).
        {
            BlendState redBlue = BlendState::Opaque;
            redBlue.setColorWriteChannelsProperty(
                ColorWriteChannels::Red | ColorWriteChannels::Blue);
            CheckLinear(RenderSingle(device, redBlue),
                        sr, dg, sb, da, "ColorWriteChannels Red|Blue preserves G/A");

            BlendState greenAlpha = BlendState::Opaque;
            greenAlpha.setColorWriteChannelsProperty(
                ColorWriteChannels::Green | ColorWriteChannels::Alpha);
            CheckLinear(RenderSingle(device, greenAlpha),
                        dr, sg, db, sa, "ColorWriteChannels Green|Alpha preserves R/B");
        }

        // A new static equation creates one compatibility entry per distinct pass depth format.
        // The `first`/`second` targets are DepthFormat::None (no depth attachment) while the
        // backbuffer carries Depth24Stencil8, so WEBGPU-39 makes the backbuffer draw a second
        // pipeline -- depth FORMAT is part of the sprite key because a WebGPU pipeline's
        // depthStencil format must match the active pass. Target/texture/object IDENTITY is still
        // absent from the key: returning to `first`, a second None target object, and a second
        // texture object all reuse the single None-depth entry.
        {
            BlendState compatibility;
            compatibility.setColorSourceBlendProperty(Blend::DestinationColor);
            compatibility.setColorDestinationBlendProperty(Blend::Zero);
            compatibility.setAlphaSourceBlendProperty(Blend::One);
            compatibility.setAlphaDestinationBlendProperty(Blend::Zero);
            RenderTarget2D first(
                device, kRtW, kRtH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                RenderTargetUsage::PreserveContents);
            RenderTarget2D second(
                device, kRtW, kRtH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                RenderTargetUsage::DiscardContents);

            const std::size_t before = renderer.GetSpritePipelineCacheSizeEXT();
            device.SetRenderTarget(&first);
            device.Clear(kDestination);
            DrawSprite(device, Rectangle(0, 0, 32, 64), kSource, compatibility);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const std::size_t afterFirstTarget = renderer.GetSpritePipelineCacheSizeEXT();

            device.Clear(kDestination);
            DrawSprite(device, Rectangle(0, 0, 64, 64), kSource, compatibility);
            Color backbufferPixel(0, 0, 0, 0);
            const Rectangle backbufferRegion(32, 32, 1, 1);
            device.GetBackBufferData(&backbufferRegion, &backbufferPixel, 0, 1);
            const std::size_t afterBackbuffer = renderer.GetSpritePipelineCacheSizeEXT();
            CheckLinear(backbufferPixel, sr * dr, sg * dg, sb * db, sa,
                        "backbuffer uses the same custom sprite blend equation");

            device.SetRenderTarget(&first);
            DrawSprite(device, Rectangle(32, 0, 32, 64), kSource, compatibility,
                       &alternateWhiteTexture_);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const auto firstPixels = ReadTarget(first);
            CheckLinear(At(firstPixels, 16, 32), sr * dr, sg * dg, sb * db, sa,
                        "target -> backbuffer -> target preserves first target region");
            CheckLinear(At(firstPixels, 48, 32), sr * dr, sg * dg, sb * db, sa,
                        "target -> backbuffer -> target renders return region");
            const std::size_t afterReturn = renderer.GetSpritePipelineCacheSizeEXT();

            device.SetRenderTarget(&second);
            device.Clear(kDestination);
            DrawSprite(device, Rectangle(0, 0, 64, 64), kSource, compatibility,
                       &alternateWhiteTexture_);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            CheckLinear(At(ReadTarget(second), 32, 32), sr * dr, sg * dg, sb * db, sa,
                        "second compatible RenderTarget2D is pixel-correct");
            const std::size_t afterSecondTarget = renderer.GetSpritePipelineCacheSizeEXT();

            Check(afterFirstTarget == before + 1,
                  "new static blend compatibility on a None-depth target creates exactly one sprite pipeline");
            Check(afterBackbuffer == afterFirstTarget + 1,
                  "the Depth24Stencil8 backbuffer needs its own sprite pipeline (WEBGPU-39: depth format, not identity)");
            Check(afterReturn == afterBackbuffer && afterSecondTarget == afterBackbuffer,
                  "compatible None-depth target objects/textures reuse one sprite pipeline");
            std::printf("[INFO] compatibility cache cardinality: before=%zu firstRT=%zu "
                        "backbuffer=%zu returnRT=%zu secondRT=%zu\n",
                        before, afterFirstTarget, afterBackbuffer, afterReturn, afterSecondTarget);
        }

        // The preceding checks are the ordinary 1x control. Engage the renderer's supported 4x
        // route once; Supports4xMsaa performs the renderer's existing WGPU validation error-scope
        // round trip. No GFX-102 production submission/wait/frame/Present workaround is added.
        graphics_->setPreferMultiSamplingProperty(true);
        graphics_->ApplyChanges();
        const int appliedSamples =
            device.getPresentationParametersProperty().getMultiSampleCountProperty();
        Check(appliedSamples == 0 || appliedSamples == 4,
              "WebGPU MSAA request resolves to the supported 0-or-4 contract");
        std::printf("[INFO] WebGPU validation error-scope MSAA probe completed: applied=%d\n",
                    appliedSamples);

        if (appliedSamples == 4)
        {
            RenderTarget2D msaaTarget(
                device, kRtW, kRtH, false, SurfaceFormat::Color, DepthFormat::None, 4,
                RenderTargetUsage::DiscardContents);
            Check(msaaTarget.getMultiSampleCountProperty() == 4,
                  "supported RenderTarget2D reports the applied 4x sample count");
            Check(renderer.GetSpritePipelineCacheSizeEXT() == 0,
                  "sample-count reconfiguration invalidates old sprite compatibility entries");

            const Color msaaSource(196, 80, 40, 255);
            BlendState allSamples = BlendState::Opaque;
            allSamples.setMultiSampleMaskProperty(-1);
            device.SetRenderTarget(&msaaTarget);
            device.Clear(Color::Black);
            DrawSprite(device, Rectangle(8, 8, 48, 48), msaaSource, allSamples);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            CheckLinear(At(ReadTarget(msaaTarget), 32, 32),
                        Unit(196), Unit(80), Unit(40), 1.0f,
                        "4x MultiSampleMask all samples control");

            BlendState oneSample = BlendState::Opaque;
            oneSample.setMultiSampleMaskProperty(0x1);
            device.SetRenderTarget(&msaaTarget);
            device.Clear(Color::Black);
            DrawSprite(device, Rectangle(8, 8, 48, 48), msaaSource, oneSample);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            CheckLinear(At(ReadTarget(msaaTarget), 32, 32),
                        Unit(196) * 0.25f, Unit(80) * 0.25f, Unit(40) * 0.25f, 1.0f,
                        "4x MultiSampleMask bit 0 preserves three destination samples", 8);

            BlendState noSamples = BlendState::Opaque;
            noSamples.setMultiSampleMaskProperty(0);
            device.SetRenderTarget(&msaaTarget);
            device.Clear(Color::Black);
            DrawSprite(device, Rectangle(8, 8, 48, 48), msaaSource, noSamples);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            CheckLinear(At(ReadTarget(msaaTarget), 32, 32),
                        0.0f, 0.0f, 0.0f, 1.0f,
                        "4x MultiSampleMask zero preserves every destination sample");
            Check(renderer.GetSpritePipelineCacheSizeEXT() == 3,
                  "4x sample-mask static states create exactly three cache entries");
            std::printf("[INFO] 4x SpriteBatch pipeline-cache cardinality: %zu\n",
                        renderer.GetSpritePipelineCacheSizeEXT());
        }
        else
        {
            std::printf("[SKIP] Adapter exposes no supported 4x WebGPU render target; "
                        "MultiSampleMask runtime assertion not applicable\n");
        }

        const std::size_t validationErrors = renderer.GetUncapturedErrorCountEXT();
        std::printf("[INFO] WebGPU uncaptured validation/device error count: %zu\n",
                    validationErrors);
        Check(validationErrors == 0,
              "WebGPU validation/error logging remains clean for complete GFX-102 route");

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
