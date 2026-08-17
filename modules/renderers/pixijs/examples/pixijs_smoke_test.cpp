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
    constexpr int kExpectedChecks = 12;
}

class PixiJsSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<Texture2D> semiTransparentTexture_;
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
        // A single half-alpha red pixel -- used to prove BlendState::Opaque genuinely overwrites
        // (ignores source alpha) rather than blending like AlphaBlend/NonPremultiplied would.
        semiTransparentTexture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1, std::vector<std::uint8_t>{
                255, 0, 0, 128,
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
        else if (frame_ == 7)
        {
            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            Exit();
            return;
        }

        dev.Clear(Color::CornflowerBlue);

        if (frame_ == 1)
        {
            // Scaled, unrotated, anchor=(0,0) draw. Source texture is a 2x2 RGBA8 grid
            // (TL=Red, TR=Green, BL=Blue, BR=Yellow), scaled 4x into an 8x8 destination rect.
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
        else if (frame_ == 2)
        {
            // 180-degree rotation around the texture's exact center (origin=(1,1) in
            // source-pixel space, matching the 2x2 texture's own midpoint). destRect(8,8,8,8) means
            // the origin point lands at screen (8,8); the unrotated quad would span (4,4)-(12,12).
            // Rotating 180 around that same anchor point swaps which quadrant shows which texel:
            // Red (originally top-left) ends up in the bottom-right quadrant, and Yellow
            // (originally bottom-right) ends up in the top-left quadrant, while the bounding box
            // itself (4,4)-(12,12) does not move.
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            spriteBatch_->Draw(*texture_, Rectangle(8, 8, 8, 8), Rectangle(0, 0, 2, 2), Color::White,
                                3.14159265358979323846f, Vector2(1.0f, 1.0f), SpriteEffects::None, 0.0f);
            spriteBatch_->End();

            std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[4 * 64 + 4] == Color(255, 255, 0, 255),
                  "180-degree rotation around center puts the source's bottom-right texel at the bounding box's top-left");
            check(pixels[11 * 64 + 11] == Color(255, 0, 0, 255),
                  "180-degree rotation around center puts the source's top-left texel at the bounding box's bottom-right");
        }
        else if (frame_ == 3)
        {
            // FlipHorizontally, origin=(0,0), unrotated, destRect(20,8,8,8): the destination
            // rectangle must NOT move (REMED-PIXIJS-2's own fix -- a naive negative-scale flip
            // shifted the footprint left by 8px here, confirmed empirically before the fix). Only
            // sampling mirrors left-right: the top-left quadrant now shows the source's top-right
            // texel (Green), and the bottom-right quadrant shows the source's bottom-left (Blue).
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            spriteBatch_->Draw(*texture_, Rectangle(20, 8, 8, 8), Rectangle(0, 0, 2, 2), Color::White,
                                0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
            spriteBatch_->End();

            std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[8 * 64 + 20] == Color(0, 255, 0, 255),
                  "FlipHorizontally shows the source's top-right texel at the (unmoved) destination top-left");
            check(pixels[15 * 64 + 27] == Color(0, 0, 255, 255),
                  "FlipHorizontally shows the source's bottom-left texel at the (unmoved) destination bottom-right");
        }
        else if (frame_ == 4)
        {
            // BlendState::Additive over the CornflowerBlue(100,149,237) background: draw the
            // texture's fully-opaque top-left Red(255,0,0,255) texel unscaled at (0,0). Additive
            // blending sums source and destination (clamped to 255) regardless of the exact
            // (srcBlend,dstBlend) factor pairing, since srcAlpha=1 here makes every plausible
            // Additive formulation agree: R clamps at 255+100->255, G/B pass the background
            // through unchanged since the source contributes 0 there.
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Additive);
            spriteBatch_->Draw(*texture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
            spriteBatch_->End();

            std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0] == Color(255, 149, 237, 255),
                  "Additive blend sums the opaque red source onto the CornflowerBlue background, clamped");
        }
        else if (frame_ == 5)
        {
            // BlendState::Opaque with a half-alpha (128/255) source pixel over the
            // CornflowerBlue background: real Opaque semantics (srcBlend=One, dstBlend=Zero) mean
            // an unconditional overwrite that ignores source alpha entirely -- the destination
            // pixel must become the source's own RGB with full alpha, NOT a blend with the
            // background (REMED-PIXIJS-3: BLEND_MODES.NONE, not the NORMAL every other preset
            // still uses in this v1 scope).
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            spriteBatch_->Draw(*semiTransparentTexture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
            spriteBatch_->End();

            std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0] == Color(255, 0, 0, 255),
                  "Opaque overwrites unconditionally: a half-alpha source pixel still fully replaces the background");
        }
        else if (frame_ == 6)
        {
            // BlendState::AlphaBlend, same half-alpha (128/255) red source pixel over the
            // CornflowerBlue(100,149,237) background: real straight-alpha "over" compositing gives
            // result = src*a + dst*(1-a) -- (255*128+100*127)/255=178.03->178,
            // (0*128+149*127)/255=74.27->74, (0*128+237*127)/255=118.13->118 (PixiJS's own integer
            // rounding, empirically confirmed via a standalone probe against this exact fixture
            // before writing this assertion, not hand-derived and hoped-for).
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend);
            spriteBatch_->Draw(*semiTransparentTexture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
            spriteBatch_->End();

            std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0] == Color(178, 74, 118, 255),
                  "AlphaBlend composites a half-alpha source pixel over the background with correct straight-over math");
        }
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
