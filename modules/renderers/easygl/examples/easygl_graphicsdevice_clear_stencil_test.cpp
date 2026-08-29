// SPDX-License-Identifier: MS-PL
// Task 871: verify GraphicsDevice::Clear() actually clears the stencil buffer to the requested
// value, on ClearOptions combinations that include ClearOptions::Stencil.
//
// Confirmed gap: `GraphicsDevice::Clear(ClearOptions options, const Color& color, float depth,
// int stencil)` discarded its own `stencil` parameter entirely (`(void)stencil;`) and never
// checked `hasClearFlag(options, ClearOptions::Stencil)` -- no code path cleared the stencil
// buffer to any value, requested or not, on any renderer.
//
// Checks A and B are spread across separate frames, one step per Draw() call. That was originally
// described as unavoidable: Vulkan's renderer deferred every frame's draws into one render pass
// whose clear ran before ALL of them regardless of where Clear() sat in the public call order, so a
// same-frame "stamp, then Clear(), then compare" would always have seen the stamp win.
//
// REMED-GFX-129 CORRECTED THAT CLAIM rather than working around it: a Clear() is now an ordered
// command at its exact public call position on Vulkan too. The frame split is kept for A and B --
// it is valid everywhere and they are Task 871's original regression -- and check C below makes the
// same measurement WITHIN ONE FRAME, which is the shape the old note asserted could never work.
// Without C, this file could not tell a correctly ordered stencil clear from one that silently
// happens at the start of the pass.
//
// Per check: stamp the whole screen to stencil=0x07 in frame N via a real draw
// (StencilFunction::Always, StencilPass::Replace, ReferenceStencil=0x07); in frame N+1, issue the
// GraphicsDevice::Clear() under test, then a StencilFunction::Equal test draw comparing against
// the value the clear was supposed to establish.
//
// Check A -- ClearOptions::Stencil alone (exercises IGraphicsRenderer::ClearStencil): clear
//   stencil to 0x03 (NOT Target/DepthBuffer). Test draw with ReferenceStencil=0x03. If the clear
//   took effect, buffer=0x03 matches -> GREEN; if it silently did nothing, buffer is still the
//   stamped 0x07 -> mismatch -> BACKGROUND.
// Check B -- Target|DepthBuffer|Stencil together with a non-zero stencil value (exercises
//   IGraphicsRenderer::ClearColorDepthAndStencil): re-stamp to 0x07, clear to 0x09, then test draw
//   with ReferenceStencil=0x09. Deliberately non-zero (unlike the plain single-arg Clear(Color)
//   overload, which always implies stencil=0) so a broken dispatch that leaves the stencil clear
//   value at its uninitialized/leftover-default 0 cannot coincidentally still match the expected
//   value and mask the bug.
// Check C -- REMED-GFX-129: the whole stamp / ClearOptions::Stencil / compare sequence inside ONE
//   frame, with nothing between the three steps. A renderer that delivers a stencil clear only
//   through the render-pass load action runs it before the stamp, so the buffer still holds the
//   stamped 0x05 and the Equal-0x0c test draw finds nothing -- BACKGROUND. Measured red on Vulkan
//   before REMED-GFX-129 and green after; values distinct from A and B so a leftover clear value
//   from an earlier frame cannot make it pass.
//
// Exit code 0 = all three checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
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

    void DrawQuad(GraphicsDevice& dev, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3(-1.0f, -1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3( 1.0f,  1.0f, 0.5f), color },
        };
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    void ApplyBasicEffect(GraphicsDevice& dev)
    {
        static BasicEffect* fx = nullptr;
        if (fx == nullptr) fx = new BasicEffect(dev);
        fx->setWorldProperty(Matrix::getIdentityProperty());
        fx->setViewProperty(Matrix::getIdentityProperty());
        fx->setProjectionProperty(Matrix::getIdentityProperty());
        fx->VertexColorEnabled = true;
        fx->Apply();
    }

    void StampStencil(GraphicsDevice& dev, int value)
    {
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kBackground, 1.0f, 0);
        dev.setBlendStateProperty(BlendState::Opaque);
        ApplyBasicEffect(dev);

        DepthStencilState stamp;
        stamp.setDepthBufferEnableProperty(false);
        stamp.setStencilEnableProperty(true);
        stamp.setStencilFunctionProperty(CompareFunction::Always);
        stamp.setStencilPassProperty(StencilOperation::Replace);
        stamp.setReferenceStencilProperty(value);
        dev.setDepthStencilStateProperty(stamp);
        DrawQuad(dev, kBackground);
    }

    /// REMED-GFX-237: does a stencil clear put the WRITE mask back?
    ///
    /// XNA's Clear ignores `StencilWriteMask`; `glClear` obeys it, so the clear has to force the
    /// mask open. Leaving it open is the defect: the next draw then writes stencil bits the active
    /// state forbids. Assign a state whose write mask is 0x00 -- a draw through it must change
    /// nothing -- clear the buffer to @p seeded, draw through that state, then compare.
    /// Restored: the buffer still holds @p seeded and the compare draws GREEN.
    /// Not restored: the draw replaced it with 0x0A and the compare finds nothing.
    bool ClearThenWriteMaskedDrawKeepsStencil(GraphicsDevice& dev, int seeded)
    {
        DepthStencilState masked;
        masked.setDepthBufferEnableProperty(false);
        masked.setStencilEnableProperty(true);
        masked.setStencilFunctionProperty(CompareFunction::Always);
        masked.setStencilPassProperty(StencilOperation::Replace);
        masked.setReferenceStencilProperty(0x0A);
        masked.setStencilWriteMaskProperty(0x00);
        dev.setDepthStencilStateProperty(masked);

        dev.Clear(ClearOptions::Stencil, kBackground, 1.0f, seeded);
        dev.setBlendStateProperty(BlendState::Opaque);
        ApplyBasicEffect(dev);
        DrawQuad(dev, kBackground);

        DepthStencilState test;
        test.setDepthBufferEnableProperty(false);
        test.setStencilEnableProperty(true);
        test.setStencilFunctionProperty(CompareFunction::Equal);
        test.setReferenceStencilProperty(seeded);
        test.setStencilPassProperty(StencilOperation::Keep);
        test.setStencilFailProperty(StencilOperation::Keep);
        dev.setDepthStencilStateProperty(test);
        DrawQuad(dev, kGreen);

        const auto& vp = dev.getViewportProperty();
        Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color c(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &c, 0, 1);
        return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
    }

    bool ClearThenTestStencilEquals(GraphicsDevice& dev, ClearOptions clearOptions, int clearStencilValue,
                                     int expected)
    {
        dev.Clear(clearOptions, kBackground, 1.0f, clearStencilValue);
        dev.setBlendStateProperty(BlendState::Opaque);
        ApplyBasicEffect(dev);

        DepthStencilState test;
        test.setDepthBufferEnableProperty(false);
        test.setStencilEnableProperty(true);
        test.setStencilFunctionProperty(CompareFunction::Equal);
        test.setReferenceStencilProperty(expected);
        test.setStencilPassProperty(StencilOperation::Keep);
        test.setStencilFailProperty(StencilOperation::Keep);
        dev.setDepthStencilStateProperty(test);
        DrawQuad(dev, kGreen);

        const auto& vp = dev.getViewportProperty();
        Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color c(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &c, 0, 1);
        return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
    }
}

class GraphicsDeviceClearStencilTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  step_      = 0;
    int  passCount_ = 0;
    bool done_      = false;
    int  result_    = 1;

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;

        auto& dev = getGraphicsDeviceProperty();

        switch (step_)
        {
        case 0:
            // Frame 1: stamp stencil=0x07 for Check A.
            StampStencil(dev, 0x07);
            break;
        case 1:
        {
            // Frame 2: Check A -- ClearOptions::Stencil alone, then compare.
            const bool okA = ClearThenTestStencilEquals(dev, ClearOptions::Stencil, 0x03, 0x03);
            std::printf("[%s] ClearOptions::Stencil alone (clear to 0x03 over a 0x07 stamp)\n",
                        okA ? "PASS" : "FAIL");
            if (okA) ++passCount_;
            break;
        }
        case 2:
            // Frame 3: re-stamp stencil=0x07 for Check B.
            StampStencil(dev, 0x07);
            break;
        case 3:
        {
            // Frame 4: Check B -- Target|DepthBuffer|Stencil together, non-zero stencil value.
            const bool okB = ClearThenTestStencilEquals(
                dev, ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                0x09, 0x09);
            std::printf("[%s] Target|DepthBuffer|Stencil together (clear to 0x09 over a 0x07 stamp)\n",
                        okB ? "PASS" : "FAIL");
            if (okB) ++passCount_;
            break;
        }
        case 4:
        {
            // Frame 5: Check C -- REMED-GFX-129. Stamp, clear and compare with NOTHING between
            // them, so a clear that is really the pass load action cannot pass by running first.
            StampStencil(dev, 0x05);
            const bool okC = ClearThenTestStencilEquals(dev, ClearOptions::Stencil, 0x0c, 0x0c);
            std::printf("[%s] ORDERED: stamp 0x05, ClearOptions::Stencil to 0x0c and compare, all "
                        "in ONE frame\n", okC ? "PASS" : "FAIL");
            if (okC) ++passCount_;

            // Check D -- REMED-GFX-237: a clear must ignore the stencil WRITE mask AND put it back.
            // Nothing covered the restore: with it deleted, all 65 stencil tests still passed.
            StampStencil(dev, 0x05);
            const bool okD = ClearThenWriteMaskedDrawKeepsStencil(dev, 0x05);
            std::printf("[%s] RESTORE: a StencilWriteMask=0x00 draw after ClearOptions::Stencil "
                        "must leave the cleared value alone\n", okD ? "PASS" : "FAIL");
            if (okD) ++passCount_;

            std::printf("=== %d/4 PASS ===\n", passCount_);
            result_ = (passCount_ == 4) ? 0 : 1;
            done_ = true;
            Exit();
            break;
        }
        default:
            break;
        }
        ++step_;
    }

public:
    GraphicsDeviceClearStencilTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    GraphicsDeviceClearStencilTest game;
    game.Run();
    return game.getResult();
}
