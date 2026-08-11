// SPDX-License-Identifier: MS-PL
// Stencil regression test for the PortableGL renderer.
//
// PortableGL reported GraphicsCapability::StencilBuffer as supported while ApplyDepthStencilState
// ignored the whole stencil half of the state and SetReferenceStencil was never overridden -- the
// plane existed (PGL_D24S8) and was clearable, but no draw ever consulted it. Every check below
// is written so that it fails if the stencil plane is merely present: each one draws through a
// stencil test that must reject or accept, and reads the colour buffer to find out which happened.
//
// Check A -- a stencil write in one pass gates a later pass (Replace then Equal).
// Check B -- GraphicsDevice.ReferenceStencil, changed on its own, re-gates that test.
// Check C -- StencilFail runs when the comparison fails (Never + Replace still writes).
// Check D -- StencilWriteMask suppresses the write without suppressing the test.
// Check E -- StencilOperation.Increment WRAPS while IncrementSaturation CLAMPS (XNA's naming is
//   the opposite way round from GL's, so a straight-through mapping fails this).
// Check F -- TwoSidedStencilMode applies the CounterClockwise* half to counter-clockwise-wound
//   triangles and the primary half to clockwise ones -- a differential check, not an assumption.
// Check G -- a stencil-enabled state and a stencil-disabled state alternate correctly.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "portablegl_test_support.hpp"

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace portablegl_test;

namespace
{
    constexpr int kSize = 32;
    const Color kBlack(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);

    /// A DepthStencilState with depth off and the stencil half fully specified.
    DepthStencilState Stencil(CompareFunction func, StencilOperation pass, int reference,
                              StencilOperation fail = StencilOperation::Keep,
                              int writeMask = 0xFF)
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(false);
        state.setDepthBufferWriteEnableProperty(false);
        state.setStencilEnableProperty(true);
        state.setStencilFunctionProperty(func);
        state.setStencilPassProperty(pass);
        state.setStencilFailProperty(fail);
        state.setStencilDepthBufferFailProperty(StencilOperation::Keep);
        state.setReferenceStencilProperty(reference);
        state.setStencilWriteMaskProperty(writeMask);
        return state;
    }
}

class PortableGLStencilTest : public PortableGLTestGame
{
    Rectangle Whole() const { return Rectangle(0, 0, kSize, kSize); }
    Rectangle LeftHalf() const { return Rectangle(0, 0, kSize / 2, kSize); }
    Rectangle RightHalf() const { return Rectangle(kSize / 2, 0, kSize / 2, kSize); }

    /// Draws a full-viewport quad in @p color, front-facing under CullCounterClockwise.
    void DrawQuad(const Color& color)
    {
        DrawVertices(FullQuad(color).data());
    }

    /// Draws a full-viewport quad in @p color wound the other way -- back-facing under
    /// CullCounterClockwise, front-facing under CullClockwise.
    void DrawReversedQuad(const Color& color)
    {
        DrawVertices(ReversedFullQuad(color).data());
    }

    /// Draws a half-width quad covering x in [0, kSize/2), in NDC x from -1 to 0.
    void DrawLeftHalfQuad(const Color& color)
    {
        const VertexPositionColor verts[6] = {
            {Vector3(-1.0f,  1.0f, 0.0f), color},
            {Vector3( 0.0f,  1.0f, 0.0f), color},
            {Vector3( 0.0f, -1.0f, 0.0f), color},
            {Vector3(-1.0f,  1.0f, 0.0f), color},
            {Vector3( 0.0f, -1.0f, 0.0f), color},
            {Vector3(-1.0f, -1.0f, 0.0f), color},
        };
        DrawVertices(verts);
    }

