// SPDX-License-Identifier: MS-PL
//
// plans/plan_html_dom.md HTMLDOM-95: verifies that HtmlDomTextureRenderer/HtmlDomRenderTargetRenderer
// dispose/cleanup is REAL, not just "looks right on paper". Code review alone showed
// ~HtmlDomTextureRenderer() calling CNA_HtmlDom_DestroyTexture (which deletes
// Module['cnaDomTextures'][id]) and ~HtmlDomRenderTargetRenderer() unbinding itself if it was the
// currently-bound target -- this test creates and destroys many textures and render targets in a
// real browser and checks the JS-side registry actually shrinks back down, rather than trusting
// that the C++ destructor chain runs the way the source suggests it does.
//
// Driven by the same scripts/run-htmldom-browser-test.sh / htmldom-browser-test.mjs harness as the
// other HTML_DOM test pages: pass this page's path as the argument.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/HtmlDom/HtmlDomRenderer.hpp"
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomState.hpp"

#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::HtmlDom;

namespace
{
    constexpr int kExpectedChecks = 17;
    constexpr int kBatchSize = 50;
    constexpr int kRenderTargetCount = 20;
    constexpr int kChurnIterations = 200;

#if defined(__EMSCRIPTEN__)
    /// Number of textures currently registered in Module['cnaDomTextures'] -- every live
    /// Texture2D/RenderTarget2D owns exactly one entry, so this is a direct proxy for "how many
    /// off-screen canvases does the renderer currently think it owns".
    EM_JS(int, JsTextureRegistryCount, (), {
        return Module['cnaDomTextures'] ? Object.keys(Module['cnaDomTextures']).length : 0;
    });

    // plans/plan_html_dom.md HTMLDOM-109: total live entries in the global variant cache, across every
    // texture/render target -- the same size a texture/render-target destruction or a render-target
    // rebind must shrink by exactly its own contribution, not leave behind as orphaned records.
    EM_JS(int, JsVariantCacheSize, (), {
        const cache = Module['cnaDomVariantCache'];
        return cache ? cache.size : 0;
    });

    // plans/plan_html_dom.md HTMLDOM-114: whether the shared DOM surface (#cna-dom-root and everything
    // CNA_HtmlDom_EnsureRoot owns) currently exists at all.
    EM_JS(int, JsSurfaceExists, (), { return Module['cnaDomRoot'] ? 1 : 0; });

    /// plans/plan_html_dom.md HTMLDOM-114: how many live HtmlDomRenderer instances currently
    /// reference the shared DOM surface -- see CNA_HtmlDom_EnsureRoot/DestroyRoot's own comments.
    EM_JS(int, JsRendererRefCount, (), { return Module['cnaDomRendererRefCount'] || 0; });

    EM_JS(void, JsPublishResult, (int result, int passed, int expected), {
        window.__cnaSmokeResult = result;
        window.__cnaSmokePassed = passed;
        window.__cnaSmokeExpected = expected;
        window.__cnaSmokeDone = true;
    });
#else
    int JsTextureRegistryCount() { return -1; }
    int JsVariantCacheSize() { return -1; }
    int JsSurfaceExists() { return 0; }
    int JsRendererRefCount() { return 0; }
    void JsPublishResult(int, int, int) {}
#endif
}

class HtmlDomDisposeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;
    int baselineCount_ = -1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        if (ok) ++passCount_;
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

        if (frame_ == 1)
        {
            // Established once, after LoadContent's own texture_ already registered -- everything
            // below is checked as a delta against this, not an assumed absolute value, since the
            // exact baseline (e.g. whether a 1x1 placeholder texture exists elsewhere) isn't this
            // test's concern.
            baselineCount_ = JsTextureRegistryCount();

            std::vector<std::unique_ptr<Texture2D>> batch;
            batch.reserve(kBatchSize);
            for (int i = 0; i < kBatchSize; ++i)
            {
                batch.push_back(std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
                    dev, 2, 2, std::vector<std::uint8_t>{
                        255, 0, 0, 255,   0, 255, 0, 255,
                        0, 0, 255, 255,   255, 255, 0, 255,
                    })));
            }
            const int afterCreate = JsTextureRegistryCount();
            check(afterCreate == baselineCount_ + kBatchSize,
                  "HTMLDOM-95a: creating 50 textures registers exactly 50 new entries");

            batch.clear();   // destroys all 50 Texture2Ds (and their renderers) right here.
            const int afterDestroy = JsTextureRegistryCount();
            check(afterDestroy == baselineCount_,
                  "HTMLDOM-95a: destroying all 50 textures removes exactly those 50 entries, "
                  "not leaving any of them orphaned in the JS-side registry");
        }

        if (frame_ == 2)
        {
            const int before = JsTextureRegistryCount();

            std::vector<std::unique_ptr<RenderTarget2D>> targets;
            targets.reserve(kRenderTargetCount);
            for (int i = 0; i < kRenderTargetCount; ++i)
                targets.push_back(std::make_unique<RenderTarget2D>(dev, 8, 8));

            const int afterCreate = JsTextureRegistryCount();
            check(afterCreate == before + kRenderTargetCount,
                  "HTMLDOM-95b: creating 20 render targets registers exactly 20 new entries "
                  "(each owns one texture canvas)");

            // Bind the LAST one and draw into it, then destroy every target -- including the one
            // still bound -- WITHOUT ever explicitly unbinding it first. A destroyed target must
            // never leave the draw path pointing at a canvas that no longer exists.
            dev.SetRenderTarget(targets.back().get());
            dev.Clear(Color(200, 50, 25, 255));
            targets.clear();

            check(CNA::Internal::Renderers::HtmlDom::GetBoundRenderTargetIdEXT() == 0,
                  "HTMLDOM-95c: destroying the currently-bound render target resets the renderer's "
                  "bound-target id back to the DOM backbuffer (0), rather than leaving it pointing "
                  "at a canvas that no longer exists");

            // GraphicsDevice's OWN renderTargetBound_ flag is a separate piece of bookkeeping from
            // the renderer's bound-canvas id just checked above -- it has no way to know the target
            // object was destroyed out from under it, and Present() (called automatically by
            // Game::EndDraw() right after this Draw() returns) throws while it is still set. Real
            // XNA/FNA usage always calls SetRenderTarget(null) before disposing a bound target; this
            // mirrors that, deliberately AFTER the check above so that check still exercises the
            // destructor's own defensive unbind in isolation, not this call's.
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            // Drawing to the ordinary backbuffer afterwards must work exactly as normal -- the real,
            // observable consequence of HTMLDOM-95c actually holding: if the bound id had been left
            // dangling, this Clear()+Draw() would either silently do nothing (writing into a canvas
            // nothing reads back) or throw.
            dev.Clear(Color::CornflowerBlue);
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color::White);
            spriteBatch_->End();

            const int afterDestroy = JsTextureRegistryCount();
            check(afterDestroy == before,
                  "HTMLDOM-95b: destroying all 20 render targets removes exactly those 20 "
                  "entries, including the one that was still bound");
        }

        if (frame_ == 3)
        {
            // plans/plan_html_dom.md HTMLDOM-95d: rapid create-then-immediately-destroy churn, the
            // pattern most likely to expose an off-by-one in registry bookkeeping or an
            // accumulating leak that a single batch (frame 1's own check) is too coarse to catch.
            // Texture ids are assigned monotonically and never reused (NextTextureId()), so this
            // also confirms the registry's SIZE stays bounded to the currently-alive set across
            // many distinct ids cycling through it, not merely that any one id was cleaned up.
            const int before = JsTextureRegistryCount();
            for (int i = 0; i < kChurnIterations; ++i)
            {
                Texture2D t = Texture2D::CreateFromPixels(
                    dev, 2, 2, std::vector<std::uint8_t>{
                        1, 2, 3, 4,   5, 6, 7, 8,
                        9, 10, 11, 12,   13, 14, 15, 16,
                    });
                (void)t;   // destroyed at the end of this same iteration.
            }
            const int after = JsTextureRegistryCount();
            check(after == before,
                  "HTMLDOM-95d: 200 rapid create/destroy cycles with distinct, never-reused "
                  "texture ids leave the registry exactly where it started, not accumulating");
        }

        // plans/plan_html_dom.md HTMLDOM-109: destroying a texture must remove exactly its own live
        // records from the global variant cache -- previously the cache's own LRU array kept a
        // now-meaningless (id,key) pair around until it happened to reach the front of the eviction
        // queue, at which point it could wrongly delete a DIFFERENT, still-live texture's variant
        // that had since reused the same numeric id's map slot... except ids are never reused here,
        // so the practical failure mode was a leaked slot: the cache stayed "full" of phantom
        // entries pointing at nothing, permanently shrinking its real capacity for live textures.
        if (frame_ == 4)
        {
            const int cacheBefore = JsVariantCacheSize();
            {
                Texture2D t = Texture2D::CreateFromPixels(
                    dev, 2, 2, std::vector<std::uint8_t>{
                        255, 255, 255, 255,   255, 255, 255, 255,
                        255, 255, 255, 255,   255, 255, 255, 255,
                    });
                spriteBatch_->Begin();
                for (int i = 0; i < 10; ++i)
                {
                    spriteBatch_->Draw(t, Vector2(0, 0),
                                       Color(static_cast<std::uint8_t>(i * 20),
                                             static_cast<std::uint8_t>(200),
                                             static_cast<std::uint8_t>(150), static_cast<std::uint8_t>(255)));
                }
                spriteBatch_->End();
                const int cacheWithLiveTexture = JsVariantCacheSize();
                check(cacheWithLiveTexture == cacheBefore + 10,
                      "HTMLDOM-109: drawing 10 distinct tints registers exactly 10 new "
                      "variant-cache entries");
            }   // t destroyed here.
            const int cacheAfterDestroy = JsVariantCacheSize();
            std::printf("       HTMLDOM-109 destroy cleanup: cacheBefore=%d cacheAfterDestroy=%d\n",
                        cacheBefore, cacheAfterDestroy);
            std::fflush(stdout);
            check(cacheAfterDestroy == cacheBefore,
                  "HTMLDOM-109: destroying a texture removes exactly its own variant-cache "
                  "records, not leaving them behind as orphaned entries the cache never reclaims");
        }

        // plans/plan_html_dom.md HTMLDOM-109: rebinding a render target as a render target -- the
        // invalidation path that used to just reset `entry.variants = {}` -- must drop exactly that
        // target's own cache records too, without disturbing any other live texture's entries.
        if (frame_ == 5)
        {
            auto rt = std::make_unique<RenderTarget2D>(dev, 4, 4);
            dev.SetRenderTarget(rt.get());
            dev.Clear(Color(100, 50, 25, 255));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const int cacheBeforeSample = JsVariantCacheSize();
            spriteBatch_->Begin();
            for (int i = 0; i < 5; ++i)
            {
                spriteBatch_->Draw(*rt, Vector2(0, 0),
                                   Color(static_cast<std::uint8_t>(i * 30 + 1),
                                         static_cast<std::uint8_t>(210),
                                         static_cast<std::uint8_t>(140), static_cast<std::uint8_t>(255)));
            }
            spriteBatch_->End();
            const int cacheAfterSample = JsVariantCacheSize();
            check(cacheAfterSample == cacheBeforeSample + 5,
                  "HTMLDOM-109: sampling a render target as a Draw() source under 5 distinct tints "
                  "registers exactly 5 new variant-cache entries");

            dev.SetRenderTarget(rt.get());
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const int cacheAfterRebind = JsVariantCacheSize();
            std::printf("       HTMLDOM-109 rebind cleanup: cacheBeforeSample=%d cacheAfterSample=%d "
                        "cacheAfterRebind=%d\n",
                        cacheBeforeSample, cacheAfterSample, cacheAfterRebind);
            std::fflush(stdout);
            check(cacheAfterRebind == cacheBeforeSample,
                  "HTMLDOM-109: rebinding a render target drops exactly its own variant-cache "
                  "records, matching the invalidation it replaces, not leaving them as stale "
                  "(id,key) pairs the cache never reclaims");
        }

        // plans/plan_html_dom.md HTMLDOM-114: a SECOND HtmlDomRenderer, constructed while the
        // FIRST (this test's own, real) one is still alive and sharing the SAME browser DOM
        // surface, must not silently ADOPT that surface and then rip it out from under the first
        // renderer
        // when the second one alone is destroyed. This is a real, confirmed defect the reference-
        // counted CNA_HtmlDom_EnsureRoot/DestroyRoot fix closes: constructing a second renderer was
        // ALREADY a no-op for the JS surface itself (guarded as "already initialized"), but
        // destroying that second renderer unconditionally tore the WHOLE shared surface down --
        // breaking the first, still-alive renderer too, purely because ANOTHER renderer happened to
        // exist and get destroyed.
        if (frame_ == 6)
        {
            check(JsSurfaceExists() == 1 && JsRendererRefCount() == 1,
                  "HTMLDOM-114: before constructing a second renderer, the shared surface exists "
                  "with exactly one live reference -- this test's own real renderer");
            {
                CNA::Internal::Renderers::GraphicsRendererCreateArgs altArgs;
                // HtmlDomRenderer shares one JS surface per browser page. Give the low-level test
                // instance a distinct registry key so it cannot replace the real GraphicsDevice
                // renderer while both intentionally share that page surface.
                altArgs.surface.windowId =
                    std::numeric_limits<CNA::Platform::WindowId>::max();
                altArgs.surface.nativeHandle = getWindowProperty().GetNativeWindowHandleEXT();
                altArgs.surface.drawableSize = {64, 64};
                altArgs.virtualWidth = 64;
                altArgs.virtualHeight = 64;
                altArgs.presentationMode =
                    CNA::Internal::Renderers::CnaPresentationMode::FixedHeightDynamicWidth;
                HtmlDomRenderer altRenderer(altArgs);
                check(JsRendererRefCount() == 2,
                      "HTMLDOM-114: constructing a second renderer sharing the same DOM surface "
                      "increments the shared surface's reference count to 2, rather than either "
                      "silently creating an independent surface or leaving the count unaware of it");
                check(JsSurfaceExists() == 1,
                      "HTMLDOM-114: the shared surface still exists while both renderers are alive");
            }   // altRenderer destroyed here.
            check(JsRendererRefCount() == 1,
                  "HTMLDOM-114: destroying the second renderer decrements the reference count back "
                  "to 1, rather than tearing the surface down out from under the first");
            check(JsSurfaceExists() == 1,
                  "HTMLDOM-114: the shared surface genuinely SURVIVES the second renderer's own "
                  "destruction -- the real defect this task fixed: an earlier version tore the "
                  "surface down unconditionally on ANY renderer's destruction, breaking the "
                  "first, still-alive renderer too");

            // The real, observable consequence: THIS test's own real renderer must still be
            // GENUINELY functional after the second renderer's full construct-then-destroy cycle --
            // not just "the root element object still exists".
            const int beforeFresh = JsTextureRegistryCount();
            {
                Texture2D freshTex = Texture2D::CreateFromPixels(
                    dev, 1, 1, std::vector<std::uint8_t>{1, 2, 3, 4});
                check(JsTextureRegistryCount() == beforeFresh + 1,
                      "HTMLDOM-114: creating a texture through the real renderer still works "
                      "normally after the second renderer's construct-destroy cycle");
            }
            check(JsTextureRegistryCount() == beforeFresh,
                  "HTMLDOM-114: ...and destroying that texture still works normally too");

            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            std::fflush(stdout);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            JsPublishResult(result_, passCount_, kExpectedChecks);
            Exit();
        }
    }

public:
    HtmlDomDisposeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    HtmlDomDisposeTest game;
    game.Run();
    return game.getResult();
}
