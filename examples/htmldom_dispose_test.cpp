// SPDX-License-Identifier: MS-PL
//
// plan_html_dom.md HTMLDOM-95: verifies that HtmlDomTextureBackend/HtmlDomRenderTargetBackend
// dispose/cleanup is REAL, not just "looks right on paper". Code review alone showed
// ~HtmlDomTextureBackend() calling CNA_HtmlDom_DestroyTexture (which deletes
// Module['cnaDomTextures'][id]) and ~HtmlDomRenderTargetBackend() unbinding itself if it was the
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

#include "CNA/Internal/Backends/HtmlDom/HtmlDomState.hpp"

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
    constexpr int kExpectedChecks = 6;
    constexpr int kBatchSize = 50;
    constexpr int kRenderTargetCount = 20;
    constexpr int kChurnIterations = 200;

#if defined(__EMSCRIPTEN__)
    /// Number of textures currently registered in Module['cnaDomTextures'] -- every live
    /// Texture2D/RenderTarget2D owns exactly one entry, so this is a direct proxy for "how many
    /// off-screen canvases does the backend currently think it owns".
    EM_JS(int, JsTextureRegistryCount, (), {
        return Module['cnaDomTextures'] ? Object.keys(Module['cnaDomTextures']).length : 0;
    });

    EM_JS(void, JsPublishResult, (int result, int passed, int expected), {
        window.__cnaSmokeResult = result;
        window.__cnaSmokePassed = passed;
        window.__cnaSmokeExpected = expected;
        window.__cnaSmokeDone = true;
    });
#else
    int JsTextureRegistryCount() { return -1; }
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

            batch.clear();   // destroys all 50 Texture2Ds (and their backends) right here.
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

            check(CNA::Internal::Backends::HtmlDom::GetBoundRenderTargetIdEXT() == 0,
                  "HTMLDOM-95c: destroying the currently-bound render target resets the backend's "
                  "bound-target id back to the DOM backbuffer (0), rather than leaving it pointing "
                  "at a canvas that no longer exists");

            // GraphicsDevice's OWN renderTargetBound_ flag is a separate piece of bookkeeping from
            // the backend's bound-canvas id just checked above -- it has no way to know the target
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
            // plan_html_dom.md HTMLDOM-95d: rapid create-then-immediately-destroy churn, the
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
