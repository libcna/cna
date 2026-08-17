// plan_pixijs.md PIXIJS-80/PIXIJS-85: the PIXIJS renderer's real-browser pixel suite.
//
// This is not a "did it crash" smoke test. Every check below reads real pixels back from a real
// WebGL context and compares them against a value derived from XNA's own documented arithmetic, so
// a regression in blend factors, sampler state, submission order, render-target routing or
// resource lifetime shows up as a wrong colour rather than as a silent no-op.
//
// It cannot run under plain `node`: PixiJS needs a DOM and a WebGL context, and the platform's own
// video-subsystem startup fails first without one. Run it with
// `node scripts/run_pixijs_browser_tests.mjs <build-dir>`, which serves the built page over HTTP
// and drives it with headless Chromium.
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
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::PixiJs;

namespace
{
    constexpr int kExpectedChecks = 66;
    constexpr int kBackBuffer = 64;

    /// CornflowerBlue, the background every frame that clears uses.
    const Color kBackground{100, 149, 237, 255};
}

class PixiJsSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<Texture2D> semiTransparentTexture_;
    std::unique_ptr<Texture2D> whiteTexture_;
    std::unique_ptr<Texture2D> grayTexture_;
    std::unique_ptr<Texture2D> premultipliedTexture_;
    std::unique_ptr<Texture2D> straightTexture_;
    std::unique_ptr<SpriteFont> font_;
    std::vector<Color> pixels_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    /// Reports the actual colour on failure -- iterating on a pixel test without it means guessing.
    void checkPixel(const Color& actual, const Color& expected, const char* label)
    {
        const bool ok = actual == expected;
        if (!ok)
            std::printf("       got (%d,%d,%d,%d) want (%d,%d,%d,%d)\n",
                        actual.getRProperty(), actual.getGProperty(),
                        actual.getBProperty(), actual.getAProperty(),
                        expected.getRProperty(), expected.getGProperty(),
                        expected.getBProperty(), expected.getAProperty());
        check(ok, label);
    }

    /// Channel-wise comparison with a tolerance, for the handful of values that pass through a
    /// float32 GL constant before reaching an 8-bit framebuffer.
    void checkPixelNear(const Color& actual, const Color& expected, int tolerance, const char* label)
    {
        auto near = [tolerance](int a, int b) { return a - b <= tolerance && b - a <= tolerance; };
        const bool ok = near(actual.getRProperty(), expected.getRProperty()) &&
                        near(actual.getGProperty(), expected.getGProperty()) &&
                        near(actual.getBProperty(), expected.getBProperty()) &&
                        near(actual.getAProperty(), expected.getAProperty());
        if (!ok)
            std::printf("       got (%d,%d,%d,%d) want (%d,%d,%d,%d) +/-%d\n",
                        actual.getRProperty(), actual.getGProperty(),
                        actual.getBProperty(), actual.getAProperty(),
                        expected.getRProperty(), expected.getGProperty(),
                        expected.getBProperty(), expected.getAProperty(), tolerance);
        check(ok, label);
    }

    const Color& backbufferPixel(int x, int y) const { return pixels_[y * kBackBuffer + x]; }

    void readBackbuffer()
    {
        pixels_.assign(kBackBuffer * kBackBuffer, Color(0, 0, 0, 0));
        getGraphicsDeviceProperty().GetBackBufferData(
            pixels_.data(), 0, static_cast<int>(pixels_.size()));
    }

    Texture2D makeTexture(int w, int h, std::vector<std::uint8_t> rgba)
    {
        return Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), w, h, std::move(rgba));
    }

    /// A BlendState built from raw factors -- every test below that needs a non-preset state.
    static BlendState makeBlendState(Blend colorSrc, Blend colorDst, Blend alphaSrc, Blend alphaDst)
    {
        BlendState state;
        state.setColorSourceBlendProperty(colorSrc);
        state.setColorDestinationBlendProperty(colorDst);
        state.setAlphaSourceBlendProperty(alphaSrc);
        state.setAlphaDestinationBlendProperty(alphaDst);
        return state;
    }

    /// One deferred Opaque batch drawing a source rectangle into a destination rectangle, with
    /// Point sampling.
    ///
    /// Point rather than the LinearClamp default on purpose: most checks below assert an EXACT
    /// texel colour, and linear magnification of a sub-rectangle of a multi-texel atlas legitimately
    /// bleeds a neighbouring texel in near the frame's edges (the boundary PIXIJS-46 documents).
    /// The frames that are about filtering select their own SamplerState explicitly.
    void drawTexel(const Texture2D& texture, const Rectangle& source, const Rectangle& destination)
    {
        SamplerState pointClamp;
        pointClamp.setFilterProperty(TextureFilter::Point);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        spriteBatch_->Draw(texture, destination, source, Color::White);
        spriteBatch_->End();
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        // 2x2 RGBA8 grid: TL=Red, TR=Green, BL=Blue, BR=Yellow.
        texture_ = std::make_unique<Texture2D>(makeTexture(2, 2, {
            255, 0, 0, 255,   0, 255, 0, 255,
            0, 0, 255, 255,   255, 255, 0, 255,
        }));
        // A single half-alpha red pixel -- proves BlendState::Opaque genuinely overwrites.
        semiTransparentTexture_ = std::make_unique<Texture2D>(makeTexture(1, 1, {255, 0, 0, 128}));
        whiteTexture_ = std::make_unique<Texture2D>(makeTexture(1, 1, {255, 255, 255, 255}));
        grayTexture_ = std::make_unique<Texture2D>(makeTexture(1, 1, {128, 128, 128, 255}));
        // PIXIJS-51: the same visual colour in both of CNA's alpha conventions. Straight colour
        // (255,100,50) at alpha 128; premultiplied, that is (255*128/255, 100*128/255, 50*128/255).
        // The same fixture shape HTMLDOM-85 uses for the same distinction.
        premultipliedTexture_ = std::make_unique<Texture2D>(makeTexture(1, 1, {128, 50, 25, 128}));
        straightTexture_ = std::make_unique<Texture2D>(makeTexture(1, 1, {255, 100, 50, 128}));

        // plan_pixijs.md PIXIJS-60: a one-glyph SpriteFont ('A', a 4x4 fully-opaque atlas cell) --
        // same fixture htmldom_smoke_test.cpp's own HTMLDOM-38 uses.
        std::vector<std::uint8_t> glyphPixels(4 * 4 * 4, 0);
        for (std::size_t i = 0; i < glyphPixels.size(); i += 4)
        {
            glyphPixels[i] = 255; glyphPixels[i + 1] = 200; glyphPixels[i + 2] = 0; glyphPixels[i + 3] = 255;
        }
        font_ = std::make_unique<SpriteFont>(
            makeTexture(4, 4, glyphPixels),
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

        // PIXIJS-91: frame 1 deliberately never calls Clear. Every other frame does.
        const bool clearThisFrame = frame_ != 1;
        if (clearThisFrame) dev.Clear(kBackground);

        switch (frame_)
        {
        case 1: FrameFirstOperationsWithoutClear(dev, renderer); break;
        case 2: FrameScaledDraw(); break;
        case 3: FrameRotation(); break;
        case 4: FrameFlip(); break;
        case 5: FrameAdditive(); break;
        case 6: FrameOpaqueOverwrite(); break;
        case 7: FrameAlphaBlendLiteralFactors(); break;
        case 8: FrameNonPremultiplied(); break;
        case 9: FrameAlphaConventions(); break;
        case 10: FrameRenderTargetRoundTrip(dev); break;
        case 11: FrameRenderTargetSwitching(dev); break;
        case 12: FrameRenderTargetSetData(dev); break;
        case 13: FrameTwoDeferredBatches(); break;
        case 14: FrameImmediateDraws(); break;
        case 15: FrameDeferredThenImmediate(); break;
        case 16: FrameTwoCustomBlendStates(); break;
        case 17: FrameBlendFactor(dev); break;
        case 18: FrameColorWriteChannels(dev); break;
        case 19: FrameTwoSamplerStates(); break;
        case 20: FrameMixedAddressModesRejected(); break;
        case 21: FrameSamplerFilterPoint(); break;
        case 22: FrameDrawString(); break;
        case 23: FrameTransformMatrix(); break;
        case 24: FrameTextureLifetime(dev); break;
        case 25: FrameResize(dev, renderer); break;
        case 26: FrameRendererRecreation(dev); break;
        default:
            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            Exit();
            return;
        }
    }

    // ---------------------------------------------------------------------------------------
    // PIXIJS-91 -- initialization must not depend on Clear
    // ---------------------------------------------------------------------------------------
    //
    // This is the process's FIRST frame and it never calls Clear. Before the commit-on-submit
    // rework the PIXI.Application was created lazily from Clear() alone, and every other entry
    // point returned silently when it was missing -- so this whole frame's drawing was lost, and
    // binding a render target as the first operation did nothing at all.
    void FrameFirstOperationsWithoutClear(GraphicsDevice& dev, PixiJsRenderer& renderer)
    {
        float logX = -1.0f, logY = -1.0f;
        const bool mapped = renderer.TransformWindowToLogical(0.0f, 0.0f, logX, logY);
        check(mapped && logX == 0.0f && logY == 0.0f,
              "TransformWindowToLogical() maps the surface origin -- the platform surface reached the renderer");

        float backX = -1.0f, backY = -1.0f;
        check(renderer.TransformLogicalToWindow(64.0f, 32.0f, backX, backY) &&
                  backX > 0.0f && backY > 0.0f,
              "TransformLogicalToWindow() inverts the same scale");

        int w = 0, h = 0;
        renderer.GetViewportSize(w, h);
        check(w > 0 && h > 0, "GetViewportSize() reports a positive logical size");

        // The very first graphics operation of the process is SetRenderTarget, with no Clear
        // anywhere before it.
        RenderTarget2D rt(dev, 4, 4);
        dev.SetRenderTarget(&rt);
        drawTexel(*texture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 2, 2));
        dev.SetRenderTarget(nullptr);

        std::vector<Color> rtPixels(16, Color(0, 0, 0, 0));
        rt.GetData(rtPixels.data(), 0, static_cast<int>(rtPixels.size()));
        checkPixel(rtPixels[0], Color(255, 0, 0, 255),
                   "SetRenderTarget as the first operation of the process, before any Clear, still receives the draw");

        // Still no Clear: a plain back-buffer draw on the first frame must survive too.
        drawTexel(*texture_, Rectangle(0, 0, 1, 1), Rectangle(8, 8, 4, 4));
        readBackbuffer();
        checkPixel(backbufferPixel(8, 8), Color(255, 0, 0, 255),
                   "the process's first back-buffer draw lands without any preceding Clear");
        checkPixel(backbufferPixel(11, 11), Color(255, 0, 0, 255),
                   "that first draw covers its whole destination rectangle");
    }

    // ---------------------------------------------------------------------------------------
    // Core draw geometry
    // ---------------------------------------------------------------------------------------

    void FrameScaledDraw()
    {
        // 2x2 RGBY source scaled 4x into an 8x8 destination rect.
        drawTexel(*texture_, Rectangle(0, 0, 2, 2), Rectangle(8, 8, 8, 8));
        readBackbuffer();
        checkPixel(backbufferPixel(8, 8), Color(255, 0, 0, 255),
                   "scaled draw starts with the source's top-left texel");
        checkPixel(backbufferPixel(15, 15), Color(255, 255, 0, 255),
                   "scaled draw reaches the exact destination bottom-right texel");
    }

    void FrameRotation()
    {
        // 180 degrees around the texture's exact centre (origin=(1,1) in source-pixel space).
        // destRect(8,8,8,8) puts the origin point at screen (8,8); the unrotated quad would span
        // (4,4)-(12,12). Rotating 180 about that anchor swaps which quadrant shows which texel
        // while the bounding box itself does not move.
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        spriteBatch_->Draw(*texture_, Rectangle(8, 8, 8, 8), Rectangle(0, 0, 2, 2), Color::White,
                           3.14159265358979323846f, Vector2(1.0f, 1.0f), SpriteEffects::None, 0.0f);
        spriteBatch_->End();
        readBackbuffer();
        checkPixel(backbufferPixel(4, 4), Color(255, 255, 0, 255),
                   "180-degree rotation around centre puts the source's bottom-right texel at the bounding box's top-left");
        checkPixel(backbufferPixel(11, 11), Color(255, 0, 0, 255),
                   "180-degree rotation around centre puts the source's top-left texel at the bounding box's bottom-right");
    }

    void FrameFlip()
    {
        // FlipHorizontally must NOT move the destination rectangle (REMED-PIXIJS-2): only sampling
        // mirrors left-right.
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        spriteBatch_->Draw(*texture_, Rectangle(20, 8, 8, 8), Rectangle(0, 0, 2, 2), Color::White,
                           0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
        spriteBatch_->End();
        readBackbuffer();
        checkPixel(backbufferPixel(20, 8), Color(0, 255, 0, 255),
                   "FlipHorizontally shows the source's top-right texel at the (unmoved) destination top-left");
        checkPixel(backbufferPixel(27, 15), Color(0, 0, 255, 255),
                   "FlipHorizontally shows the source's bottom-left texel at the (unmoved) destination bottom-right");
    }

    // ---------------------------------------------------------------------------------------
    // Blend states -- all rendered from their literal XNA factors (PIXIJS-87)
    // ---------------------------------------------------------------------------------------

    void FrameAdditive()
    {
        // Additive over CornflowerBlue(100,149,237): R clamps at 255+100, G/B pass through since
        // the opaque red source contributes 0 there.
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Additive);
        spriteBatch_->Draw(*texture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        readBackbuffer();
        checkPixel(backbufferPixel(0, 0), Color(255, 149, 237, 255),
                   "Additive sums the opaque red source onto the background, clamped");
    }

    void FrameOpaqueOverwrite()
    {
        // Opaque is (One, Zero): an unconditional overwrite that ignores source alpha entirely.
        drawTexel(*semiTransparentTexture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1));
        readBackbuffer();
        checkPixel(backbufferPixel(0, 0), Color(255, 0, 0, 255),
                   "Opaque overwrites unconditionally: a half-alpha source pixel still fully replaces the background");
    }

    void FrameAlphaBlendLiteralFactors()
    {
        // PIXIJS-87. BlendState::AlphaBlend is (One, InverseSourceAlpha). Applied literally to a
        // STRAIGHT-alpha source (255,0,0,128) over CornflowerBlue:
        //   R = 255*1 + 100*(1-128/255) = 255 + 49.8 -> clamps to 255
        //   G = 0     + 149*(127/255)   = 74.2       -> 74
        //   B = 0     + 237*(127/255)   = 118.0      -> 118
        // which is what EasyGL, the software rasterizer and every other CNA renderer produce for the
        // same call. This renderer used to report (178,74,118) here -- NonPremultiplied's answer --
        // because PixiJS silently rewrites BLEND_MODES.NORMAL to NORMAL_NPM (a different factor
        // tuple) for any non-premultiplied texture. Rendering from the literal factors through this
        // renderer's own blend slot is what fixed it, and frame 9 shows the distinction is real.
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend);
        spriteBatch_->Draw(*semiTransparentTexture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        readBackbuffer();
        checkPixel(backbufferPixel(0, 0), Color(255, 74, 118, 255),
                   "AlphaBlend applies XNA's literal (One, InverseSourceAlpha) factors");
    }

    void FrameNonPremultiplied()
    {
        // (SourceAlpha, InverseSourceAlpha) on the same straight-alpha source:
        //   R = 255*(128/255) + 100*(127/255) = 128 + 49.8 = 177.8 -> 178
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied);
        spriteBatch_->Draw(*semiTransparentTexture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        readBackbuffer();
        checkPixel(backbufferPixel(0, 0), Color(178, 74, 118, 255),
                   "NonPremultiplied applies XNA's literal (SourceAlpha, InverseSourceAlpha) factors");
    }

    void FrameAlphaConventions()
    {
        // PIXIJS-51 -- the premultiplied/straight distinction, made measurable.
        //
        // The same visual colour (255,100,50 at alpha 128) is supplied twice: once premultiplied
        // and once straight. Drawn under the preset that matches its own convention, each must
        // composite to the SAME correct result over CornflowerBlue -- which is the whole point of
        // XNA having two presets. Drawn under the wrong preset, the premultiplied fixture must come
        // out visibly darker. Before PIXIJS-87 all three of these produced identical pixels,
        // because both presets reached the same NORMAL_NPM factor tuple.
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend);
        spriteBatch_->Draw(*premultipliedTexture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();

        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied);
        spriteBatch_->Draw(*straightTexture_, Rectangle(2, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();

        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied);
        spriteBatch_->Draw(*premultipliedTexture_, Rectangle(4, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();

        readBackbuffer();
        // AlphaBlend on premultiplied content: 128 + 100*(127/255) = 177.8; 50 + 149*(127/255) =
        // 124.2; 25 + 237*(127/255) = 143.0.
        checkPixel(backbufferPixel(0, 0), Color(178, 124, 143, 255),
                   "AlphaBlend composites genuinely premultiplied source content correctly");
        // NonPremultiplied on straight content: the identical result, from the identical colour
        // expressed in the other convention.
        checkPixel(backbufferPixel(2, 0), Color(178, 124, 143, 255),
                   "NonPremultiplied composites the same colour in straight-alpha form to the same result");
        // The wrong preset for the content: 128*(128/255) + 100*(127/255) = 64.3 + 49.8 = 114.
        check(backbufferPixel(4, 0).getRProperty() == 114,
              "the two presets are genuinely distinguished -- premultiplied content under NonPremultiplied is visibly darker");
    }

    // ---------------------------------------------------------------------------------------
    // Render targets
    // ---------------------------------------------------------------------------------------

    void FrameRenderTargetRoundTrip(GraphicsDevice& dev)
    {
        // A full round trip that actually DRAWS into the target with SpriteBatch, not only Clear():
        // bind, clear to a distinct colour, draw a 2x2 sprite into its top-left corner, unbind,
        // then read it back both by sampling it as an ordinary texture and via GetData.
        RenderTarget2D rt(dev, 4, 4);
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(10, 20, 30, 255));
        drawTexel(*texture_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2));
        dev.SetRenderTarget(nullptr);

        drawTexel(rt, Rectangle(0, 0, 4, 4), Rectangle(40, 40, 4, 4));
        readBackbuffer();
        checkPixel(backbufferPixel(40, 40), Color(255, 0, 0, 255),
                   "a sprite drawn INTO a render target survives being sampled back as a texture");
        checkPixel(backbufferPixel(41, 41), Color(255, 255, 0, 255),
                   "the whole sprite drawn into the render target survives, not just its first texel");
        checkPixel(backbufferPixel(43, 43), Color(10, 20, 30, 255),
                   "the render target's Clear() fill survives outside the drawn sprite");

        std::vector<Color> rtPixels(16, Color(0, 0, 0, 0));
        rt.GetData(rtPixels.data(), 0, static_cast<int>(rtPixels.size()));
        checkPixel(rtPixels[0], Color(255, 0, 0, 255),
                   "RenderTarget2D::GetData sees the sprite drawn into the target");
        checkPixel(rtPixels[3 * 4 + 3], Color(10, 20, 30, 255),
                   "RenderTarget2D::GetData sees the target's own Clear() fill");
    }

    void FrameRenderTargetSwitching(GraphicsDevice& dev)
    {
        // A -> B -> A -> back buffer. Each target must keep exactly its own content: PixiJS
        // re-parents a node on addChild, so the previous retained design moved pooled sprites out
        // of whichever container held them the moment another target was bound.
        // Note on re-binding: CNA implements XNA's RenderTargetUsage::DiscardContents, so binding a
        // target clears it. Each bind below therefore establishes its own content deliberately, and
        // what is under test is that a target keeps THAT content while other targets are bound and
        // drawn into.
        RenderTarget2D a(dev, 4, 4);
        RenderTarget2D b(dev, 4, 4);

        dev.SetRenderTarget(&a);
        dev.Clear(Color(200, 10, 10, 255));
        dev.SetRenderTarget(&b);
        dev.Clear(Color(10, 200, 10, 255));
        drawTexel(*texture_, Rectangle(1, 1, 1, 1), Rectangle(0, 0, 1, 1)); // Yellow into B
        dev.SetRenderTarget(&a);
        dev.Clear(Color(200, 10, 10, 255));
        drawTexel(*texture_, Rectangle(0, 1, 1, 1), Rectangle(0, 0, 1, 1)); // Blue into A
        dev.SetRenderTarget(nullptr);

        drawTexel(a, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4));
        drawTexel(b, Rectangle(0, 0, 4, 4), Rectangle(8, 0, 4, 4));
        readBackbuffer();
        checkPixel(backbufferPixel(0, 0), Color(0, 0, 255, 255),
                   "a draw made after switching BACK to render target A landed in A");
        checkPixel(backbufferPixel(1, 0), Color(200, 10, 10, 255),
                   "render target A holds its own Clear() colour outside that draw");
        checkPixel(backbufferPixel(8, 0), Color(255, 255, 0, 255),
                   "render target B kept the sprite drawn into it while A was bound and drawn into afterwards");
        checkPixel(backbufferPixel(9, 0), Color(10, 200, 10, 255),
                   "render target B kept its own, independent Clear() colour -- nothing from A leaked in");

        // Drawing to the back buffer afterwards must not mutate what A already holds.
        std::vector<Color> aPixels(16, Color(0, 0, 0, 0));
        a.GetData(aPixels.data(), 0, static_cast<int>(aPixels.size()));
        checkPixel(aPixels[0], Color(0, 0, 255, 255),
                   "render target A is unchanged by the back-buffer drawing that followed it");
        checkPixel(aPixels[1], Color(200, 10, 10, 255),
                   "render target A's clear region is unchanged by later back-buffer drawing too");
    }

    void FrameRenderTargetSetData(GraphicsDevice& dev)
    {
        // Texture2D::SetData straight onto an UNBOUND render target (no SpriteBatch involved).
        RenderTarget2D rt(dev, 2, 2);
        std::vector<Color> newPixels{
            Color(9, 8, 7, 255), Color(6, 5, 4, 255),
            Color(3, 2, 1, 255), Color(0, 255, 0, 255),
        };
        rt.SetData(newPixels.data(), static_cast<int>(newPixels.size()));

        drawTexel(rt, Rectangle(0, 0, 2, 2), Rectangle(48, 48, 2, 2));
        readBackbuffer();
        checkPixel(backbufferPixel(48, 48), Color(9, 8, 7, 255),
                   "Texture2D::SetData on an unbound RenderTarget2D is visible when sampled (top-left texel)");
        checkPixel(backbufferPixel(49, 49), Color(0, 255, 0, 255),
                   "Texture2D::SetData on an unbound RenderTarget2D is visible when sampled (bottom-right texel)");
    }

    // ---------------------------------------------------------------------------------------
    // Submission ordering (PIXIJS-87)
    // ---------------------------------------------------------------------------------------

    void FrameTwoDeferredBatches()
    {
        // Two separate Deferred Begin/End pairs in ONE frame, overlapping. Before the rework the
        // second flush restarted the sprite pool at index 0 and simply overwrote the first.
        drawTexel(*texture_, Rectangle(0, 0, 1, 1), Rectangle(2, 2, 4, 4));  // Red   (2,2)-(6,6)
        drawTexel(*texture_, Rectangle(1, 0, 1, 1), Rectangle(4, 4, 4, 4));  // Green (4,4)-(8,8)
        readBackbuffer();
        checkPixel(backbufferPixel(2, 2), Color(255, 0, 0, 255),
                   "the FIRST of two deferred batches in one frame is still visible");
        checkPixel(backbufferPixel(7, 7), Color(0, 255, 0, 255),
                   "the SECOND of two deferred batches in one frame is visible");
        checkPixel(backbufferPixel(5, 5), Color(0, 255, 0, 255),
                   "where two deferred batches overlap, the later one wins -- XNA submission order");
    }

    void FrameImmediateDraws()
    {
        // SpriteSortMode::Immediate with three draws. Every one of them must be painted; the
        // previous design left only the last, because each Draw rewrote pool[0] and painted nothing.
        SamplerState pointClamp;
        pointClamp.setFilterProperty(TextureFilter::Point);
        spriteBatch_->Begin(SpriteSortMode::Immediate, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        spriteBatch_->Draw(*texture_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->Draw(*texture_, Rectangle(4, 0, 2, 2), Rectangle(1, 0, 1, 1), Color::White);
        spriteBatch_->Draw(*texture_, Rectangle(8, 0, 2, 2), Rectangle(0, 1, 1, 1), Color::White);
        spriteBatch_->End();
        readBackbuffer();
        checkPixel(backbufferPixel(0, 0), Color(255, 0, 0, 255),
                   "Immediate mode draws the FIRST sprite of the batch");
        checkPixel(backbufferPixel(4, 0), Color(0, 255, 0, 255),
                   "Immediate mode draws the MIDDLE sprite of the batch");
        checkPixel(backbufferPixel(8, 0), Color(0, 0, 255, 255),
                   "Immediate mode draws the LAST sprite of the batch");
    }

    void FrameDeferredThenImmediate()
    {
        drawTexel(*texture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 8, 8));   // Red   (0,0)-(8,8)
        SamplerState pointClamp;
        pointClamp.setFilterProperty(TextureFilter::Point);
        spriteBatch_->Begin(SpriteSortMode::Immediate, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        spriteBatch_->Draw(*texture_, Rectangle(4, 4, 8, 8), Rectangle(1, 0, 1, 1), Color::White); // Green
        spriteBatch_->End();
        readBackbuffer();
        checkPixel(backbufferPixel(0, 0), Color(255, 0, 0, 255),
                   "a Deferred batch survives an Immediate batch submitted after it");
        checkPixel(backbufferPixel(11, 11), Color(0, 255, 0, 255),
                   "the Immediate batch that followed is painted");
        checkPixel(backbufferPixel(5, 5), Color(0, 255, 0, 255),
                   "Deferred-then-Immediate ordering matches submission order where they overlap");
    }

    // ---------------------------------------------------------------------------------------
    // Per-batch graphics state (PIXIJS-87/88/89)
    // ---------------------------------------------------------------------------------------

    void FrameTwoCustomBlendStates()
    {
        // TWO different non-preset BlendStates, in separate batches, before one readback. PixiJS's
        // StateSystem skips re-applying a blend mode whose id is unchanged, so the previous design
        // -- one reserved slot mutated in place -- silently rendered the second batch with the
        // FIRST batch's factors. Distinct slots per distinct tuple is what fixes it, and these two
        // expectations are far apart enough that a mix-up cannot pass.
        const BlendState multiply = makeBlendState(Blend::DestinationColor, Blend::Zero,
                                                   Blend::One, Blend::Zero);
        const BlendState inverseDestination = makeBlendState(Blend::InverseDestinationColor, Blend::Zero,
                                                             Blend::One, Blend::Zero);

        spriteBatch_->Begin(SpriteSortMode::Deferred, multiply);
        spriteBatch_->Draw(*grayTexture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();

        spriteBatch_->Begin(SpriteSortMode::Deferred, inverseDestination);
        spriteBatch_->Draw(*whiteTexture_, Rectangle(2, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();

        readBackbuffer();
        // gray(128) * CornflowerBlue(100,149,237) / 255.
        checkPixel(backbufferPixel(0, 0), Color(50, 75, 119, 255),
                   "the first of two custom BlendStates composited with its OWN factors");
        // white(255) * (255 - CornflowerBlue) / 255.
        checkPixel(backbufferPixel(2, 0), Color(155, 106, 18, 255),
                   "the second of two custom BlendStates composited with its OWN factors, not the first's");
    }

    void FrameBlendFactor(GraphicsDevice& dev)
    {
        // PIXIJS-88: BlendFactor/InverseBlendFactor reach WebGL's gl.blendColor for real. The
        // constant is (255, 0, 128, 255)/255 = (1.0, 0.0, 0.50196, 1.0); R and G are exactly
        // representable, B passes through a float32 constant so it is checked to +/-1.
        //
        // The constant is carried on the BlendState, which is where XNA puts it: applying a
        // BlendState re-applies its own BlendFactor, so a value assigned to GraphicsDevice.BlendFactor
        // before Begin() would be overwritten by the state Begin() applies.
        dev.Clear(Color(0, 0, 0, 255));

        BlendState byFactor = makeBlendState(Blend::BlendFactor, Blend::Zero, Blend::One, Blend::Zero);
        byFactor.setBlendFactorProperty(Color(255, 0, 128, 255));
        spriteBatch_->Begin(SpriteSortMode::Deferred, byFactor);
        spriteBatch_->Draw(*whiteTexture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();

        BlendState byInverseFactor = makeBlendState(Blend::InverseBlendFactor, Blend::Zero, Blend::One, Blend::Zero);
        byInverseFactor.setBlendFactorProperty(Color(255, 0, 128, 255));
        spriteBatch_->Begin(SpriteSortMode::Deferred, byInverseFactor);
        spriteBatch_->Draw(*whiteTexture_, Rectangle(2, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();

        readBackbuffer();
        checkPixelNear(backbufferPixel(0, 0), Color(255, 0, 128, 255), 1,
                       "Blend::BlendFactor multiplies by the real BlendState.BlendFactor constant");
        checkPixelNear(backbufferPixel(2, 0), Color(0, 255, 127, 255), 1,
                       "Blend::InverseBlendFactor multiplies by one minus that same constant");
        dev.setBlendStateProperty(BlendState::AlphaBlend);
    }

    void FrameColorWriteChannels(GraphicsDevice& dev)
    {
        // PIXIJS-89: ColorWriteChannels is honoured for real through gl.colorMask, and a
        // MultiSampleMask that disables coverage sample 0 is rejected rather than ignored.
        BlendState redOnly = BlendState::Opaque;
        redOnly.setColorWriteChannelsProperty(ColorWriteChannels::Red);
        spriteBatch_->Begin(SpriteSortMode::Deferred, redOnly);
        spriteBatch_->Draw(*whiteTexture_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();

        // The mask must not leak into the next batch.
        drawTexel(*texture_, Rectangle(1, 0, 1, 1), Rectangle(2, 0, 1, 1)); // Green, all channels

        readBackbuffer();
        checkPixel(backbufferPixel(0, 0), Color(255, 149, 237, 255),
                   "ColorWriteChannels::Red writes only the red channel, leaving G/B from the background");
        checkPixel(backbufferPixel(2, 0), Color(0, 255, 0, 255),
                   "a colour write mask does not leak into the next batch");

        BlendState noCoverage = BlendState::Opaque;
        noCoverage.setMultiSampleMaskProperty(0xFFFFFFFE);
        bool rejected = false;
        try
        {
            dev.setBlendStateProperty(noCoverage);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        check(rejected,
              "a MultiSampleMask that disables coverage sample 0 is rejected, not silently ignored");
        dev.setBlendStateProperty(BlendState::AlphaBlend);
    }

    // ---------------------------------------------------------------------------------------
    // Sampler state
    // ---------------------------------------------------------------------------------------

    void FrameTwoSamplerStates()
    {
        // PIXIJS-90: the SAME source texture drawn twice, in two batches, with different sampler
        // filters, before one readback. Sampler state lives on the shared PIXI.BaseTexture, so with
        // deferred rasterization both batches collapsed onto whichever filter was set last.
        SamplerState pointClamp;
        pointClamp.setFilterProperty(TextureFilter::Point);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        spriteBatch_->Draw(*texture_, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 2, 2), Color::White);
        spriteBatch_->End();

        SamplerState linearClamp;
        linearClamp.setFilterProperty(TextureFilter::Linear);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &linearClamp, nullptr, nullptr);
        spriteBatch_->Draw(*texture_, Rectangle(16, 0, 8, 8), Rectangle(0, 0, 2, 2), Color::White);
        spriteBatch_->End();

        readBackbuffer();
        // (3,1) is the last column of the top-left (Red) texel's 4x4 footprint, and (19,1) is the
        // same relative pixel of the second draw. Point filtering must leave it exactly the source
        // texel; linear filtering must blend it measurably toward the neighbouring Green texel.
        // The Point value is asserted exactly; the linear one is asserted as "genuinely blended"
        // rather than pinned to a single triple, because the exact interpolation weights of a
        // magnified 2x2 texture are a property of the GPU's sampler, not of this renderer. Either
        // way the two cannot both pass unless each batch really carried its own sampler state --
        // which is the regression under test, since sampler state lives on the shared base texture.
        const Color pointPixel = backbufferPixel(3, 1);
        const Color linearPixel = backbufferPixel(19, 1);
        checkPixel(pointPixel, Color(255, 0, 0, 255),
                   "the Point-filtered batch kept its own sampler state");
        if (!(linearPixel.getGProperty() >= 48 && linearPixel.getRProperty() <= 224))
            std::printf("       got (%d,%d,%d,%d), expected a blend toward Green (G>=48, R<=224)\n",
                        linearPixel.getRProperty(), linearPixel.getGProperty(),
                        linearPixel.getBProperty(), linearPixel.getAProperty());
        check(linearPixel.getGProperty() >= 48 && linearPixel.getRProperty() <= 224,
              "the Linear-filtered batch kept its own sampler state, in the same frame and on the same texture");
    }

    void FrameMixedAddressModesRejected()
    {
        // PIXIJS-90: a PIXI.BaseTexture has a single wrapMode. A mixed per-axis request used to be
        // stored and then half-applied; it is now rejected, so the caller learns the state it asked
        // for is not what would have been rendered.
        SamplerState mixed;
        mixed.setAddressUProperty(TextureAddressMode::Wrap);
        mixed.setAddressVProperty(TextureAddressMode::Clamp);
        bool rejected = false;
        try
        {
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &mixed, nullptr, nullptr);
            spriteBatch_->End();
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        check(rejected, "a mixed AddressU/AddressV SamplerState is rejected rather than half-applied");

        // Matching wrap modes stay supported, and the batch is reusable after the rejection.
        SamplerState wrapBoth;
        wrapBoth.setAddressUProperty(TextureAddressMode::Wrap);
        wrapBoth.setAddressVProperty(TextureAddressMode::Wrap);
        wrapBoth.setFilterProperty(TextureFilter::Point);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &wrapBoth, nullptr, nullptr);
        spriteBatch_->Draw(*texture_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        readBackbuffer();
        checkPixel(backbufferPixel(0, 0), Color(255, 0, 0, 255),
                   "a matching AddressU/AddressV pair is accepted, and the batch works after a rejection");
    }

    void FrameSamplerFilterPoint()
    {
        SamplerState pointClamp;
        pointClamp.setFilterProperty(TextureFilter::Point);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        spriteBatch_->Draw(*texture_, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 2, 2), Color::White);
        spriteBatch_->End();
        readBackbuffer();
        checkPixel(backbufferPixel(3, 1), Color(255, 0, 0, 255),
                   "SetSamplerFilter(Point) keeps a texel-edge pixel unblended, unlike the LinearClamp default");
    }

    // ---------------------------------------------------------------------------------------
    // Shared upper layers
    // ---------------------------------------------------------------------------------------

    void FrameDrawString()
    {
        spriteBatch_->Begin();
        spriteBatch_->DrawString(*font_, "A", Vector2(2, 2), Color::White);
        spriteBatch_->End();
        readBackbuffer();
        checkPixel(backbufferPixel(2, 2), Color(255, 200, 0, 255),
                   "DrawString renders the glyph's own atlas colour at the requested position");
    }

    void FrameTransformMatrix()
    {
        // Begin(transformMatrix) applies AFTER the sprite's own local placement (FNA's contract),
        // so the whole 8x8 quad frame 2 placed at (8,8) lands at (24,24) instead.
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr,
                            nullptr, nullptr, Matrix::CreateTranslation(16.0f, 16.0f, 0.0f));
        spriteBatch_->Draw(*texture_, Rectangle(8, 8, 8, 8), Rectangle(0, 0, 2, 2), Color::White);
        spriteBatch_->End();
        readBackbuffer();
        checkPixel(backbufferPixel(24, 24), Color(255, 0, 0, 255),
                   "Begin(transformMatrix) translation shifts the whole draw, top-left texel unaffected");
        checkPixel(backbufferPixel(31, 31), Color(255, 255, 0, 255),
                   "Begin(transformMatrix) translation shifts the whole draw, bottom-right texel unaffected");
        checkPixel(backbufferPixel(8, 8), kBackground,
                   "the untransformed origin is back to the background -- the draw moved, it was not replicated");
    }

    // ---------------------------------------------------------------------------------------
    // Resource lifetime (PIXIJS-87/92)
    // ---------------------------------------------------------------------------------------

    void FrameTextureLifetime(GraphicsDevice& dev)
    {
        // SpriteBatch releases its texture references inside End(), so a Texture2D may legally be
        // destroyed the moment End() returns -- long before Present. With deferred rasterization
        // the pooled sprite still referenced the destroyed GPU resource at paint time; with
        // commit-on-submit the draw is already in the target and the texture is free to go.
        {
            Texture2D scoped = makeTexture(1, 1, {12, 34, 56, 255});
            drawTexel(scoped, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 4, 4));
        }
        // Also destroy a render target that is still bound: the renderer must fall back to the back
        // buffer rather than keep rendering into a freed framebuffer.
        {
            RenderTarget2D bound(dev, 4, 4);
            dev.SetRenderTarget(&bound);
        }
        dev.SetRenderTarget(nullptr);

        readBackbuffer();
        checkPixel(backbufferPixel(0, 0), Color(12, 34, 56, 255),
                   "a draw survives its source Texture2D being destroyed between End() and the readback");
        checkPixel(backbufferPixel(3, 3), Color(12, 34, 56, 255),
                   "the whole destination rectangle survives that destruction");

        drawTexel(*texture_, Rectangle(0, 0, 1, 1), Rectangle(8, 0, 2, 2));
        readBackbuffer();
        checkPixel(backbufferPixel(8, 0), Color(255, 0, 0, 255),
                   "the renderer keeps drawing to the back buffer after a still-bound render target was destroyed");
    }

    void FrameResize(GraphicsDevice& dev, PixiJsRenderer& renderer)
    {
        // PIXIJS-93: a drawable-size change reported by the platform must reach PixiJS's own
        // renderer, or its projection and GL viewport stay pinned to the size the application was
        // created with and every later draw lands in the wrong rectangle.
        RendererSurfaceInfo resized = renderer.GetSurfaceInfo();
        resized.drawableSize = {96, 48};
        renderer.OnSurfaceChanged(resized);

        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        // x=80 does not exist in the original 64-wide buffer, so this can only pass if the resize
        // genuinely took effect.
        drawTexel(*texture_, Rectangle(0, 0, 1, 1), Rectangle(80, 0, 4, 4));

        std::vector<std::uint8_t> px(4, 0);
        renderer.ReadBackbuffer(80, 0, 1, 1, px.data());
        check(px[0] == 255 && px[1] == 0 && px[2] == 0,
              "after a resize, a draw beyond the old width lands in the enlarged back buffer");
        renderer.ReadBackbuffer(0, 0, 1, 1, px.data());
        check(px[0] == 0 && px[1] == 0 && px[2] == 255,
              "after a resize, Clear() covers the enlarged back buffer");

        RendererSurfaceInfo restored = resized;
        restored.drawableSize = {kBackBuffer, kBackBuffer};
        renderer.OnSurfaceChanged(restored);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        drawTexel(*texture_, Rectangle(1, 0, 1, 1), Rectangle(0, 0, 2, 2));
        readBackbuffer();
        checkPixel(backbufferPixel(0, 0), Color(0, 255, 0, 255),
                   "the renderer keeps drawing correctly after being resized back down");
        (void)dev;
    }

    void FrameRendererRecreation(GraphicsDevice& dev)
    {
        // PIXIJS-92: a second renderer instance shares the one PIXI.Application, and destroying it
        // tears that application (and every JS resource it owns) down. A later operation must then
        // rebuild everything from scratch rather than trip over a half-freed application -- which
        // is exactly what a GraphicsDevice being destroyed and recreated does.
        //
        // Deliberately the last frame that draws: the teardown invalidates every texture created
        // against the destroyed WebGL context, this Game's own included.
        {
            GraphicsRendererCreateArgs args;
            args.surface.windowId = 77;
            args.surface.nativeHandle.system = CNA::Platform::NativeWindowSystem::Web;
            args.surface.drawableSize = {kBackBuffer, kBackBuffer};
            args.surface.displayScale = 1.0f;
            args.virtualWidth = kBackBuffer;
            args.virtualHeight = kBackBuffer;
            args.presentationMode = CnaPresentationMode::FixedHeightDynamicWidth;

            PixiJsRenderer second(args);
            second.Clear(1.0f, 0.0f, 0.0f, 1.0f);
            std::vector<std::uint8_t> px(4, 0);
            second.ReadBackbuffer(0, 0, 1, 1, px.data());
            check(px[0] == 255 && px[1] == 0 && px[2] == 0,
                  "a second PixiJsRenderer instance drives the same application correctly");
        } // second's destructor tears the whole PIXI.Application down.

        // Nothing PixiJS-side survives that teardown; the next operation must rebuild it.
        dev.Clear(Color(0, 255, 0, 255));
        readBackbuffer();
        checkPixel(backbufferPixel(0, 0), Color(0, 255, 0, 255),
                   "after a full renderer teardown, the next operation rebuilds the application and works");

        Texture2D fresh = makeTexture(1, 1, {7, 6, 5, 255});
        drawTexel(fresh, Rectangle(0, 0, 1, 1), Rectangle(4, 4, 2, 2));
        readBackbuffer();
        checkPixel(backbufferPixel(4, 4), Color(7, 6, 5, 255),
                   "a texture created after the teardown uploads and draws against the rebuilt application");
    }

public:
    PixiJsSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackBuffer);
        gdm_->setPreferredBackBufferHeightProperty(kBackBuffer);
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
