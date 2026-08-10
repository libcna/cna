// SPDX-License-Identifier: MS-PL
// REMED-GFX-084: the Bgfx renderer cannot represent two DIFFERENT SpriteBatch.Begin(transformMatrix)
// states submitted to the SAME GraphicsDevice.Viewport within one frame. EnsureViewState() bakes the
// batch transform into the sprite view's PER-VIEW bgfx::setViewTransform (orthoWithTransform =
// transformColMajor * ortho), which is resolved once at bgfx::frame() (last-write-wins). GFX-065's
// ordered view segmentation keys segments on the VIEWPORT only -- a transform-only change does NOT
// start a new segment -- so two same-viewport / different-transform batches share one view id and the
// LAST batch's transform wins for BOTH (the first batch's sprites are drawn with the wrong transform).
//
// XNA/FNA semantics: each batch renders under the transformMatrix passed to its own Begin().
//   Begin(transform A) -> draw RED, Begin(transform B) -> draw GREEN  ==>
//   RED at A's transformed location, GREEN at B's transformed location (both survive, disjoint).
//
// This test renders BOTH batches in ONE frame (no Present between), reads the WHOLE target in a single
// GetBackBufferData call (Bgfx reflects only the first read per rendered frame), and probes each region.
// Pre-fix: the first batch is transformed by the LAST batch's matrix -> its own location is empty.
//
// Sections: A translation A vs B (backbuffer); B A->B->A ordering (BLUE wins, GREEN preserved);
// C scale transforms (not just translation); D 70 same-transform Begin/End cycles (reuse, no segment
// explosion / no view-id exhaustion); E RenderTarget2D; F 32 distinct transforms (ordered segments at
// scale); G Viewport+transform composition (GFX-065 x GFX-084); H default vs explicit-identity vs custom.
//
// The Viewport is held CONSTANT (full target) in A-F so the boundary is proven to be the transform
// state, not GFX-065 viewport segmentation. Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBBW = 96;
    constexpr int kBBH = 72;

    bool isRed(const Color& c)   { return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60; }
    bool isGreen(const Color& c) { return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60; }
    bool isBlue(const Color& c)  { return c.getBProperty() >= 200 && c.getRProperty() <= 60 && c.getGProperty() <= 60; }
    bool isBlack(const Color& c) { return c.getRProperty() < 30 && c.getGProperty() < 30 && c.getBProperty() < 30; }
}

class BgfxSpriteBatchTransformTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<RenderTarget2D> rt_;
    std::unique_ptr<Texture2D> redTex_;
    std::unique_ptr<Texture2D> greenTex_;
    std::unique_ptr<Texture2D> blueTex_;
    int pass_ = 0, fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++pass_; else ++fail_;
    }

    static Color at(const std::vector<Color>& buf, int x, int y, int w = kBBW)
    {
        return buf[static_cast<std::size_t>(y) * w + x];
    }
    template <typename Pred>
    static int count(const std::vector<Color>& buf, Pred p)
    {
        int n = 0; for (const Color& c : buf) if (p(c)) ++n; return n;
    }

    // Reset to the full-backbuffer viewport so a section that expects it is independent of a prior
    // section that left a custom viewport set (GraphicsDevice does not reset it on Clear). Setting the
    // exact full rect is treated as NON-custom by the segment logic (no viewport segmentation).
    void fullViewport(GraphicsDevice& dev) { dev.setViewportProperty(Viewport(0, 0, kBBW, kBBH)); }

    std::unique_ptr<Texture2D> solidTex(GraphicsDevice& dev, const Color& c)
    {
        auto t = std::make_unique<Texture2D>(dev, 8, 8);
        std::vector<Color> px(64, c);
        t->SetData(px.data(), 64);
        return t;
    }

    // One SpriteBatch batch: Begin(...transform) / Draw(tex at destRect) / End. Full-target viewport
    // (default) unless the caller set a custom one just before. PointClamp so an 8x8 solid maps cleanly.
    void spriteBatch(Texture2D& tex, const Rectangle& destRect, const Matrix& transform)
    {
        SamplerState point = SamplerState::PointClamp;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr, nullptr, transform);
        sb_->Draw(tex, destRect, Rectangle(0, 0, 8, 8), Color::White);
        sb_->End();
    }

    std::vector<Color> readWhole(GraphicsDevice& dev, int w = kBBW, int h = kBBH)
    {
        std::vector<Color> buf(static_cast<std::size_t>(w) * h, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, w, h);
        dev.GetBackBufferData(&whole, buf.data(), 0, w * h);
        return buf;
    }

    // Render `scene` repeatedly and read the whole target back, discarding the first few frames to drain
    // the PRIOR section's stale content (bgfx reflects a readback ~1-2 frames behind the submit), then
    // accept the first frame that satisfies `fresh` -- a predicate on a pixel UNIQUE to this section.
    template <typename Scene, typename Fresh>
    std::vector<Color> settle(GraphicsDevice& dev, Scene scene, Fresh fresh, int w = kBBW, int h = kBBH)
    {
        std::vector<Color> buf(static_cast<std::size_t>(w) * h, Color(0, 0, 0, 0));
        for (int i = 0; i < 30; ++i)
        {
            scene();
            buf = readWhole(dev, w, h);
            if (i >= 3 && fresh(buf)) break;   // >=3 warmup frames drain the previous section
        }
        return buf;
    }

    // ---- Section A: same (full) viewport, translation A vs B, one frame (THE primary defect) ----
    // base destRect (5,5,20,16). A=(7,5) -> RED at x[12,32) y[10,26) c(22,18);
    //                            B=(45,31) -> GREEN at x[50,70) y[36,52) c(60,44).
    void sectionA(GraphicsDevice& dev)
    {
        const Rectangle base(5, 5, 20, 16);
        auto scene = [&] {
            dev.Clear(Color::Black);
            fullViewport(dev);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            spriteBatch(*redTex_,   base, Matrix::CreateTranslation(7, 5, 0));
            spriteBatch(*greenTex_, base, Matrix::CreateTranslation(45, 31, 0));
        };
        // GREEN at B is last-wins-stable (green there pre- and post-fix) -> reliable freshness discriminator.
        std::vector<Color> buf = settle(dev, scene, [&](const std::vector<Color>& b) { return isGreen(at(b, 60, 44)); });
        check(isGreen(at(buf, 60, 44)), "A: transform B batch GREEN at its own location (60,44)");
        check(isRed(at(buf, 22, 18)),   "A: transform A batch RED at its own location (22,18) [FAILS pre-fix -> black, last-wins]");
        check(count(buf, isRed) > 100,  "A: RED survives (transform A batch not overwritten by B): " + std::to_string(count(buf, isRed)) + "px");
        check(isBlack(at(buf, 40, 30)), "A: the gap between A and B stays black (no contamination)");
    }

    // ---- Section B: A->B->A transform ordering (overlapping A); BLUE (3rd, transform A) must win ----
    // A=(10,10) region x[10,34) y[10,30) c(22,20): RED then BLUE.  B=(55,40) region x[55,79) y[40,60) c(67,50): GREEN.
    void sectionB(GraphicsDevice& dev)
    {
        const Rectangle base(0, 0, 24, 20);
        const Matrix txA = Matrix::CreateTranslation(10, 10, 0);
        const Matrix txB = Matrix::CreateTranslation(55, 40, 0);
        auto scene = [&] {
            dev.Clear(Color::Black);
            fullViewport(dev);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            spriteBatch(*redTex_,   base, txA);   // RED   (transform A)
            spriteBatch(*greenTex_, base, txB);   // GREEN (transform B)
            spriteBatch(*blueTex_,  base, txA);   // BLUE  (transform A again -> NEW ordered segment)
        };
        // BLUE at the A region is present pre- AND post-fix (both put the 3rd draw there) -> stable freshness.
        std::vector<Color> buf = settle(dev, scene, [&](const std::vector<Color>& b) { return isBlue(at(b, 22, 20)); });
        check(isBlue(at(buf, 22, 20)),  "B: A region (22,20) is BLUE (3rd batch, transform A, on top of RED)");
        check(isGreen(at(buf, 67, 50)), "B: B region (67,50) is GREEN (transform B preserved) [FAILS pre-fix -> black]");
        check(isBlack(at(buf, 44, 35)), "B: gap between A and B regions stays black");
    }

    // ---- Section C: scale transforms (proves the key is the full transform, not just translation) ----
    // A: scale 0.5 of destRect(20,20,20,16) -> x[10,20) y[10,18) c(15,14) RED.
    // B: identity of destRect(60,45,20,16)  -> x[60,76) y[45,61) c(68,53) GREEN.
    // Pre-fix last-wins = identity: RED renders unscaled at x[20,40) y[20,36) -> (15,14) BLACK, (30,28) RED.
    void sectionC(GraphicsDevice& dev)
    {
        auto scene = [&] {
            dev.Clear(Color::Black);
            fullViewport(dev);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            spriteBatch(*redTex_,   Rectangle(20, 20, 20, 16), Matrix::CreateScale(0.5f));
            spriteBatch(*greenTex_, Rectangle(60, 45, 20, 16), Matrix::getIdentityProperty());
        };
        std::vector<Color> buf = settle(dev, scene, [&](const std::vector<Color>& b) { return isGreen(at(b, 68, 53)); });
        check(isRed(at(buf, 15, 14)),   "C: scale-0.5 batch RED at scaled location (15,14) [FAILS pre-fix -> black]");
        check(isBlack(at(buf, 30, 28)), "C: NOT red at the unscaled location (30,28) [pre-fix renders RED here, last-wins ignores the 0.5 scale]");
        check(isGreen(at(buf, 68, 53)), "C: identity batch GREEN at its own location (68,53)");
    }

    // ---- Section D: 70 same-transform Begin/End cycles in one frame -> reuse, NO segment explosion ----
    // If a transform-only change (or an unchanged transform) allocated a segment per Begin, 70 > 63 would
    // exhaust the [192,255) pool and throw. Reuse -> no allocation -> no throw. Correctness: same dest, so
    // the LAST colour wins (RED x69, GREEN last) -> green centre.
    void sectionD(GraphicsDevice& dev)
    {
        const Rectangle base(30, 25, 24, 20);   // +(8,6) -> x[38,62) y[31,51) c(50,41)
        const Matrix txA = Matrix::CreateTranslation(8, 6, 0);
        bool threw = false;
        auto scene = [&] {
            dev.Clear(Color::Black);
            fullViewport(dev);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            for (int i = 0; i < 70; ++i)
                spriteBatch((i == 69) ? *greenTex_ : *redTex_, base, txA);
        };
        std::vector<Color> buf;
        try {
            buf = settle(dev, scene, [&](const std::vector<Color>& b) { return isGreen(at(b, 50, 41)); });
        } catch (const std::exception& e) {
            threw = true;
            std::printf("  (exception during 70-cycle scene: %s)\n", e.what());
            buf.assign(static_cast<std::size_t>(kBBW) * kBBH, Color(0, 0, 0, 0));
        }
        check(!threw, "D: 70 same-transform Begin/End cycles in one frame did NOT exhaust segment ids (reuse, no explosion)");
        check(isGreen(at(buf, 50, 41)), "D: last of 70 same-transform batches (green) is on top at (50,41)");
    }

    // ---- Section E: RenderTarget2D, same viewport, two transforms (transform axis on an RT) ----
    // Positions are checked orientation-robustly by colour survival (GFX-067 flips RT sampling V); RED
    // surviving at ALL is the discriminator (pre-fix RED is transformed onto GREEN's spot and covered).
    void sectionE(GraphicsDevice& dev)
    {
        const int rtW = 48, rtH = 40;
        SamplerState point = SamplerState::PointClamp;
        const Rectangle base(4, 4, 16, 12);
        auto scene = [&] {
            dev.SetRenderTarget(rt_.get());
            dev.Clear(Color::Black);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            spriteBatch(*redTex_,   base, Matrix::CreateTranslation(2, 2, 0));    // RED  on RT
            spriteBatch(*greenTex_, base, Matrix::CreateTranslation(26, 22, 0));  // GREEN on RT (disjoint)
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            dev.Clear(Color::Black);
            fullViewport(dev);
            dev.setRasterizerStateProperty(RasterizerState());
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb_->Draw(*rt_, Rectangle(0, 0, rtW, rtH), Rectangle(0, 0, rtW, rtH), Color::White);
            sb_->End();
        };
        std::vector<Color> buf = settle(dev, scene, [&](const std::vector<Color>& b) { return count(b, isGreen) > 50; });
        check(count(buf, isGreen) > 50, "E: RT transform B batch (green) present: " + std::to_string(count(buf, isGreen)) + "px");
        check(count(buf, isRed) > 50,   "E: RT transform A batch (red) SURVIVES on the RT: " + std::to_string(count(buf, isRed)) + "px [FAILS pre-fix -> ~0, covered by B]");
    }

    // ---- Section F (stress): 32 DISTINCT transforms in one frame -> 32 ordered segments at scale ----
    // sprite i (3x3) at translation (i*3, i*2), alternating red/green. 32 distinct transforms consume
    // 31 segment ids (< 63). Pre-fix: all collapse onto the LAST transform's spot -> only 1 visible.
    void sectionF(GraphicsDevice& dev)
    {
        constexpr int kN = 32;
        const Rectangle base(0, 0, 3, 3);
        auto scene = [&] {
            dev.Clear(Color::Black);
            fullViewport(dev);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            for (int i = 0; i < kN; ++i)
                spriteBatch((i % 2 == 0) ? *redTex_ : *greenTex_, base,
                            Matrix::CreateTranslation(static_cast<float>(i * 3), static_cast<float>(i * 2), 0));
        };
        // last sprite (i=31, odd -> green) centre (94,63): drawn in every frame -> stable freshness.
        std::vector<Color> buf = settle(dev, scene, [&](const std::vector<Color>& b) { return isGreen(at(b, 94, 63)); });
        int ok = 0;
        for (int i = 0; i < kN; ++i)
        {
            const Color c = at(buf, i * 3 + 1, i * 2 + 1);
            if ((i % 2 == 0) ? isRed(c) : isGreen(c)) ++ok;
        }
        check(ok == kN, "F: 32 distinct transforms each render at their own spot (ordered segments at scale): "
              + std::to_string(ok) + "/32");
    }

    // ---- Section G: Viewport + transform composition (GFX-065 x GFX-084 compose) ----
    // (VpA, TxX) RED ; (VpA, TxY) GREEN [same viewport, transform change -> GFX-084 segment] ;
    // (VpB, TxY) BLUE [viewport change -> GFX-065 segment]. All three must survive at distinct spots.
    void sectionG(GraphicsDevice& dev)
    {
        const Viewport vpA(0, 0, 48, 36), vpB(48, 36, 48, 36);
        const Rectangle base(0, 0, 10, 8);
        auto scene = [&] {
            dev.Clear(Color::Black);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.setViewportProperty(vpA);
            spriteBatch(*redTex_,   base, Matrix::CreateTranslation(2, 2, 0));    // VpA local (2,2)  -> screen (2,2)
            spriteBatch(*greenTex_, base, Matrix::CreateTranslation(30, 4, 0));   // VpA local (30,4) -> screen (30,4)
            dev.setViewportProperty(vpB);
            spriteBatch(*blueTex_,  base, Matrix::CreateTranslation(2, 2, 0));    // VpB local (2,2)  -> screen (50,38)
        };
        std::vector<Color> buf = settle(dev, scene, [&](const std::vector<Color>& b) { return isBlue(at(b, 55, 42)); });
        check(count(buf, isRed) > 30,   "G: (VpA,TxX) RED survives a same-viewport transform change: " + std::to_string(count(buf, isRed)) + "px [FAILS pre-fix]");
        check(count(buf, isGreen) > 30, "G: (VpA,TxY) GREEN present: " + std::to_string(count(buf, isGreen)) + "px");
        check(isBlue(at(buf, 55, 42)),  "G: (VpB,TxY) BLUE present at (55,42) (viewport segmentation still works)");
    }

    // ---- Section H: default Begin() vs explicit Matrix::Identity vs custom transform ----
    // default RED and explicit-identity GREEN share the SAME effective state (both identity) -> reuse one
    // segment; custom BLUE gets a new one. All three at distinct disjoint spots.
    void sectionH(GraphicsDevice& dev)
    {
        SamplerState point = SamplerState::PointClamp;
        auto scene = [&] {
            dev.Clear(Color::Black);
            fullViewport(dev);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            // default Begin (no transform arg -> identity)
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb_->Draw(*redTex_, Rectangle(5, 5, 18, 14), Rectangle(0, 0, 8, 8), Color::White);   // x[5,23) y[5,19) c(14,12)
            sb_->End();
            // explicit identity
            spriteBatch(*greenTex_, Rectangle(60, 50, 18, 14), Matrix::getIdentityProperty());    // x[60,78) y[50,64) c(69,57)
            // custom translation
            spriteBatch(*blueTex_,  Rectangle(0, 30, 18, 14), Matrix::CreateTranslation(10, 0, 0));// x[10,28) y[30,44) c(19,37)
        };
        std::vector<Color> buf = settle(dev, scene, [&](const std::vector<Color>& b) { return isBlue(at(b, 19, 37)); });
        check(isRed(at(buf, 14, 12)),   "H: default-Begin RED at (14,12) [FAILS pre-fix -> shifted by the custom batch]");
        check(isGreen(at(buf, 69, 57)), "H: explicit-identity GREEN at (69,57) [FAILS pre-fix -> shifted]");
        check(isBlue(at(buf, 19, 37)),  "H: custom-transform BLUE at (19,37)");
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        rt_ = std::make_unique<RenderTarget2D>(dev, 48, 40);
        redTex_   = solidTex(dev, Color(255, 0, 0, 255));
        greenTex_ = solidTex(dev, Color(0, 255, 0, 255));
        blueTex_  = solidTex(dev, Color(0, 0, 255, 255));
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        sectionA(dev);
        sectionB(dev);
        sectionC(dev);
        sectionD(dev);
        sectionE(dev);
        sectionF(dev);
        sectionG(dev);
        sectionH(dev);
        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxSpriteBatchTransformTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxSpriteBatchTransformTest game;
    game.Run();
    return game.getResult();
}
