// SPDX-License-Identifier: MS-PL
// Depth-test and backbuffer-resize pixel-oracle test for the PortableGL (rswinkle/PortableGL)
// graphics renderer. Both capabilities are advertised by the renderer -- SupportsCapability
// (DepthStencilBuffer) is true and SetVirtualResolution() really re-allocates PortableGL's
// framebuffer -- and neither was verified end to end anywhere before this file existed.
//
// Check A -- depth test ON, far quad then near quad: the NEAR color wins.
// Check B -- depth test ON, near quad then far quad: the NEAR color still wins, i.e. the later
//   draw is genuinely REJECTED by the depth comparison. A and B together rule out "whatever was
//   drawn last simply overwrote the buffer".
// Check C -- depth test OFF (DepthStencilState::None), near quad then far quad: now the FAR color
//   wins. This is the control that proves A/B were produced by the depth test doing real work and
//   not by some fixed draw ordering, and that DepthStencilState reaches PortableGL for real.
// Check D -- a resize to a LARGER backbuffer (GraphicsDevice::Reset with new
//   PresentationParameters, which is the public route into SetVirtualResolution): GetViewportSize()
//   reports the new size, and a clear plus a full-viewport draw produce correct pixels at
//   coordinates that did not exist at the original size.
// Check E -- the same, resizing to a SMALLER backbuffer, across the whole new extent.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/PortableGL/PortableGLRenderer.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::PortableGL;

namespace
{
    constexpr int kSize = 64;

    VertexDeclaration PosColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
    }
}

class PortableGLDepthResizeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    bool RegionIs(const Rectangle& region, const Color& expected)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(region.Width) *
                                      static_cast<std::size_t>(region.Height),
                                  Color(0, 0, 0, 0));
        getGraphicsDeviceProperty().GetBackBufferData(
            &region, pixels.data(), 0, static_cast<int>(pixels.size()));
        for (const Color& p : pixels)
        {
            if (p.getRProperty() != expected.getRProperty() ||
                p.getGProperty() != expected.getGProperty() ||
                p.getBProperty() != expected.getBProperty())
                return false;
        }
        return true;
    }

    /// Resizes the backbuffer through the public XNA route -- GraphicsDevice::Reset() with new
    /// PresentationParameters -- which is what reaches IGraphicsRenderer::SetVirtualResolution().
    void ResizeBackBuffer(int width, int height)
    {
        auto& dev = getGraphicsDeviceProperty();
        PresentationParameters pp = dev.getPresentationParametersProperty().Clone();
        pp.setBackBufferWidthProperty(width);
        pp.setBackBufferHeightProperty(height);
        dev.Reset(pp);
    }

    /// Draws a full-viewport quad at NDC depth @p ndcZ in @p color. PortableGL maps clip-space z
    /// from [-1,1] onto the [0,1] depth range (glDepthRange's default), the same post-divide 0..1
    /// depth convention SoftwareRenderer documents, so a smaller ndcZ is nearer the viewer and the
    /// default LessEqual comparison keeps it.
    void DrawFullViewportQuad(float ndcZ, const Color& color)
    {
        auto& dev = getGraphicsDeviceProperty();
        // Clockwise in XNA's top-left screen space (TL, TR, BR / TL, BR, BL), i.e. front-facing
        // under XNA's own default RasterizerState.CullCounterClockwise.
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f, 1.0f, ndcZ),  color },
            { Vector3(1.0f, 1.0f, ndcZ),   color },
            { Vector3(1.0f, -1.0f, ndcZ),  color },
            { Vector3(-1.0f, 1.0f, ndcZ),  color },
            { Vector3(1.0f, -1.0f, ndcZ),  color },
            { Vector3(-1.0f, -1.0f, ndcZ), color },
        };
        VertexBuffer vb(dev, PosColorDecl(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        BasicEffect fx(dev);
        // BasicEffect.VertexColorEnabled defaults to false and this renderer honours that, so a
        // check that reads back the packed vertex colour has to enable it explicitly.
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<PortableGLRenderer&>(dev.GetRenderer());

        const Color nearColor(0, 255, 0, 255);
        const Color farColor(255, 0, 0, 255);
        constexpr float kNearZ = -0.5f;
        constexpr float kFarZ = 0.5f;

        // Check A -- depth ON, far first.
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.Clear(Color::Black);
        DrawFullViewportQuad(kFarZ, farColor);
        DrawFullViewportQuad(kNearZ, nearColor);
        check(RegionIs(Rectangle(28, 28, 8, 8), nearColor),
              "check A: with depth testing on, a nearer quad drawn after a farther one wins");

        // Check B -- depth ON, near first: the farther quad must be rejected.
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.Clear(Color::Black);
        DrawFullViewportQuad(kNearZ, nearColor);
        DrawFullViewportQuad(kFarZ, farColor);
        check(RegionIs(Rectangle(28, 28, 8, 8), nearColor),
              "check B: with depth testing on, a farther quad drawn after a nearer one is rejected");
        check(RegionIs(Rectangle(0, 0, 3, 3), nearColor),
              "check B: the depth rejection holds across the whole viewport, not only its centre");

        // Check C -- control: depth OFF, same order as B, so the last draw must win instead.
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.Clear(Color::Black);
        DrawFullViewportQuad(kNearZ, nearColor);
        DrawFullViewportQuad(kFarZ, farColor);
        check(RegionIs(Rectangle(28, 28, 8, 8), farColor),
              "check C: with depth testing off, the last draw wins -- A/B were the depth test's doing");

        // Check D -- grow the backbuffer.
        {
            constexpr int kBig = 96;
            ResizeBackBuffer(kBig, kBig);
            int w = 0, h = 0;
            renderer.GetViewportSize(w, h);
            check(w == kBig && h == kBig,
                  "check D: GetViewportSize reports the enlarged virtual resolution");

            const Color clearColor(10, 20, 30, 255);
            dev.Clear(clearColor);
            check(RegionIs(Rectangle(kBig - 4, kBig - 4, 4, 4), clearColor),
                  "check D: Clear reaches the far corner that did not exist at the original size");

            const Color drawColor(0, 128, 255, 255);
            DrawFullViewportQuad(0.0f, drawColor);
            check(RegionIs(Rectangle(0, 0, 4, 4), drawColor) &&
                      RegionIs(Rectangle(kBig / 2, kBig / 2, 4, 4), drawColor) &&
                      RegionIs(Rectangle(kBig - 4, kBig - 4, 4, 4), drawColor),
                  "check D: a full-viewport draw covers the whole enlarged backbuffer");
        }

        // Check E -- shrink the backbuffer below the original size.
        {
            constexpr int kSmall = 32;
            ResizeBackBuffer(kSmall, kSmall);
            int w = 0, h = 0;
            renderer.GetViewportSize(w, h);
            check(w == kSmall && h == kSmall,
                  "check E: GetViewportSize reports the reduced virtual resolution");

            const Color clearColor(90, 80, 70, 255);
            dev.Clear(clearColor);
            check(RegionIs(Rectangle(0, 0, kSmall, kSmall), clearColor),
                  "check E: Clear covers the entire reduced backbuffer");

            const Color drawColor(255, 200, 0, 255);
            DrawFullViewportQuad(0.0f, drawColor);
            check(RegionIs(Rectangle(0, 0, kSmall, kSmall), drawColor),
                  "check E: a full-viewport draw covers the entire reduced backbuffer");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 10);
        result_ = (passCount_ == 10) ? 0 : 1;
        Exit();
    }

public:
    PortableGLDepthResizeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    PortableGLDepthResizeTest game;
    game.Run();
    return game.getResult();
}
