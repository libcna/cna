// SPDX-License-Identifier: MS-PL
// Task 763: verify DepthStencilState.TwoSidedStencilMode actually applies a SEPARATE stencil
// function/ops (CounterClockwiseStencilFunction/Fail/DepthBufferFail/Pass) to back-facing
// triangles instead of the front-face ones, on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_depthstencilstate_stencil_twosided_test.cpp (Task
// 318, already reused verbatim on Vulkan). Not a verbatim reuse: that file reads back 2
// spatially-separate columns from a SINGLE rendered frame, but Bgfx's own GetBackBufferData only
// reliably reflects the FIRST read per rendered frame (Task 406 finding) -- restructured into one
// separately-read RunCheck() pass per check, mirroring Task 759-762's established Bgfx pattern.
//
// CRITICAL LESSON (matches Task 317/318's own lesson): a test where every check expects the SAME
// pass/fail outcome cannot distinguish "the feature works" from "the stencil test is bypassed
// entirely" (Task 870's own reconfirmed Vulkan bug). This test is ONE genuinely differential pair:
// same back-facing triangle, same front/CCW stencil property values, with ONLY
// TwoSidedStencilMode toggled between the two checks, expecting OPPOSITE final stencil values.
//
// Method per check (identical logic to Task 318's per-column method, just full-screen instead of
// a column, since every check now gets its own frame). RasterizerState.CullMode=None is used
// throughout so a deliberately back-facing triangle is actually rasterized.
//   1. Stamp: front-facing quad, StencilFunction=Always/StencilPass=Replace/ReferenceStencil=0x05
//      -> buffer=0x05.
//   2. Operation: a BACK-FACING triangle (reversed winding), ReferenceStencil=0x05, with:
//        Front-face: StencilFunction=Equal (0x05==0x05 -> PASSES), StencilPass=Decrement.
//        Back-face (CCW): CounterClockwiseStencilFunction=NotEqual (0x05!=0x05 -> FAILS),
//                          CounterClockwiseStencilFail=Increment.
//      Check 0 (TwoSidedStencilMode=true): the CCW settings apply to this back-facing triangle ->
//        stencil test FAILS -> CounterClockwiseStencilFail (Increment) fires -> buffer -> 0x06.
//      Check 1 (TwoSidedStencilMode=false, contrast/control): the CCW settings are ignored; the
//        FRONT-face settings apply to ALL faces including this back-facing one -> stencil test
//        PASSES (Equal) -> StencilPass (Decrement) fires -> buffer -> 0x04.
//   3. Read-back: both checks query the SAME ReferenceStencil=0x06 with StencilFunction=Equal.
//      Check 0 expects PASS (GREEN, buffer genuinely is 0x06). Check 1 expects FAIL (BACKGROUND,
//      buffer is 0x04, not 0x06) -- proving TwoSidedStencilMode is what changed the outcome.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
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

    void DrawQuadFront(GraphicsDevice& dev, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3(-1.0f, -1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3( 1.0f,  1.0f, 0.5f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    // Reversed winding -> back-facing (requires CullMode::None to actually rasterize).
    void DrawQuadBack(GraphicsDevice& dev, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3(-1.0f, -1.0f, 0.5f), color },
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3( 1.0f,  1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3(-1.0f,  1.0f, 0.5f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    DepthStencilState MakeStampState()
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Always);
        ds.setCounterClockwiseStencilFunctionProperty(CompareFunction::Always);
        ds.setStencilPassProperty(StencilOperation::Replace);
        ds.setCounterClockwiseStencilPassProperty(StencilOperation::Replace);
        ds.setReferenceStencilProperty(0x05);
        return ds;
    }

    DepthStencilState MakeOpState(bool twoSided)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setTwoSidedStencilModeProperty(twoSided);
        ds.setReferenceStencilProperty(0x05);

        // Front-face: passes (Equal, 0x05==0x05), then Decrement.
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setStencilPassProperty(StencilOperation::Decrement);
        ds.setStencilFailProperty(StencilOperation::Keep);
        ds.setStencilDepthBufferFailProperty(StencilOperation::Keep);

        // Back-face (CCW): fails (NotEqual, 0x05!=0x05 is false), then Increment on fail.
        ds.setCounterClockwiseStencilFunctionProperty(CompareFunction::NotEqual);
        ds.setCounterClockwiseStencilFailProperty(StencilOperation::Increment);
        ds.setCounterClockwiseStencilPassProperty(StencilOperation::Keep);
        ds.setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation::Keep);
        return ds;
    }

    DepthStencilState MakeReadBackState()
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setReferenceStencilProperty(0x06);
        ds.setStencilPassProperty(StencilOperation::Keep);
        ds.setStencilFailProperty(StencilOperation::Keep);
        return ds;
    }
}

class BgfxDepthStencilStateStencilTwoSidedTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    static bool IsGreen(const Color& c)
    {
        return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
    }

    Color RunCheck(GraphicsDevice& dev, bool twoSided)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kBackground, 1.0f, 0);
            dev.setBlendStateProperty(BlendState::Opaque);

            RasterizerState rsNoCull;
            rsNoCull.setCullModeProperty(CullMode::None);
            dev.setRasterizerStateProperty(rsNoCull);

            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;
            fx.Apply();

            dev.setDepthStencilStateProperty(MakeStampState());
            DrawQuadFront(dev, kBackground);
            dev.setDepthStencilStateProperty(MakeOpState(twoSided));
            DrawQuadBack(dev, kBackground);
            dev.setDepthStencilStateProperty(MakeReadBackState());
            DrawQuadFront(dev, kGreen);

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

        struct Check { const char* name; bool twoSided; bool expectGreen; };
        const Check checks[2] = {
            { "TwoSidedStencilMode=true (CCW ops apply, expect PASS)",       true,  true  },
            { "TwoSidedStencilMode=false (front ops apply, must reject)",    false, false },
        };

        int passCount = 0;
        for (const auto& check : checks)
        {
            const Color c = RunCheck(dev, check.twoSided);
            const bool sawGreen = IsGreen(c);
            const bool ok = check.expectGreen ? sawGreen : !sawGreen;
            std::printf("[%s] %s: centre=(%d,%d,%d), expected %s\n",
                        ok ? "PASS" : "FAIL", check.name,
                        c.getRProperty(), c.getGProperty(), c.getBProperty(),
                        check.expectGreen ? "GREEN" : "BACKGROUND");
            if (ok) ++passCount;
        }

        std::printf("=== %d/2 PASS ===\n", passCount);
        result_ = (passCount == 2) ? 0 : 1;
        Exit();
    }

public:
    BgfxDepthStencilStateStencilTwoSidedTest()
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
    BgfxDepthStencilStateStencilTwoSidedTest game;
    game.Run();
    return game.getResult();
}
