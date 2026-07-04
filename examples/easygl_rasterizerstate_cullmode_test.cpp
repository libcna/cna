// SPDX-License-Identifier: MS-PL
// Task 323 (+ Task 324/325 coverage): verify RasterizerState.CullMode actually gates
// rasterization by winding order, and specifically that CullMode::None disables culling
// entirely so BOTH winding orders render in the same frame.
//
// Method: two columns, each drawn with a different, verified winding order:
//   Column 0 (left half):  DrawQuadCW  -- signed area < 0 in NDC (matches the same winding as
//                           vulkan_fill_mode_test.cpp's reference triangle, empirically confirmed
//                           visible under the default RasterizerState there) -- survives XNA's
//                           default CullCounterClockwiseFace state.
//   Column 1 (right half): DrawQuadCCW -- the reversed winding (signed area > 0) -- culled by
//                           XNA's default CullCounterClockwiseFace state, requires CullMode::None
//                           or CullMode::CullClockwiseFace to render.
//
// NOTE: an earlier draft of this file borrowed Task 318's DrawQuadFront/DrawQuadBack naming and
// comment ("front-facing... survives the default state"), but Task 318 only ever draws with
// CullMode::None, so that claim was never actually exercised under a real cull mode. Running
// this test empirically showed the OPPOSITE: Task 318's "front" winding is culled by the default
// state, and its "back" winding survives. Renamed here to CW/CCW based on directly-measured
// signed area rather than a borrowed, unverified label -- Task 318's own file is unaffected
// (it never depended on the absolute label, only on the two quads having opposite windings,
// which they still do).
//
// CRITICAL LESSON applied from Tasks 317/318: a test where every check expects the SAME
// outcome cannot distinguish "culling works" from "culling is bypassed entirely" -- both look
// identical. This test is built with genuine contrast from the start: the SAME two quads are
// redrawn under all 3 CullMode values, so each quad's own visibility flips between checks
// depending on the mode:
//
//   CullMode::None                  -> column 0 RED (visible), column 1 GREEN (visible)
//     [Task 323's own goal: both winding orders render when culling is disabled]
//   CullMode::CullCounterClockwiseFace (XNA default)
//                                    -> column 0 RED (CW survives), column 1 BACKGROUND (CCW culled)
//     [doubles as Task 325's "cull counter-clockwise -- verify expected triangle disappears"]
//   CullMode::CullClockwiseFace     -> column 0 BACKGROUND (CW now culled), column 1 GREEN (CCW survives)
//     [doubles as Task 324's "cull clockwise -- verify expected triangle disappears"]
//
// A broken/bypassed cull implementation (culling always off, or always on for one winding)
// would fail at least one of these 6 checks, so this genuinely exercises CullMode end to end
// rather than just checking the "nothing culled" case in isolation.
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

namespace
{
    const Color kBackground(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);

    // Signed area < 0 in NDC -- empirically confirmed to survive XNA's default
    // CullCounterClockwiseFace state (see file header note).
    void DrawQuadCW(GraphicsDevice& dev, float x0, float x1, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(x1, -1.0f, 0.0f), color },
            { Vector3(x0, -1.0f, 0.0f), color },
            { Vector3(x0,  1.0f, 0.0f), color },
            { Vector3(x1,  1.0f, 0.0f), color },
            { Vector3(x1, -1.0f, 0.0f), color },
            { Vector3(x0,  1.0f, 0.0f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    // Reversed winding (signed area > 0 in NDC) -- culled by XNA's default
    // CullCounterClockwiseFace state.
    void DrawQuadCCW(GraphicsDevice& dev, float x0, float x1, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(x0,  1.0f, 0.0f), color },
            { Vector3(x0, -1.0f, 0.0f), color },
            { Vector3(x1, -1.0f, 0.0f), color },
            { Vector3(x0,  1.0f, 0.0f), color },
            { Vector3(x1, -1.0f, 0.0f), color },
            { Vector3(x1,  1.0f, 0.0f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class RasterizerStateCullModeTest : public Game
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

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();
        const int width = vp.getWidthProperty();
        const int height = vp.getHeightProperty();

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;

        auto sampleAt = [&](float ndcX) -> Color
        {
            const int px = static_cast<int>((ndcX + 1.0f) * 0.5f * static_cast<float>(width));
            Rectangle reg(px, height / 2, 1, 1);
            Color c(0, 0, 0, 0);
            dev.GetBackBufferData(&reg, &c, 0, 1);
            return c;
        };

        auto runCheck = [&](CullMode mode) -> std::pair<Color, Color>
        {
            dev.Clear(kBackground);
            dev.setBlendStateProperty(BlendState::Opaque);
            RasterizerState rs;
            rs.setCullModeProperty(mode);
            dev.setRasterizerStateProperty(rs);
            fx.Apply();
            DrawQuadCW(dev, -1.0f, 0.0f, kRed);     // column 0, left half
            DrawQuadCCW(dev, 0.0f, 1.0f, kGreen);   // column 1, right half
            return { sampleAt(-0.5f), sampleAt(0.5f) };
        };

        int passCount = 0;
        int checkCount = 0;
        auto check = [&](bool ok, const char* label, const Color& got, const char* expected)
        {
            ++checkCount;
            std::printf("[%s] %s: (%d,%d,%d) expected %s\n", ok ? "PASS" : "FAIL", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            if (ok) ++passCount;
        };

        {
            auto [c0, c1] = runCheck(CullMode::None);
            check(IsRed(c0), "CullNone: column0 (CW)", c0, "RED");
            check(IsGreen(c1), "CullNone: column1 (CCW)", c1, "GREEN");
        }
        {
            auto [c0, c1] = runCheck(CullMode::CullCounterClockwiseFace);
            check(IsRed(c0), "CullCounterClockwiseFace (default): column0 (CW)", c0, "RED");
            check(IsBackground(c1),
                  "CullCounterClockwiseFace (default): column1 (CCW, must be culled)",
                  c1, "BACKGROUND");
        }
        {
            auto [c0, c1] = runCheck(CullMode::CullClockwiseFace);
            check(IsBackground(c0),
                  "CullClockwiseFace: column0 (CW, must be culled)", c0, "BACKGROUND");
            check(IsGreen(c1), "CullClockwiseFace: column1 (CCW)", c1, "GREEN");
        }

        std::printf("\nResult: %d/%d PASS\n", passCount, checkCount);
        result_ = (passCount == checkCount) ? 0 : 1;
        Exit();
    }

public:
    RasterizerStateCullModeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return result_; }
};

int main()
{
    RasterizerStateCullModeTest game;
    game.Run();
    return game.getResult();
}
