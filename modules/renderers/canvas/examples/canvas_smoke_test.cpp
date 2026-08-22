// plans/plan_canvas.md CANVAS-15: structural smoke test for the CANVAS (HTML Canvas 2D) graphics
// renderer. Constructs a real Game (GraphicsDeviceManager, Clear(), a Texture2D + SpriteBatch
// draws), verifies exact scaled-destination pixels, and checks the AlphaBlend cache/bulk replay
// counters across a couple of frames.
//
// Design decision 9 / this file's own empirical finding: this renderer is Emscripten-only, and
// SDL_Init(SDL_INIT_VIDEO) itself throws under this repo's `node CnaTests.js` runner (no real
// browser DOM -- "ReferenceError: window is not defined" comes from SDL3's own Emscripten video
// driver, before any Canvas-specific code ever runs). So this executable is deliberately NOT
// registered as a ctest (see cna_diag_software's own precedent in CMakeLists.txt) -- it only
// produces a real, meaningful PASS/FAIL when actually run in a browser (e.g. via `emrun`) against
// a page that has a real <canvas> element. CI only proves it configures and links.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/Canvas/CanvasRenderer.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Canvas;

namespace
{
    constexpr int kExpectedChecks = 8;

#if defined(__EMSCRIPTEN__)
    EM_JS(void, JsResetCanvasDiagnostics, (), {
        Module['cnaCanvasUnpremultiplyBuildCount'] = 0;
        Module['cnaCanvasBulkFlushCount'] = 0;
        Module['cnaCanvasBulkSpriteCount'] = 0;
    });

    EM_JS(int, JsUnpremultiplyBuildCount, (), {
        return Module['cnaCanvasUnpremultiplyBuildCount'] || 0;
    });

    EM_JS(int, JsBulkFlushCount, (), {
        return Module['cnaCanvasBulkFlushCount'] || 0;
    });

    EM_JS(int, JsBulkSpriteCount, (), {
        return Module['cnaCanvasBulkSpriteCount'] || 0;
    });
#else
    void JsResetCanvasDiagnostics() {}
    int JsUnpremultiplyBuildCount() { return 0; }
    int JsBulkFlushCount() { return 0; }
    int JsBulkSpriteCount() { return 0; }
#endif
}

class CanvasSmokeTest : public Game
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
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        // 2x2 RGBA8, one distinct color per pixel -- enough to exercise a real putImageData().
        texture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 2, 2, std::vector<std::uint8_t>{
                255, 0, 0, 255,   0, 255, 0, 255,
                0, 0, 255, 255,   255, 255, 0, 255,
            }));
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CanvasRenderer&>(dev.GetRenderer());

        if (frame_ == 1)
        {
            check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) != nullptr, "GraphicsDevice has a real SDL_Window under the Canvas renderer");
            check(SDL_GetRenderer(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty())) == nullptr, "SDL_GetRenderer(window) is null -- no SDL_Renderer exists on this renderer");

            int w = 0, h = 0;
            renderer.GetViewportSize(w, h);
            check(w > 0 && h > 0, "GetViewportSize() reports a positive logical size");
            JsResetCanvasDiagnostics();
        }
        else if (frame_ == 2)
        {
            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            Exit();
            return;
        }

        dev.Clear(Color::CornflowerBlue);

        // CANVAS-86 scale regression: a 2x2 source expanded to exactly 8x8. Before the fix the
        // destination/source ratio was applied both to ctx.scale() and the drawImage destination,
        // producing a 32x32 footprint instead.
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        spriteBatch_->Draw(*texture_, Rectangle(8, 8, 8, 8), Rectangle(0, 0, 2, 2), Color::White);
        spriteBatch_->End();

        std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
        dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
        check(pixels[8 * 64 + 8] == Color(255, 0, 0, 255),
              "scaled draw starts with the source's top-left texel");
        check(pixels[15 * 64 + 15] == Color(255, 255, 0, 255),
              "scaled draw reaches the exact destination bottom-right texel");
        check(pixels[16 * 64 + 16] == Color::CornflowerBlue,
              "scaled draw does not escape its destination rectangle");

        // Two ordinary AlphaBlend sprites from one uploaded texture must share one cached
        // straight-alpha variant and cross wasm->JS in one deferred-batch flush.
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend);
        spriteBatch_->Draw(*texture_, Rectangle(24, 8, 8, 8), Rectangle(0, 0, 2, 2), Color::White);
        spriteBatch_->Draw(*texture_, Rectangle(36, 8, 8, 8), Rectangle(0, 0, 2, 2), Color::White);
        spriteBatch_->End();
        check(JsUnpremultiplyBuildCount() == 1,
              "two AlphaBlend draws build one cached straight-alpha texture variant");
        check(JsBulkFlushCount() == 2 && JsBulkSpriteCount() == 3,
              "two deferred batches use two JS crossings for three sprites");
    }

public:
    CanvasSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    CanvasSmokeTest game;
    game.Run();
    return game.getResult();
}
