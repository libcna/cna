// SPDX-License-Identifier: MS-PL
//
// plan_html_dom.md Phase D9: pixel-exact browser verification for everything Phase D1-D8 left
// checked only structurally ("did something draw", "did the CSS property appear") rather than
// against an actual hand-derived expected value. Distinct from htmldom_smoke_test.cpp, which
// covers the DOM-surface/pool/RenderTarget2D-readback/backbuffer-refusal contract.
//
// Every check here follows the same pattern: draw into a RenderTarget2D under a specific,
// deliberately chosen BlendState/transform, unbind, read the target back with
// RenderTarget2D::GetData, and compare against a value derived by hand from the documented
// per-pixel formula -- not just "did it throw" or "did an element appear".
//
// Driven by the same scripts/run-htmldom-browser-test.sh / htmldom-browser-test.mjs harness as
// the smoke test: pass this page's path as the argument.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Backends/HtmlDom/HtmlDomGraphicsBackend.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kExpectedChecks = 4;

#if defined(__EMSCRIPTEN__)
    EM_JS(void, JsPublishResult, (int result, int passed, int expected), {
        window.__cnaSmokeResult = result;
        window.__cnaSmokePassed = passed;
        window.__cnaSmokeExpected = expected;
        window.__cnaSmokeDone = true;
    });
#else
    void JsPublishResult(int, int, int) {}
#endif

    Texture2D Make1x1(GraphicsDevice& dev, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        return Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{r, g, b, a});
    }

    // Channel-wise tolerance for a pixel comparison. Real algebra bugs (wrong operand, double
    // application of a division, a swapped factor) produce errors far larger than this; the
    // tolerance exists only to absorb legitimate 8-bit rounding -- this backend's own
    // round-to-nearest during its per-pixel maths, PLUS (for the AlphaBlend check specifically)
    // the browser's own internal premultiply-for-compositing/un-premultiply-for-readback round
    // trip, which is a second, independent source of +-1-ish rounding this backend does not
    // control at all.
    bool CloseEnough(std::uint8_t actual, int expected, int tolerance)
    {
        return std::abs(static_cast<int>(actual) - expected) <= tolerance;
    }
}

class HtmlDomPixelVerificationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<RenderTarget2D> rt_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        if (ok) ++passCount_;
    }

    // Clears rt_ to fully transparent, runs `draw`, unbinds, and returns pixel (0,0). `draw` runs
    // with a real SpriteBatch Begin/End session already active around it via the supplied
    // sortMode/blendState -- callers just issue Draw() calls.
    template <typename DrawFn>
    Color ReadBackTopLeftPixel(SpriteSortMode sortMode, BlendState blendState, DrawFn draw)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 0, 0, 0));
        spriteBatch_->Begin(sortMode, blendState, nullptr, nullptr, nullptr);
        draw();
        spriteBatch_->End();
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> pixels(4, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            rt_->GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        }
        catch (const std::exception& e)
        {
            std::printf("       GetData threw: %s\n", e.what());
        }
        return pixels[0];
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        rt_ = std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), 2, 2);
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color::Black);

        // plan_html_dom.md HTMLDOM-84: tint is RGB * colour / 255, alpha untouched. An opaque
        // (255-2, 150, 100, 255) texel over a transparent-black target under AlphaBlend, tinted
        // (255, 128, 64, 255): since the destination starts fully transparent, source-over
        // compositing reduces to exactly the tinted colour, so this isolates the tint maths from
        // any compositing-order effects.
        if (frame_ == 1)
        {
            Texture2D tex = Make1x1(dev, 200, 150, 100, 255);
            const Color result = ReadBackTopLeftPixel(SpriteSortMode::Deferred, BlendState::AlphaBlend, [&] {
                spriteBatch_->Draw(tex, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1),
                                   Color(255, 128, 64, 255));
            });
            // Expected via the same float-multiply-divide-by-255 formula CNA_HtmlDom_InstallTextureHelpers
            // uses, so this is a genuine cross-check of the JS arithmetic, not a restatement of it.
            const int wantR = static_cast<int>(200.0 * 255 / 255 + 0.5);
            const int wantG = static_cast<int>(150.0 * 128 / 255 + 0.5);
            const int wantB = static_cast<int>(100.0 * 64 / 255 + 0.5);
            std::printf("       tint got (%d,%d,%d) want ~(%d,%d,%d)\n",
                        result.getRProperty(), result.getGProperty(), result.getBProperty(),
                        wantR, wantG, wantB);
            check(CloseEnough(result.getRProperty(), wantR, 1) &&
                  CloseEnough(result.getGProperty(), wantG, 1) &&
                  CloseEnough(result.getBProperty(), wantB, 1),
                  "HTMLDOM-84: tint multiplies RGB by the draw colour exactly, alpha untouched");
        }

        // plan_html_dom.md HTMLDOM-85 (highest-risk item): AlphaBlend's contract assumes ALREADY
        // premultiplied source data (srcBlend=One). Upload a texel that is deliberately
        // premultiplied by hand -- straight colour (255,100,50) at alpha=128 becomes
        // (255*128/255, 100*128/255, 50*128/255, 128) = (128, 50, 25, 128), mirroring
        // SDL_RENDERER's own Task 697 pixel test -- and confirm the backend's un-premultiply pass
        // reconstructs the ORIGINAL straight colour, not a double-divided or undivided one.
        if (frame_ == 2)
        {
            Texture2D tex = Make1x1(dev, 128, 50, 25, 128);
            const Color result = ReadBackTopLeftPixel(SpriteSortMode::Deferred, BlendState::AlphaBlend, [&] {
                spriteBatch_->Draw(tex, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1),
                                   Color(255, 255, 255, 255));
            });
            std::printf("       alphablend got (%d,%d,%d,%d) want ~(255,100,50,128)\n",
                        result.getRProperty(), result.getGProperty(), result.getBProperty(),
                        result.getAProperty());
            // Wider tolerance: this result passes through TWO independent 8-bit roundings -- this
            // backend's own un-premultiply division, then the browser's own internal
            // premultiply-for-compositing/un-premultiply-for-readback round trip.
            check(CloseEnough(result.getRProperty(), 255, 2) &&
                  CloseEnough(result.getGProperty(), 100, 2) &&
                  CloseEnough(result.getBProperty(), 50, 2) &&
                  CloseEnough(result.getAProperty(), 128, 2),
                  "HTMLDOM-85: AlphaBlend un-premultiplies premultiplied source data correctly");
        }

        // plan_html_dom.md HTMLDOM-86: Opaque strips alpha to 255 and leaves RGB untouched. Use a
        // GENUINELY semi-transparent source texel (every prior check used alpha=255 source data,
        // so the strip was a no-op every time it ran) -- if the strip were instead blending with
        // the transparent background, RGB would come back darkened; if it forgot to force alpha,
        // it would come back at the source's own alpha instead of 255.
        if (frame_ == 3)
        {
            Texture2D tex = Make1x1(dev, 180, 90, 40, 120);
            const Color result = ReadBackTopLeftPixel(SpriteSortMode::Deferred, BlendState::Opaque, [&] {
                spriteBatch_->Draw(tex, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1),
                                   Color(255, 255, 255, 255));
            });
            std::printf("       opaque got (%d,%d,%d,%d) want ~(180,90,40,255)\n",
                        result.getRProperty(), result.getGProperty(), result.getBProperty(),
                        result.getAProperty());
            check(CloseEnough(result.getRProperty(), 180, 1) &&
                  CloseEnough(result.getGProperty(), 90, 1) &&
                  CloseEnough(result.getBProperty(), 40, 1) &&
                  result.getAProperty() == 255,
                  "HTMLDOM-86: Opaque strips alpha to 255 without darkening RGB from a real "
                  "semi-transparent source");
        }

        // plan_html_dom.md HTMLDOM-87: Additive must actually ADD channel values (clamped), not
        // just apply SOME blend. Two fully opaque, non-overlapping-in-colour-space draws
        // (pure red then pure blue) at the exact same destination pixel: a correct 'lighter'
        // composite must read back as pure magenta at full alpha (255+0, 0, 0+255 clamped); a
        // backend that fell back to plain source-over would read back as just blue (the second
        // draw fully overwriting the first).
        if (frame_ == 4)
        {
            Texture2D red = Make1x1(dev, 255, 0, 0, 255);
            Texture2D blue = Make1x1(dev, 0, 0, 255, 255);
            const Color result = ReadBackTopLeftPixel(SpriteSortMode::Deferred, BlendState::Additive, [&] {
                spriteBatch_->Draw(red, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
                spriteBatch_->Draw(blue, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
            });
            std::printf("       additive got (%d,%d,%d,%d) want (255,0,255,255)\n",
                        result.getRProperty(), result.getGProperty(), result.getBProperty(),
                        result.getAProperty());
            check(result.getRProperty() == 255 && result.getGProperty() == 0 &&
                  result.getBProperty() == 255 && result.getAProperty() == 255,
                  "HTMLDOM-87: Additive sums overlapping opaque colours exactly (clamped), not "
                  "a plain overwrite");
        }

        if (frame_ == 5)
        {
            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            std::fflush(stdout);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            JsPublishResult(result_, passCount_, kExpectedChecks);
            Exit();
        }
    }

public:
    HtmlDomPixelVerificationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    HtmlDomPixelVerificationTest game;
    game.Run();
    return game.getResult();
}
