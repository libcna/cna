// SPDX-License-Identifier: MS-PL
//
// plans/plan_html_dom.md Phase D9: HTMLDOM-89 (performance) and HTMLDOM-90 (long-running stability),
// the two remaining unverified claims about this renderer -- its whole performance premise had
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

#include "CNA/Internal/Renderers/HtmlDom/HtmlDomRenderer.hpp"
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomTextureRenderer.hpp"

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
    constexpr int kExpectedChecks = 10;

    // plans/plan_html_dom.md HTMLDOM-89: 500 sprites is a reasonable "real 2D game" upper-middle
    // sprite count (well past typical tile/UI/particle counts for the kind of game this renderer
    // targets, per its own documented "static sprite sheets, moving sprites" sweet spot).
    constexpr int kBenchmarkSpriteCount = 500;
    constexpr int kBenchmarkFrames = 60;

    // plans/plan_html_dom.md HTMLDOM-90: long enough to cycle through more than the 256-entry variant
    // LRU cap multiple times over (each frame's sprites use a distinct tint derived from the
    // frame number), and to exercise the sprite pool growing and shrinking repeatedly rather than
    // monotonically in one direction.
    constexpr int kStabilityFrames = 300;

    // plans/plan_html_dom.md HTMLDOM-110: a real XNA game resubmits its static sprites every frame --
    // this measures exactly that (byte-identical position/tint/texture, every frame, no per-frame
    // variation at all), the scenario the "zero cost" performance claim is actually about.
    constexpr int kStaticSpriteCount = 200;
    constexpr int kStaticMeasureFrames = 30;

#if defined(__EMSCRIPTEN__)
    EM_JS(double, JsNow, (), { return performance.now(); });

    EM_JS(int, JsPooledSpriteCount, (), {
        const root = document.getElementById('cna-dom-root');
        return root ? root.children.length : -1;
    });

    // Module['cnaDomVariantCache'] is only created lazily, on the first variant that needs one
    // (plain untinted/un-blended draws share the base canvas and never enrol) -- undefined until
    // then is the correct, expected state, not a bug, so this reports 0 for that case.
    EM_JS(int, JsVariantCacheSize, (), {
        const cache = Module['cnaDomVariantCache'];
        return cache ? cache.size : 0;
    });

    // plans/plan_html_dom.md HTMLDOM-109: whether the global variant cache currently holds a live record
    // for this exact (id, mode, r, g, b) key -- the same combined-key format cnaDomVariantCacheGet/
    // Put use, restated here rather than exposed as its own JS function, so this test observes the
    // cache the same way any other reader of Module['cnaDomVariantCache'] would.
    EM_JS(int, JsVariantCacheHasKey, (int id, int mode, int r, int g, int b), {
        const cache = Module['cnaDomVariantCache'];
        if (!cache) return 0;
        const combined = id + ':' + mode + ':' + r + ',' + g + ',' + b;
        return cache.has(combined) ? 1 : 0;
    });

    // plans/plan_html_dom.md HTMLDOM-110: CNAEXT instrumentation reads -- see
    // HtmlDomSpriteBatchRenderer.cpp's own cnaDomStyleWriteCount/cnaDomFlushCallCount comments for
    // what each counts. Reset variants zero the counter for a clean measurement window.
    EM_JS(int, JsStyleWriteCount, (), { return Module['cnaDomStyleWriteCount'] || 0; });
    EM_JS(void, JsResetStyleWriteCount, (), { Module['cnaDomStyleWriteCount'] = 0; });
    EM_JS(int, JsFlushCallCount, (), { return Module['cnaDomFlushCallCount'] || 0; });
    EM_JS(void, JsResetFlushCallCount, (), { Module['cnaDomFlushCallCount'] = 0; });

    EM_JS(void, JsPublishResult, (int result, int passed, int expected), {
        window.__cnaSmokeResult = result;
        window.__cnaSmokePassed = passed;
        window.__cnaSmokeExpected = expected;
        window.__cnaSmokeDone = true;
    });
