// SPDX-License-Identifier: MS-PL
// plans/plan_blend2d.md remediation (post-3da643712 review): regression coverage for RenderTarget2D
// public-property truthfulness and custom-Effect rejection, driven through the REAL public API
// (Game/GraphicsDeviceManager/GraphicsDevice/RenderTarget2D/SpriteBatch) rather than the internal
// Blend2DRenderer/Blend2DRenderTargetRenderer classes the other blend2d_*_test.cpp files drive
// directly. That distinction matters here specifically: RenderTarget2D.cpp is the shared code
// that decides what the PUBLIC DepthStencilFormat/MultiSampleCount/LevelCount properties report,
// and only exercising it through the real class proves the public contract, not just the internal
// renderer method it happens to call.
//
// Check group A -- DepthStencilFormat truthfully reports DepthFormat::None for every requested
//   preferred depth format (None/Depth16/Depth24/Depth24Stencil8): Blend2D never allocates a real
//   depth/stencil plane, so the public property must never claim one exists.
// Check group B -- GetRenderTargetRenderer()->HasRealDepthBuffer()/HasRealStencilBuffer() are
//   both false for every requested depth format, and a non-default DepthStencilState still throws
//   through the real GraphicsDevice.DepthStencilState property (depth/stencil ops stay rejected).
// Check group C -- MultiSampleCount publicly reports 0 even when a large preferred sample count is
//   requested (no fake MSAA claim), and a mipMap=true RenderTarget2D reports a LevelCount > 1
//   (matching the shared cross-renderer convention that LevelCount echoes the request) but
//   GetData() on any level beyond 0 throws NotSupportedException rather than returning fabricated
//   pixel data -- no mip level beyond level 0 is ever actually usable.
// Check group D -- SpriteBatch.Begin() with a real, minimal, non-stock custom Effect throws; null
//   and the exact stock SpriteEffect are both accepted; and a rejected custom-effect Begin() does
//   not poison the SpriteBatch -- an immediately following Begin()/Draw()/End() with the default
//   effect succeeds and rasterizes correctly.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::IRenderTargetRenderer;

namespace
{
    /// Smallest possible legitimate non-stock Effect: overrides only the two members Effect
    /// declares pure virtual (OnApply/Clone) and otherwise takes every base default, including
    /// IsExactStockSpriteEffectEXT() -- which stays `false` (the base Effect default), so this
    /// type is genuinely indistinguishable, from Blend2D's point of view, from any other
    /// game-authored custom shader Effect it has no pipeline to run.
    class MinimalCustomEffectEXT final : public Effect
    {
    public:
        explicit MinimalCustomEffectEXT(GraphicsDevice& device) : Effect(device) {}

    protected:
        void OnApply() override {}

    public:
        [[nodiscard]] Effect* Clone() override
        {
            return new MinimalCustomEffectEXT(*getGraphicsDeviceProperty());
        }
    };
}

