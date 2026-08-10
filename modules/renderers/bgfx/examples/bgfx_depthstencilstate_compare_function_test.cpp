// SPDX-License-Identifier: MS-PL
// Task 760 (Phase 72 Bgfx gap closure): verify all 8 depth CompareFunction values actually change
// depth-test outcome on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_depthstencilstate_compare_function_test.cpp
// (Task 314, already reused verbatim on Vulkan). Not a verbatim reuse for 2 reasons: (1) that
// file only covers 5 of the 8 real XNA CompareFunction values (missing Equal/GreaterEqual/
// NotEqual, this row's own ask is "all 8"); (2) it reads back 5 spatially-separate columns in the
// SAME rendered frame, but Bgfx's own GetBackBufferData only reliably reflects the FIRST read per
// rendered frame (Task 406 finding) — restructured into one separately-read pass per check,
// mirroring Task 759's own established Bgfx pattern.
//
// Method per check: draw a reference quad A (red) at depth 0.5 with DepthStencilState::Default
// (writes depth), then a quad B (green) at a depth chosen to give an unambiguous,
// function-distinguishing expected result, using a custom DepthStencilState with
// DepthBufferFunction set to the function under test:
//
//   Always       : B at depth 0.9 (WORSE than A) -- Always ignores the comparison -> PASS -> GREEN.
//   Never        : B at depth 0.1 (BETTER than A) -- Never unconditionally rejects -> FAIL -> RED.
//   Less         : B at depth 0.3 (< 0.5)         -- 0.3 < 0.5 is true             -> PASS -> GREEN.
//   LessEqual    : B at depth 0.5 (== 0.5)        -- 0.5 <= 0.5 is true            -> PASS -> GREEN
//                  (distinguishes LessEqual's inclusive equality from strict Less).
//   Equal        : B at depth 0.5 (== 0.5)        -- 0.5 == 0.5 is true            -> PASS -> GREEN.
//   GreaterEqual : B at depth 0.5 (== 0.5)        -- 0.5 >= 0.5 is true            -> PASS -> GREEN.
//   Greater      : B at depth 0.7 (> 0.5)         -- 0.7 > 0.5 is true             -> PASS -> GREEN
//                  (opposite depth direction from Less -- catches a Less/Greater direction swap).
//   NotEqual     : B at depth 0.3 (!= 0.5)        -- 0.3 != 0.5 is true            -> PASS -> GREEN.
//
// Exit code 0 = all 8 checks PASS, 1 = any FAIL.

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

    DepthStencilState MakeCompareState(CompareFunction func)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(true);
        ds.setDepthBufferWriteEnableProperty(true);
        ds.setDepthBufferFunctionProperty(func);
        return ds;
    }
}

class BgfxDepthStencilStateCompareFunctionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    Color RunCheck(GraphicsDevice& dev, CompareFunction func, float bDepth)
    {
        const Color kRed(255, 0, 0, 255);
        const Color kGreen(0, 255, 0, 255);

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
            DrawQuad(dev, 0.5f, kRed);
            dev.setDepthStencilStateProperty(MakeCompareState(func));
            DrawQuad(dev, bDepth, kGreen);

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

        struct Check { const char* name; CompareFunction func; float bDepth; bool expectPass; };
        const Check checks[8] = {
            { "Always",       CompareFunction::Always,       0.9f, true  },
            { "Never",        CompareFunction::Never,        0.1f, false },
            { "Less",         CompareFunction::Less,         0.3f, true  },
            { "LessEqual",    CompareFunction::LessEqual,    0.5f, true  },
            { "Equal",        CompareFunction::Equal,        0.5f, true  },
            { "GreaterEqual", CompareFunction::GreaterEqual, 0.5f, true  },
            { "Greater",      CompareFunction::Greater,      0.7f, true  },
            { "NotEqual",     CompareFunction::NotEqual,     0.3f, true  },
        };

        int passCount = 0;
        for (const auto& check : checks)
        {
            const Color c = RunCheck(dev, check.func, check.bDepth);
            const bool sawGreen = c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
            const bool sawRed   = c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60;
            const bool ok = check.expectPass ? sawGreen : sawRed;
            std::printf("[%s] %s: centre=(%d,%d,%d), expected %s\n",
                        ok ? "PASS" : "FAIL", check.name,
                        c.getRProperty(), c.getGProperty(), c.getBProperty(),
                        check.expectPass ? "GREEN (B passes)" : "RED (B rejected)");
            if (ok) ++passCount;
        }

        std::printf("=== %d/8 PASS ===\n", passCount);
        result_ = (passCount == 8) ? 0 : 1;
        Exit();
    }

public:
    BgfxDepthStencilStateCompareFunctionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxDepthStencilStateCompareFunctionTest game;
    game.Run();
    return game.getResult();
}
