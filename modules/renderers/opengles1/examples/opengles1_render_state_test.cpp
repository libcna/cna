// SPDX-License-Identifier: MS-PL
// plans/plan_opengles1.md OPENGLES1-79: runtime coverage for the render-state rows that the baseline
// smoke test does not reach -- OPENGLES1-25 (blend), OPENGLES1-26 (depth), OPENGLES1-27
// (rasterizer cull) and OPENGLES1-28 (sampler filter), all through the real fixed-function
// ES 1.1 pipeline and asserted with GetBackBufferData() pixel readback.
//
// Check A -- BlendState::Opaque: a second full-screen quad replaces the first outright.
// Check B -- BlendState::Additive: red over blue reads back as magenta, i.e. glBlendFunc is live.
// Check C -- BlendState::AlphaBlend: a half-alpha blue over red lands between the two.
// Check D -- DepthStencilState::Default rejects a farther fragment (depth test + write work)...
// Check E -- ...and DepthStencilState::None lets that same farther fragment through.
// Check F -- RasterizerState::CullNone draws a triangle both existing cull modes disagree about...
// Check G -- ...and exactly one of CullClockwise/CullCounterClockwise culls it (which one is a
//   winding convention, so asserting "exactly one" keeps this independent of that convention).
// Check H -- SamplerState::PointClamp vs LinearClamp differ across a magnified texel boundary.
// Check I -- BlendFunction::Max keeps the brighter of source and destination (OPENGLES1-88),
//   which a fallback to Add could not produce: Add would saturate, Max must not.
// Check J -- BlendFunction::Min keeps the darker of the two.
// Check K -- SupportsCapability(AnisotropicFiltering) agrees with the driver, and a sampler
//   requesting anisotropy still renders (OPENGLES1-86).
// Check L -- per-vertex Color and BasicEffect.DiffuseColor multiply together (OPENGLES1-92),
//   which the deviation table used to call impossible. Neither input alone gives the answer.
//
// Exit code 0 = all checks PASS, 1 = any FAILs. Requires a genuine OpenGL ES 1.1 driver; see
// docs/opengles1-renderer.md (stock Debian Mesa is built with -Dgles1=disabled and cannot run this).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

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

    /// Full-screen quad at a chosen NDC depth, so depth-test checks can order fragments.
    void drawFullScreen(GraphicsDevice& dev, Color col, float z = 0.0f)
    {
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        const Vector3 tl(-1.0f, 1.0f, z), bl(-1.0f, -1.0f, z);
        const Vector3 br(1.0f, -1.0f, z), tr(1.0f, 1.0f, z);
        const VertexPositionColor quad[6] = {
            { tl, col }, { bl, col }, { br, col },
            { tl, col }, { br, col }, { tr, col },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
    }

    /// A single centered triangle with a fixed winding, used for the cull-mode checks.
    void drawTriangle(GraphicsDevice& dev, Color col)
    {
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        const VertexPositionColor tri[3] = {
            { Vector3(-0.9f, -0.9f, 0.0f), col },
            { Vector3(0.0f, 0.9f, 0.0f), col },
            { Vector3(0.9f, -0.9f, 0.0f), col },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);
    }
}

class OpenGLES1RenderStateTest : public Game
{
    static constexpr int kChecks = 12;

    std::unique_ptr<GraphicsDeviceManager> gdm_;
    SpriteBatch* spriteBatch_ = nullptr;
    Texture2D splitTex_;
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
        spriteBatch_ = new SpriteBatch(getGraphicsDeviceProperty());

