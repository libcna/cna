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
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"

#include "CNA/Internal/Renderers/PixiJs/PixiJsRenderer.hpp"

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::PixiJs;

namespace
{
    constexpr int kExpectedChecks = 23;
}

class PixiJsSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<Texture2D> semiTransparentTexture_;
    std::unique_ptr<SpriteFont> font_;
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

        // plan_pixijs.md PIXIJS-60: a one-glyph SpriteFont ('A', a 4x4 fully-opaque atlas cell, no
        // cropping offset, no bearing) -- same fixture htmldom_smoke_test.cpp's own HTMLDOM-38 uses
        // -- enough to exercise DrawString() through the shared SpriteFont/SpriteBatch layer and
        // confirm (via a real pixel readback, not just DOM-property inspection) that it reaches
        // this renderer with zero PixiJS-specific code.
        std::vector<std::uint8_t> glyphPixels(4 * 4 * 4, 0);
        for (std::size_t i = 0; i < glyphPixels.size(); i += 4)
        {
            glyphPixels[i] = 255; glyphPixels[i + 1] = 200; glyphPixels[i + 2] = 0; glyphPixels[i + 3] = 255;
        }
        Texture2D glyphAtlas = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 4, 4, glyphPixels);
        font_ = std::make_unique<SpriteFont>(
            glyphAtlas,
            std::vector<Rectangle>{Rectangle(0, 0, 4, 4)},
            std::vector<Rectangle>{Rectangle(0, 0, 4, 4)},
            std::vector<charcs>{u'A'},
            /*lineSpacing=*/4, /*spacing=*/0.0f,
            std::vector<Vector3>{Vector3(0.0f, 4.0f, 0.0f)},
            std::nullopt);
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
        else if (frame_ == 14)
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
        else if (frame_ == 7)
        {
            // PixiJsRenderTargetRenderer end-to-end: bind a 4x4 render target, Clear() it to a
            // distinct color (proving Clear() really targets the bound RenderTexture, not the main
            // stage), unbind, then both (a) sample the render target as an ordinary texture back
            // onto the main backbuffer, and (b) read it directly via RenderTarget2D::GetData --
            // both must see the same fill color.
            RenderTarget2D rt(dev, 4, 4);
            dev.SetRenderTarget(&rt);
            dev.Clear(Color(10, 20, 30, 255));
            dev.SetRenderTarget(nullptr);

            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            spriteBatch_->Draw(rt, Rectangle(40, 40, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
            spriteBatch_->End();

            std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[40 * 64 + 40] == Color(10, 20, 30, 255),
                  "a render target's Clear() fill is visible when the target is later sampled as a texture");

            std::vector<Color> rtPixels(16, Color(0, 0, 0, 0));
            rt.GetData(rtPixels.data(), 0, static_cast<int>(rtPixels.size()));
            check(rtPixels[0] == Color(10, 20, 30, 255),
                  "RenderTarget2D::GetData reads back the same fill color directly");
        }
        else if (frame_ == 8)
        {
            // SetSamplerFilter(Point) end-to-end: draw the 2x2 RGBY texture scaled 4x (each texel
            // covers a 4x4 destination block) with an explicit Point-filter SamplerState, then
            // sample destination pixel (3,1) -- the last column of the top-left (Red) texel's own
            // footprint, one pixel before the boundary with the top-right (Green) texel. With the
            // LinearClamp default (used by every earlier frame's Begin()), this exact pixel blends
            // toward Green (empirically measured via a standalone probe before writing this
            // assertion: (159,96,0,255)); Point filtering must keep it a pure, unblended Red instead
            // -- the only way this check passes is if SetSamplerFilter's Point mapping actually
            // reached PIXI.SCALE_MODES.NEAREST on the sampled base texture.
            SamplerState pointClamp;
            pointClamp.setFilterProperty(TextureFilter::Point);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
            spriteBatch_->Draw(*texture_, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 2, 2), Color::White);
            spriteBatch_->End();

            std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[1 * 64 + 3] == Color(255, 0, 0, 255),
                  "SetSamplerFilter(Point) keeps a texel-edge pixel unblended, unlike the LinearClamp default");
        }
        else if (frame_ == 9)
        {
            // plan_pixijs.md PIXIJS-60: DrawString needs no renderer-specific code -- every glyph
            // funnels through the same Draw() overload frames 1-8 already pixel-verified.
            spriteBatch_->Begin();
            spriteBatch_->DrawString(*font_, "A", Vector2(2, 2), Color::White);
            spriteBatch_->End();

            std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[2 * 64 + 2] == Color(255, 200, 0, 255),
                  "DrawString renders the glyph's own atlas color at the requested position, with zero renderer-specific code");
        }
        else if (frame_ == 10)
        {
            // plan_pixijs.md PIXIJS-45: Begin(transformMatrix) with a real (non-identity) camera-
            // style translation. The same scaled draw as frame 1 (destRect(8,8,8,8), 2x2 source),
            // but with a Matrix::CreateTranslation(16,16,0) transform -- the transform applies AFTER
            // the sprite's own local placement (FNA's own contract), so the whole 8x8 quad that
            // frame 1 placed at (8,8)-(16,16) must land at (24,24)-(32,32) instead, with the same
            // per-texel content.
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr,
                                nullptr, nullptr, Matrix::CreateTranslation(16.0f, 16.0f, 0.0f));
            spriteBatch_->Draw(*texture_, Rectangle(8, 8, 8, 8), Rectangle(0, 0, 2, 2), Color::White);
            spriteBatch_->End();

            std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[24 * 64 + 24] == Color(255, 0, 0, 255),
                  "Begin(transformMatrix) translation shifts the whole draw, top-left texel unaffected");
            check(pixels[31 * 64 + 31] == Color(255, 255, 0, 255),
                  "Begin(transformMatrix) translation shifts the whole draw, bottom-right texel unaffected");
            check(pixels[8 * 64 + 8] == Color(100, 149, 237, 255),
                  "the untransformed origin (8,8) is back to the CornflowerBlue background -- the draw truly moved, not just replicated");
        }
        else if (frame_ == 11)
        {
            // plan_pixijs.md PIXIJS-32: Texture2D::SetData directly on a RenderTarget2D (no
            // SpriteBatch involved), while it is NOT the currently-bound render target -- exercises
            // PixiJsRenderTargetRenderer::UpdatePixels's real implementation (a throwaway
            // buffer-backed texture painted over the target with BLEND_MODES.NONE), not the
            // SpriteBatch::Draw path frame 7 already covers.
            RenderTarget2D rt2(dev, 2, 2);
            std::vector<Color> newPixels{
                Color(9, 8, 7, 255), Color(6, 5, 4, 255),
                Color(3, 2, 1, 255), Color(0, 255, 0, 255),
            };
            rt2.SetData(newPixels.data(), static_cast<int>(newPixels.size()));

            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            spriteBatch_->Draw(rt2, Rectangle(48, 48, 2, 2), Rectangle(0, 0, 2, 2), Color::White);
            spriteBatch_->End();

            std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[48 * 64 + 48] == Color(9, 8, 7, 255),
                  "Texture2D::SetData on an unbound RenderTarget2D is visible when later sampled as a texture (top-left texel)");
            check(pixels[49 * 64 + 49] == Color(0, 255, 0, 255),
                  "Texture2D::SetData on an unbound RenderTarget2D is visible when later sampled as a texture (bottom-right texel)");
        }
        else if (frame_ == 12)
        {
            // plan_pixijs.md PIXIJS-52: a fully generic BlendState -- a "multiply" blend
            // (ColorSourceBlend=DestinationColor, ColorDestinationBlend=Zero, AlphaSourceBlend=One,
            // AlphaDestinationBlend=Zero) that is NOT one of the 4 standard presets, exercising the
            // real custom PixiJS blend-mode table registration path rather than the fixed
            // Opaque/AlphaBlend/NonPremultiplied/Additive set frames 4-6 already cover. Draws an
            // opaque gray (128,128,128,255) texel over the CornflowerBlue(100,149,237) background;
            // exact expected result (50,75,119,255) confirmed via a standalone probe registering the
            // identical GL blend factors/equation before this assertion was written.
            BlendState multiply;
            multiply.setColorSourceBlendProperty(Blend::DestinationColor);
            multiply.setColorDestinationBlendProperty(Blend::Zero);
            multiply.setAlphaSourceBlendProperty(Blend::One);
            multiply.setAlphaDestinationBlendProperty(Blend::Zero);

            Texture2D grayTexture(Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{
                128, 128, 128, 255,
            }));
            spriteBatch_->Begin(SpriteSortMode::Deferred, multiply);
            spriteBatch_->Draw(grayTexture, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
            spriteBatch_->End();

            std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0] == Color(50, 75, 119, 255),
                  "a generic (non-preset) BlendState composites via a real custom PixiJS blend-mode table entry");
        }
        else if (frame_ == 13)
        {
            // plan_pixijs.md PIXIJS-50/51: BlendState::NonPremultiplied had no test of its own --
            // only ever exercised implicitly by sharing AlphaBlend's code path. This proves it is
            // genuinely wired (reaches PixiJsSpriteBatchRenderer, produces a real composite) rather
            // than silently falling through to some other default. Same fixture and expected result
            // as frame 6's own AlphaBlend check (178,74,118,255) -- this renderer's known,
            // documented v1-scope limitation is that NonPremultiplied and AlphaBlend are not yet
            // distinguished (PIXIJS-51's own precisely-characterized premultiply gap), so an
            // identical result here is the CORRECT current behavior to lock in, not a bug in this
            // test's own expectation.
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied);
            spriteBatch_->Draw(*semiTransparentTexture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
            spriteBatch_->End();

            std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0] == Color(178, 74, 118, 255),
                  "BlendState::NonPremultiplied is genuinely wired and composites straight-alpha content correctly");
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
