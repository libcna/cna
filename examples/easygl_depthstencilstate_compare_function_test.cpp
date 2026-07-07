// SPDX-License-Identifier: MS-PL
// Task 314: verify DepthStencilState.DepthBufferFunction actually changes depth-test outcome for
// Always/Never/Less/LessEqual/Greater.
//
// Method: 5 side-by-side columns, one per CompareFunction. In each column, draw a reference quad
// A (red) at depth 0.5 with DepthStencilState::Default (writes depth), then a quad B (green) at a
// depth chosen specifically to give an unambiguous, function-distinguishing expected result, using
// a custom DepthStencilState with DepthBufferFunction set to the function under test:
//
//   Always     : B at depth 0.9 (WORSE than A) -- Always ignores the comparison -> PASS -> GREEN.
//   Never      : B at depth 0.1 (BETTER than A) -- Never unconditionally rejects -> FAIL -> RED.
//   Less       : B at depth 0.3 (< 0.5)         -- 0.3 < 0.5 is true            -> PASS -> GREEN.
//   LessEqual  : B at depth 0.5 (== 0.5)         -- 0.5 <= 0.5 is true          -> PASS -> GREEN
//                (distinguishes LessEqual's inclusive equality from strict Less).
//   Greater    : B at depth 0.7 (> 0.5)          -- 0.7 > 0.5 is true            -> PASS -> GREEN
//                (opposite depth direction from Less -- catches a Less/Greater direction swap).
//
// All checks share one Clear() (the 5 columns don't spatially overlap) and one frame. Z values
// are deliberately kept within [0, 1]: with an identity World/View/Projection, this project's
// Vulkan backend expects clip-space Z in DirectX's [0,+w] range (matching real XNA semantics), and
// a negative Z would silently clip away on Vulkan only -- see Task 313's test file for the full
// explanation. Do not reintroduce negative Z values here.
//
// NOTE: this project's Vulkan backend (VulkanGraphicsBackend::ApplyDepthStencilState) is already
// confirmed (Task 313, tracked as Task 870) to ignore DepthBufferFunction entirely, hardcoding
// depthCompareOp per pipeline-creation-function to VK_COMPARE_OP_LESS or LESS_OR_EQUAL regardless
// of what's requested. Expect only the checks that happen to match whichever hardcoded op the
// exercised pipeline uses to pass on Vulkan, and the rest to fail -- this is a reconfirmation of
// Task 870, not a new bug. Kept the Vulkan variant registered as a documented known failure.
//
// Exit code 0 = all 5 checks PASS, 1 = any FAIL.

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
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real
        // default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    DepthStencilState MakeCompareState(CompareFunction func)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(true);
        ds.setDepthBufferWriteEnableProperty(true);
        ds.setDepthBufferFunctionProperty(func);
        return ds;
    }
}

class DepthStencilStateCompareFunctionTest : public Game
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

        const Color kRed(255, 0, 0, 255);
        const Color kGreen(0, 255, 0, 255);

        struct Check { const char* name; CompareFunction func; float bDepth; bool expectPass; };
        const Check checks[5] = {
            { "Always",    CompareFunction::Always,    0.9f, true  },
            { "Never",     CompareFunction::Never,     0.1f, false },
            { "Less",      CompareFunction::Less,      0.3f, true  },
            { "LessEqual", CompareFunction::LessEqual, 0.5f, true  },
            { "Greater",   CompareFunction::Greater,   0.7f, true  },
        };

        const float colW = 2.0f / 5.0f;
        Color results[5] = {
            Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0)
        };

        for (int i = 0; i < 5; ++i)
        {
            const float x0 = -1.0f + colW * static_cast<float>(i);
            const float x1 = x0 + colW;

            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            DrawQuad(dev, x0, x1, 0.5f, kRed);

            dev.setDepthStencilStateProperty(MakeCompareState(checks[i].func));
            DrawQuad(dev, x0, x1, checks[i].bDepth, kGreen);

            const float cx = (x0 + x1) * 0.5f;
            const int px = static_cast<int>((cx + 1.0f) * 0.5f * vp.getWidthProperty());
            Rectangle reg(px, vp.getHeightProperty() / 2, 1, 1);
            Color c(0, 0, 0, 0);
            dev.GetBackBufferData(&reg, &c, 0, 1);
            results[i] = c;
        }

        int passCount = 0;
        for (int i = 0; i < 5; ++i)
        {
            const Color& c = results[i];
            const bool sawGreen = c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
            const bool sawRed   = c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60;
            const bool ok = checks[i].expectPass ? sawGreen : sawRed;
            std::printf("[%s] %s: centre=(%d,%d,%d), expected %s\n",
                        ok ? "PASS" : "FAIL", checks[i].name,
                        c.getRProperty(), c.getGProperty(), c.getBProperty(),
                        checks[i].expectPass ? "GREEN (B passes)" : "RED (B rejected)");
            if (ok) ++passCount;
        }

        result_ = (passCount == 5) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    DepthStencilStateCompareFunctionTest game;
    game.Run();
    return game.getResult();
}
