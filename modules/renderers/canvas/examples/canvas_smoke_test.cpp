// plan_canvas.md CANVAS-15: structural smoke test for the CANVAS (HTML Canvas 2D) graphics
// renderer. Constructs a real Game (GraphicsDeviceManager, Clear(), a Texture2D + SpriteBatch draw
// with rotation/origin/tint) and runs a couple of frames.
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

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Canvas;

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
        }

        dev.Clear(Color::CornflowerBlue);

        spriteBatch_->Begin();
        spriteBatch_->Draw(*texture_, Vector2(8, 8), Color::White);
        spriteBatch_->Draw(*texture_, Rectangle(20, 8, 16, 16), Rectangle(0, 0, 2, 2), Color(128, 128, 255, 200),
                           0.5f, Vector2(1, 1), SpriteEffects::FlipHorizontally, 0.0f);
        spriteBatch_->End();

        if (frame_ == 2)
        {
            std::printf("=== %d/%d PASS ===\n", passCount_, 3);
            result_ = (passCount_ == 3) ? 0 : 1;
            Exit();
        }
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
    // Heap-allocated, not a local: emscripten_set_main_loop(..., simulateInfiniteLoop=1) unwinds
    // this stack frame via a JS-level throw (see docs/emscripten-mainloop-game-lifetime.md) --
    // a stack-local Game here would have its storage reclaimed while the loop callback still
    // holds a raw pointer to it.
    CanvasSmokeTest* game = new CanvasSmokeTest();
    game->Run();
    return game->getResult();
}
