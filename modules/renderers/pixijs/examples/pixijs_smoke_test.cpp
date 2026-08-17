// plan_pixijs.md PIXIJS-84: structural smoke test for the PIXIJS renderer, mirroring
// canvas/canvas_smoke_test.cpp's own shape. Constructs a real Game (GraphicsDeviceManager,
// Clear(), a Texture2D + SpriteBatch draw), reads the backbuffer back, and checks a couple of
// destination pixels.
//
// plan_pixijs.md status block: this session has no Emscripten toolchain at all -- this file has
// never been compiled, let alone run. It exists so the first future session with a real emsdk has
// something concrete to build and iterate on, not a claim that it currently passes anything.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/PixiJs/PixiJsRenderer.hpp"

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::PixiJs;

namespace
{
    constexpr int kExpectedChecks = 5;
}

class PixiJsSmokeTest : public Game
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
        auto& renderer = static_cast<PixiJsRenderer&>(dev.GetRenderer());

        if (frame_ == 1)
        {
            check(renderer.GetWindowInternal() != nullptr, "GraphicsDevice has a real SDL_Window under the PixiJS renderer");
            check(renderer.GetRendererInternal() == nullptr, "GetRendererInternal() is null -- no SDL_Renderer exists on this renderer");

            int w = 0, h = 0;
            renderer.GetViewportSize(w, h);
            check(w > 0 && h > 0, "GetViewportSize() reports a positive logical size");
        }
        else if (frame_ == 2)
        {
            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            Exit();
            return;
        }

        dev.Clear(Color::CornflowerBlue);

        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        spriteBatch_->Draw(*texture_, Rectangle(8, 8, 8, 8), Rectangle(0, 0, 2, 2), Color::White);
        spriteBatch_->End();

        std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
        dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
        check(pixels[8 * 64 + 8] == Color(255, 0, 0, 255),
              "scaled draw starts with the source's top-left texel");
        check(pixels[15 * 64 + 15] == Color(255, 255, 0, 255),
              "scaled draw reaches the exact destination bottom-right texel");
    }

public:
    PixiJsSmokeTest()
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
    // this stack frame via a JS-level throw (see docs/emscripten-mainloop-game-lifetime.md) -- a
    // stack-local Game here would have its storage reclaimed while the loop callback still holds a
    // raw pointer to it.
    PixiJsSmokeTest* game = new PixiJsSmokeTest();
    game->Run();
    return game->getResult();
}
