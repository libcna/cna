// SPDX-License-Identifier: MS-PL
// Task 317: verify DepthStencilState.StencilFail/StencilDepthBufferFail/StencilPass (the three
// StencilOperation slots for front-facing triangles) each perform their own distinct operation,
// exercising Keep/Replace/Increment/Decrement.
//
// IMPORTANT: PresentationParameters.DepthStencilFormat defaults to DepthFormat::Depth24 (no
// stencil aspect) -- this test's constructor explicitly requests DepthFormat::Depth24Stencil8 via
// GraphicsDeviceManager, same as Tasks 315/316's tests. Do not remove this. Also remember
// GraphicsDevice::Clear ignores ClearOptions::Stencil entirely (Task 871) -- every column
// establishes its own known stencil baseline via a real "stamp" draw with StencilFunction=Always,
// never via Clear().
//
// Method: 4 columns -- 3 per operation slot, plus a 4th contrast/control check.
// Columns 0-2, each:
//   1. Stamp: draw at depth 0.5 with DepthBufferFunction=Always (always writes depth) and
//      StencilFunction=Always/StencilPass=Replace/ReferenceStencil=0x05 -> establishes a known
//      depth AND stencil baseline (buffer=0x05).
//   2. Operation draw: a second quad whose DepthStencilState is set up so that ONLY the operation
//      slot under test can fire; the other two slots are set to Decrement as a trap -- if the
//      wrong slot fires instead, the final buffer value won't match and the read-back check fails.
//      Column 0 (StencilFail): DepthBufferFunction=Always (depth trivially passes) with
//        StencilFunction=Equal, ReferenceStencil=0x99 (deliberate mismatch) -> stencil test FAILS
//        -> only StencilFail (set to Increment) can fire. Expected buffer: 0x06.
//      Column 1 (StencilDepthBufferFail): StencilFunction=Equal, ReferenceStencil=0x05 (matches,
//        stencil PASSES), DepthBufferFunction=Less, drawn at depth 0.8 (worse than the 0.5
//        baseline, so 0.8<0.5 is false -> depth test FAILS) -> only StencilDepthBufferFail (set to
//        Increment) can fire. Expected buffer: 0x06.
//      Column 2 (StencilPass): same stencil match as column 1, but drawn at depth 0.2 (better than
//        0.5, so 0.2<0.5 is true -> depth test PASSES too) -> only StencilPass (set to Increment)
//        can fire. Expected buffer: 0x06.
//   3. Read-back: a GREEN quad with depth testing disabled, StencilFunction=Equal,
//      ReferenceStencil=0x06, StencilPass=StencilFail=Keep (read-only) -> PASSES (GREEN) only if
//      the correct operation slot actually fired Increment; any other outcome (wrong slot fired,
//      or no slot fired) leaves the buffer at something other than 0x06 and this check FAILs
//      (stays BACKGROUND).
//
// Column 3 (contrast/control, CRITICAL for this test's validity): identical to column 2
// (StencilPass fires, buffer becomes 0x06), but the read-back quad deliberately queries
// ReferenceStencil=0x99 (a guaranteed mismatch) instead of 0x06. A genuinely working stencil test
// must REJECT this (0x06 != 0x99) -> stays BACKGROUND. Without this column, columns 0-2 alone
// cannot distinguish "the ops work correctly" from "the stencil test is bypassed entirely and
// every fragment always passes regardless of the buffer" -- both scenarios show GREEN on all 3.
// Column 3 is the one case in this test where a WORKING implementation must show BACKGROUND, not
// GREEN, making it the only check capable of catching a fully-bypassed stencil test.
//
// NOTE: this project's Vulkan backend (VulkanGraphicsBackend::ApplyDepthStencilState, tracked as
// Task 870) discards stencilPass/stencilFail/stencilDepthFail entirely and never enables the
// stencil test at all (stencilTestEnable is never set), so every fragment always passes the
// (nonexistent) stencil test regardless of the requested compare or reference value. This was
// discovered mid-development: an earlier version of this test WITHOUT column 3 coincidentally
// passed all its checks on Vulkan too, since every one of columns 0-2 happens to expect GREEN
// (PASS) already -- exactly what a fully-bypassed stencil test also produces, giving the test zero
// power to detect the bug. Column 3 is designed to fail specifically on a bypassed stencil test
// (expects BACKGROUND, but a bypass always shows GREEN) -- expect columns 0-2 to pass on Vulkan
// purely by coincidence and column 3 to fail, revealing the bug. This is a fourth reconfirmation
// of Task 870, not a new bug; do not read columns 0-2 passing as evidence stencil ops work.
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
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBackground(20, 20, 20, 255);
    const Color kGreen(0, 255, 0, 255);

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

    // stencilPasses controls whether the stencil compare itself passes (ReferenceStencil match).
    // depthFunc/z control whether the depth test passes. Only the operation under test is set to
    // Increment; the other two slots are Decrement traps.
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

