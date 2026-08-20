// SPDX-License-Identifier: MS-PL
// plans/plan_opengles1.md OPENGLES1-79: runtime coverage for OPENGLES1-29 (SetViewport/SetScissorRect)
// through the real fixed-function ES 1.1 pipeline, asserted with GetBackBufferData() readback.
//
// Both features are tested the same way: a quad covering the whole of NDC space is drawn, and the
// question is only which backbuffer pixels it is allowed to reach.
//
// Check A -- with a right-half Viewport, a full-NDC quad reaches only the right half...
// Check B -- ...and the left half keeps the cleared background.
// Check C -- restoring the full Viewport lets the same quad cover everything again.
// Check D -- Viewport round-trips through setViewportProperty()/getViewportProperty().
// Check E -- with ScissorTestEnable=true and a right-half scissor rect, only the right half is
//   written...
// Check F -- ...while the left half is left alone.
// Check G -- a scissor rectangle set while ScissorTestEnable=false clips nothing.
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
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

#include <cmath>
#include <cstdio>
#include <memory>

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

    void drawFullScreen(GraphicsDevice& dev, Color col)
    {
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        const Vector3 tl(-1.0f, 1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br(1.0f, -1.0f, 0.0f), tr(1.0f, 1.0f, 0.0f);
        const VertexPositionColor quad[6] = {
            { tl, col }, { bl, col }, { br, col },
            { tl, col }, { br, col }, { tr, col },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
    }
}

class OpenGLES1ViewportScissorTest : public Game
{
    static constexpr int kChecks = 7;

    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
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
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const int mid    = kSize / 2;
        const int leftX  = kSize / 4;        // centre of the left half
        const int rightX = 3 * kSize / 4;    // centre of the right half

        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);

        const Viewport fullVp = dev.getViewportProperty();

        // ---- Checks A/B: a right-half viewport confines the draw ------------------------
        dev.Clear(Color::Black);
        const Viewport rightVp(mid, 0, kSize - mid, kSize);
        dev.setViewportProperty(rightVp);
        drawFullScreen(dev, Color::Red);

        check(colorNear(readPixel(dev, rightX, mid), Color::Red),
              "Viewport(right half) -- a full-NDC quad reaches the right half");
        check(colorNear(readPixel(dev, leftX, mid), Color::Black),
              "Viewport(right half) -- the left half keeps the cleared background");

        // ---- Check C: restoring the full viewport ---------------------------------------
        dev.setViewportProperty(fullVp);
        dev.Clear(Color::Black);
        drawFullScreen(dev, Color::Red);
        check(colorNear(readPixel(dev, leftX, mid), Color::Red) &&
                  colorNear(readPixel(dev, rightX, mid), Color::Red),
              "Viewport(full) restored -- the same quad covers the whole backbuffer again");

        // ---- Check D: round-trip ---------------------------------------------------------
        {
            const Viewport probe(7, 5, 21, 13);
            dev.setViewportProperty(probe);
            const Viewport got = dev.getViewportProperty();
            check(got.getXProperty() == 7 && got.getYProperty() == 5 &&
                      got.getWidthProperty() == 21 && got.getHeightProperty() == 13,
                  "Viewport round-trips through setViewportProperty()/getViewportProperty()");
            dev.setViewportProperty(fullVp);
        }

        // ---- Checks E/F: scissor test on ------------------------------------------------
        RasterizerState scissorOn = RasterizerState::CullNone;
        scissorOn.setScissorTestEnableProperty(true);

        dev.Clear(Color::Black);
        dev.setRasterizerStateProperty(scissorOn);
        dev.setScissorRectangleProperty(Rectangle(mid, 0, kSize - mid, kSize));
        drawFullScreen(dev, Color::Lime);

        check(colorNear(readPixel(dev, rightX, mid), Color::Lime),
              "ScissorTestEnable=true -- the quad is written inside the scissor rectangle");
        check(colorNear(readPixel(dev, leftX, mid), Color::Black),
              "ScissorTestEnable=true -- pixels outside the scissor rectangle are untouched");

        // ---- Check G: scissor rect present but test disabled ----------------------------
        dev.Clear(Color::Black);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);   // ScissorTestEnable=false
        dev.setScissorRectangleProperty(Rectangle(mid, 0, kSize - mid, kSize));
        drawFullScreen(dev, Color::Lime);

        check(colorNear(readPixel(dev, leftX, mid), Color::Lime) &&
                  colorNear(readPixel(dev, rightX, mid), Color::Lime),
              "ScissorTestEnable=false -- a set scissor rectangle clips nothing");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = (passCount_ == kChecks) ? 0 : 1;
        Exit();
    }

public:
    OpenGLES1ViewportScissorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    OpenGLES1ViewportScissorTest game;
    game.Run();
    return game.getResult();
}
