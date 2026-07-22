// SPDX-License-Identifier: MS-PL
// REMED-GFX-065: the Bgfx backend cannot represent two DIFFERENT GraphicsDevice.Viewport states
// submitted to the same target within one frame. bgfx view state (setViewRect / setViewTransform)
// is PER-VIEW (the last call before bgfx::frame() applies to every draw in that view), and both the
// 3D path (ApplyViewportOverride -> setViewRect(currentViewId_)) and the SpriteBatch path
// (EnsureViewState -> full rect + offset ortho on spriteViewId) submit every draw on the same target
// to one shared view id -> the LAST viewport wins for ALL draws (last-wins contamination).
//
// XNA/FNA semantics: each draw/batch must render under the Viewport active when it was submitted.
//   Viewport A -> draw RED, Viewport B -> draw GREEN  ==>  RED only in A, GREEN only in B.
//
// This test renders BOTH viewports in ONE frame (no Present between), then reads the WHOLE backbuffer
// in a single GetBackBufferData call (Bgfx reflects only the first read per rendered frame) and probes
// each region. Pre-fix: the first draw is lost (overwritten in the last viewport) -> region A is empty.
//
// Sections: A 3D two disjoint viewports; B SpriteBatch two disjoint viewports; C 3D A->B->A ordering
// (overlapping, blue must win); D RenderTarget2D two viewports; E same-viewport consecutive draws.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
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

class BgfxMultiViewportTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<RenderTarget2D> rt_;
    std::unique_ptr<Texture2D> redTex_;
    std::unique_ptr<Texture2D> greenTex_;
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

    // Full-NDC colored quad -> fills the entire current Viewport rect.
    void quad3D(GraphicsDevice& dev, BasicEffect& fx, const Color& col)
    {
        const VertexPositionColor v[6] = {
            { Vector3(-1.f,  1.f, 0.f), col }, { Vector3( 1.f,  1.f, 0.f), col }, { Vector3(-1.f, -1.f, 0.f), col },
            { Vector3(-1.f, -1.f, 0.f), col }, { Vector3( 1.f,  1.f, 0.f), col }, { Vector3( 1.f, -1.f, 0.f), col },
        };
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, v, 0, 2);
    }

    void setupFx(BasicEffect& fx)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
    }

    std::unique_ptr<Texture2D> solidTex(GraphicsDevice& dev, const Color& c)
    {
        auto t = std::make_unique<Texture2D>(dev, 8, 8);
        std::vector<Color> px(64, c);
        t->SetData(px.data(), 64);
        return t;
    }

    std::vector<Color> readWhole(GraphicsDevice& dev, int w = kBBW, int h = kBBH)
    {
        std::vector<Color> buf(static_cast<std::size_t>(w) * h, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, w, h);
        dev.GetBackBufferData(&whole, buf.data(), 0, w * h);
        return buf;
    }

    // Render `scene` repeatedly and read the whole target back, discarding the first few frames to
    // drain the PRIOR section's stale content (bgfx reflects a readback ~1-2 frames behind the submit,
    // and ReadBackbuffer itself advances frames), then accept the first frame that satisfies `fresh`
    // -- a predicate on a pixel UNIQUE to this section, so a leftover frame can never satisfy it.
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

    // ---- Section A: 3D, two disjoint viewports in one frame ----
    // A = (5,5,30,24) RED ; B = (55,38,30,24) GREEN. Centres (20,17) and (70,50).
    void sectionA(GraphicsDevice& dev)
    {
        auto scene = [&] {
            dev.Clear(Color::Black);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            BasicEffect fx(dev); setupFx(fx);
            dev.setViewportProperty(Viewport(5, 5, 30, 24));
            quad3D(dev, fx, Color(255, 0, 0, 255));
            dev.setViewportProperty(Viewport(55, 38, 30, 24));
            quad3D(dev, fx, Color(0, 255, 0, 255));
        };
        std::vector<Color> buf = settle(dev, scene, [&](const std::vector<Color>& b) { return isGreen(at(b, 70, 50)); });
        check(isGreen(at(buf, 70, 50)), "A: viewport B centre (70,50) is GREEN");
        check(isRed(at(buf, 20, 17)),   "A: viewport A centre (20,17) is RED [FAILS pre-fix -> black, last-wins]");
        check(count(buf, isRed) > 100,  "A: RED survives (viewport A not overwritten): " + std::to_string(count(buf, isRed)) + "px");
        check(isBlack(at(buf, 20, 50)) && isBlack(at(buf, 70, 17)), "A: the disjoint gap stays black (no cross-viewport contamination)");
    }

    // ---- Section B: SpriteBatch, two disjoint viewports in one frame ----
    void sectionB(GraphicsDevice& dev)
    {
        SamplerState point = SamplerState::PointClamp;
        auto scene = [&] {
            dev.Clear(Color::Black);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.setViewportProperty(Viewport(5, 5, 30, 24));
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb_->Draw(*redTex_, Rectangle(0, 0, 30, 24), Rectangle(0, 0, 8, 8), Color::White);
            sb_->End();
            dev.setViewportProperty(Viewport(55, 38, 30, 24));
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb_->Draw(*greenTex_, Rectangle(0, 0, 30, 24), Rectangle(0, 0, 8, 8), Color::White);
            sb_->End();
        };
        std::vector<Color> buf = settle(dev, scene, [&](const std::vector<Color>& b) { return isGreen(at(b, 70, 50)); });
        check(isGreen(at(buf, 70, 50)), "B: SpriteBatch viewport B centre (70,50) is GREEN");
        check(isRed(at(buf, 20, 17)),   "B: SpriteBatch viewport A centre (20,17) is RED [FAILS pre-fix -> black]");
        check(count(buf, isRed) > 100,  "B: SpriteBatch RED survives: " + std::to_string(count(buf, isRed)) + "px");
    }

    // ---- Section C: 3D A->B->A ordering (overlapping); BLUE (3rd, viewport A) must win in overlap ----
    // A = (10,10,50,40) ; B = (30,25,50,40). They overlap in x[30,60) y[25,50).
    void sectionC(GraphicsDevice& dev)
    {
        const Viewport vpA(10, 10, 50, 40), vpB(30, 25, 50, 40);
        auto scene = [&] {
            dev.Clear(Color::Black);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            BasicEffect fx(dev); setupFx(fx);
            dev.setViewportProperty(vpA); quad3D(dev, fx, Color(255, 0, 0, 255));   // RED
            dev.setViewportProperty(vpB); quad3D(dev, fx, Color(0, 255, 0, 255));   // GREEN
            dev.setViewportProperty(vpA); quad3D(dev, fx, Color(0, 0, 255, 255));   // BLUE
        };
        // Freshness: BLUE at the A-only corner is unique to this section AND is the post-fix result
        // (pre-fix last-wins ALSO renders BLUE there since all three draws collapse into vpA -- so this
        // reliably settles on a genuine section-C frame either way; the discriminator is the B-only corner).
        std::vector<Color> buf = settle(dev, scene, [&](const std::vector<Color>& b) { return isBlue(at(b, 15, 15)); });
        check(isBlue(at(buf, 15, 15)), "C: A-only corner (15,15) is BLUE (3rd draw, viewport A, on top)");
        check(isBlue(at(buf, 45, 37)), "C: overlap (45,37) is BLUE (A->B->A order preserved; A2 not merged into A1)");
        // vpB-only corner (75,60): only the GREEN draw (vpB) reaches it -> GREEN (post-fix ordering guard).
        check(isGreen(at(buf, 75, 60)), "C: B-only corner (75,60) is GREEN (green segment preserved, not lost)");
    }

    // ---- Section D: RenderTarget2D, two disjoint viewports in one frame ----
    void sectionD(GraphicsDevice& dev)
    {
        const int rtW = 64, rtH = 48;
        SamplerState point = SamplerState::PointClamp;
        auto scene = [&] {
            dev.SetRenderTarget(rt_.get());
            dev.Clear(Color::Black);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            BasicEffect fx(dev); setupFx(fx);
            dev.setViewportProperty(Viewport(4, 4, 24, 18));  quad3D(dev, fx, Color(255, 0, 0, 255));
            dev.setViewportProperty(Viewport(36, 26, 24, 18)); quad3D(dev, fx, Color(0, 255, 0, 255));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            dev.Clear(Color::Black);
            dev.setRasterizerStateProperty(RasterizerState());
            dev.setBlendStateProperty(BlendState::Opaque);
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb_->Draw(*rt_, Rectangle(0, 0, rtW, rtH), Rectangle(0, 0, rtW, rtH), Color::White);
            sb_->End();
        };
        std::vector<Color> buf = settle(dev, scene, [&](const std::vector<Color>& b) { return isGreen(at(b, 48, 35)); });
        check(isGreen(at(buf, 48, 35)), "D: RT viewport B centre is GREEN");
        check(isRed(at(buf, 16, 13)),   "D: RT viewport A centre is RED [FAILS pre-fix]");
    }

    // ---- Section E: same viewport, consecutive draws -> one segment suffices (correctness) ----
    void sectionE(GraphicsDevice& dev)
    {
        auto scene = [&] {
            dev.Clear(Color::Black);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            BasicEffect fx(dev); setupFx(fx);
            dev.setViewportProperty(Viewport(20, 15, 40, 30));
            quad3D(dev, fx, Color(255, 0, 0, 255));   // red first
            quad3D(dev, fx, Color(0, 255, 0, 255));   // green second, same viewport -> on top
        };
        std::vector<Color> buf = settle(dev, scene, [&](const std::vector<Color>& b) { return isGreen(at(b, 40, 30)); });
        check(isGreen(at(buf, 40, 30)), "E: same-viewport consecutive draws -> later (green) on top (ordering within a segment)");
        // Both draws share ONE viewport -> ONE view/segment, so green fully overwrites red inside the
        // viewport rect (20,15,40,30). (Only red WITHIN the viewport is checked: bgfx's Clear is scoped to
        // the active view rect, matching pre-fix behavior, so content OUTSIDE the viewport is left as-is.)
        int redInVp = 0;
        for (int y = 15; y < 45; ++y) for (int x = 20; x < 60; ++x) if (isRed(at(buf, x, y))) ++redInVp;
        check(redInVp == 0, "E: no red survives inside the viewport (green fully covers -- single segment reused): "
              + std::to_string(redInVp) + "px");
    }

    // ---- Section F (stress): many distinct viewports in ONE frame -> ordered segments at scale ----
    // 16 disjoint vertical bands (6px each) across the 96-wide backbuffer, alternating red/green, each
    // under its own Viewport in one frame. Exercises 16 segment views + per-frame recycling across the
    // settle loop (segment ids reset every frame; a monotonic leak would exhaust the range and throw).
    void sectionF(GraphicsDevice& dev)
    {
        constexpr int kBands = 16, kBandW = 6;   // 16*6 = 96
        auto scene = [&] {
            dev.Clear(Color::Black);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            BasicEffect fx(dev); setupFx(fx);
            for (int i = 0; i < kBands; ++i)
            {
                dev.setViewportProperty(Viewport(i * kBandW, 0, kBandW, kBBH));
                quad3D(dev, fx, (i % 2 == 0) ? Color(255, 0, 0, 255) : Color(0, 255, 0, 255));
            }
        };
        // Freshness: the last band (15, odd -> green) centre. Unique enough after the warmup drain.
        std::vector<Color> buf = settle(dev, scene, [&](const std::vector<Color>& b) { return isGreen(at(b, 15 * kBandW + 3, 36)); });
        int okBands = 0;
        for (int i = 0; i < kBands; ++i)
        {
            const Color c = at(buf, i * kBandW + 3, 36);
            const bool ok = (i % 2 == 0) ? isRed(c) : isGreen(c);
            if (ok) ++okBands;
        }
        check(okBands == kBands,
              "F: 16 distinct viewports in one frame each render their own colour (ordered segments at scale): "
              + std::to_string(okBands) + "/16 bands correct");
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        rt_ = std::make_unique<RenderTarget2D>(dev, 64, 48);
        redTex_ = solidTex(dev, Color(255, 0, 0, 255));
        greenTex_ = solidTex(dev, Color(0, 255, 0, 255));
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
        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxMultiViewportTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxMultiViewportTest game;
    game.Run();
    return game.getResult();
}
