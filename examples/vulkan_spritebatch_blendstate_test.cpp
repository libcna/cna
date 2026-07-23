// SPDX-License-Identifier: MS-PL
// REMED-GFX-071: the Vulkan 2D SpriteBatch pipeline must honor the SpriteBatch.Begin() BlendState,
// not a single hardcoded alpha-blend equation.
//
// Pre-fix, GetOrCreatePipeline2D()/GetOrCreatePipeline2DMsaa() hardcode
//   srcColor=SRC_ALPHA, dstColor=ONE_MINUS_SRC_ALPHA, srcAlpha=ONE, dstAlpha=ZERO, op=ADD
// (non-premultiplied "over" for colour + straight-copy alpha) and cache the pipeline by depth
// format only, so SpriteBatch.Begin(sortMode, blendState) has NO effect on the blend equation:
// Opaque / AlphaBlend / Additive / NonPremultiplied / custom factors / custom functions /
// BlendFactor / separate alpha are all silent no-ops for sprites. The 3D path already honors the
// BlendState (Task 868 FillBlendAttachmentState + per-draw blendParams). The four "known-correct"
// backends (EasyGL, D3D11, SdlGpu, Bgfx) DERIVE the sprite blend from the BlendState and thus
// produce the XNA-correct PREMULTIPLIED AlphaBlend (colorSrc=One) for the default Begin(); Vulkan
// (and WebGPU) were the outliers hardcoding non-premultiplied SourceAlpha.
//
// Every scene renders into an off-screen 64x64 RenderTarget2D and reads it back with GetData()
// (Vulkan RT GetData is synchronous after REMED-GFX-074). The destination is set with Clear(bg);
// a single semi-transparent (or white, for the constant-factor scenes) sprite is then drawn with
// the BlendState under test, and the fully-covered centre pixel is compared to the analytic blend
// result. GetData carries the real RGBA of the R8G8B8A8_UNORM target, so alpha is observable.
//
// Colour source S (for the colored scenes) = white texel * tint = tint = (200,40,40) with
// srcAlpha 128 (0.50196). Destination D = Clear colour = (60,120,180,255). op = Add unless noted.
//
//   Opaque            : colour = S (replace), alpha = Sa                 -> (200, 40, 40, 128)
//   AlphaBlend (def.) : colour = S*1     + D*(1-Sa)  [PREMULTIPLIED]     -> (230,100,130, 255)
//   NonPremultiplied  : colour = S*Sa    + D*(1-Sa)  [straight]          -> (130, 80,110, 191)
//   Additive          : colour = S*Sa    + D*1                           -> (160,140,200, 255)
//   Custom SourceColor: colour = S*S,  alpha = Sa                        -> (157,  6,  6, 128)
//   Custom Subtract   : colour = S*1 - D*1 (clamp>=0), alpha = Sa        -> (140,  0,  0, 128)
//   Separate alpha    : colour = AlphaBlend, alpha src=Zero/dst=Zero     -> (230,100,130,   0)
//   BlendFactor       : white * C,  C = (64,192,128)                     -> ( 64,192,128, 255)
//   InverseBlendFactor: white * (1-C)                                    -> (191, 63,127, 255)
//   Multi (one frame) : A=AlphaBlend, B=Opaque, C=Additive, 3 regions    -> per-region as above
//   Constant A→B→A    : RED, GREEN, RED captured across three batches
//   Pipeline P1→P2→P1: constant RED, ordinary Opaque BLUE, constant RED
//
// Pre-fix the hardcoded pipeline reads (130,80,110,128) for EVERY colored scene and white
// (255,255,255,255) for the constant-factor scenes, so all checks fail except NonPremultiplied's
// colour (which coincidentally matches the hardcode) — whose ALPHA check still fails (128 vs 191).
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRtW = 64;
    constexpr int kRtH = 64;

    // Destination background (fully opaque) that every colored scene blends over.
    const Color kBg(60, 120, 180, 255);
    // Colored semi-transparent source: srcAlpha = 128.
    const Color kSrc(200, 40, 40, 128);

    int AbsI(int v) { return v < 0 ? -v : v; }
}

class VulkanSpriteBatchBlendStateTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    Texture2D whiteTex_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    // One Begin/Draw/End cycle with an explicit BlendState (point sampler so the 1x1 white texel is
    // reproduced exactly at every fragment; the frag shader outputs texel*tint = tint).
    void DrawSprite(GraphicsDevice& dev, const Rectangle& dest, const Color& tint,
                    const BlendState& blend)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, blend, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, dest, Rectangle(0, 0, 1, 1), tint);
        sb.End();
    }

    // Override GraphicsDevice.BlendFactor after Begin() applies the BlendState and before End()
    // snapshots the batch. This isolates per-batch value capture from static BlendState identity.
    void DrawSpriteWithFactor(GraphicsDevice& dev, const Rectangle& dest, const Color& tint,
                              const BlendState& blend, const Color& factor)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, blend, &point, nullptr, nullptr);
        dev.setBlendFactorProperty(factor);
        sb.Draw(whiteTex_, dest, Rectangle(0, 0, 1, 1), tint);
        sb.End();
    }

    std::vector<Color> ReadRt()
    {
        std::vector<Color> pix(static_cast<std::size_t>(kRtW) * kRtH, Color(0, 0, 0, 0));
        rt_->GetData(0, nullptr, pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    static Color At(const std::vector<Color>& pix, int x, int y)
    {
        return pix[static_cast<std::size_t>(y) * kRtW + x];
    }

    // Render one BlendState scene into the RT over a fresh Clear(bg) and return the centre pixel.
    Color RenderScene(GraphicsDevice& dev, const Color& bg, const Rectangle& dest,
                      const Color& tint, const BlendState& blend)
    {
        dev.SetRenderTarget(rt_.get());
        dev.Clear(bg);
        DrawSprite(dev, dest, tint, blend);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const std::vector<Color> pix = ReadRt();
        return At(pix, dest.X + dest.Width / 2, dest.Y + dest.Height / 2);
    }

    void checkRGB(const Color& got, int r, int g, int b, int tol, const std::string& label)
    {
        const bool ok = AbsI(got.getRProperty() - r) <= tol &&
                        AbsI(got.getGProperty() - g) <= tol &&
                        AbsI(got.getBProperty() - b) <= tol;
        check(ok, label + " expected RGB(" + std::to_string(r) + "," + std::to_string(g) + "," +
              std::to_string(b) + ") got (" + std::to_string(got.getRProperty()) + "," +
              std::to_string(got.getGProperty()) + "," + std::to_string(got.getBProperty()) + ")");
    }

    void checkA(const Color& got, int a, int tol, const std::string& label)
    {
        check(AbsI(got.getAProperty() - a) <= tol,
              label + " expected A=" + std::to_string(a) + " got " +
              std::to_string(got.getAProperty()));
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRtW, kRtH, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1,
                        std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        const Rectangle full(8, 8, 48, 48);
        const int tol = 5;

        // Scene 1 -- Opaque: source replaces destination, alpha copied (128), colour NOT blended.
        {
            const Color c = RenderScene(dev, kBg, full, kSrc, BlendState::Opaque);
            checkRGB(c, 200, 40, 40, tol, "S1 Opaque colour");
            checkA(c, 128, tol, "S1 Opaque alpha");
        }

        // Scene 2 -- AlphaBlend (the XNA default): PREMULTIPLIED, colourSrc = One.
        {
            const Color c = RenderScene(dev, kBg, full, kSrc, BlendState::AlphaBlend);
            checkRGB(c, 230, 100, 130, tol, "S2 AlphaBlend colour (premultiplied)");
            checkA(c, 255, tol, "S2 AlphaBlend alpha");
        }

        // Scene 3 -- NonPremultiplied: straight alpha; the ALPHA (191) is the true differentiator
        // from the old hardcode (which wrote 128).
        {
            const Color c = RenderScene(dev, kBg, full, kSrc, BlendState::NonPremultiplied);
            checkRGB(c, 130, 80, 110, tol, "S3 NonPremultiplied colour");
            checkA(c, 191, tol, "S3 NonPremultiplied alpha");
        }

        // Scene 4 -- Additive.
        {
            const Color c = RenderScene(dev, kBg, full, kSrc, BlendState::Additive);
            checkRGB(c, 160, 140, 200, tol, "S4 Additive colour");
        }

        // Scene 5 -- custom colour factor SourceColor: colour = S*S (multiply-by-self). Exercises a
        // Blend value no preset uses, proving arbitrary factors reach the 2D pipeline.
        {
            BlendState bs;
            bs.setColorSourceBlendProperty(Blend::SourceColor);
            bs.setColorDestinationBlendProperty(Blend::Zero);
            bs.setAlphaSourceBlendProperty(Blend::One);
            bs.setAlphaDestinationBlendProperty(Blend::Zero);
            const Color c = RenderScene(dev, kBg, full, kSrc, bs);
            checkRGB(c, 157, 6, 6, tol, "S5 custom SourceColor factor (colour=S*S)");
        }

        // Scene 6 -- custom BlendFunction Subtract: colour = S*1 - D*1 (clamped >= 0). Proves the
        // blend OP is not silently forced to Add.
        {
            BlendState bs;
            bs.setColorSourceBlendProperty(Blend::One);
            bs.setColorDestinationBlendProperty(Blend::One);
            bs.setColorBlendFunctionProperty(BlendFunction::Subtract);
            bs.setAlphaSourceBlendProperty(Blend::One);
            bs.setAlphaDestinationBlendProperty(Blend::Zero);
            const Color c = RenderScene(dev, kBg, full, kSrc, bs);
            checkRGB(c, 140, 0, 0, tol, "S6 custom Subtract function (colour=S-D)");
        }

        // Scene 7 -- separate alpha factors: colour from AlphaBlend (One/InverseSourceAlpha) but
        // alpha src=Zero/dst=Zero -> RT alpha = 0, independent of the colour equation.
        {
            BlendState bs;
            bs.setColorSourceBlendProperty(Blend::One);
            bs.setColorDestinationBlendProperty(Blend::InverseSourceAlpha);
            bs.setAlphaSourceBlendProperty(Blend::Zero);
            bs.setAlphaDestinationBlendProperty(Blend::Zero);
            const Color c = RenderScene(dev, kBg, full, kSrc, bs);
            checkRGB(c, 230, 100, 130, tol, "S7 separate-alpha colour (premult)");
            checkA(c, 0, tol, "S7 separate-alpha alpha (src=Zero,dst=Zero -> 0)");
        }

        // Scene 8 -- Blend::BlendFactor (GFX-070 integration): white source * constant colour. The
        // constant travels on the BlendState's own BlendFactor property, is captured per batch by
        // GFX-070, and only becomes observable once the 2D pipeline selects CONSTANT_COLOR.
        {
            BlendState bs;
            bs.setColorSourceBlendProperty(Blend::BlendFactor);
            bs.setColorDestinationBlendProperty(Blend::Zero);
            bs.setAlphaSourceBlendProperty(Blend::One);
            bs.setAlphaDestinationBlendProperty(Blend::Zero);
            bs.setBlendFactorProperty(Color(64, 192, 128, 255));
            const Color c = RenderScene(dev, kBg, full, Color(255, 255, 255, 255), bs);
            checkRGB(c, 64, 192, 128, tol, "S8 Blend::BlendFactor (white*constant)");
        }

        // Scene 9 -- Blend::InverseBlendFactor: white source * (1 - constant).
        {
            BlendState bs;
            bs.setColorSourceBlendProperty(Blend::InverseBlendFactor);
            bs.setColorDestinationBlendProperty(Blend::Zero);
            bs.setAlphaSourceBlendProperty(Blend::One);
            bs.setAlphaDestinationBlendProperty(Blend::Zero);
            bs.setBlendFactorProperty(Color(64, 192, 128, 255));
            const Color c = RenderScene(dev, kBg, full, Color(255, 255, 255, 255), bs);
            checkRGB(c, 191, 63, 127, tol, "S9 Blend::InverseBlendFactor (white*(1-constant))");
        }

        // Scene 10 -- three DIFFERENT BlendStates in ONE frame / ONE render target: AlphaBlend,
        // Opaque, Additive over three disjoint regions, one Clear, one unbind, one GetData. This is
        // the pipeline-cache-key crux: a frame-final single-BlendState solution fails here.
        {
            dev.SetRenderTarget(rt_.get());
            dev.Clear(kBg);
            const Rectangle rA(4, 8, 16, 48), rB(24, 8, 16, 48), rC(44, 8, 16, 48);
            DrawSprite(dev, rA, kSrc, BlendState::AlphaBlend);
            DrawSprite(dev, rB, kSrc, BlendState::Opaque);
            DrawSprite(dev, rC, kSrc, BlendState::Additive);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const std::vector<Color> pix = ReadRt();
            checkRGB(At(pix, rA.X + rA.Width / 2, 32), 230, 100, 130, tol, "S10 region A AlphaBlend");
            checkRGB(At(pix, rB.X + rB.Width / 2, 32), 200, 40, 40, tol, "S10 region B Opaque");
            checkRGB(At(pix, rC.X + rC.Width / 2, 32), 160, 140, 200, tol, "S10 region C Additive");
        }

        // Scene 11 -- BlendState lifetime: the BlendState object is destroyed AFTER End() but BEFORE
        // the deferred Present/GetData record consumes the batch. GFX-071 captures the blend by
        // VALUE into BatchSnapshot (not a BlendState*), so the batch must stay valid -- no
        // GFX-075-style dangling-pointer regression. Uses an Additive-equivalent custom state.
        {
            dev.SetRenderTarget(rt_.get());
            dev.Clear(kBg);
            {
                BlendState scoped;
                scoped.setColorSourceBlendProperty(Blend::SourceAlpha);
                scoped.setColorDestinationBlendProperty(Blend::One);
                scoped.setAlphaSourceBlendProperty(Blend::SourceAlpha);
                scoped.setAlphaDestinationBlendProperty(Blend::One);
                DrawSprite(dev, full, kSrc, scoped);
            } // `scoped` destroyed here, before the unbind/GetData below records the batch.
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const Color c = At(ReadRt(), full.X + full.Width / 2, full.Y + full.Height / 2);
            checkRGB(c, 160, 140, 200, tol, "S11 BlendState destroyed before Present (by-value capture)");
        }

        // Scene 12 -- mandatory GFX-091 A→B→A: one static constant-factor BlendState, three
        // different captured dynamic values across three batches in the same RT pass.
        {
            BlendState constant;
            constant.setColorSourceBlendProperty(Blend::BlendFactor);
            constant.setColorDestinationBlendProperty(Blend::Zero);
            constant.setAlphaSourceBlendProperty(Blend::One);
            constant.setAlphaDestinationBlendProperty(Blend::Zero);
            const Rectangle rA(4, 8, 16, 48), rB(24, 8, 16, 48), rA2(44, 8, 16, 48);
            dev.SetRenderTarget(rt_.get());
            dev.Clear(Color::Black);
            DrawSpriteWithFactor(dev, rA,  Color::White, constant, Color(255, 0, 0, 255));
            DrawSpriteWithFactor(dev, rB,  Color::White, constant, Color(0, 255, 0, 255));
            DrawSpriteWithFactor(dev, rA2, Color::White, constant, Color(255, 0, 0, 255));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const std::vector<Color> pix = ReadRt();
            checkRGB(At(pix, rA.X + rA.Width / 2, 32), 255, 0, 0, tol,
                     "S12 constant A (RED)");
            checkRGB(At(pix, rB.X + rB.Width / 2, 32), 0, 255, 0, tol,
                     "S12 constant B (GREEN)");
            checkRGB(At(pix, rA2.X + rA2.Width / 2, 32), 255, 0, 0, tol,
                     "S12 constant A2 (RED)");
        }

        // Scene 13 -- dynamic/static/dynamic pipeline transition in one command buffer.
        {
            BlendState constant;
            constant.setColorSourceBlendProperty(Blend::BlendFactor);
            constant.setColorDestinationBlendProperty(Blend::Zero);
            constant.setAlphaSourceBlendProperty(Blend::One);
            constant.setAlphaDestinationBlendProperty(Blend::Zero);
            const Rectangle p1(4, 8, 16, 48), p2(24, 8, 16, 48), p3(44, 8, 16, 48);
            dev.SetRenderTarget(rt_.get());
            dev.Clear(Color::Black);
            DrawSpriteWithFactor(dev, p1, Color::White, constant, Color(255, 0, 0, 255));
            DrawSprite(dev, p2, Color(0, 0, 255, 255), BlendState::Opaque);
            DrawSpriteWithFactor(dev, p3, Color::White, constant, Color(255, 0, 0, 255));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const std::vector<Color> pix = ReadRt();
            checkRGB(At(pix, p1.X + p1.Width / 2, 32), 255, 0, 0, tol,
                     "S13 P1 constant-dependent");
            checkRGB(At(pix, p2.X + p2.Width / 2, 32), 0, 0, 255, tol,
                     "S13 P2 ordinary Opaque");
            checkRGB(At(pix, p3.X + p3.Width / 2, 32), 255, 0, 0, tol,
                     "S13 P3 reused constant-dependent");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    VulkanSpriteBatchBlendStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(128);
        gdm_->setPreferredBackBufferHeightProperty(96);
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanSpriteBatchBlendStateTest game;
    game.Run();
    return game.getResult();
}
