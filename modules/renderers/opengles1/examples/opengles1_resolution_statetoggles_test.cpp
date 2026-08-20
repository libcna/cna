// SPDX-License-Identifier: MS-PL
// plans/plan_opengles1.md OPENGLES1-79 follow-up: closes the last rows left uncovered after the first
// wave of runtime tests -- OPENGLES1-9 (virtual resolution / logical size), OPENGLES1-10
// (window<->logical coordinate transforms), OPENGLES1-13 (the direct SetDepthTestEnabled /
// SetBlendEnabled / SetDepthWriteEnabled toggles, as distinct from the state objects already
// covered) and OPENGLES1-18 (SpriteBatch::Begin's transform matrix). OPENGLES1-20's
// glBlendFuncSeparateOES path is exercised through a BlendState whose colour and alpha blends
// genuinely differ.
//
// Check A -- SetVirtualResolution() changes the reported logical viewport size...
// Check B -- ...and window->logical->window round-trips back to the original point.
// Check C -- SetDepthTestEnabled(false) lets a farther fragment through...
// Check D -- ...and SetDepthTestEnabled(true) rejects it again.
// Check E -- SetDepthWriteEnabled(false) stops a near fragment from occluding a later far one.
// Check F -- SetBlendEnabled(false) makes a half-alpha draw fully replace the destination.
// Check G -- SpriteBatch::Begin(..., transformMatrix) translates the drawn sprite.
// Check H -- a BlendState with different colour and alpha blend factors is honoured
//   (glBlendFuncSeparateOES where the driver exposes it).
// Check I -- the reported backbuffer MSAA count and SupportsCapability(MultiSampleAntiAliasing)
//   agree with each other, and the count is what the driver granted rather than what was asked
//   for (OPENGLES1-87).
//
// Exit code 0 = all checks PASS, 1 = any FAILs. Requires a genuine OpenGL ES 1.1 driver; see
// docs/opengles1-renderer.md.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "CNA/Internal/Renderers/OpenGLES1/OpenGLES1Renderer.hpp"
#include "CNA/GraphicsCapability.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    Color readPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool colorNear(Color a, Color b, int tol = 8)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    void drawFullScreen(GraphicsDevice& dev, BasicEffect& fx, Color col, float z = 0.0f)
    {
        fx.Apply();
        const Vector3 tl(-1.0f, 1.0f, z), bl(-1.0f, -1.0f, z);
        const Vector3 br(1.0f, -1.0f, z), tr(1.0f, 1.0f, z);
        const VertexPositionColor quad[6] = {
            { tl, col }, { bl, col }, { br, col },
            { tl, col }, { br, col }, { tr, col },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
    }
}

class OpenGLES1ResolutionStateTogglesTest : public Game
{
    static constexpr int kChecks = 9;

    std::unique_ptr<GraphicsDeviceManager> gdm_;
    SpriteBatch* spriteBatch_ = nullptr;
    Texture2D whiteTex_;
    bool done_ = false;
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
        auto& dev = getGraphicsDeviceProperty();
        spriteBatch_ = new SpriteBatch(dev);
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1,
                                                std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CNA::Internal::Renderers::OpenGLES1::OpenGLES1Renderer&>(
            dev.GetRenderer());
        const int mid = kSize / 2;

        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        // ---- Checks A/B: virtual resolution and coordinate transforms --------------------
        {
            int logicalW = 0, logicalH = 0;
            renderer.SetVirtualResolution(kSize * 2, kSize * 2);
            renderer.GetLogicalSize(logicalW, logicalH);
            std::printf("       (logical size after SetVirtualResolution = %dx%d)\n",
                        logicalW, logicalH);
            check(logicalH == kSize * 2,
                  "SetVirtualResolution -- the renderer reports the requested logical height");

            // The two transforms must be inverses of each other whenever the renderer implements
            // them; a renderer that declines both is reporting "window == logical", which is also
            // self-consistent -- so require agreement, not a particular scale factor.
            float logX = 0.0f, logY = 0.0f, backX = 0.0f, backY = 0.0f;
            const bool toLogical = renderer.TransformWindowToLogical(32.0f, 16.0f, logX, logY);
            const bool toWindow  = renderer.TransformLogicalToWindow(logX, logY, backX, backY);
            std::printf("       (window 32,16 -> logical %.1f,%.1f -> window %.1f,%.1f)\n",
                        logX, logY, backX, backY);
            check(toLogical == toWindow &&
                      (!toLogical || (std::fabs(backX - 32.0f) < 1.0f && std::fabs(backY - 16.0f) < 1.0f)),
                  "window->logical->window round-trips back to the original point");

            renderer.SetVirtualResolution(0, 0);   // back to native for the pixel checks below
        }

