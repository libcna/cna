// SPDX-License-Identifier: MS-PL
// Task 759 (Phase 72 Bgfx gap closure): verify DepthStencilState.DepthBufferWriteEnable actually
// gates whether a draw's depth values are recorded in the depth buffer, on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_depthstencilstate_write_enable_test.cpp (Task 313,
// already reused verbatim on Vulkan). Not a verbatim reuse: that file reads back 2 spatially-
// separate regions (regA, regB) in the SAME rendered frame, but Bgfx's own GetBackBufferData only
// reliably reflects the FIRST read call per rendered frame (Task 406 finding) -- a second read in
// the same frame can silently return stale data, which would make Check B's own result meaningless
// as a sanity check. Restructured into 2 separate frames (one per check), each with exactly one
// read, using this project's established retry-until-settled idiom for Bgfx pixel tests.
//
// Method (three-quad, differential two-check test): draw a far quad A (red, depth 0.8) that
// writes depth, then a near quad B (green, depth 0.2) whose write-enable flag is the variable
// under test, then a THIRD quad C (blue) at an IN-BETWEEN depth (0.5, strictly nearer than A's
// 0.8 and strictly farther than B's 0.2) with normal DepthStencilState::Default (depth test on,
// writes on). C's fate reveals what value the depth buffer actually holds after B:
//   - If B's write was truly skipped, the buffer still holds A's 0.8. C's compare is
//     0.5 < 0.8 -> PASSES -> centre ends BLUE.
//   - If B's write happened anyway (bug: write-disable ignored), the buffer holds B's 0.2. C's
//     compare is 0.5 < 0.2 -> FAILS -> centre stays GREEN (B's colour, drawn on top of A).
//
// Check A (frame 1): B drawn with DepthBufferWriteEnable=false -> expect BLUE (C passes).
// Check B (frame 2): B drawn with DepthStencilState::Default (writes ON) -> expect GREEN
//   (C is correctly rejected) -- this is the sanity check proving the depth test itself works
//   and that Check A's BLUE result is really about the write flag, not a broken compare.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

namespace
{
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
        // Task 364/896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState on Bgfx specifically — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class BgfxDepthStencilStateWriteEnableTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    static const Color& Red()   { static const Color c(255, 0, 0, 255); return c; }
    static const Color& Green() { static const Color c(0, 255, 0, 255); return c; }
    static const Color& Blue()  { static const Color c(0, 0, 255, 255); return c; }

    Color RunCheck(GraphicsDevice& dev, bool bWritesEnabled)
    {
        DepthStencilState bState;
        bState.setDepthBufferEnableProperty(true);
        bState.setDepthBufferWriteEnableProperty(bWritesEnabled);
        bState.setDepthBufferFunctionProperty(CompareFunction::LessEqual);

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(0, 0, 0, 255), 1.0f, 0);
            dev.setBlendStateProperty(BlendState::Opaque);

            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;
            fx.Apply();

            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            DrawQuad(dev, 0.8f, Red());
            dev.setDepthStencilStateProperty(bState);
            DrawQuad(dev, 0.2f, Green());
            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            DrawQuad(dev, 0.5f, Blue());

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

        const Color a = RunCheck(dev, /*bWritesEnabled=*/false);
        const Color b = RunCheck(dev, /*bWritesEnabled=*/true);

        const bool aOk = a.getBProperty() >= 200 && a.getRProperty() <= 60 && a.getGProperty() <= 60;
        const bool bOk = b.getGProperty() >= 200 && b.getRProperty() <= 60 && b.getBProperty() <= 60;

        std::printf("[%s] WriteEnable=false: centre=(%d,%d,%d), expected BLUE\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        std::printf("[%s] WriteEnable=true (sanity): centre=(%d,%d,%d), expected GREEN\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        if (!aOk)
        {
            std::printf("[INFO] A mismatch here means DepthBufferWriteEnable=false did not\n"
                        "       actually skip recording the depth value.\n");
        }

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    BgfxDepthStencilStateWriteEnableTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxDepthStencilStateWriteEnableTest game;
    game.Run();
    return game.getResult();
}