class DepthStencilStateStencilOpsTest : public Game
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

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        const float colW = 2.0f / 4.0f;
        auto colX = [&](int i) { return -1.0f + colW * static_cast<float>(i); };

        // Column 0: StencilFail. Depth trivially passes (Always); stencil fails (0x99 != 0x05).
        {
            const float x0 = colX(0), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeStampState());
            DrawQuad(dev, x0, x1, 0.5f, kBackground);
            dev.setDepthStencilStateProperty(MakeOpState(
                /*stencilPasses=*/false, CompareFunction::Always,
                /*onFail=*/StencilOperation::Increment,
                /*onDepthFail=*/StencilOperation::Decrement,
                /*onPass=*/StencilOperation::Decrement));
            DrawQuad(dev, x0, x1, 0.5f, kBackground);
        }

        // Column 1: StencilDepthBufferFail. Stencil passes (0x05==0x05); depth fails
        // (drawn at 0.8, Less compare against the 0.5 baseline -> 0.8<0.5 is false).
        {
            const float x0 = colX(1), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeStampState());
            DrawQuad(dev, x0, x1, 0.5f, kBackground);
            dev.setDepthStencilStateProperty(MakeOpState(
                /*stencilPasses=*/true, CompareFunction::Less,
                /*onFail=*/StencilOperation::Decrement,
                /*onDepthFail=*/StencilOperation::Increment,
                /*onPass=*/StencilOperation::Decrement));
            DrawQuad(dev, x0, x1, 0.8f, kBackground);
        }

        // Column 2: StencilPass. Stencil passes; depth also passes
        // (drawn at 0.2, Less compare against the 0.5 baseline -> 0.2<0.5 is true).
        {
            const float x0 = colX(2), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeStampState());
            DrawQuad(dev, x0, x1, 0.5f, kBackground);
            dev.setDepthStencilStateProperty(MakeOpState(
                /*stencilPasses=*/true, CompareFunction::Less,
                /*onFail=*/StencilOperation::Decrement,
                /*onDepthFail=*/StencilOperation::Decrement,
                /*onPass=*/StencilOperation::Increment));
            DrawQuad(dev, x0, x1, 0.2f, kBackground);
        }

        // Column 3: contrast/control. Same op as column 2 (StencilPass fires, buffer -> 0x06), but
        // the read-back deliberately queries the WRONG reference (0x99) -- a working stencil test
        // must reject this. See the file header for why this column is essential.
        {
            const float x0 = colX(3), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeStampState());
            DrawQuad(dev, x0, x1, 0.5f, kBackground);
            dev.setDepthStencilStateProperty(MakeOpState(
                /*stencilPasses=*/true, CompareFunction::Less,
                /*onFail=*/StencilOperation::Decrement,
                /*onDepthFail=*/StencilOperation::Decrement,
                /*onPass=*/StencilOperation::Increment));
            DrawQuad(dev, x0, x1, 0.2f, kBackground);
        }

        // Read back all 4 columns. Columns 0-2 query the correct 0x06; column 3 deliberately
        // queries the wrong 0x99.
        const int readBackRef[4] = { 0x06, 0x06, 0x06, 0x99 };
        for (int i = 0; i < 4; ++i)
        {
            const float x0 = colX(i), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeReadBackState(readBackRef[i]));
            DrawQuad(dev, x0, x1, 0.5f, kGreen);
        }

        Color results[4] = {
            Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0)
        };
        for (int i = 0; i < 4; ++i)
        {
            const float cx = colX(i) + colW * 0.5f;
            const int px = static_cast<int>((cx + 1.0f) * 0.5f * vp.getWidthProperty());
            Rectangle reg(px, vp.getHeightProperty() / 2, 1, 1);
            dev.GetBackBufferData(&reg, &results[i], 0, 1);
        }

        const char* names[4] = {
            "StencilFail (Increment)", "StencilDepthBufferFail (Increment)",
            "StencilPass (Increment)", "Contrast: wrong ReferenceStencil (must reject)"
        };
        const bool expectGreen[4] = { true, true, true, false };

        int passCount = 0;
        for (int i = 0; i < 4; ++i)
        {
            const Color& c = results[i];
            const bool ok = expectGreen[i] ? IsGreen(c) : !IsGreen(c);
            std::printf("[%s] %s: centre=(%d,%d,%d), expected %s\n",
                        ok ? "PASS" : "FAIL", names[i],
                        c.getRProperty(), c.getGProperty(), c.getBProperty(),
                        expectGreen[i] ? "GREEN" : "BACKGROUND");
            if (ok) ++passCount;
        }

        result_ = (passCount == 4) ? 0 : 1;
        Exit();
    }

public:
    DepthStencilStateStencilOpsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    DepthStencilStateStencilOpsTest game;
    game.Run();
    return game.getResult();
}