class Blend2DRenderTargetPropertyTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> redTexture_;
    int frame_       = 0;
    int passCount_   = 0;
    int totalCount_  = 0;
    int result_      = 1;

    void check(bool ok, const char* label)
    {
        ++totalCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        redTexture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            getGraphicsDeviceProperty(), 2, 2,
            std::vector<std::uint8_t>{
                255, 0, 0, 255,  255, 0, 0, 255,
                255, 0, 0, 255,  255, 0, 0, 255}));
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        if (frame_ != 1) return;

        auto& dev = getGraphicsDeviceProperty();

        // ---- Check group A/B: DepthStencilFormat / HasRealDepthBuffer / HasRealStencilBuffer,
        // and that a non-default DepthStencilState still throws (depth/stencil ops stay rejected)
        // even though the render target's own DepthStencilFormat property behaves truthfully.
        struct DepthCase { DepthFormat requested; const char* name; };
        const DepthCase depthCases[] = {
            { DepthFormat::None,            "None" },
            { DepthFormat::Depth16,         "Depth16" },
            { DepthFormat::Depth24,         "Depth24" },
            { DepthFormat::Depth24Stencil8, "Depth24Stencil8" },
        };
        for (const auto& dc : depthCases)
        {
            RenderTarget2D rt(dev, 8, 8, false, SurfaceFormat::Color, dc.requested, 0,
                              RenderTargetUsage::DiscardContents);
            check(rt.getDepthStencilFormatProperty() == DepthFormat::None,
                 (std::string("RenderTarget2D(preferredDepthFormat=") + dc.name +
                  "): public DepthStencilFormat truthfully reports None (Blend2D allocates no "
                  "real depth/stencil plane)").c_str());

            IRenderTargetRenderer* rtRenderer = rt.GetRenderTargetRenderer();
            check(rtRenderer != nullptr,
                 (std::string("RenderTarget2D(") + dc.name + "): GetRenderTargetRenderer() is non-null").c_str());
            if (rtRenderer)
            {
                check(!rtRenderer->HasRealDepthBuffer(dc.requested != DepthFormat::None),
                     (std::string("RenderTarget2D(") + dc.name + "): HasRealDepthBuffer() is false").c_str());
                check(!rtRenderer->HasRealStencilBuffer(dc.requested == DepthFormat::Depth24Stencil8),
                     (std::string("RenderTarget2D(") + dc.name + "): HasRealStencilBuffer() is false").c_str());
            }
        }

        // A non-default DepthStencilState (depth testing actually enabled) must still be rejected
        // by the renderer when applied, regardless of what depth format a render target was
        // constructed with -- this renderer never gained real depth-test capability.
        {
            bool threw = false;
            try { dev.setDepthStencilStateProperty(DepthStencilState::Default); }
            catch (...) { threw = true; }
            check(threw, "GraphicsDevice.DepthStencilState = DepthStencilState.Default (depth "
                        "testing enabled) throws -- depth/stencil ops remain rejected");
            // Restore a harmless state so the rest of this frame's drawing is unaffected.
            try { dev.setDepthStencilStateProperty(DepthStencilState::None); } catch (...) {}
        }

        // ---- Check group C: MultiSampleCount and mip-level truthfulness.
        {
            RenderTarget2D rt(dev, 8, 8, false, SurfaceFormat::Color, DepthFormat::None,
                              /*preferredMultiSampleCount=*/8, RenderTargetUsage::DiscardContents);
            check(rt.getMultiSampleCountProperty() == 0,
                 "RenderTarget2D(preferredMultiSampleCount=8): public MultiSampleCount truthfully "
                 "clamps to 0 (Blend2D has no real MSAA)");
        }
        {
            RenderTarget2D rt(dev, 8, 8, /*mipMap=*/true, SurfaceFormat::Color, DepthFormat::None);
            check(rt.getLevelCountProperty() > 1,
                 "RenderTarget2D(mipMap=true, 8x8): public LevelCount reports the requested mip "
                 "chain length (matches the shared cross-renderer convention)");

            bool level1Threw = false;
            try
            {
                std::vector<Color> px(4 * 4, Color::Black);
                rt.GetData(1, nullptr, px.data(), 0, static_cast<int>(px.size()));
            }
            catch (const System::NotSupportedException&) { level1Threw = true; }
            check(level1Threw,
                 "RenderTarget2D(mipMap=true): GetData(level=1) throws NotSupportedException -- "
                 "no mip level beyond 0 is ever usable, LevelCount is metadata only");

            // Level 0 must remain fully usable -- the mip-level rejection must not have broken the
            // base level.
            bool level0Threw = false;
            try
            {
                std::vector<Color> px(8 * 8, Color::Black);
                rt.GetData(0, nullptr, px.data(), 0, static_cast<int>(px.size()));
            }
            catch (...) { level0Threw = true; }
            check(!level0Threw, "RenderTarget2D(mipMap=true): GetData(level=0) still succeeds");
        }

        // ---- Check group D: custom-effect rejection through the real public SpriteBatch API.
        {
            MinimalCustomEffectEXT customEffect(dev);
            bool threw = false;
            try
            {
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr,
                                    nullptr, nullptr, &customEffect);
            }
            catch (const std::exception&) { threw = true; }
            check(threw, "SpriteBatch.Begin() with a real minimal non-stock custom Effect throws");

            // Rejection must not poison the batch: a plain Begin()/Draw()/End() must still work,
            // AND actually rasterize (not just "not throw").
            dev.Clear(Color(0, 0, 64, 255));
            bool secondThrew = false;
            try
            {
                spriteBatch_->Begin();
                spriteBatch_->Draw(*redTexture_, Rectangle(0, 0, 2, 2), Color::White);
                spriteBatch_->End();
            }
            catch (...) { secondThrew = true; }
            check(!secondThrew,
                 "SpriteBatch.Begin()/Draw()/End() with the default effect succeeds immediately "
                 "after a rejected custom-effect Begin()");

            std::vector<Color> px(1, Color::Black);
            Rectangle r(0, 0, 1, 1);
            dev.GetBackBufferData(&r, px.data(), 0, 1);
            check(px[0].getRProperty() == 255 && px[0].getGProperty() == 0 && px[0].getBProperty() == 0,
                 "Post-rejection SpriteBatch draw genuinely rasterizes (not a silent no-op)");

            // nullptr is accepted (restores the built-in sprite path).
            bool nullThrew = false;
            try
            {
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr,
                                    nullptr, nullptr, nullptr);
                spriteBatch_->End();
            }
            catch (...) { nullThrew = true; }
            check(!nullThrew, "SpriteBatch.Begin() with a null custom effect is accepted");

            // The exact stock SpriteEffect is accepted too.
            SpriteEffect stockEffect(dev);
            bool stockThrew = false;
            try
            {
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr,
                                    nullptr, nullptr, &stockEffect);
                spriteBatch_->End();
            }
            catch (...) { stockThrew = true; }
            check(!stockThrew, "SpriteBatch.Begin() with the exact stock SpriteEffect is accepted");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    Blend2DRenderTargetPropertyTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return result_; }
};

int main()
{
    Blend2DRenderTargetPropertyTest game;
    game.Run();
    return game.getResult();
}
