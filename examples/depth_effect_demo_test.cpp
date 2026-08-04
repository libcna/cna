// SPDX-License-Identifier: MS-PL
// Manual verification harness for CNA::Graphics::DepthEffect (NOXNA) — draws a colourful
// deterministic scene (reusing demo_2d's player.png sprite, tinted across a full hue sweep, plus
// a rainbow gradient strip) through SpriteBatch with DepthEffect bound, once per colour-depth
// mode, capturing a screenshot after each pass. Not registered as a ctest (visual output only) —
// see CNA_NOXNA guard below.

#ifdef CNA_NOXNA

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Graphics/DepthEffect.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/ScreenshotEXT.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::DepthEffect;
using CNA::Graphics::DepthEffectMode;
using CNA::Graphics::DitherMode;

namespace
{
    constexpr int kWidth = 800;
    constexpr int kHeight = 600;

    struct ModeEntry { DepthEffectMode mode; DitherMode dither; const char* label; };

    // Baseline (no dither) for every colour-depth mode, plus Bayer4x4/8x8 dithered variants for
    // the modes where ordered dithering visibly helps most (8-bit colour banding, 2-bit/1-bit
    // greyscale — 1-bit especially, since without dithering it degenerates to a hard threshold).
    const ModeEntry kModes[] = {
        {DepthEffectMode::Color16Bit,    DitherMode::None,     "16bit"},
        {DepthEffectMode::Color8Bit,     DitherMode::None,     "8bit_none"},
        {DepthEffectMode::Color8Bit,     DitherMode::Bayer4x4, "8bit_bayer4x4"},
        {DepthEffectMode::Grayscale4Bit, DitherMode::None,     "4bit_bw_none"},
        {DepthEffectMode::Grayscale2Bit, DitherMode::None,     "2bit_bw_none"},
        {DepthEffectMode::Grayscale2Bit, DitherMode::Bayer4x4, "2bit_bw_bayer4x4"},
        {DepthEffectMode::Grayscale2Bit, DitherMode::Bayer8x8, "2bit_bw_bayer8x8"},
        {DepthEffectMode::Grayscale1Bit, DitherMode::None,     "1bit_bw_none"},
        {DepthEffectMode::Grayscale1Bit, DitherMode::Bayer4x4, "1bit_bw_bayer4x4"},
        {DepthEffectMode::Grayscale1Bit, DitherMode::Bayer8x8, "1bit_bw_bayer8x8"},
    };

    // Deterministic HSV -> RGB, matching the standard sextant formula.
    Color HsvToColor(float h, float s, float v)
    {
        const float c = v * s;
        const float hp = h / 60.0f;
        const float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
        float r = 0, g = 0, b = 0;
        if      (hp < 1) { r = c; g = x; }
        else if (hp < 2) { r = x; g = c; }
        else if (hp < 3) { g = c; b = x; }
        else if (hp < 4) { g = x; b = c; }
        else if (hp < 5) { r = x; b = c; }
        else             { r = c; b = x; }
        const float m = v - c;
        return Color(static_cast<int>((r + m) * 255.0f),
                     static_cast<int>((g + m) * 255.0f),
                     static_cast<int>((b + m) * 255.0f), 255);
    }
}

