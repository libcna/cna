// SPDX-License-Identifier: MS-PL
// RasterizerState / Viewport / ScissorRectangle regression test for the PortableGL renderer.
//
// PortableGL overrode none of ApplyRasterizerState, SetViewport or SetScissorRect, so
// GraphicsDevice updated its own caches while the framebuffer kept rasterizing exactly as before:
// CullMode, FillMode, ScissorTestEnable, the scissor rectangle, the viewport rectangle and the
// viewport depth range were all publicly settable and privately ignored.
//
// Every rectangle check below is deliberately asymmetric top-to-bottom or left-to-right, because
// PortableGL's window origin is bottom-left while XNA's viewport and scissor rectangles are
// top-left: an implementation that forgets the Y conversion paints the mirrored band and fails.
//
// Check A -- Viewport confines rasterization to its own rectangle, at the top of the image.
// Check B -- Viewport.MinDepth/MaxDepth really remap the window depth (differential vs the depth test).
// Check C -- ScissorRectangle plus ScissorTestEnable clips to the top-left quadrant.
// Check D -- disabling the scissor test restores full-target drawing AND full-target clears.
// Check E -- CullMode culls the winding it names, in both directions, and CullNone culls neither.
// Check F -- FillMode.WireFrame outlines instead of filling (the WireFrame capability's oracle).
// Check G -- RasterizerState.DepthBias shifts the depth comparison.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "portablegl_test_support.hpp"

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace portablegl_test;

namespace
{
    constexpr int kSize = 32;
    const Color kBlack(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
}

class PortableGLRasterStateTest : public PortableGLTestGame
{
    Rectangle Whole() const { return Rectangle(0, 0, kSize, kSize); }

    void DrawVertices(const VertexPositionColor* verts, int count)
    {
        auto& dev = getGraphicsDeviceProperty();
        VertexBuffer vb(dev, PosColorDecl(), count, BufferUsage::None);
        vb.SetData(verts, 0, count);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, count / 3);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawQuad(const Color& color, float ndcZ = 0.0f)
    {
        DrawVertices(FullQuad(color, ndcZ).data(), 6);
    }

    void DrawReversedQuad(const Color& color)
    {
        DrawVertices(ReversedFullQuad(color).data(), 6);
    }

    void RestoreFullViewport()
    {
        getGraphicsDeviceProperty().setViewportProperty(Viewport(0, 0, kSize, kSize));
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);

        // Check A -- an XNA viewport at (0,0) is the TOP of the readback image. A missing Y flip
        // paints rows 24..31 instead of 0..7 and fails both halves of this check.
        {
            dev.Clear(kBlack);
            dev.setViewportProperty(Viewport(0, 0, kSize, 8));
            DrawQuad(kRed);
            RestoreFullViewport();
            check(RegionIs(Rectangle(0, 0, kSize, 8), kRed),
                  "check A: a top-anchored Viewport rasterizes into the top rows of the image");
            check(RegionIs(Rectangle(0, 8, kSize, kSize - 8), kBlack),
                  "check A: and nothing outside it");
        }

        // Check B -- MinDepth/MaxDepth. The quad sits at NDC z = -1, i.e. window depth MinDepth.
        // With the default range that is 0.0 and beats a depth buffer cleared to 0.4 under Less;
        // with MinDepth raised to 0.5 the same geometry loses. Only a real depth-range mapping
        // separates the two.
        {
            DepthStencilState depthLess;
            depthLess.setDepthBufferEnableProperty(true);
            depthLess.setDepthBufferWriteEnableProperty(true);
            depthLess.setDepthBufferFunctionProperty(CompareFunction::Less);
            dev.setDepthStencilStateProperty(depthLess);

            dev.setViewportProperty(Viewport(0, 0, kSize, kSize));
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kBlack, 0.4f, 0);
            DrawQuad(kRed, -1.0f);
            check(RegionIs(Whole(), kRed),
                  "check B: at the default depth range the near quad passes a Less test at 0.4");

            Viewport deep(0, 0, kSize, kSize);
            deep.setMinDepthProperty(0.5f);
            deep.setMaxDepthProperty(1.0f);
            dev.setViewportProperty(deep);
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kBlack, 0.4f, 0);
            DrawQuad(kRed, -1.0f);
            check(RegionIs(Whole(), kBlack),
                  "check B: raising Viewport.MinDepth pushes the same geometry behind 0.4");

