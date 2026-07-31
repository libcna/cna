// SPDX-License-Identifier: MS-PL
//
// plan_html_dom.md Phase D9: HTMLDOM-89 (performance) and HTMLDOM-90 (long-running stability),
// the two remaining unverified claims about this backend -- its whole performance premise had
// never been measured, and the longest run to date was 6 frames.
//
// Driven by the same scripts/run-htmldom-browser-test.sh / htmldom-browser-test.mjs harness as
// the other HTML_DOM test pages: pass this page's path as the argument.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Backends/HtmlDom/HtmlDomGraphicsBackend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kExpectedChecks = 3;

    // plan_html_dom.md HTMLDOM-89: 500 sprites is a reasonable "real 2D game" upper-middle
    // sprite count (well past typical tile/UI/particle counts for the kind of game this backend
    // targets, per its own documented "static sprite sheets, moving sprites" sweet spot).
    constexpr int kBenchmarkSpriteCount = 500;
    constexpr int kBenchmarkFrames = 60;

    // plan_html_dom.md HTMLDOM-90: long enough to cycle through more than the 256-entry variant
    // LRU cap multiple times over (each frame's sprites use a distinct tint derived from the
    // frame number), and to exercise the sprite pool growing and shrinking repeatedly rather than
    // monotonically in one direction.
    constexpr int kStabilityFrames = 300;

