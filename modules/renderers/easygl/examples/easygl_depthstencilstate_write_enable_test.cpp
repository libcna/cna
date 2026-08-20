// SPDX-License-Identifier: MS-PL
// Task 313: verify DepthStencilState.DepthBufferWriteEnable actually gates whether a draw's
// depth values are recorded in the depth buffer.
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
// C is deliberately NOT drawn at the same depth as A: this project's Vulkan renderer hardcodes its
// depth-compare op per pipeline (ignoring DepthStencilState.DepthBufferFunction entirely -- a
// separate, already-tracked-shape bug, see plans/plan_graphics.md), and at least one of those hardcoded
// ops is strict VK_COMPARE_OP_LESS rather than FNA's actual LessEqual default -- an equal-depth C
// would then fail regardless of B's write behaviour, making the test unable to distinguish the two
// cases. An in-between depth is unambiguous under either a strict or inclusive compare.
//
// Check A (left half): B drawn with DepthBufferWriteEnable=false -> expect BLUE (C passes).
// Check B (right half): B drawn with DepthStencilState::Default (writes ON) -> expect GREEN
//   (C is correctly rejected) -- this is the sanity check proving the depth test itself works
//   and that Check A's BLUE result is really about the write flag, not a broken compare.
// Both checks share one Clear() (the two halves don't spatially overlap) and one frame.
//
// NOTE on Z range: with an identity World/View/Projection (as used here), the vertex Z value
// passed to BasicEffect becomes the clip-space Z directly. XNA/DirectX (and this project's Vulkan
// renderer, matching it -- see colored3d.vert.glsl) use a [0, +w] clip-space Z range, NOT OpenGL's
// [-1, +1] -- a negative Z is behind the near plane and gets clipped away entirely on Vulkan
// (silently, no error), which does NOT happen on EasyGL/OpenGL's more permissive convention. All Z
// values here are deliberately kept within [0, 1] (0.2 = near, 0.8 = far) so this test exercises
// the SAME valid geometry on both renderers -- do not reintroduce negative Z values here.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
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

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    void DrawQuad(GraphicsDevice& dev, float x0, float x1, float z, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(x0,  1.0f, z), color },
            { Vector3(x0, -1.0f, z), color },
            { Vector3(x1, -1.0f, z), color },
            { Vector3(x0,  1.0f, z), color },
            { Vector3(x1, -1.0f, z), color },
            { Vector3(x1,  1.0f, z), color },
        };
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class DepthStencilStateWriteEnableTest : public Game
{
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(0, 0, 0, 255), 1.0f, 0);
        dev.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        DepthStencilState writeDisabled;
        writeDisabled.setDepthBufferEnableProperty(true);
        writeDisabled.setDepthBufferWriteEnableProperty(false);
        writeDisabled.setDepthBufferFunctionProperty(CompareFunction::LessEqual);

        const Color kRed(255, 0, 0, 255);
        const Color kGreen(0, 255, 0, 255);
        const Color kBlue(0, 0, 255, 255);

        // Check A: left half [-1, 0] -- B drawn with writes disabled.
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(dev, -1.0f, 0.0f, 0.8f, kRed);
        dev.setDepthStencilStateProperty(writeDisabled);
        DrawQuad(dev, -1.0f, 0.0f, 0.2f, kGreen);
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(dev, -1.0f, 0.0f, 0.5f, kBlue);

        // Check B: right half [0, 1] -- B drawn with writes enabled (sanity check).
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(dev, 0.0f, 1.0f, 0.8f, kRed);
        DrawQuad(dev, 0.0f, 1.0f, 0.2f, kGreen);
        DrawQuad(dev, 0.0f, 1.0f, 0.5f, kBlue);

        const int h = vp.getHeightProperty() / 2;
        Rectangle regA(vp.getWidthProperty() / 4, h, 1, 1);
        Rectangle regB(vp.getWidthProperty() * 3 / 4, h, 1, 1);
        Color a(0, 0, 0, 0), b(0, 0, 0, 0);
        dev.GetBackBufferData(&regA, &a, 0, 1);
        dev.GetBackBufferData(&regB, &b, 0, 1);

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
    int getResult() const { return result_; }
};

int main()
{
    DepthStencilStateWriteEnableTest game;
    game.Run();
    return game.getResult();
}