class DepthEffectDemo : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<DepthEffect> depthFx_;
    Texture2D playerTexture_;
    Texture2D gradientTexture_;
    std::string outDir_;
    bool done_ = false;

    void BuildGradientTexture()
    {
        auto& device = getGraphicsDeviceProperty();
        constexpr int kGradW = 360;
        std::vector<std::uint8_t> pixels(static_cast<size_t>(kGradW) * 4);
        for (int x = 0; x < kGradW; ++x)
        {
            const Color c = HsvToColor(static_cast<float>(x), 1.0f, 1.0f);
            const size_t idx = static_cast<size_t>(x) * 4;
            pixels[idx + 0] = static_cast<std::uint8_t>(c.getRProperty());
            pixels[idx + 1] = static_cast<std::uint8_t>(c.getGProperty());
            pixels[idx + 2] = static_cast<std::uint8_t>(c.getBProperty());
            pixels[idx + 3] = 255;
        }
        gradientTexture_ = Texture2D::CreateFromPixels(device, kGradW, 1, pixels);
    }

    // DepthEffect quantizes each drawn fragment independently (a pointwise operation on the
    // sampled texel * vertex tint), so it can be bound directly as the SpriteBatch's own effect
    // while drawing the real scene -- no offscreen RenderTarget2D composite pass needed.
    void DrawScene()
    {
        SamplerState pointClamp = SamplerState::PointClamp;
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                            &pointClamp, nullptr, nullptr, depthFx_.get());

        // Rainbow gradient strip — a smooth hue sweep makes the quantization banding introduced
        // by each DepthEffect mode immediately visible. Kept within y=[140,600): under this
        // sandbox's headless Xvfb setup the top ~110px of the window's own client area is never
        // rasterized (an unrelated SDL/window-manager-less-Xvfb sizing quirk, not a DepthEffect
        // issue), so the whole scene is laid out below that margin.
        spriteBatch_->Draw(gradientTexture_, Rectangle(0, 440, kWidth, 150), Color::White);

        // A grid of the demo_2d player sprite, tinted across a full hue sweep — exercises both
        // the texture's own detail and flat colour tints together.
        const Rectangle bounds = playerTexture_.getBoundsProperty();
        constexpr int kCols = 8;
        constexpr int kRows = 3;
        constexpr float kTop = 140.0f;
        constexpr float kCellW = static_cast<float>(kWidth) / kCols;
        constexpr float kCellH = 90.0f;
        for (int row = 0; row < kRows; ++row)
        {
            for (int col = 0; col < kCols; ++col)
            {
                const int index = row * kCols + col;
                const float hue = (360.0f * static_cast<float>(index)) / (kCols * kRows);
                const Color tint = HsvToColor(hue, 0.85f, 1.0f);

                const float cx = kCellW * (static_cast<float>(col) + 0.5f);
                const float cy = kTop + kCellH * (static_cast<float>(row) + 0.5f);
                constexpr float kSize = 70.0f;

                const Rectangle dest(static_cast<int>(cx - kSize / 2.0f),
                                     static_cast<int>(cy - kSize / 2.0f),
                                     static_cast<int>(kSize), static_cast<int>(kSize));
                spriteBatch_->Draw(playerTexture_, dest, bounds, tint);
            }
        }
        spriteBatch_->End();
    }

    void CaptureAllModes()
    {
        auto& device = getGraphicsDeviceProperty();

        for (const auto& entry : kModes)
        {
            device.Clear(Color(18, 18, 32, 255));
            depthFx_->setMode(entry.mode);
            depthFx_->setDitherMode(entry.dither);
            DrawScene();

            const std::string path = outDir_ + "/depth_effect_" + entry.label + ".png";
            SaveBackBufferScreenshotEXT(device, path);
            std::printf("[depth_effect_demo] wrote %s (mode=%s)\n", path.c_str(), entry.label);
        }
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        getWindowProperty().setTitleProperty("CNA DepthEffect Demo");
    }

    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        depthFx_ = std::make_unique<DepthEffect>(device);

        playerTexture_ = getContentProperty().Load<Texture2D>("images/player.png");
        BuildGradientTexture();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        CaptureAllModes();

        Exit();
    }

public:
    explicit DepthEffectDemo(std::string outDir) : outDir_(std::move(outDir))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kWidth);
        gdm_->setPreferredBackBufferHeightProperty(kHeight);

        // Reuse demo_2d's player.png without duplicating the asset.
        getContentProperty().setRootDirectoryProperty(
            std::string(CNA_DEPTH_EFFECT_DEMO_CONTENT_ROOT));
    }
};

int main(int argc, char* argv[])
{
    std::string outDir = ".";
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--out" && i + 1 < argc)
            outDir = argv[++i];
    }

    DepthEffectDemo game(outDir);
    game.Run();
    return 0;
}

#else // CNA_NOXNA

#include <cstdio>
int main()
{
    std::puts("depth_effect_demo_test requires -DCNA_NOXNA=ON.");
    return 0;
}

#endif // CNA_NOXNA