#if defined(__EMSCRIPTEN__)
    EM_JS(double, JsNow, (), { return performance.now(); });

    EM_JS(int, JsPooledSpriteCount, (), {
        const root = document.getElementById('cna-dom-root');
        return root ? root.children.length : -1;
    });

    // Module['cnaDomVariantLru'] is only created lazily, on the first variant that needs one
    // (plain untinted/un-blended draws share the base canvas and never enrol) -- undefined until
    // then is the correct, expected state, not a bug, so this reports 0 for that case.
    EM_JS(int, JsVariantLruLength, (), {
        const lru = Module['cnaDomVariantLru'];
        return lru ? lru.length : 0;
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
    int JsVariantLruLength() { return -1; }
    void JsPublishResult(int, int, int) {}
#endif
}

class HtmlDomStressTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    double benchmarkTotalMs_ = 0.0;
    int benchmarkFramesTimed_ = 0;
    int peakSpritesInOneFrame_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        if (ok) ++passCount_;
    }

    void DrawSprites(int count, float tintSeed)
    {
        peakSpritesInOneFrame_ = std::max(peakSpritesInOneFrame_, count);
        spriteBatch_->Begin();
        for (int i = 0; i < count; ++i)
        {
            const float x = static_cast<float>((i * 37) % 400);
            const float y = static_cast<float>((i * 53) % 200);
            // Cycles the tint across a wide range so, over many frames, the variant cache sees far
            // more than 256 distinct (mode, r, g, b) keys -- real LRU eviction, not a cache that
            // merely never fills up.
            const std::uint8_t r = static_cast<std::uint8_t>(std::fmod(tintSeed + i, 256.0f));
            spriteBatch_->Draw(*texture_, Vector2(x, y),
                               Color(r, static_cast<std::uint8_t>(200), static_cast<std::uint8_t>(150),
                                     static_cast<std::uint8_t>(255)));
        }
        spriteBatch_->End();
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        texture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            getGraphicsDeviceProperty(), 2, 2, std::vector<std::uint8_t>{
                255, 255, 255, 255,  255, 255, 255, 255,
                255, 255, 255, 255,  255, 255, 255, 255,
            }));
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(10, 10, 20, 255));

        // plan_html_dom.md HTMLDOM-89: frame 1 pays the one-time pool-creation cost (every sprite
        // element gets created and appended for the first time) -- deliberately excluded from the
        // measured average, which is meant to characterise the STEADY STATE the backend's whole
        // performance premise ("moving sprites costs nothing") is actually about.
        if (frame_ == 1)
        {
            DrawSprites(kBenchmarkSpriteCount, 0.0f);
        }
        else if (frame_ <= 1 + kBenchmarkFrames)
        {
            const double t0 = JsNow();
            DrawSprites(kBenchmarkSpriteCount, static_cast<float>(frame_) * 3.7f);
            const double t1 = JsNow();
            benchmarkTotalMs_ += (t1 - t0);
            ++benchmarkFramesTimed_;
        }
        else if (frame_ == 2 + kBenchmarkFrames)
        {
            const double avgMs = benchmarkFramesTimed_ > 0 ? benchmarkTotalMs_ / benchmarkFramesTimed_ : -1.0;
            std::printf("       HTMLDOM-89: %d sprites/frame, steady-state average over %d frames: "
                        "%.3f ms/frame (%.1f fps-equivalent)\n",
                        kBenchmarkSpriteCount, benchmarkFramesTimed_, avgMs,
                        avgMs > 0 ? 1000.0 / avgMs : 0.0);
            std::fflush(stdout);
            // Deliberately generous: this runs in a headless, possibly-virtualized/shared-CPU
            // container that may be far slower than a real user's machine, and the point of this
            // threshold is to catch a genuine architectural regression (e.g. an accidental
            // per-sprite JS call reappearing, or O(n^2) DOM work), not to assert a specific
            // competitive frame budget. 50ms/frame for 500 sprites is roughly 20x looser than a
            // 60fps budget would demand.
            check(avgMs >= 0.0 && avgMs < 50.0,
                  "HTMLDOM-89: steady-state per-frame cost for 500 animated sprites stays well "
                  "within a sane budget (no accidental O(n^2)/per-sprite-call regression)");
        }

        // plan_html_dom.md HTMLDOM-90: sprite count oscillates between a low and high value every
        // frame (exercising the pool growing AND shrinking repeatedly, not just monotonically),
        // and the tint continues cycling from the benchmark phase's own seed, so across the whole
        // run (benchmark + stability) far more than 256 distinct tint values are drawn.
        if (frame_ > 2 + kBenchmarkFrames && frame_ <= 2 + kBenchmarkFrames + kStabilityFrames)
        {
            const int localFrame = frame_ - (2 + kBenchmarkFrames);
            const int count = (localFrame % 20 < 10) ? 50 : 5;
            DrawSprites(count, static_cast<float>(frame_) * 5.3f);
        }

        if (frame_ == 3 + kBenchmarkFrames + kStabilityFrames)
        {
            const int pooled = JsPooledSpriteCount();
            const int lruLen = JsVariantLruLength();
            std::printf("       HTMLDOM-90: after %d stability frames -- pooled elements=%d "
                        "(peak sprites/frame=%d), variant LRU length=%d\n",
                        kStabilityFrames, pooled, peakSpritesInOneFrame_, lruLen);
            std::fflush(stdout);
            // The pool must never need more elements than the most sprites any single frame ever
            // drew -- if it grew beyond that, elements would be leaking rather than being
            // recycled. A little headroom (2x) allows for the benchmark phase's own separate peak
            // without being a meaningless "any value passes" check.
            check(pooled >= 0 && pooled <= peakSpritesInOneFrame_ * 2,
                  "HTMLDOM-90a: the sprite pool stays bounded by the peak per-frame sprite count "
                  "across 300 frames of oscillating draw counts, not growing unboundedly");
            // The LRU cap (256) must actually hold under real, sustained eviction pressure -- not
            // just "never filled up because the test never tried hard enough".
            check(lruLen >= 0 && lruLen <= 256,
                  "HTMLDOM-90b: the variant LRU cache never exceeds its 256-entry cap despite "
                  "cycling through far more than 256 distinct tint values over the run");

            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            std::fflush(stdout);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            JsPublishResult(result_, passCount_, kExpectedChecks);
            Exit();
        }
    }

public:
    HtmlDomStressTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(480);
        gdm_->setPreferredBackBufferHeightProperty(270);
    }

    int getResult() const { return result_; }
};

int main()
{
    HtmlDomStressTest game;
    game.Run();
    return game.getResult();
}
