// SPDX-License-Identifier: MS-PL
// Task 762: verify DepthStencilState.StencilFail/StencilDepthBufferFail/StencilPass (the three
// front-facing StencilOperation slots) each perform their own distinct operation on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_depthstencilstate_stencil_ops_test.cpp (Task 317,
// already reused verbatim on Vulkan). Not a verbatim reuse: that file reads back 4
// spatially-separate columns from a SINGLE rendered frame, but Bgfx's own GetBackBufferData only
// reliably reflects the FIRST read per rendered frame (Task 406 finding) -- restructured into one
// separately-read RunCheck() pass per column, mirroring Task 759-761's established Bgfx pattern.
//
// Method per check (identical logic to Task 317's per-column method, just full-screen instead of
// a column, since every check now gets its own frame):
//   1. Stamp: draw at depth 0.5 with DepthBufferFunction=Always (always writes depth) and
//      StencilFunction=Always/StencilPass=Replace/ReferenceStencil=0x05 -> known depth+stencil
//      baseline (buffer=0x05).
//   2. Operation draw: a second quad whose DepthStencilState is set up so ONLY the operation slot
//      under test can fire; the other two slots are set to Decrement as a trap.
//   3. Read-back: a GREEN quad with depth testing disabled, StencilFunction=Equal,
//      ReferenceStencil=0x06, StencilPass=StencilFail=Keep (read-only) -> GREEN only if the
//      correct slot fired Increment (0x05 -> 0x06); anything else leaves BACKGROUND.
//
// Checks 0-2 (StencilFail / StencilDepthBufferFail / StencilPass): each expects GREEN.
// Check 3 (contrast/control, CRITICAL for this test's validity): identical operation to check 2
//   (StencilPass fires, buffer -> 0x06), but the read-back quad deliberately queries
//   ReferenceStencil=0x99 (guaranteed mismatch) -- a genuinely working stencil test must REJECT
//   this (0x06 != 0x99) -> BACKGROUND. Without this check, checks 0-2 alone cannot distinguish
//   "the ops work correctly" from "the stencil test is bypassed entirely and every fragment always
//   passes" -- both scenarios would show GREEN on all 3. This is the one check where a WORKING
//   implementation must show BACKGROUND, not GREEN.
//
// Exit code 0 = all 4 checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <functional>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

namespace
{
    const Color kBackground(20, 20, 20, 255);
    const Color kGreen(0, 255, 0, 255);

    void DrawQuad(GraphicsDevice& dev, float z, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3(-1.0f, -1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3( 1.0f,  1.0f, z), color },
        };
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real
        // default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    DepthStencilState MakeStampState()
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(true);
        ds.setDepthBufferWriteEnableProperty(true);
        ds.setDepthBufferFunctionProperty(CompareFunction::Always);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Always);
        ds.setStencilPassProperty(StencilOperation::Replace);
        ds.setReferenceStencilProperty(0x05);
        return ds;
    }

    DepthStencilState MakeOpState(bool stencilPasses, CompareFunction depthFunc,
                                   StencilOperation onFail, StencilOperation onDepthFail,
                                   StencilOperation onPass)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(true);
        ds.setDepthBufferWriteEnableProperty(false);
        ds.setDepthBufferFunctionProperty(depthFunc);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setReferenceStencilProperty(stencilPasses ? 0x05 : 0x99);
        ds.setStencilFailProperty(onFail);
        ds.setStencilDepthBufferFailProperty(onDepthFail);
        ds.setStencilPassProperty(onPass);
        return ds;
    }

    DepthStencilState MakeReadBackState(int referenceStencil)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setReferenceStencilProperty(referenceStencil);
        ds.setStencilPassProperty(StencilOperation::Keep);
        ds.setStencilFailProperty(StencilOperation::Keep);
        return ds;
    }
}

class BgfxDepthStencilStateStencilOpsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    static bool IsGreen(const Color& c)
    {
        return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
    }

    Color RunCheck(GraphicsDevice& dev, const std::function<void(GraphicsDevice&)>& drawFn)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kBackground, 1.0f, 0);
            dev.setBlendStateProperty(BlendState::Opaque);

            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;
            fx.Apply();

            drawFn(dev);

            const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
            dev.GetBackBufferData(&reg, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;  // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding); this test reads once per frame.
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        struct Check { const char* name; std::function<void(GraphicsDevice&)> drawFn; bool expectGreen; };

        const Check checks[4] = {
            { "StencilFail (Increment)", [&](GraphicsDevice& d) {
                  d.setDepthStencilStateProperty(MakeStampState());
                  DrawQuad(d, 0.5f, kBackground);
                  d.setDepthStencilStateProperty(MakeOpState(
                      /*stencilPasses=*/false, CompareFunction::Always,
                      /*onFail=*/StencilOperation::Increment,
                      /*onDepthFail=*/StencilOperation::Decrement,
                      /*onPass=*/StencilOperation::Decrement));
                  DrawQuad(d, 0.5f, kBackground);
                  d.setDepthStencilStateProperty(MakeReadBackState(0x06));
                  DrawQuad(d, 0.5f, kGreen);
              }, true },
            { "StencilDepthBufferFail (Increment)", [&](GraphicsDevice& d) {
                  d.setDepthStencilStateProperty(MakeStampState());
                  DrawQuad(d, 0.5f, kBackground);
                  d.setDepthStencilStateProperty(MakeOpState(
                      /*stencilPasses=*/true, CompareFunction::Less,
                      /*onFail=*/StencilOperation::Decrement,
                      /*onDepthFail=*/StencilOperation::Increment,
                      /*onPass=*/StencilOperation::Decrement));
                  DrawQuad(d, 0.8f, kBackground);
                  d.setDepthStencilStateProperty(MakeReadBackState(0x06));
                  DrawQuad(d, 0.5f, kGreen);
              }, true },
            { "StencilPass (Increment)", [&](GraphicsDevice& d) {
                  d.setDepthStencilStateProperty(MakeStampState());
                  DrawQuad(d, 0.5f, kBackground);
                  d.setDepthStencilStateProperty(MakeOpState(
                      /*stencilPasses=*/true, CompareFunction::Less,
                      /*onFail=*/StencilOperation::Decrement,
                      /*onDepthFail=*/StencilOperation::Decrement,
                      /*onPass=*/StencilOperation::Increment));
                  DrawQuad(d, 0.2f, kBackground);
                  d.setDepthStencilStateProperty(MakeReadBackState(0x06));
                  DrawQuad(d, 0.5f, kGreen);
              }, true },
            { "Contrast: wrong ReferenceStencil (must reject)", [&](GraphicsDevice& d) {
                  d.setDepthStencilStateProperty(MakeStampState());
                  DrawQuad(d, 0.5f, kBackground);
                  d.setDepthStencilStateProperty(MakeOpState(
                      /*stencilPasses=*/true, CompareFunction::Less,
                      /*onFail=*/StencilOperation::Decrement,
                      /*onDepthFail=*/StencilOperation::Decrement,
                      /*onPass=*/StencilOperation::Increment));
                  DrawQuad(d, 0.2f, kBackground);
                  d.setDepthStencilStateProperty(MakeReadBackState(0x99));
                  DrawQuad(d, 0.5f, kGreen);
              }, false },
        };

        int passCount = 0;
        for (const auto& check : checks)
        {
            const Color c = RunCheck(dev, check.drawFn);
            const bool sawGreen = IsGreen(c);
            const bool ok = check.expectGreen ? sawGreen : !sawGreen;
            std::printf("[%s] %s: centre=(%d,%d,%d), expected %s\n",
                        ok ? "PASS" : "FAIL", check.name,
                        c.getRProperty(), c.getGProperty(), c.getBProperty(),
                        check.expectGreen ? "GREEN" : "BACKGROUND");
            if (ok) ++passCount;
        }

        std::printf("=== %d/4 PASS ===\n", passCount);
        result_ = (passCount == 4) ? 0 : 1;
        Exit();
    }

public:
    BgfxDepthStencilStateStencilOpsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxDepthStencilStateStencilOpsTest game;
    game.Run();
    return game.getResult();
}
