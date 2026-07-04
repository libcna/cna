// SPDX-License-Identifier: MS-PL
// Task 318: verify DepthStencilState.TwoSidedStencilMode actually applies a SEPARATE stencil
// function/ops (CounterClockwiseStencilFunction/Fail/DepthBufferFail/Pass) to back-facing
// triangles instead of the front-face ones.
//
// IMPORTANT: PresentationParameters.DepthStencilFormat defaults to DepthFormat::Depth24 (no
// stencil aspect) -- this test's constructor explicitly requests DepthFormat::Depth24Stencil8 via
// GraphicsDeviceManager, same as Tasks 315-317's tests. Do not remove this. Also remember
// GraphicsDevice::Clear ignores ClearOptions::Stencil entirely (Task 871) -- every column
// establishes its own known stencil baseline via a real "stamp" draw, never via Clear().
// RasterizerState.CullMode=None is used throughout so a deliberately back-facing triangle is
// actually rasterized (face orientation for stencil-op selection is independent of whether
// culling would have discarded it).
//
// CRITICAL LESSON from Task 318's own earlier draft (matching Task 317's lesson): a test where
// every check expects the SAME pass/fail outcome cannot distinguish "the feature works" from "the
// stencil test is bypassed entirely" (Task 870) -- both look identical. This test is built as ONE
// genuinely differential pair from the start: same back-facing triangle, same front/CCW stencil
// property values, with ONLY TwoSidedStencilMode toggled between the two columns, expecting
// OPPOSITE final stencil values (and therefore opposite read-back outcomes).
//
// Method: both columns share this stamp+operation setup on a BACK-FACING triangle (reversed
// winding vs the "front-facing" quads used elsewhere in this project's tests):
//   1. Stamp: front-facing quad, StencilFunction=Always/StencilPass=Replace/ReferenceStencil=0x05
//      -> buffer=0x05 (winding doesn't matter here since Always ignores face-specific settings by
//      construction -- front and back use the identical Always/Replace/0x05 setup).
//   2. Operation: a BACK-FACING triangle covering the column, ReferenceStencil=0x05 (shared -- XNA
//      has only one ReferenceStencil, not a separate one per face), with:
//        Front-face: StencilFunction=Equal (0x05==0x05 -> PASSES), StencilPass=Decrement.
//        Back-face (CCW): CounterClockwiseStencilFunction=NotEqual (0x05!=0x05 -> FAILS),
//                          CounterClockwiseStencilFail=Increment.
//      Column A (TwoSidedStencilMode=true): the CCW settings apply to this back-facing triangle ->
//        stencil test FAILS -> CounterClockwiseStencilFail (Increment) fires -> buffer becomes
//        0x06.
//      Column B (TwoSidedStencilMode=false, contrast/control): the CCW settings are ignored; the
//        FRONT-face settings apply to ALL faces including this back-facing one -> stencil test
//        PASSES (Equal) -> StencilPass (Decrement) fires -> buffer becomes 0x04.
//   3. Read-back: both columns query the SAME ReferenceStencil=0x06 with StencilFunction=Equal.
//      Column A expects PASS (GREEN, buffer genuinely is 0x06). Column B expects FAIL (BACKGROUND,
//      buffer is 0x04, not 0x06) -- proving TwoSidedStencilMode is what changed the outcome, not
//      some other confound.
//
// NOTE: this project's Vulkan backend (VulkanGraphicsBackend::ApplyDepthStencilState, tracked as
// Task 870) discards twoSidedStencilMode/ccwStencilFunc/ccwStencilPass/ccwStencilFail/
// ccwStencilDepthFail entirely and never enables the stencil test at all, so neither column's
// operation draw ever actually modifies the stencil buffer, and every read-back trivially passes
// regardless of the requested reference value. Expect column A to pass on Vulkan purely by
// coincidence (it already expects PASS) and column B to FAIL (it expects a genuine REJECT, which
// Vulkan's bypassed stencil test cannot produce) -- a fifth reconfirmation of Task 870, not a new
// bug.
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
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBackground(20, 20, 20, 255);
    const Color kGreen(0, 255, 0, 255);

    // Front-facing winding (matches the pattern used throughout this project's other tests).
    void DrawQuadFront(GraphicsDevice& dev, float x0, float x1, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(x0,  1.0f, 0.5f), color },
            { Vector3(x0, -1.0f, 0.5f), color },
            { Vector3(x1, -1.0f, 0.5f), color },
            { Vector3(x0,  1.0f, 0.5f), color },
            { Vector3(x1, -1.0f, 0.5f), color },
            { Vector3(x1,  1.0f, 0.5f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    // Reversed winding -> back-facing (requires CullMode::None to actually rasterize).
    void DrawQuadBack(GraphicsDevice& dev, float x0, float x1, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(x1, -1.0f, 0.5f), color },
            { Vector3(x0, -1.0f, 0.5f), color },
            { Vector3(x0,  1.0f, 0.5f), color },
            { Vector3(x1,  1.0f, 0.5f), color },
            { Vector3(x1, -1.0f, 0.5f), color },
            { Vector3(x0,  1.0f, 0.5f), color },
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

class DepthStencilStateStencilTwoSidedTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    static bool IsGreen(const Color& c)
    {
        return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
    }

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();

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

        const float colW = 2.0f / 2.0f;
        auto colX = [&](int i) { return -1.0f + colW * static_cast<float>(i); };

        // Column 0: TwoSidedStencilMode=true -- CCW settings apply to the back-facing triangle.
        {
            const float x0 = colX(0), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeStampState());
            DrawQuadFront(dev, x0, x1, kBackground);
            dev.setDepthStencilStateProperty(MakeOpState(/*twoSided=*/true));
            DrawQuadBack(dev, x0, x1, kBackground);
        }

        // Column 1: TwoSidedStencilMode=false (contrast/control) -- front-face settings apply to
        // ALL faces, including this same back-facing triangle.
        {
            const float x0 = colX(1), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeStampState());
            DrawQuadFront(dev, x0, x1, kBackground);
            dev.setDepthStencilStateProperty(MakeOpState(/*twoSided=*/false));
            DrawQuadBack(dev, x0, x1, kBackground);
        }

        for (int i = 0; i < 2; ++i)
        {
            const float x0 = colX(i), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeReadBackState());
            DrawQuadFront(dev, x0, x1, kGreen);
        }

        Color results[2] = { Color(0, 0, 0, 0), Color(0, 0, 0, 0) };
        for (int i = 0; i < 2; ++i)
        {
            const float cx = colX(i) + colW * 0.5f;
            const int px = static_cast<int>((cx + 1.0f) * 0.5f * vp.getWidthProperty());
            Rectangle reg(px, vp.getHeightProperty() / 2, 1, 1);
            dev.GetBackBufferData(&reg, &results[i], 0, 1);
        }

        const char* names[2] = {
            "TwoSidedStencilMode=true (CCW ops apply, expect PASS)",
            "TwoSidedStencilMode=false (front ops apply, must reject)",
        };
        const bool expectGreen[2] = { true, false };

        int passCount = 0;
        for (int i = 0; i < 2; ++i)
        {
            const Color& c = results[i];
            const bool ok = expectGreen[i] ? IsGreen(c) : !IsGreen(c);
            std::printf("[%s] %s: centre=(%d,%d,%d), expected %s\n",
                        ok ? "PASS" : "FAIL", names[i],
                        c.getRProperty(), c.getGProperty(), c.getBProperty(),
                        expectGreen[i] ? "GREEN" : "BACKGROUND");
            if (ok) ++passCount;
        }

        result_ = (passCount == 2) ? 0 : 1;
        Exit();
    }

public:
    DepthStencilStateStencilTwoSidedTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    DepthStencilStateStencilTwoSidedTest game;
    game.Run();
    return game.getResult();
}
