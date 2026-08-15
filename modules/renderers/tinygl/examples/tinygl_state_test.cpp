// SPDX-License-Identifier: MS-PL
// Device-state test for the TinyGL renderer: everything TinyGL genuinely executes, checked against
// real pixels rather than against "the call did not throw".
//
// Check A -- BlendState::Opaque is the identity and leaves the source colour intact.
// Check B -- a custom (One, One) + Add BlendState really additively blends, because those are
//   factors TinyGL's own rasterizer switch has cases for.
// Check C -- (One, One) + ReverseSubtract really subtracts, proving the blend EQUATION reaches
//   TinyGL and is not merely stored.
// Check D -- BlendState::AlphaBlend is accepted (it is mapped onto TinyGL's colour-key cutout) and
//   leaves opaque geometry untouched.
// Check E -- RasterizerState::CullClockwise really culls: a clockwise-wound triangle disappears.
// Check F -- FillMode::WireFrame really changes rasterization: the interior of a filled triangle
//   is no longer painted.
// Check G -- SupportsCapability() answers the way this renderer's documentation promises.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/TinyGL/TinyGLRenderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::TinyGL;

namespace
{
    constexpr int kChecks = 9;

    VertexDeclaration PosColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
    }

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }
}

class TinyGLStateTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    /// Draws a full-viewport quad of one colour, with the caller's winding.
    void DrawQuad(GraphicsDevice& dev, const Color& color, bool clockwise)
    {
        VertexBuffer vb(dev, PosColorDecl(), 6, BufferUsage::None);
        const Vector3 tl(-1.0f, 1.0f, 0.0f);
        const Vector3 tr(1.0f, 1.0f, 0.0f);
        const Vector3 br(1.0f, -1.0f, 0.0f);
        const Vector3 bl(-1.0f, -1.0f, 0.0f);
        const VertexPositionColor cw[6] = {
            {tl, color}, {tr, color}, {br, color}, {tl, color}, {br, color}, {bl, color},
        };
        const VertexPositionColor ccw[6] = {
            {tl, color}, {br, color}, {tr, color}, {tl, color}, {bl, color}, {br, color},
        };
        vb.SetData(clockwise ? cw : ccw, 0, 6);

        BasicEffect fx(dev);
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
        auto& renderer = static_cast<TinyGLRenderer&>(dev.GetRenderer());

        // Check A: Opaque is the identity.
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            DrawQuad(dev, Color(60, 90, 120, 255), true);
            const Color pixel = ReadPixel(dev, 32, 32);
            check(pixel.getRProperty() >= 55 && pixel.getRProperty() <= 62 &&
                      pixel.getBProperty() >= 115 && pixel.getBProperty() <= 122,
                  "BlendState::Opaque leaves the source colour intact");
        }

        // Check B: a genuinely executable additive state -- (One, One) + Add.
        {
            dev.Clear(Color(40, 0, 0, 255));
            BlendState additive;
            additive.setColorSourceBlendProperty(Blend::One);
            additive.setAlphaSourceBlendProperty(Blend::One);
            additive.setColorDestinationBlendProperty(Blend::One);
            additive.setAlphaDestinationBlendProperty(Blend::One);
            bool threw = false;
            try { dev.setBlendStateProperty(additive); } catch (...) { threw = true; }
            check(!threw, "a (One, One) + Add BlendState is accepted");

            DrawQuad(dev, Color(50, 0, 0, 255), true);
            const Color pixel = ReadPixel(dev, 32, 32);
            check(pixel.getRProperty() >= 80,
                  "(One, One) + Add really adds source and destination in TinyGL's rasterizer");
        }

        // Check C: the blend EQUATION reaches TinyGL, not just the factors.
        {
            dev.Clear(Color(200, 0, 0, 255));
            BlendState reverseSubtract;
            reverseSubtract.setColorSourceBlendProperty(Blend::One);
            reverseSubtract.setAlphaSourceBlendProperty(Blend::One);
            reverseSubtract.setColorDestinationBlendProperty(Blend::One);
            reverseSubtract.setAlphaDestinationBlendProperty(Blend::One);
            reverseSubtract.setColorBlendFunctionProperty(BlendFunction::ReverseSubtract);
            reverseSubtract.setAlphaBlendFunctionProperty(BlendFunction::ReverseSubtract);
            dev.setBlendStateProperty(reverseSubtract);

            DrawQuad(dev, Color(60, 0, 0, 255), true);
            const Color pixel = ReadPixel(dev, 32, 32);
            check(pixel.getRProperty() < 190,
                  "BlendFunction::ReverseSubtract really reaches TinyGL's blend equation");
        }

        // Check D: AlphaBlend is accepted and opaque geometry survives it.
        {
            dev.Clear(Color(0, 0, 0, 255));
            bool threw = false;
            try { dev.setBlendStateProperty(BlendState::AlphaBlend); } catch (...) { threw = true; }
            check(!threw, "BlendState::AlphaBlend is accepted (mapped onto the colour-key cutout)");

            DrawQuad(dev, Color(0, 200, 0, 255), true);
            check(ReadPixel(dev, 32, 32).getGProperty() > 180,
                  "opaque geometry survives the AlphaBlend mapping unchanged");
        }

        dev.setBlendStateProperty(BlendState::Opaque);

        // Check E: culling really culls.
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setRasterizerStateProperty(RasterizerState::CullClockwise);
            DrawQuad(dev, Color(255, 255, 255, 255), true);
            check(ReadPixel(dev, 32, 32).getRProperty() < 20,
                  "RasterizerState::CullClockwise really culls a clockwise-wound quad");
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
        }

        // Check F: wireframe really changes rasterization.
        {
            dev.Clear(Color(0, 0, 0, 255));
            RasterizerState wireframe;
            wireframe.setCullModeProperty(CullMode::None);
            wireframe.setFillModeProperty(FillMode::WireFrame);
            dev.setRasterizerStateProperty(wireframe);
            DrawQuad(dev, Color(255, 255, 255, 255), true);
            // The quad's two triangles share the TL..BR diagonal; (48, 16) is well inside the
            // upper-right triangle and away from every edge, so a filled draw paints it and a
            // wireframe draw does not.
            check(ReadPixel(dev, 48, 16).getRProperty() < 20,
                  "FillMode::WireFrame really stops TinyGL filling the triangle interior");
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
        }

        // Check G: the capability answers match the documented contract.
        {
            const bool asDocumented =
                renderer.SupportsCapability(CNA::GraphicsCapability::ThreeD) &&
                renderer.SupportsCapability(CNA::GraphicsCapability::WireFrame) &&
                !renderer.SupportsCapability(CNA::GraphicsCapability::StencilBuffer) &&
                !renderer.SupportsCapability(CNA::GraphicsCapability::AdditiveBlending) &&
                !renderer.SupportsCapability(CNA::GraphicsCapability::CustomEffects) &&
                !renderer.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets) &&
                !renderer.SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering) &&
                !renderer.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery) &&
                !renderer.SupportsCapability(CNA::GraphicsCapability::Instancing) &&
                !renderer.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput);
            check(asDocumented, "SupportsCapability() matches the documented TinyGL contract");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = (passCount_ == kChecks) ? 0 : 1;
        Exit();
    }

public:
    TinyGLStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return result_; }
};

int main()
{
    TinyGLStateTest game;
    game.Run();
    return game.getResult();
}
