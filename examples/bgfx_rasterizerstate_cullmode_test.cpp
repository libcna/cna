// SPDX-License-Identifier: MS-PL
// Task 765: verify RasterizerState.CullMode actually gates rasterization by winding order on
// Bgfx, specifically that CullMode::None disables culling entirely so both winding orders render.
//
// Bgfx-specific adaptation of examples/easygl_rasterizerstate_cullmode_test.cpp (Task 323/324/325,
// already reused verbatim on Vulkan). Not a verbatim reuse: that file draws BOTH a CW and a CCW
// quad in the SAME frame and samples 2 spatially-separate columns, but Bgfx's own GetBackBufferData
// only reliably reflects the FIRST read per rendered frame (Task 406 finding) -- restructured into
// one separately-read RunCheck() pass per (CullMode, winding) pair, mirroring Tasks 759-764's
// established Bgfx pattern. Each check now draws only the single quad under test, full-screen.
//
// CRITICAL LESSON applied from Tasks 317/318/323: a test where every check expects the SAME
// outcome cannot distinguish "culling works" from "culling is bypassed entirely" -- both look
// identical. This test redraws the SAME two windings under all 3 CullMode values, so each winding's
// own visibility flips between checks depending on the mode:
//
//   CullMode::None                     -> CW RED (visible), CCW GREEN (visible)
//   CullMode::CullCounterClockwiseFace (XNA default)
//                                       -> CW RED (survives), CCW BACKGROUND (culled)
//   CullMode::CullClockwiseFace        -> CW BACKGROUND (now culled), CCW GREEN (survives)
//
// A broken/bypassed cull implementation (culling always off, or always on for one winding) would
// fail at least one of these 6 checks.
//
// Exit code 0 = all 6 checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
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
    const Color kBackground(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);

    // Signed area < 0 in NDC -- empirically confirmed to survive XNA's default
    // CullCounterClockwiseFace state (see Task 323's own file header note).
    void DrawQuadCW(GraphicsDevice& dev, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3( 1.0f, -1.0f, 0.0f), color },
            { Vector3(-1.0f, -1.0f, 0.0f), color },
            { Vector3(-1.0f,  1.0f, 0.0f), color },
            { Vector3( 1.0f,  1.0f, 0.0f), color },
            { Vector3( 1.0f, -1.0f, 0.0f), color },
            { Vector3(-1.0f,  1.0f, 0.0f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    // Reversed winding (signed area > 0 in NDC) -- culled by XNA's default
    // CullCounterClockwiseFace state.
    void DrawQuadCCW(GraphicsDevice& dev, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), color },
            { Vector3(-1.0f, -1.0f, 0.0f), color },
            { Vector3( 1.0f, -1.0f, 0.0f), color },
            { Vector3(-1.0f,  1.0f, 0.0f), color },
            { Vector3( 1.0f, -1.0f, 0.0f), color },
            { Vector3( 1.0f,  1.0f, 0.0f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class BgfxRasterizerStateCullModeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    static bool IsRed(const Color& c)
    {
        return c.getRProperty() >= 200 && c.getGProperty() <= 30 && c.getBProperty() <= 30;
    }
    static bool IsGreen(const Color& c)
    {
        return c.getGProperty() >= 200 && c.getRProperty() <= 30 && c.getBProperty() <= 30;
    }
    static bool IsBackground(const Color& c)
    {
        return c.getRProperty() <= 30 && c.getGProperty() <= 30 && c.getBProperty() <= 30;
    }

    Color RunCheck(GraphicsDevice& dev, CullMode mode, bool drawCW)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBackground);
            dev.setBlendStateProperty(BlendState::Opaque);

            RasterizerState rs;
            rs.setCullModeProperty(mode);
            dev.setRasterizerStateProperty(rs);

            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;
            fx.Apply();

            if (drawCW)
                DrawQuadCW(dev, kRed);
            else
                DrawQuadCCW(dev, kGreen);

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

        struct Check { const char* name; CullMode mode; bool drawCW; bool expectRed; bool expectGreen; };
        const Check checks[6] = {
            { "CullNone: CW quad",                            CullMode::None,                    true,  true,  false },
            { "CullNone: CCW quad",                           CullMode::None,                    false, false, true  },
            { "CullCounterClockwiseFace (default): CW quad",  CullMode::CullCounterClockwiseFace, true,  true,  false },
            { "CullCounterClockwiseFace (default): CCW quad (must be culled)", CullMode::CullCounterClockwiseFace, false, false, false },
            { "CullClockwiseFace: CW quad (must be culled)",  CullMode::CullClockwiseFace,        true,  false, false },
            { "CullClockwiseFace: CCW quad",                  CullMode::CullClockwiseFace,        false, false, true  },
        };

        int passCount = 0;
        for (const auto& check : checks)
        {
            const Color c = RunCheck(dev, check.mode, check.drawCW);
            bool ok;
            const char* expected;
            if (check.expectRed)       { ok = IsRed(c);        expected = "RED"; }
            else if (check.expectGreen) { ok = IsGreen(c);      expected = "GREEN"; }
            else                        { ok = IsBackground(c); expected = "BACKGROUND"; }
            std::printf("[%s] %s: (%d,%d,%d) expected %s\n",
                        ok ? "PASS" : "FAIL", check.name,
                        c.getRProperty(), c.getGProperty(), c.getBProperty(), expected);
            if (ok) ++passCount;
        }

        std::printf("=== %d/6 PASS ===\n", passCount);
        result_ = (passCount == 6) ? 0 : 1;
        Exit();
    }

public:
    BgfxRasterizerStateCullModeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxRasterizerStateCullModeTest game;
    game.Run();
    return game.getResult();
}