#else
    double JsNow() { return 0.0; }
    int JsPooledSpriteCount() { return -1; }
    int JsVariantCacheSize() { return -1; }
    int JsVariantCacheHasKey(int, int, int, int, int) { return -1; }
    int JsStyleWriteCount() { return -1; }
    void JsResetStyleWriteCount() {}
    int JsFlushCallCount() { return -1; }
    void JsResetFlushCallCount() {}
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

    double staticTotalMs_ = 0.0;
    int staticFramesTimed_ = 0;

    // plans/plan_html_dom.md HTMLDOM-111: see benchmarkEndToEndTotalMs_'s own comment at its use site.
    double lastFrameStart_ = -1.0;
    double benchmarkEndToEndTotalMs_ = 0.0;
    int benchmarkEndToEndFramesTimed_ = 0;

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
        const double frameStart = JsNow();
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(10, 10, 20, 255));

        // plans/plan_html_dom.md HTMLDOM-89: frame 1 pays the one-time pool-creation cost (every sprite
        // element gets created and appended for the first time) -- deliberately excluded from the
        // measured average.
        //
        // plans/plan_html_dom.md HTMLDOM-111: this workload's own position formula ((i*37)%400, fixed per
        // sprite index `i`) never depends on `frame_` at all -- only the TINT (via `tintSeed`) does.
        // So despite this row's own older text, this measures HEAVY TINT CHURN with STATIC
        // position, not "moving sprites" -- the opposite of what "moving sprites costs nothing" is
        // actually about. `graphics_renderer_benchmark.cpp`'s own HTMLDOM-111 rework measures BOTH a
        // genuinely moving/stable-tint workload and this heavy-churn one side by side, which is the
        // right place to compare the two; this benchmark's job is narrower -- catch a gross
        // regression under sustained load, which the churn case (this renderer's own documented
        // worst case, not its best) is if anything the MORE conservative choice for that purpose.
        //
        // plans/plan_html_dom.md HTMLDOM-111: submission time (t0/t1 bracketing just Begin/Draw/End,
        // below) is NOT a frame rate -- it excludes Present() and every deferred browser layout/
        // paint/composite cost. benchmarkEndToEndTotalMs_ (the wall-clock gap between successive
        // Draw() calls) is the real one: Game::Run() drives Emscripten's main loop with fps=0
        // (Game.cpp), which per Emscripten's own contract means requestAnimationFrame-paced, so
        // Draw() already fires once per real browser frame and the start-to-start gap can only
        // elapse once the PREVIOUS frame's rendering actually finished.
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
            if (frame_ > 2)   // excludes the frame-1-to-2 gap, which includes frame 1's own warm-up.
            {
                benchmarkEndToEndTotalMs_ += (frameStart - lastFrameStart_);
                ++benchmarkEndToEndFramesTimed_;
            }
        }
        else if (frame_ == 2 + kBenchmarkFrames)
        {
            const double avgMs = benchmarkFramesTimed_ > 0 ? benchmarkTotalMs_ / benchmarkFramesTimed_ : -1.0;
            const double avgEndToEndMs = benchmarkEndToEndFramesTimed_ > 0
                ? benchmarkEndToEndTotalMs_ / benchmarkEndToEndFramesTimed_ : -1.0;
            std::printf("       HTMLDOM-89: %d sprites/frame (heavy tint churn, static position) "
                        "over %d frames -- submission %.3f ms/frame (NOT a frame rate); real "
                        "end-to-end %.3f ms/frame (%.1f real fps, includes browser layout/paint/"
                        "composite)\n",
                        kBenchmarkSpriteCount, benchmarkFramesTimed_, avgMs, avgEndToEndMs,
                        avgEndToEndMs > 0 ? 1000.0 / avgEndToEndMs : 0.0);
            std::fflush(stdout);
            // Deliberately generous: this runs in a headless, possibly-virtualized/shared-CPU
            // container that may be far slower than a real user's machine, and the point of this
            // threshold is to catch a genuine architectural regression (e.g. an accidental
            // per-sprite JS call reappearing, or O(n^2) DOM work), not to assert a specific
            // competitive frame budget. 50ms/frame for 500 sprites is roughly 20x looser than a
            // 60fps budget would demand. Checked against SUBMISSION time deliberately -- the
            // real end-to-end number is dominated by the browser's own vsync/compositor cadence
            // (headless Chromium still paces requestAnimationFrame near a real display's refresh
            // rate), which this renderer's own CPU work does not control and a regression in this
            // renderer's own code cannot fix or break on its own.
            check(avgMs >= 0.0 && avgMs < 50.0,
                  "HTMLDOM-89: submission CPU cost for 500 sprites/frame under heavy tint churn "
                  "stays well within a sane budget (no accidental O(n^2)/per-sprite-call "
                  "regression) -- see HTMLDOM-111 for the separately measured real end-to-end "
                  "frame cadence, which this check deliberately does not gate on");
        }
        lastFrameStart_ = frameStart;

        // plans/plan_html_dom.md HTMLDOM-90: sprite count oscillates between a low and high value every
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
            const int cacheSize = JsVariantCacheSize();
            std::printf("       HTMLDOM-90: after %d stability frames -- pooled elements=%d "
                        "(peak sprites/frame=%d), variant cache size=%d\n",
                        kStabilityFrames, pooled, peakSpritesInOneFrame_, cacheSize);
            std::fflush(stdout);
            // plans/plan_html_dom.md HTMLDOM-113: exact identity, not a `<=2*peak` headroom check -- the
            // pool never shrinks (elements are hidden via display:none, never removed), and exactly
            // one new element is ever created per newly-reached pool index (cnaDomGetRegion's own
            // `if (el === undefined) { ...create...; }` guard), so its final size must equal EXACTLY
            // the highest sprite count any single frame across the WHOLE run (benchmark phase
            // included) ever drew -- not merely "somewhere under a doubled bound", which would have
            // let a real leak (e.g. one stray extra element per stability-phase oscillation) pass
            // undetected as long as it stayed under 2x.
            check(pooled == peakSpritesInOneFrame_,
                  "HTMLDOM-90a: the sprite pool's final size is EXACTLY the peak per-frame sprite "
                  "count reached anywhere in the whole run, not merely bounded under a headroom "
                  "multiplier -- proves real recycling, not just an absence of unbounded growth");
            // The cache cap (256) must actually hold under real, sustained eviction pressure -- not
            // just "never filled up because the test never tried hard enough". A tight `== 256`
            // isn't asserted HERE (this run's own r-value coverage across two independently-varying
            // tint seeds isn't hand-derived) -- HTMLDOM-109's own deterministic frames below assert
            // exact capacity and real eviction identity instead, which this soak run complements
            // rather than duplicates.
            check(cacheSize >= 0 && cacheSize <= 256,
                  "HTMLDOM-90b: the variant cache never exceeds its 256-entry cap despite cycling "
                  "through far more than 256 distinct tint values over the run");
        }

        // plans/plan_html_dom.md HTMLDOM-109: real LRU hit-promotion, proven with a deterministic
        // eviction-identity scenario rather than the soak run's own loose "<=256" bound above. Fills
        // the cache to exactly its 256-entry cap with distinct (mode=1 i.e. AlphaBlend, r=0..255,
        // g=200, b=150) keys -- g/b fixed means r alone spans the entire possible key space for this
        // texture/mode, so this is deterministic, not probabilistic. Touches key r=0 again (a cache
        // HIT, promoting it to most-recently-used) before inserting ONE more, never-before-seen key
        // (g=201, distinct from every key inserted so far) -- a real LRU must therefore evict r=1
        // (the next-oldest UNTOUCHED key), not r=0 (touched) and not the brand-new insertion.
        if (frame_ == 4 + kBenchmarkFrames + kStabilityFrames)
        {
            const auto& renderer =
                static_cast<CNA::Internal::Renderers::HtmlDom::HtmlDomTextureRenderer&>(texture_->GetRenderer());
            const int id = renderer.GetCanvasId();

            spriteBatch_->Begin();
            for (int r = 0; r < 256; ++r)
            {
                spriteBatch_->Draw(*texture_, Vector2(0, 0),
                                   Color(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(200),
                                         static_cast<std::uint8_t>(150), static_cast<std::uint8_t>(255)));
            }
            spriteBatch_->End();
            const int sizeAfterFill = JsVariantCacheSize();

            // Touch r=0 again -- a pure cache hit, no new key -- then insert one brand-new key.
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color(0, 200, 150, 255));
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color(0, 201, 150, 255));
            spriteBatch_->End();
            const int sizeAfterOneMore = JsVariantCacheSize();

            const bool touchedSurvived = JsVariantCacheHasKey(id, 1, 0, 200, 150) == 1;
            const bool untouchedEvicted = JsVariantCacheHasKey(id, 1, 1, 200, 150) == 0;
            const bool newKeyPresent = JsVariantCacheHasKey(id, 1, 0, 201, 150) == 1;
            std::printf("       HTMLDOM-109 hot-entry: sizeAfterFill=%d sizeAfterOneMore=%d "
                        "r=0/g=200 survived=%d r=1/g=200 evicted=%d new-key present=%d\n",
                        sizeAfterFill, sizeAfterOneMore, touchedSurvived, untouchedEvicted, newKeyPresent);
            std::fflush(stdout);
            check(sizeAfterFill == 256 && sizeAfterOneMore == 256,
                  "HTMLDOM-109: the variant cache reaches and then stays pinned at exactly its "
                  "256-entry cap, never growing past it on a further insertion");
            check(touchedSurvived && untouchedEvicted,
                  "HTMLDOM-109: a cache HIT promotes that entry to most-recently-used, so it "
                  "survives an eviction that instead claims the next-oldest UNTOUCHED entry -- real "
                  "LRU recency, not FIFO-by-creation-order");
            check(newKeyPresent,
                  "HTMLDOM-109: the newly inserted key that triggered the eviction is itself present "
                  "in the cache afterwards");
        }

        // plans/plan_html_dom.md HTMLDOM-109: SetData/UpdatePixels must drop this texture's own live cache
        // records, not merely reset the (now-removed) per-entry lookup map and leave the global
        // records to rot as stale (id,key) pairs that could later delete a freshly regenerated
        // variant sharing the same key.
        if (frame_ == 5 + kBenchmarkFrames + kStabilityFrames)
        {
            const auto& renderer =
                static_cast<CNA::Internal::Renderers::HtmlDom::HtmlDomTextureRenderer&>(texture_->GetRenderer());
            const int id = renderer.GetCanvasId();

            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color(77, 202, 151, 255));
            spriteBatch_->End();
            const bool presentBeforeUpdate = JsVariantCacheHasKey(id, 1, 77, 202, 151) == 1;

            std::vector<std::uint8_t> newPixels{
                10, 20, 30, 255,   10, 20, 30, 255,
                10, 20, 30, 255,   10, 20, 30, 255,
            };
            texture_->SetDataRGBA(newPixels.data(), 4);   // texture_ is 2x2 -- 4 pixels.
            const bool goneAfterUpdate = JsVariantCacheHasKey(id, 1, 77, 202, 151) == 0;

            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color(77, 202, 151, 255));
            spriteBatch_->End();
            const bool presentAfterRedraw = JsVariantCacheHasKey(id, 1, 77, 202, 151) == 1;

            std::printf("       HTMLDOM-109 SetData regen: presentBeforeUpdate=%d goneAfterUpdate=%d "
                        "presentAfterRedraw=%d\n",
                        presentBeforeUpdate, goneAfterUpdate, presentAfterRedraw);
            std::fflush(stdout);
            check(presentBeforeUpdate && goneAfterUpdate,
                  "HTMLDOM-109: SetData drops this texture's own cached variant for a key that was "
                  "live a moment before, rather than leaving a stale record pointing at pixels that "
                  "no longer exist");
            check(presentAfterRedraw,
                  "HTMLDOM-109: redrawing the same (mode, tint) key after SetData regenerates a "
                  "fresh cache entry from the NEW pixels -- the key is reusable, not permanently "
                  "poisoned by the earlier stale-entry bug");
        }

        // plans/plan_html_dom.md HTMLDOM-110: warm-up for the static-resubmit measurement below -- pays
        // the one-time pool-creation cost (every sprite element created and appended for the first
        // time), excluded from what gets measured, the same "frame 1 is excluded" shape HTMLDOM-89's
        // own benchmark above already uses. Resets both instrumentation counters right after, so the
        // measurement window below starts clean.
        if (frame_ == 6 + kBenchmarkFrames + kStabilityFrames)
        {
            DrawSprites(kStaticSpriteCount, 0.0f);
            JsResetStyleWriteCount();
            JsResetFlushCallCount();
        }

        // plans/plan_html_dom.md HTMLDOM-110: the actual claim under test -- byte-identical content
        // (SAME tintSeed=0.0f every single frame, unlike DrawSprites' other callers above, which
        // deliberately vary it) resubmitted kStaticMeasureFrames times in a row. A real XNA game
        // does exactly this for anything that is not currently animating.
        if (frame_ > 6 + kBenchmarkFrames + kStabilityFrames &&
            frame_ <= 6 + kBenchmarkFrames + kStabilityFrames + kStaticMeasureFrames)
        {
            const double t0 = JsNow();
            DrawSprites(kStaticSpriteCount, 0.0f);
            const double t1 = JsNow();
            staticTotalMs_ += (t1 - t0);
            ++staticFramesTimed_;
        }

        if (frame_ == 7 + kBenchmarkFrames + kStabilityFrames + kStaticMeasureFrames)
        {
            const int styleWrites = JsStyleWriteCount();
            const int flushCalls = JsFlushCallCount();
            const double avgMs = staticFramesTimed_ > 0 ? staticTotalMs_ / staticFramesTimed_ : -1.0;
            std::printf("       HTMLDOM-110: %d static (byte-identical) sprites/frame over %d "
                        "frames -- %d flush calls, %d CSS property writes, %.3f ms/frame average "
                        "(%.1f fps-equivalent)\n",
                        kStaticSpriteCount, staticFramesTimed_, flushCalls, styleWrites, avgMs,
                        avgMs > 0 ? 1000.0 / avgMs : 0.0);
            std::fflush(stdout);
            // "No JS runs" is false, measurably: one real CNA_HtmlDom_FlushSprites call happens
            // for every one of the kStaticMeasureFrames frames, no fewer -- a real XNA game
            // resubmits static sprites every frame, and this renderer's own End() has no way to
            // know in advance that a batch will turn out identical to the last one without first
            // walking it, which is exactly what this call does.
            check(flushCalls == kStaticMeasureFrames,
                  "HTMLDOM-110: a real JS flush call happens on EVERY resubmitted frame, even a "
                  "byte-identical one -- 'no JS runs' is measurably false for a static scene");
            // The claim that IS true, and now proven by direct measurement rather than asserted:
            // byte-identical resubmitted content produces ZERO CSS property writes across the
            // whole measurement window -- no layout, no paint, no composite-order work either,
            // since none of those can happen without a write to trigger them. Before this task,
            // 'full'-region sprites got an UNCONDITIONAL style.zIndex write every single flush
            // (HTMLDOM-103's own per-flush paint-order counter, which never repeats a value) --
            // this specific, previously-undocumented cost is what made the old "no JS runs" claim
            // wrong even for the narrower "no style writes" reading, not just pedantically wrong
            // about the JS call itself.
            check(styleWrites == 0,
                  "HTMLDOM-110: byte-identical resubmitted content produces genuinely ZERO CSS "
                  "property writes -- the corrected, real 'nothing changes -> nothing costs "
                  "anything' guarantee, now measured rather than merely claimed");

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