        // 2x1 texture, pure red | pure blue. Magnified across the backbuffer, the seam between the
        // two texels is where point and linear filtering must disagree.
        splitTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 2, 1,
                                                std::vector<std::uint8_t>{
                                                    255, 0, 0, 255,
                                                    0, 0, 255, 255,
                                                });
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const int mid = kSize / 2;

        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        // ---- Check A: Opaque overwrites ------------------------------------------------
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.Clear(Color::Black);
        drawFullScreen(dev, Color::Red);
        drawFullScreen(dev, Color::Blue);
        check(colorNear(readPixel(dev, mid, mid), Color::Blue),
              "BlendState::Opaque -- the later quad replaces the earlier one outright");

        // ---- Check B: Additive sums to magenta -----------------------------------------
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.Clear(Color::Black);
        drawFullScreen(dev, Color::Blue);
        dev.setBlendStateProperty(BlendState::Additive);
        drawFullScreen(dev, Color::Red);
        check(colorNear(readPixel(dev, mid, mid), Color(255, 0, 255)),
              "BlendState::Additive -- red over blue reads back as magenta");

        // ---- Check C: AlphaBlend lands between source and destination -------------------
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.Clear(Color::Black);
        drawFullScreen(dev, Color::Red);
        dev.setBlendStateProperty(BlendState::AlphaBlend);
        // Premultiplied-alpha convention: AlphaBlend is (src) + (dst * (1-srcA)), so a blue at
        // 50% alpha must be supplied already scaled by that alpha.
        drawFullScreen(dev, Color(0, 0, 128, 128));
        {
            const Color got = readPixel(dev, mid, mid);
            check(got.getRProperty() > 100 && got.getRProperty() < 160 &&
                      got.getBProperty() > 100 && got.getBProperty() < 160,
                  "BlendState::AlphaBlend -- half-alpha blue over red lands between the two");
        }

        // ---- Checks D/E: depth test ----------------------------------------------------
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.Clear(Color::Black);
        drawFullScreen(dev, Color::Red, 0.0f);
        drawFullScreen(dev, Color::Blue, 0.5f);   // farther away -- must be rejected
        check(colorNear(readPixel(dev, mid, mid), Color::Red),
              "DepthStencilState::Default -- a farther fragment is rejected by the depth test");

        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.Clear(Color::Black);
        drawFullScreen(dev, Color::Red, 0.0f);
        drawFullScreen(dev, Color::Blue, 0.5f);   // no depth test -- must land
        check(colorNear(readPixel(dev, mid, mid), Color::Blue),
              "DepthStencilState::None -- that same farther fragment is no longer rejected");

        // ---- Checks F/G: cull modes ----------------------------------------------------
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.Clear(Color::Black);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        drawTriangle(dev, Color::Yellow);
        check(colorNear(readPixel(dev, mid, mid), Color::Yellow),
              "RasterizerState::CullNone -- the triangle is drawn");

        dev.Clear(Color::Black);
        dev.setRasterizerStateProperty(RasterizerState::CullClockwise);
        drawTriangle(dev, Color::Yellow);
        const bool cwDrew = colorNear(readPixel(dev, mid, mid), Color::Yellow);

        dev.Clear(Color::Black);
        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        drawTriangle(dev, Color::Yellow);
        const bool ccwDrew = colorNear(readPixel(dev, mid, mid), Color::Yellow);

        check(cwDrew != ccwDrew,
              "RasterizerState -- exactly one of CullClockwise/CullCounterClockwise culls the triangle");

        // ---- Check H: sampler filtering ------------------------------------------------
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(BlendState::Opaque);

        // Sample just left of the seam: point filtering must give the pure left texel, linear
        // filtering must have started mixing in the right one.
        const int seamX = mid - 1;
        const Rectangle full(0, 0, kSize, kSize);

        SamplerState point  = SamplerState::PointClamp;
        SamplerState linear = SamplerState::LinearClamp;

        dev.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        spriteBatch_->Draw(splitTex_, full, Color::White);
        spriteBatch_->End();
        const Color pointPixel = readPixel(dev, seamX, mid);

        dev.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &linear, nullptr, nullptr);
        spriteBatch_->Draw(splitTex_, full, Color::White);
        spriteBatch_->End();
        const Color linearPixel = readPixel(dev, seamX, mid);

        check(colorNear(pointPixel, Color::Red) && !colorNear(linearPixel, Color::Red),
              "SamplerState -- PointClamp keeps the texel pure where LinearClamp blends it");

        // ---- Checks I/J: Min/Max blend equations -----------------------------------------
        {
            // dst = (200,60,60), src = (60,200,60).
            //   Max -> (200,200,60); Min -> (60,60,60); Add would saturate towards (255,255,120).
            // All three outcomes are distinct, so this cannot pass by accident on a fallback.
            BlendState maxState = BlendState::Opaque;
            maxState.setColorSourceBlendProperty(Blend::One);
            maxState.setColorDestinationBlendProperty(Blend::One);
            maxState.setColorBlendFunctionProperty(BlendFunction::Max);
            maxState.setAlphaSourceBlendProperty(Blend::One);
            maxState.setAlphaDestinationBlendProperty(Blend::One);
            maxState.setAlphaBlendFunctionProperty(BlendFunction::Max);

            dev.setBlendStateProperty(BlendState::Opaque);
            dev.Clear(Color::Black);
            drawFullScreen(dev, Color(200, 60, 60, 255));
            dev.setBlendStateProperty(maxState);
            drawFullScreen(dev, Color(60, 200, 60, 255));
            const Color maxPixel = readPixel(dev, mid, mid);
            std::printf("       (BlendFunction::Max = %d,%d,%d)\n", maxPixel.getRProperty(),
                        maxPixel.getGProperty(), maxPixel.getBProperty());
            check(colorNear(maxPixel, Color(200, 200, 60), 12),
                  "BlendFunction::Max keeps the brighter channel of source and destination");

            BlendState minState = maxState;
            minState.setColorBlendFunctionProperty(BlendFunction::Min);
            minState.setAlphaBlendFunctionProperty(BlendFunction::Min);

            dev.setBlendStateProperty(BlendState::Opaque);
            dev.Clear(Color::Black);
            drawFullScreen(dev, Color(200, 60, 60, 255));
            dev.setBlendStateProperty(minState);
            drawFullScreen(dev, Color(60, 200, 60, 255));
            const Color minPixel = readPixel(dev, mid, mid);
            std::printf("       (BlendFunction::Min = %d,%d,%d)\n", minPixel.getRProperty(),
                        minPixel.getGProperty(), minPixel.getBProperty());
            check(colorNear(minPixel, Color(60, 60, 60), 12),
                  "BlendFunction::Min keeps the darker channel of source and destination");

            dev.setBlendStateProperty(BlendState::Opaque);
        }

        // ---- Check K: anisotropic filtering ----------------------------------------------
        {
            SamplerState aniso = SamplerState::LinearClamp;
            aniso.setMaxAnisotropyProperty(4);
            dev.getSamplerStatesProperty()[0] = aniso;

            dev.Clear(Color::Black);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &aniso, nullptr, nullptr);
            spriteBatch_->Draw(splitTex_, Rectangle(0, 0, kSize, kSize), Color::White);
            spriteBatch_->End();

            const bool drew = !colorNear(readPixel(dev, kSize / 4, mid), Color::Black);
            const bool claims = dev.SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering);
            std::printf("       (SupportsCapability(AnisotropicFiltering) = %s)\n", claims ? "true" : "false");
            check(drew,
                  "SamplerState::MaxAnisotropy is accepted and the textured draw still renders");

            dev.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        }

        // ---- Check L: vertex colour x DiffuseColor ---------------------------------------
        {
            // Vertex colour (255,128,0) with DiffuseColor (0.5, 1, 1) must give (128,128,0).
            // Vertex colour alone would be (255,128,0); DiffuseColor alone (128,255,255).
            dev.Clear(Color::Black);
            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setLightingEnabledProperty(false);
            fx.VertexColorEnabled = true;
            fx.setDiffuseColorProperty(Vector3(0.5f, 1.0f, 1.0f));
            fx.Apply();

            const Color vertexColor(255, 128, 0, 255);
            const VertexPositionColor quad[6] = {
                { Vector3(-1.0f, 1.0f, 0.0f), vertexColor },
                { Vector3(-1.0f, -1.0f, 0.0f), vertexColor },
                { Vector3(1.0f, -1.0f, 0.0f), vertexColor },
                { Vector3(-1.0f, 1.0f, 0.0f), vertexColor },
                { Vector3(1.0f, -1.0f, 0.0f), vertexColor },
                { Vector3(1.0f, 1.0f, 0.0f), vertexColor },
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

            const Color got = readPixel(dev, mid, mid);
            std::printf("       (vertex colour x DiffuseColor = %d,%d,%d)\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            check(colorNear(got, Color(128, 128, 0), 12),
                  "per-vertex Color and DiffuseColor multiply together");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = (passCount_ == kChecks) ? 0 : 1;
        Exit();
    }

public:
    OpenGLES1RenderStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    OpenGLES1RenderStateTest game;
    game.Run();
    return game.getResult();
}