    void DrawVertices(const VertexPositionColor* verts)
    {
        auto& dev = getGraphicsDeviceProperty();
        VertexBuffer vb(dev, PosColorDecl(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void ClearAll(const Color& color, int stencil)
    {
        getGraphicsDeviceProperty().Clear(
            ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
            color, 1.0f, stencil);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);

        // Check A -- pass 1 stamps stencil 1 into the left half; pass 2 only paints where the
        // stencil equals 1. If the stencil plane were never consulted, pass 2 would cover the
        // whole viewport.
        {
            ClearAll(kBlack, 0);
            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Always, StencilOperation::Replace, 1));
            DrawLeftHalfQuad(kBlack);

            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Equal, StencilOperation::Keep, 1));
            DrawQuad(kRed);
            check(RegionIs(LeftHalf(), kRed),
                  "check A: the stencil-equal pass paints where the earlier pass wrote the reference");
            check(RegionIs(RightHalf(), kBlack),
                  "check A: and paints nothing where it did not");
        }

        // Check B -- ReferenceStencil is an independent device property; changing it alone must
        // re-gate the very same DepthStencilState.
        {
            dev.setReferenceStencilProperty(2);
            DrawQuad(kGreen);
            check(RegionIs(LeftHalf(), kRed) && RegionIs(RightHalf(), kBlack),
                  "check B: raising ReferenceStencil on its own makes the equal test fail everywhere");

            dev.setReferenceStencilProperty(1);
            DrawQuad(kGreen);
            check(RegionIs(LeftHalf(), kGreen),
                  "check B: restoring ReferenceStencil on its own makes it pass again");
            check(RegionIs(RightHalf(), kBlack),
                  "check B: and still only where the stencil says so");
        }

        // Check C -- StencilFail. A Never comparison rejects every fragment, but its fail
        // operation still runs, so the plane it stamps gates a later equal test.
        {
            ClearAll(kBlack, 0);
            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Never, StencilOperation::Keep, 7,
                        StencilOperation::Replace));
            DrawLeftHalfQuad(kRed);
            check(RegionIs(Whole(), kBlack),
                  "check C: a Never stencil comparison rejects every fragment's colour");

            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Equal, StencilOperation::Keep, 7));
            DrawQuad(kGreen);
            check(RegionIs(LeftHalf(), kGreen) && RegionIs(RightHalf(), kBlack),
                  "check C: but its StencilFail operation still wrote the stencil plane");
        }

        // Check D -- StencilWriteMask 0 suppresses the write while leaving the test running.
        {
            ClearAll(kBlack, 0);
            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Always, StencilOperation::Replace, 5,
                        StencilOperation::Keep, /*writeMask=*/0));
            DrawLeftHalfQuad(kRed);
            check(RegionIs(LeftHalf(), kRed),
                  "check D: a zero StencilWriteMask does not stop the fragment being drawn");

            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Equal, StencilOperation::Keep, 5));
            DrawQuad(kGreen);
            check(RegionIs(LeftHalf(), kRed) && RegionIs(RightHalf(), kBlack),
                  "check D: but it did suppress the stencil write, so nothing equals 5");
        }

        // Check E -- XNA's Increment WRAPS and IncrementSaturation CLAMPS; GL's GL_INCR is the
        // clamping one, so a mapping that trusts the names fails exactly one half of this.
        {
            ClearAll(kBlack, 255);
            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Always, StencilOperation::Increment, 0));
            DrawQuad(kBlack);
            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Equal, StencilOperation::Keep, 0));
            DrawQuad(kRed);
            check(RegionIs(Whole(), kRed),
                  "check E: StencilOperation.Increment wraps 255 round to 0");

            ClearAll(kBlack, 255);
            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Always, StencilOperation::IncrementSaturation, 0));
            DrawQuad(kBlack);
            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Equal, StencilOperation::Keep, 0));
            DrawQuad(kRed);
            check(RegionIs(Whole(), kBlack),
                  "check E: StencilOperation.IncrementSaturation clamps 255 to 255 instead");

            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Equal, StencilOperation::Keep, 255));
            DrawQuad(kGreen);
            check(RegionIs(Whole(), kGreen),
                  "check E: and the saturated value really is still 255");
        }

        // Check F -- two-sided stencil. With culling off, the counter-clockwise half of the state
        // must be what a counter-clockwise-wound triangle uses. Only the CCW half replaces; the
        // clockwise half keeps. So the reversed quad stamps the plane and the ordinary one does
        // not -- swapping the two faces fails this pair, and applying one half to both fails it too.
        {
            dev.setRasterizerStateProperty(RasterizerState::CullNone);

            DepthStencilState twoSided = Stencil(CompareFunction::Always, StencilOperation::Keep, 3);
            twoSided.setTwoSidedStencilModeProperty(true);
            twoSided.setCounterClockwiseStencilFunctionProperty(CompareFunction::Always);
            twoSided.setCounterClockwiseStencilPassProperty(StencilOperation::Replace);
            twoSided.setCounterClockwiseStencilFailProperty(StencilOperation::Keep);
            twoSided.setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation::Keep);

            ClearAll(kBlack, 0);
            dev.setDepthStencilStateProperty(twoSided);
            DrawReversedQuad(kBlack);          // counter-clockwise on screen -> Replace with 3
            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Equal, StencilOperation::Keep, 3));
            DrawQuad(kRed);
            check(RegionIs(Whole(), kRed),
                  "check F: the CounterClockwise* stencil half applies to a counter-clockwise triangle");

            ClearAll(kBlack, 0);
            dev.setDepthStencilStateProperty(twoSided);
            DrawQuad(kBlack);                  // clockwise on screen -> the primary half: Keep
            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Equal, StencilOperation::Keep, 3));
            DrawQuad(kRed);
            check(RegionIs(Whole(), kBlack),
                  "check F: and the primary half applies to a clockwise triangle, which keeps");

            dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        }

        // Check G -- transitions. A stencil-disabled state must ignore the plane entirely, and
        // re-enabling must restore the test rather than leave it off.
        {
            ClearAll(kBlack, 0);
            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Always, StencilOperation::Replace, 1));
            DrawLeftHalfQuad(kBlack);

            dev.setDepthStencilStateProperty(DepthStencilState::None);
            DrawQuad(kRed);
            check(RegionIs(Whole(), kRed),
                  "check G: a stencil-disabled DepthStencilState draws everywhere");

            dev.setDepthStencilStateProperty(
                Stencil(CompareFunction::Equal, StencilOperation::Keep, 1));
            DrawQuad(kGreen);
            check(RegionIs(LeftHalf(), kGreen) && RegionIs(RightHalf(), kRed),
                  "check G: re-enabling the stencil test restores the gating");
        }

        Finish();
    }

public:
    PortableGLStencilTest() : PortableGLTestGame(kSize) {}
};

int main()
{
    PortableGLStencilTest game;
    game.Run();
    return game.getResult();
}
