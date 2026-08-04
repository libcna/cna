// SPDX-License-Identifier: MS-PL
// Manual verification harness for CNA::Graphics::CRTEffect (NOXNA) — renders the same colourful
// deterministic scene as depth_effect_demo_test.cpp (demo_2d's player.png sprite tinted across a
// hue sweep, plus a rainbow gradient strip) into an offscreen RenderTarget2D once, then redraws
// that single composited frame full-screen through SpriteBatch with CRTEffect bound, once per
// parameter combination, capturing a screenshot after each pass (see RenderSceneToTexture()'s own
// comment for why CRTEffect needs a single full-screen source, unlike DepthEffect). Not
// registered as a ctest (visual output only) — see CNA_NOXNA guard below.

#ifdef CNA_NOXNA

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Graphics/CRTEffect.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/ScreenshotEXT.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::CRTEffect;
using CNA::Graphics::CRTMaskType;

namespace
{
    constexpr int kWidth = 800;
    constexpr int kHeight = 600;

    struct CRTParams
    {
        float scanlineIntensity;
        float curvature;
        float vignetteIntensity;
        float maskIntensity;
        CRTMaskType maskType;
        const char* label;
    };

    const CRTParams kParams[] = {
        {0.5f, 0.0f,  0.10f, 0.0f, CRTMaskType::None,           "scanlines_only"},
        {0.3f, 0.08f, 0.25f, 0.5f, CRTMaskType::ApertureGrille, "aperture_grille"},
        {0.3f, 0.08f, 0.25f, 0.5f, CRTMaskType::ShadowMask,     "shadow_mask"},
        {0.4f, 0.15f, 0.40f, 0.4f, CRTMaskType::ApertureGrille, "full_crt"},
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

class CRTEffectDemo : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<CRTEffect> crtFx_;
    std::unique_ptr<RenderTarget2D> sceneRT_;
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

    // Unlike DepthEffect (a pure per-pixel colour transform), CRTEffect's curvature/vignette warp
    // and measure position in vTexCoord -- which is only a meaningful proxy for "where on screen
    // this fragment is" when the whole final frame is drawn as ONE full-screen quad. Bound
    // directly to a multi-draw-call scene (one sprite per Draw() call), each sprite gets its OWN
    // little curvature/vignette applied around ITS OWN local texture rect instead of one global
    // curved screen -- so the scene is rendered into an offscreen RenderTarget2D first (no
    // CRTEffect bound), then that single composited texture is redrawn full-screen through
    // CRTEffect. Scanlines/the RGB mask don't have this restriction (they already index by
    // gl_FragCoord, real screen pixels) but the single-full-screen-pass requirement is simplest
    // to document as applying to the whole effect.
    void RenderSceneToTexture()
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(sceneRT_.get());
        device.Clear(Color(18, 18, 32, 255));

        // Rainbow gradient strip. Kept within y=[140,600): under this sandbox's headless Xvfb
        // setup the top ~110px of the window's own client area is never rasterized (an
        // unrelated SDL/window-manager-less-Xvfb sizing quirk, not a CRTEffect issue), so the
        // whole scene is laid out below that margin.
        spriteBatch_->Begin();
        spriteBatch_->Draw(gradientTexture_, Rectangle(0, 440, kWidth, 150), Color::White);

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

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    // Redraws sceneRT_ as a single full-screen quad through CRTEffect. FlipVertically
    // compensates for RenderTarget2D content being stored bottom-up in GL versus a normally
    // top-down-loaded texture -- without it the composited frame comes out upside down.
    void DrawCrtPass()
    {
        SamplerState pointClamp = SamplerState::PointClamp;
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            &pointClamp, nullptr, nullptr, crtFx_.get());
        spriteBatch_->Draw(*sceneRT_, Rectangle(0, 0, kWidth, kHeight),
                           Rectangle(0, 0, kWidth, kHeight), Color::White,
                           0.0f, Vector2::Zero, SpriteEffects::FlipVertically, 0.0f);
        spriteBatch_->End();
    }

    void CaptureAllParams()
    {
        auto& device = getGraphicsDeviceProperty();
        RenderSceneToTexture();

        for (const auto& p : kParams)
        {
            device.Clear(Color::Black);
            crtFx_->setScanlineIntensity(p.scanlineIntensity);
            crtFx_->setCurvature(p.curvature);
            crtFx_->setVignetteIntensity(p.vignetteIntensity);
            crtFx_->setMaskIntensity(p.maskIntensity);
            crtFx_->setMaskType(p.maskType);
            DrawCrtPass();

            const std::string path = outDir_ + "/crt_effect_" + p.label + ".png";
            SaveBackBufferScreenshotEXT(device, path);
            std::printf("[crt_effect_demo] wrote %s\n", path.c_str());
        }
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        getWindowProperty().setTitleProperty("CNA CRTEffect Demo");
    }

    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        crtFx_ = std::make_unique<CRTEffect>(device);
        sceneRT_ = std::make_unique<RenderTarget2D>(device, kWidth, kHeight);

        playerTexture_ = getContentProperty().Load<Texture2D>("images/player.png");
        BuildGradientTexture();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        CaptureAllParams();

        Exit();
    }

public:
    explicit CRTEffectDemo(std::string outDir) : outDir_(std::move(outDir))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kWidth);
        gdm_->setPreferredBackBufferHeightProperty(kHeight);

        // Reuse demo_2d's player.png without duplicating the asset.
        getContentProperty().setRootDirectoryProperty(
            std::string(CNA_CRT_EFFECT_DEMO_CONTENT_ROOT));
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

    CRTEffectDemo game(outDir);
    game.Run();
    return 0;
}

#else // CNA_NOXNA

#include <cstdio>
int main()
{
    std::puts("crt_effect_demo_test requires -DCNA_NOXNA=ON.");
    return 0;
}

#endif // CNA_NOXNA
