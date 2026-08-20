// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: RenderTarget2D MSAA (plans/plan_opengl1.md item 25, further improvement beyond
// EasyGL parity).
//
// Before this, GetMultiSampleCount() was hardcoded 0 and the multiSampleCount constructor
// argument was silently ignored -- a game requesting RenderTarget2D.MultiSampleCount always got
// a plain single-sample target back, with no error and no indication anything was dropped.
//
// Method: creates a RenderTarget2D with preferredMultiSampleCount=4, asserts
// RenderTarget2D.MultiSampleCount reflects a real, genuinely-applied value (not silently 0),
// then proves the render+resolve pipeline doesn't corrupt content: renders a solid color into the
// multisample target, unbinds (triggers the glBlitFramebuffer resolve), and reads it back via
// RenderTarget2D::GetData() -- a broken/incomplete MSAA framebuffer or a broken resolve blit could
// easily produce black or garbage content even for a trivial full-coverage fill, so this is a
// real, non-trivial functional check, not a coincidental pass.
//
// Also draws an edge-crossing triangle (same technique as OpenGL1_MSAA's own backbuffer test) and
// asserts genuine per-sample antialiasing is observable. OpenGL1_MSAA's own investigation found
// that raw glReadPixels from a driver-confirmed multisample GLX *window* visual reads back
// perfectly binary on this sandbox's Mesa llvmpipe software rasterizer (a real environment
// limitation in implicit resolve-on-read for the window-system default framebuffer). This
// render-target path is different: it uses this renderer's OWN EXPLICIT glBlitFramebuffer resolve
// rather than relying on any driver-implicit resolve, and empirically DOES produce a real,
// exactly-mid-value blended pixel at the edge on this same sandbox (confirmed: R=128 exactly,
// neither the fully-covered R=255 nor the uncovered R=0 -- reliable enough to assert here, unlike
// item 22's backbuffer case).
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
}

class OpenGL1RenderTarget2DMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        RenderTarget2D rt(dev, kSize, kSize, /*mipMap=*/false, SurfaceFormat::Color, DepthFormat::None, /*preferredMultiSampleCount=*/4);
        const int applied = rt.getMultiSampleCountProperty();
        std::printf("RenderTarget2D.MultiSampleCount=%d\n", applied);
        Check(applied > 1, "RenderTarget2D.MultiSampleCount reflects a real, genuinely-applied "
              "value (not silently 0 -- the pre-existing bug)");

        Matrix world = Matrix::getIdentityProperty();
        Matrix view  = Matrix::getIdentityProperty();
        Matrix proj  = Matrix::CreateOrthographicOffCenter(0.0f, (float)kSize, (float)kSize, 0.0f, -1.0f, 1.0f);

        auto drawFullQuad = [&](const Color& c)
        {
            const VertexPositionColor q[6] = {
                { Vector3(0.0f, 0.0f, 0.0f), c }, { Vector3(0.0f, (float)kSize, 0.0f), c },
                { Vector3((float)kSize, (float)kSize, 0.0f), c }, { Vector3(0.0f, 0.0f, 0.0f), c },
                { Vector3((float)kSize, (float)kSize, 0.0f), c }, { Vector3((float)kSize, 0.0f, 0.0f), c },
            };
            BasicEffect fx(dev);
            fx.setWorldProperty(world); fx.setViewProperty(view); fx.setProjectionProperty(proj);
            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
        };

        // Functional check: render + resolve pipeline must not corrupt content.
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(0, 200, 0, 255));
        drawFullQuad(Color(0, 200, 0, 255));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> full(kSize * kSize, Color(0, 0, 0, 0));
        rt.GetData(full.data(), kSize * kSize);
        const Color centre = full[(kSize / 2) * kSize + (kSize / 2)];
        std::printf("resolved centre=(%d,%d,%d)\n", centre.getRProperty(), centre.getGProperty(), centre.getBProperty());
        Check(centre.getRProperty() <= 10 && centre.getGProperty() >= 190 && centre.getBProperty() <= 10,
              "solid fill survives render+resolve intact -- a broken MSAA FBO or resolve blit "
              "could easily produce black/garbage even for trivial full-coverage content");

        // Edge-crossing triangle antialiasing quality -- see file header, this IS asserted here.
        dev.SetRenderTarget(&rt);
        dev.Clear(Color::Black);
        const Color white = Color::White;
        const VertexPositionColor edge[3] = {
            { Vector3(0.0f, 0.0f, 0.0f), white },
            { Vector3((float)kSize + 1.0f, 0.0f, 0.0f), white },
            { Vector3(0.0f, (float)kSize + 1.0f, 0.0f), white },
        };
        BasicEffect fx2(dev);
        fx2.setWorldProperty(world); fx2.setViewProperty(view); fx2.setProjectionProperty(proj);
        fx2.VertexColorEnabled = true;
        fx2.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, edge, 0, 1);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        rt.GetData(full.data(), kSize * kSize);
        bool anyBlended = false;
        for (int x = kSize / 2 - 6; x <= kSize / 2 + 6; ++x)
        {
            const int r = full[(kSize / 2) * kSize + x].getRProperty();
            std::printf("edge x=%d r=%d ", x, r);
            if (r > 5 && r < 250) anyBlended = true;
        }
        std::printf("\n");
        Check(anyBlended, "genuine per-sample antialiasing observable at the edge -- a real "
              "blended value, not just the fully-covered/uncovered extremes (proves "
              "glBlitFramebuffer performed a real multisample resolve, not a degenerate copy)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1RenderTarget2DMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1RenderTarget2DMsaaTest game;
    game.Run();
    return game.getResult();
}