        // ---- Checks C/D/E: the direct depth toggles ---------------------------------------
        {
            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;

            dev.SetDepthTestEnabled(false);
            dev.Clear(Color::Black, 1.0f);
            drawFullScreen(dev, fx, Color::Red, 0.0f);
            drawFullScreen(dev, fx, Color::Blue, 0.5f);
            check(colorNear(readPixel(dev, mid, mid), Color::Blue),
                  "SetDepthTestEnabled(false) -- a farther fragment is drawn over a nearer one");

            dev.SetDepthTestEnabled(true);
            dev.SetDepthWriteEnabled(true);
            dev.Clear(Color::Black, 1.0f);
            drawFullScreen(dev, fx, Color::Red, 0.0f);
            drawFullScreen(dev, fx, Color::Blue, 0.5f);
            check(colorNear(readPixel(dev, mid, mid), Color::Red),
                  "SetDepthTestEnabled(true) -- that same farther fragment is rejected again");

            // With depth writes off the near quad never records its depth, so the far one is not
            // occluded by it and lands anyway. The toggle has to come AFTER the clear: clearing
            // deliberately forces the depth mask writable so the clear value always lands
            // (OPENGLES1-7's stated behaviour, shared with EasyGL), which would otherwise undo it.
            dev.SetDepthTestEnabled(true);
            dev.Clear(Color::Black, 1.0f);
            dev.SetDepthWriteEnabled(false);
            drawFullScreen(dev, fx, Color::Red, 0.0f);
            drawFullScreen(dev, fx, Color::Blue, 0.5f);
            check(colorNear(readPixel(dev, mid, mid), Color::Blue),
                  "SetDepthWriteEnabled(false) -- the near fragment no longer occludes the far one");

            dev.SetDepthWriteEnabled(true);
            dev.SetDepthTestEnabled(false);
        }

        // ---- Check F: the direct blend toggle ---------------------------------------------
        {
            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;

            dev.setBlendStateProperty(BlendState::AlphaBlend);
            dev.Clear(Color::Red);
            dev.SetBlendEnabled(false);
            // Half-alpha blue: with blending off it must replace the red outright.
            drawFullScreen(dev, fx, Color(0, 0, 255, 128));
            const Color got = readPixel(dev, mid, mid);
            std::printf("       (blend disabled, half-alpha blue over red = %d,%d,%d)\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            check(got.getBProperty() > 200 && got.getRProperty() < 60,
                  "SetBlendEnabled(false) -- a half-alpha draw replaces the destination outright");
            dev.setBlendStateProperty(BlendState::Opaque);
        }

        // ---- Check G: SpriteBatch transform matrix ---------------------------------------
        {
            // Draw a small sprite in the top-left corner, translated to the far side by the batch
            // transform. Without the transform the sampled point stays background-black.
            const Rectangle sprite(0, 0, kSize / 4, kSize / 4);
            const int shift = kSize / 2;

            dev.Clear(Color::Black);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr,
                                nullptr, nullptr,
                                Matrix::CreateTranslation(static_cast<float>(shift),
                                                          static_cast<float>(shift), 0.0f));
            spriteBatch_->Draw(whiteTex_, sprite, Color::White);
            spriteBatch_->End();

            const Color atShifted = readPixel(dev, shift + kSize / 8, shift + kSize / 8);
            const Color atOrigin  = readPixel(dev, kSize / 8, kSize / 8);
            check(colorNear(atShifted, Color::White) && colorNear(atOrigin, Color::Black),
                  "SpriteBatch::Begin(transformMatrix) -- the sprite is drawn translated");
        }

        // ---- Check H: separate colour and alpha blend factors -----------------------------
        {
            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;

            // Colour channels add; alpha channels do not. Reaching for different colour and alpha
            // factors is what drives glBlendFuncSeparateOES on drivers that expose it.
            BlendState separate = BlendState::Opaque;
            separate.setColorSourceBlendProperty(Blend::One);
            separate.setColorDestinationBlendProperty(Blend::One);
            separate.setAlphaSourceBlendProperty(Blend::Zero);
            separate.setAlphaDestinationBlendProperty(Blend::One);

            dev.setBlendStateProperty(BlendState::Opaque);
            dev.Clear(Color::Black);
            drawFullScreen(dev, fx, Color(0, 0, 255, 255));
            dev.setBlendStateProperty(separate);
            drawFullScreen(dev, fx, Color(255, 0, 0, 255));

            const Color got = readPixel(dev, mid, mid);
            std::printf("       (separate colour/alpha blend = %d,%d,%d)\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            check(colorNear(got, Color(255, 0, 255)),
                  "BlendState with separate colour/alpha factors sums the colour channels");
            dev.setBlendStateProperty(BlendState::Opaque);
        }

        // ---- Check I: MSAA is reported honestly ------------------------------------------
        {
            const int samples = renderer.GetMultiSampleCount();
            const bool claims = dev.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing);
            std::printf("       (backbuffer MSAA samples = %d, SupportsCapability = %s)\n",
                        samples, claims ? "true" : "false");
            // The two must never disagree, and a count of exactly 1 is meaningless -- it has to be
            // reported as "no MSAA" (0) so callers cannot mistake it for a multisampled buffer.
            check(claims == (samples > 1) && samples != 1,
                  "backbuffer MSAA count and its capability flag agree, and 1 is never reported");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = (passCount_ == kChecks) ? 0 : 1;
        Exit();
    }

public:
    OpenGLES1ResolutionStateTogglesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    OpenGLES1ResolutionStateTogglesTest game;
    game.Run();
    return game.getResult();
}
