// SPDX-License-Identifier: MS-PL
//
// plans/plan_html_dom.md HTMLDOM-116: measures and bounds the compositor-layer/DOM memory this renderer's
// sprite pool retains, rather than trusting the "will-change:transform keeps every sprite on its own
// compositor layer" design decision to be harmless at scale on claims alone. Every pooled sprite
// element gets its own permanent compositor layer (HtmlDomSpriteBatchRenderer.cpp's own comment on
// why), and until this task the pool never released one once created -- elements past the current
// frame's count were only ever hidden via display:none, never removed. This test proves both halves
// of the age-out fix in CNA_HtmlDom_PresentFrame (HtmlDomRenderer.cpp): (1) a transient high
// sprite-count burst (5,000/10,000 sprites, well past this renderer's own documented "normal 2D game"
// sweet spot -- HTMLDOM-89's 500-sprite benchmark) does NOT permanently retain that many pool
// elements once usage genuinely settles back down, and (2) ordinary fluctuating gameplay sprite
// counts -- which also dip below any past peak, constantly -- never trigger that age-out, so the fix
// costs nothing during real play.
//
// Driven by the same scripts/run-htmldom-browser-test.sh / htmldom-browser-test.mjs harness as the
// other HTML_DOM test pages: pass this page's path as the argument.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kExpectedChecks = 6;

    // plans/plan_html_dom.md HTMLDOM-116: the ticket's own named stress tiers -- well past this renderer's
    // documented "normal 2D game" sweet spot (HTMLDOM-89's own 500-sprite benchmark).
    constexpr int kMidBurstSpriteCount = 5000;
    constexpr int kBigBurstSpriteCount = 10000;

    // Must match CNA_HtmlDom_PresentFrame's own kIdleShrinkFrames constant in
    // HtmlDomRenderer.cpp -- duplicated here deliberately, the same way every test file that
    // checks the 256-entry variant-cache cap restates that number rather than reading it back from
    // the renderer, since an EM_JS-local constant isn't otherwise observable from C++.
    constexpr int kIdleShrinkFrames = 180;
    constexpr int kIdleSpriteCount = 20;
    // A margin past the exact threshold -- proves the shrink actually fires within this window,
    // not merely that it would have on some later frame this test never reaches.
    constexpr int kIdleHoldFrames = kIdleShrinkFrames + 10;

    // plans/plan_html_dom.md HTMLDOM-90: a real game's own sprite count never sits perfectly flat --
    // oscillating between two values (matching htmldom_stress_test.cpp's own HTMLDOM-90 pattern),
    // held for LONGER than kIdleShrinkFrames, proves the age-out genuinely requires a settled scene,
    // not merely "some frames below peak".
    constexpr int kOscillateFrames = kIdleShrinkFrames + 20;

#if defined(__EMSCRIPTEN__)
    EM_JS(double, JsNow, (), { return performance.now(); });

    EM_JS(int, JsPooledSpriteCount, (), {
        const root = document.getElementById('cna-dom-root');
        return root ? root.children.length : -1;
    });

    EM_JS(void, JsPublishResult, (int result, int passed, int expected), {
        window.__cnaSmokeResult = result;
        window.__cnaSmokePassed = passed;
        window.__cnaSmokeExpected = expected;
        window.__cnaSmokeDone = true;
    });
#else
    double JsNow() { return 0.0; }
    int JsPooledSpriteCount() { return -1; }
    void JsPublishResult(int, int, int) {}
#endif
}

class HtmlDomMemoryTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        if (ok) ++passCount_;
    }

    void DrawSprites(int count)
    {
        spriteBatch_->Begin();
        for (int i = 0; i < count; ++i)
        {
            const float x = static_cast<float>((i * 13) % 2000);
            const float y = static_cast<float>((i * 29) % 1200);
            spriteBatch_->Draw(*texture_, Vector2(x, y), Color::White);
        }
        spriteBatch_->End();
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        texture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            getGraphicsDeviceProperty(), 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(10, 10, 20, 255));

        // Frame 1: a 5,000-sprite burst -- measured on its own since this is the pool's entirely
        // one-time creation cost (every element created + appended for the first time), the
        // expensive case this task's own measurement is actually about.
        if (frame_ == 1)
        {
            const double t0 = JsNow();
            DrawSprites(kMidBurstSpriteCount);
            const double t1 = JsNow();
            const int pooled = JsPooledSpriteCount();
            std::printf("       HTMLDOM-116: first-creation burst of %d sprites -- %.3f ms, "
                        "pooled DOM elements=%d\n", kMidBurstSpriteCount, t1 - t0, pooled);
            std::fflush(stdout);
            check(pooled == kMidBurstSpriteCount,
                  "HTMLDOM-116: a 5,000-sprite burst creates exactly 5,000 pool elements");
        }

        // Frame 2: push further to 10,000 -- the ticket's own upper named tier.
        if (frame_ == 2)
        {
            const double t0 = JsNow();
            DrawSprites(kBigBurstSpriteCount);
            const double t1 = JsNow();
            const int pooled = JsPooledSpriteCount();
            std::printf("       HTMLDOM-116: growing the same burst to %d sprites -- %.3f ms, "
                        "pooled DOM elements=%d\n", kBigBurstSpriteCount, t1 - t0, pooled);
            std::fflush(stdout);
            check(pooled == kBigBurstSpriteCount,
                  "HTMLDOM-116: growing the burst to 10,000 sprites creates exactly 10,000 pool "
                  "elements -- confirms the pool's own growth stays exact at this scale too, not "
                  "just at the smaller counts every other test in this suite already exercises");
            // Loose, deliberately generous bound -- catches a real O(n^2)/per-element regression,
            // not a competitive performance target (matching htmldom_stress_test.cpp's own
            // HTMLDOM-89 threshold philosophy). Measured ~44 ms in this same container as of this
            // task -- 500 ms is roughly 11x looser, the same order of headroom HTMLDOM-89 uses.
            check(t1 - t0 < 500.0,
                  "HTMLDOM-116: creating 10,000 pool elements for the first time stays within a "
                  "sane budget, not exhibiting runaway per-element or O(n^2) cost");
        }

        // Frames 3..(2+kIdleHoldFrames): settle to a small, genuinely UNCHANGING sprite count --
        // this is the "long idle period" the ticket names, and the case the age-out fix targets:
        // usage identical every single frame, for longer than kIdleShrinkFrames in a row.
        if (frame_ > 2 && frame_ <= 2 + kIdleHoldFrames)
        {
            DrawSprites(kIdleSpriteCount);
        }

        if (frame_ == 3 + kIdleHoldFrames)
        {
            const int pooled = JsPooledSpriteCount();
            std::printf("       HTMLDOM-116: after %d frames settled at %d sprites/frame (down "
                        "from a %d-sprite peak) -- pooled DOM elements=%d\n",
                        kIdleHoldFrames, kIdleSpriteCount, kBigBurstSpriteCount, pooled);
            std::fflush(stdout);
            check(pooled == kIdleSpriteCount,
                  "HTMLDOM-116: after a sustained settled period, the pool actually SHRINKS back "
                  "down to the genuinely current sprite count, releasing the excess DOM nodes and "
                  "compositor layers a one-time 10,000-sprite burst would otherwise retain "
                  "permanently for the rest of the session");
        }

        // Proves regrowth still works normally post-shrink -- this fix must not leave the pool
        // permanently capped at whatever it last shrank to.
        if (frame_ == 4 + kIdleHoldFrames)
        {
            DrawSprites(kMidBurstSpriteCount);
            const int pooled = JsPooledSpriteCount();
            std::printf("       HTMLDOM-116: regrowth after shrink -- drawing %d sprites again -- "
                        "pooled DOM elements=%d\n", kMidBurstSpriteCount, pooled);
            std::fflush(stdout);
            check(pooled == kMidBurstSpriteCount,
                  "HTMLDOM-116: the pool grows back to exactly the requested count on demand after "
                  "shrinking -- the age-out never leaves the pool permanently undersized");
        }

        // Oscillate for LONGER than kIdleShrinkFrames -- proves ordinary fluctuating gameplay
        // (never perfectly flat) never triggers the age-out, matching htmldom_stress_test.cpp's own
        // HTMLDOM-90 oscillation pattern but held deliberately longer than the shrink threshold, so
        // the "never" claim is actually exercised at that length, not merely untested there.
        if (frame_ > 4 + kIdleHoldFrames && frame_ <= 4 + kIdleHoldFrames + kOscillateFrames)
        {
            const int localFrame = frame_ - (4 + kIdleHoldFrames);
            DrawSprites((localFrame % 2 == 0) ? kMidBurstSpriteCount : kMidBurstSpriteCount / 4);
        }

        if (frame_ == 5 + kIdleHoldFrames + kOscillateFrames)
        {
            const int pooled = JsPooledSpriteCount();
            std::printf("       HTMLDOM-116: after %d frames oscillating (never repeating the "
                        "same count twice in a row) -- pooled DOM elements=%d (peak=%d)\n",
                        kOscillateFrames, pooled, kMidBurstSpriteCount);
            std::fflush(stdout);
            check(pooled == kMidBurstSpriteCount,
                  "HTMLDOM-116: ordinary fluctuating sprite counts -- never settling on one exact "
                  "value for kIdleShrinkFrames in a row -- never trigger the age-out, even over a "
                  "run deliberately longer than the shrink threshold: the pool stays pinned at the "
                  "oscillation's own peak, exactly like it always did before this task");

            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            std::fflush(stdout);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            JsPublishResult(result_, passCount_, kExpectedChecks);
            Exit();
        }
    }

public:
    HtmlDomMemoryTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1920);
        gdm_->setPreferredBackBufferHeightProperty(1080);
    }

    int getResult() const { return result_; }
};

int main()
{
    HtmlDomMemoryTest game;
    game.Run();
    return game.getResult();
}