            RestoreFullViewport();
            dev.setDepthStencilStateProperty(DepthStencilState::None);
        }

        // Check C -- the scissor rectangle, again top-left anchored. The full-target clear happens
        // BEFORE the test is enabled, because a clear is itself scissored once it is on.
        {
            dev.Clear(kBlack);
            RasterizerState scissored;
            scissored.setScissorTestEnableProperty(true);
            dev.setRasterizerStateProperty(scissored);
            dev.setScissorRectangleProperty(Rectangle(0, 0, kSize / 2, kSize / 2));
            DrawQuad(kRed);
            check(RegionIs(Rectangle(0, 0, kSize / 2, kSize / 2), kRed),
                  "check C: ScissorRectangle clips drawing to the top-left quadrant it names");
            check(RegionIs(Rectangle(0, kSize / 2, kSize, kSize / 2), kBlack) &&
                      RegionIs(Rectangle(kSize / 2, 0, kSize / 2, kSize / 2), kBlack),
                  "check C: and nothing is drawn outside it");
        }

        // Check D -- disabling the test restores the full target for drawing AND for clearing.
        // (PortableGL folds the scissor into its always-on rasterizer bounds the moment glScissor
        // is called, so a renderer that hands the rectangle over while the test is off corrupts
        // the next full-target clear.)
        {
            dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
            dev.Clear(kGreen);
            check(RegionIs(Whole(), kGreen),
                  "check D: with the scissor test off, Clear covers the whole target again");
            DrawQuad(kRed);
            check(RegionIs(Whole(), kRed),
                  "check D: and drawing covers the whole target again");
        }

        // Check E -- culling, both directions plus none.
        {
            dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
            dev.Clear(kBlack);
            DrawQuad(kRed);                 // clockwise on screen -> front facing -> kept
            check(RegionIs(Whole(), kRed),
                  "check E: CullCounterClockwise keeps a clockwise-wound triangle");

            dev.Clear(kBlack);
            DrawReversedQuad(kRed);         // counter-clockwise -> culled
            check(RegionIs(Whole(), kBlack),
                  "check E: CullCounterClockwise culls a counter-clockwise-wound triangle");

            dev.setRasterizerStateProperty(RasterizerState::CullClockwise);
            dev.Clear(kBlack);
            DrawQuad(kRed);
            check(RegionIs(Whole(), kBlack),
                  "check E: CullClockwise culls the clockwise-wound triangle instead");

            dev.Clear(kBlack);
            DrawReversedQuad(kGreen);
            check(RegionIs(Whole(), kGreen),
                  "check E: and keeps the counter-clockwise-wound one");

            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.Clear(kBlack);
            DrawReversedQuad(kRed);
            check(RegionIs(Whole(), kRed), "check E: CullNone culls neither winding");
        }

        // Check F -- wireframe. The interior of a filled quad is painted; the interior of a
        // wireframe one is not, while its outline still is. Both halves matter: "nothing drawn at
        // all" would satisfy the interior check on its own.
        {
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.Clear(kBlack);
            DrawQuad(kRed);
            const int filledCount = CountPixelsDifferentFrom(Whole(), kBlack);
            check(RegionIs(Rectangle(kSize / 2, kSize / 4, 2, 2), kRed),
                  "check F: a solid quad fills its interior");

            RasterizerState wire;
            wire.setCullModeProperty(CullMode::None);
            wire.setFillModeProperty(FillMode::WireFrame);
            dev.setRasterizerStateProperty(wire);
            dev.Clear(kBlack);
            DrawQuad(kRed);
            const int wireCount = CountPixelsDifferentFrom(Whole(), kBlack);
            check(RegionIs(Rectangle(kSize / 2, kSize / 4, 2, 2), kBlack),
                  "check F: FillMode.WireFrame leaves the same interior unpainted");
            check(wireCount > 0 && wireCount < filledCount / 2,
                  "check F: but does draw the outline -- some pixels, far fewer than the fill");

            dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        }

        // Check G -- DepthBias. Two coplanar quads under a Less depth test: the second is normally
        // rejected, and a negative constant bias pulls it in front so it wins.
        {
            DepthStencilState depthLess;
            depthLess.setDepthBufferEnableProperty(true);
            depthLess.setDepthBufferWriteEnableProperty(true);
            depthLess.setDepthBufferFunctionProperty(CompareFunction::Less);
            dev.setDepthStencilStateProperty(depthLess);

            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kBlack, 1.0f, 0);
            DrawQuad(kRed, 0.0f);
            DrawQuad(kGreen, 0.0f);
            check(RegionIs(Whole(), kRed),
                  "check G: without a bias, the coplanar second quad loses the Less depth test");

            RasterizerState biased;
            biased.setDepthBiasProperty(-2000.0f);
            dev.setRasterizerStateProperty(biased);
            DrawQuad(kGreen, 0.0f);
            check(RegionIs(Whole(), kGreen),
                  "check G: a negative RasterizerState.DepthBias pulls it in front and it wins");

            dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
            dev.setDepthStencilStateProperty(DepthStencilState::None);
        }

        Finish();
    }

public:
    PortableGLRasterStateTest() : PortableGLTestGame(kSize) {}
};

int main()
{
    PortableGLRasterStateTest game;
    game.Run();
    return game.getResult();
}
