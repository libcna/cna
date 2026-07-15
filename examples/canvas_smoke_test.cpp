// plan_canvas.md CANVAS-15: structural smoke test for the CANVAS (HTML Canvas 2D) graphics
// backend. Constructs a real Game (GraphicsDeviceManager, Clear(), a SpriteBatch draw once Phase
// C4 lands) and runs a couple of frames.
//
// Design decision 9 / this file's own empirical finding: this backend is Emscripten-only, and
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

#include "CNA/Internal/Backends/Canvas/CanvasGraphicsBackend.hpp"

#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::Canvas;

class CanvasSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        auto& backend = static_cast<CanvasGraphicsBackend&>(dev.GetBackend());

        if (frame_ == 1)
        {
            check(backend.GetWindowInternal() != nullptr, "GraphicsDevice has a real SDL_Window under the Canvas backend");
            check(backend.GetRendererInternal() == nullptr, "GetRendererInternal() is null -- no SDL_Renderer exists on this backend");

            int w = 0, h = 0;
            backend.GetViewportSize(w, h);
            check(w > 0 && h > 0, "GetViewportSize() reports a positive logical size");
        }

        dev.Clear(Color::CornflowerBlue);

        // Real SpriteBatch/Texture2D coverage lands once Phase C3/C4 replace CreateTexture()/
        // CreateSpriteBatch()'s current NotYetImplemented stubs.

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
    CanvasSmokeTest game;
    game.Run();
    return game.getResult();
}
