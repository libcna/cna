// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-16: real dynamic BlendState/DepthStencilState/RasterizerState mapping for
// the OpenGL4 graphics renderer -- previously every 3D draw used whatever GL's own defaults were;
// GraphicsDevice.BlendState/DepthStencilState/RasterizerState assignments had no effect at all.
//
// BlendState (ApplyBlendState):
// Check A -- BlendState::Opaque (Blend::One/Blend::Zero, XNA's "no blending" encoding) truly
//   disables blending: a 50%-alpha red draw over a green background shows FULL red, not a blend.
// Check B -- BlendState::NonPremultiplied (SourceBlend=SourceAlpha, DestinationBlend=
//   InverseSourceAlpha -- the standard straight-alpha blend; BlendState::AlphaBlend is
//   deliberately NOT used here since it assumes an already-premultiplied source colour and would
//   need the test's own red pre-multiplied by its alpha first) blends correctly: the same
//   50%-alpha red draw over green now shows the expected src-over blend math, not full red.
// Check C -- a custom BlendState (SourceBlend=One, DestinationBlend=One, both funcs=Add, i.e.
//   additive) sums a red draw over a pre-existing green background into yellow -- proves the
//   general glBlendFuncSeparate/glBlendEquationSeparate wiring, not just the 2 built-in presets.
// Check D -- SetBlendFactor (GraphicsDevice.BlendFactor / glBlendColor): a custom BlendState using
//   Blend::BlendFactor as its source factor with a 50%-grey BlendFactor blends a full-alpha red
//   draw down to half brightness -- proves the constant blend colour actually reaches the GPU.
//
// DepthStencilState (ApplyDepthStencilState):
// Check E -- DepthStencilState::Default (depth read+write enabled): a farther red quad then a
//   nearer green quad -- the nearer one wins, proving depth state applies via the state object,
//   not just the standalone SetDepthTestEnabled/SetDepthWriteEnabled toggles GL4-9 already covers.
// Check F -- a real 2-pass stencil test: pass 1 writes ReferenceStencil=1 into the stencil buffer
//   for a quad's footprint (StencilFunction=Always, StencilPass=Replace); pass 2 draws a
//   full-screen quad with StencilFunction=Equal, ReferenceStencil=1 -- only the stencilled
//   footprint shows the pass-2 colour, everywhere else keeps the pass-1 background.
//
// RasterizerState (ApplyRasterizerState) + SetScissorRect:
// Check G/H -- CullMode's front/back-face selection is real, not a fixed "always cull"/"never
//   cull" no-op. Uses the SAME triangle winding `examples/easygl_rasterizerstate_cullmode_test.cpp`
//   already empirically verified (there, its "DrawQuadCW" -- negative NDC signed area) survives
//   XNA's own default RasterizerState.CullCounterClockwise and is culled by CullClockwise; this
//   test's triangle has the identical negative-signed-area winding, so the same two outcomes are
//   the correct, authoritative expectation here too (not merely "whatever this renderer happens to
//   do").
// Check G -- RasterizerState::CullCounterClockwise (XNA's own default) keeps the triangle visible.
// Check H -- RasterizerState::CullClockwise culls that same triangle.
// Check I -- RasterizerState.ScissorTestEnable=true + GraphicsDevice.ScissorRectangle: a
//   full-screen quad only appears inside the scissor rectangle.
// Check J -- RasterizerState.FillMode=WireFrame: a point strictly inside one of the quad's two
//   triangles (away from every edge, including the shared diagonal that a filled quad's two
//   triangles share -- the fullscreen quad's own centre pixel sits exactly ON that diagonal, so
//   this check deliberately samples elsewhere) stays background, proving
//   glPolygonMode(GL_FRONT_AND_BACK, GL_LINE) is real, not silently falling back to a filled solid.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol = 8)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    void DrawFullscreenQuad(GraphicsDevice& dev, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            {Vector3(-1.0f, -1.0f, 0.0f), color}, {Vector3(-1.0f, 1.0f, 0.0f), color}, {Vector3(1.0f, -1.0f, 0.0f), color},
            {Vector3(-1.0f, 1.0f, 0.0f), color}, {Vector3(1.0f, 1.0f, 0.0f), color}, {Vector3(1.0f, -1.0f, 0.0f), color},
        };
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    // Negative NDC signed area -- the exact same winding as
    // examples/easygl_rasterizerstate_cullmode_test.cpp's own empirically-verified "DrawQuadCW",
    // which survives XNA's default RasterizerState.CullCounterClockwise and is culled by
    // CullClockwise (see that file's own header comment for how this was established).
    void DrawCwTriangle(GraphicsDevice& dev, const Color& color)
    {
        const VertexPositionColor verts[3] = {
            {Vector3(-0.8f, -0.8f, 0.0f), color},
            {Vector3(0.0f, 0.8f, 0.0f), color},
            {Vector3(0.8f, -0.8f, 0.0f), color},
        };
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        vb.SetData(verts, 0, 3);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);
    }
}

class OpenGL4RenderStateTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);

        // --- Check A: BlendState::Opaque truly disables blending ---
        {
            dev.Clear(Color::Green);
            dev.setBlendStateProperty(BlendState::Opaque);
            DrawFullscreenQuad(dev, Color(255, 0, 0, 128)); // 50%-alpha red
            ExpectPixel("Check A: BlendState::Opaque ignores alpha -- full red, not blended",
                        centre, Color::Red);
        }

        // --- Check B: BlendState::NonPremultiplied blends correctly (straight-alpha src-over) ---
        {
            dev.Clear(Color::Green);
            dev.setBlendStateProperty(BlendState::NonPremultiplied);
            DrawFullscreenQuad(dev, Color(255, 0, 0, 128)); // 50%-alpha red over green
            // src-over: result = src.rgb*srcA + dst.rgb*(1-srcA); alpha ~128/255 ~= 0.502.
            // Color::Green in this codebase is XNA's real (0,128,0) (Lime is the (0,255,0) one),
            // so dst.g=128, not 255: 0*0.502 + 128*0.498 ~= 64.
            const Color expected(128, 64, 0, 255);
            ExpectPixel("Check B: BlendState::NonPremultiplied produces the expected src-over blend",
                        centre, expected, /*tolerance=*/12);
        }

        // --- Check C: custom additive BlendState sums red + green into yellow ---
        {
            dev.Clear(Color::Black);
            dev.setBlendStateProperty(BlendState::Opaque);
            DrawFullscreenQuad(dev, Color(0, 255, 0, 255));

            BlendState additive;
            additive.setColorSourceBlendProperty(Blend::One);
            additive.setColorDestinationBlendProperty(Blend::One);
            additive.setAlphaSourceBlendProperty(Blend::One);
            additive.setAlphaDestinationBlendProperty(Blend::One);
            additive.setColorBlendFunctionProperty(BlendFunction::Add);
            additive.setAlphaBlendFunctionProperty(BlendFunction::Add);
            dev.setBlendStateProperty(additive);
            DrawFullscreenQuad(dev, Color(255, 0, 0, 255));

            ExpectPixel("Check C: a custom additive BlendState sums red+green into yellow",
                        centre, Color::Yellow, /*tolerance=*/8);
        }

        // --- Check D: SetBlendFactor (glBlendColor) actually reaches the GPU ---
        {
            dev.Clear(Color::Black);
            dev.setBlendStateProperty(BlendState::Opaque);
            DrawFullscreenQuad(dev, Color::Black);

            BlendState factorBlend;
            factorBlend.setColorSourceBlendProperty(Blend::BlendFactor);
            factorBlend.setColorDestinationBlendProperty(Blend::Zero);
            factorBlend.setAlphaSourceBlendProperty(Blend::BlendFactor);
            factorBlend.setAlphaDestinationBlendProperty(Blend::Zero);
            factorBlend.setColorBlendFunctionProperty(BlendFunction::Add);
            factorBlend.setAlphaBlendFunctionProperty(BlendFunction::Add);
            factorBlend.setBlendFactorProperty(Color(128, 128, 128, 255)); // baked-in 50% grey
            dev.setBlendStateProperty(factorBlend);
            DrawFullscreenQuad(dev, Color(255, 255, 255, 255)); // full-alpha white * 50% factor

            ExpectPixel("Check D: SetBlendFactor's constant colour actually scales the draw",
                        centre, Color(128, 128, 128, 255), /*tolerance=*/8);
        }

        // --- Check E: DepthStencilState::Default depth test via the state object ---
        {
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setDepthStencilStateProperty(DepthStencilState::Default);

            const VertexPositionColor farVerts[6] = {
                {Vector3(-1.0f, -1.0f, 0.5f), Color::Red}, {Vector3(-1.0f, 1.0f, 0.5f), Color::Red}, {Vector3(1.0f, -1.0f, 0.5f), Color::Red},
                {Vector3(-1.0f, 1.0f, 0.5f), Color::Red}, {Vector3(1.0f, 1.0f, 0.5f), Color::Red}, {Vector3(1.0f, -1.0f, 0.5f), Color::Red},
            };
            VertexBuffer farVb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
            farVb.SetData(farVerts, 0, 6);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();
            dev.SetVertexBuffer(&farVb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            const VertexPositionColor nearVerts[6] = {
                {Vector3(-1.0f, -1.0f, -0.5f), Color::Green}, {Vector3(-1.0f, 1.0f, -0.5f), Color::Green}, {Vector3(1.0f, -1.0f, -0.5f), Color::Green},
                {Vector3(-1.0f, 1.0f, -0.5f), Color::Green}, {Vector3(1.0f, 1.0f, -0.5f), Color::Green}, {Vector3(1.0f, -1.0f, -0.5f), Color::Green},
            };
            VertexBuffer nearVb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
            nearVb.SetData(nearVerts, 0, 6);
            dev.SetVertexBuffer(&nearVb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);

            dev.setDepthStencilStateProperty(DepthStencilState::None);
            ExpectPixel("Check E: DepthStencilState::Default depth test -- nearer green quad wins",
                        centre, Color::Green);
        }

        // --- Check F: real 2-pass stencil test ---
        {
            dev.Clear(ClearOptions::Target | ClearOptions::Stencil, Color::Black, 1.0f, 0);
            dev.setBlendStateProperty(BlendState::Opaque);

            // Pass 1: always pass, replace stencil with 1, for a smaller centred quad only.
            DepthStencilState writeStencil;
            writeStencil.setDepthBufferEnableProperty(false);
            writeStencil.setStencilEnableProperty(true);
            writeStencil.setStencilFunctionProperty(CompareFunction::Always);
            writeStencil.setStencilPassProperty(StencilOperation::Replace);
            writeStencil.setReferenceStencilProperty(1);
            dev.setDepthStencilStateProperty(writeStencil);

            const VertexPositionColor smallVerts[6] = {
                {Vector3(-0.3f, -0.3f, 0.0f), Color::Blue}, {Vector3(-0.3f, 0.3f, 0.0f), Color::Blue}, {Vector3(0.3f, -0.3f, 0.0f), Color::Blue},
                {Vector3(-0.3f, 0.3f, 0.0f), Color::Blue}, {Vector3(0.3f, 0.3f, 0.0f), Color::Blue}, {Vector3(0.3f, -0.3f, 0.0f), Color::Blue},
            };
            VertexBuffer smallVb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
            smallVb.SetData(smallVerts, 0, 6);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();
            dev.SetVertexBuffer(&smallVb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);

            // Pass 2: full-screen white quad, only where the stencil buffer reads 1.
            DepthStencilState testStencil;
            testStencil.setDepthBufferEnableProperty(false);
            testStencil.setStencilEnableProperty(true);
            testStencil.setStencilFunctionProperty(CompareFunction::Equal);
            testStencil.setStencilPassProperty(StencilOperation::Keep);
            testStencil.setReferenceStencilProperty(1);
            dev.setDepthStencilStateProperty(testStencil);
            DrawFullscreenQuad(dev, Color::White);

            dev.setDepthStencilStateProperty(DepthStencilState::None);

            ExpectPixel("Check F: stencil test -- centre (inside the stencilled footprint) is white",
                        centre, Color::White);
            ExpectPixel("Check F: stencil test -- corner (outside the stencilled footprint) stays black",
                        Rectangle(2, 2, 1, 1), Color::Black);
        }

        // --- Check G/H: RasterizerState CullMode ---
        {
            dev.Clear(Color::Black);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
            DrawCwTriangle(dev, Color::Red);
            ExpectPixel("Check G: RasterizerState::CullCounterClockwise (XNA default) keeps the triangle visible",
                        centre, Color::Red);

            dev.Clear(Color::Black);
            dev.setRasterizerStateProperty(RasterizerState::CullClockwise);
            DrawCwTriangle(dev, Color::Red);
            ExpectPixel("Check H: RasterizerState::CullClockwise culls that same triangle",
                        centre, Color::Black);

            dev.setRasterizerStateProperty(RasterizerState::CullNone);
        }

        // --- Check I: ScissorTestEnable + ScissorRectangle ---
        {
            dev.Clear(Color::Black);
            dev.setBlendStateProperty(BlendState::Opaque);
            RasterizerState scissorRs;
            scissorRs.setCullModeProperty(CullMode::None);
            scissorRs.setScissorTestEnableProperty(true);
            dev.setRasterizerStateProperty(scissorRs);
            dev.setScissorRectangleProperty(Rectangle(0, 0, kSize / 2, kSize / 2));
            DrawFullscreenQuad(dev, Color::Red);

            ExpectPixel("Check I: inside the scissor rectangle is drawn",
                        Rectangle(kSize / 4, kSize / 4, 1, 1), Color::Red);
            ExpectPixel("Check I: outside the scissor rectangle stays background",
                        Rectangle(kSize - 2, kSize - 2, 1, 1), Color::Black);

            RasterizerState noScissor;
            noScissor.setCullModeProperty(CullMode::None);
            noScissor.setScissorTestEnableProperty(false);
            dev.setRasterizerStateProperty(noScissor);
        }

        // --- Check J: FillMode::WireFrame leaves the interior unfilled ---
        {
            dev.Clear(Color::Black);
            dev.setBlendStateProperty(BlendState::Opaque);
            RasterizerState wireframeRs;
            wireframeRs.setCullModeProperty(CullMode::None);
            wireframeRs.setFillModeProperty(FillMode::WireFrame);
            dev.setRasterizerStateProperty(wireframeRs);
            DrawFullscreenQuad(dev, Color::Red);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);

            // NDC (0.5, 0.5) -> screen (48, 16) at kSize=64 -- well inside one triangle, far from
            // every edge (including the diagonal, which passes through the quad's own centre).
            ExpectPixel("Check J: FillMode::WireFrame leaves the triangle interior unfilled",
                        Rectangle(48, 16, 1, 1), Color::Black);
        }
    }

public:
    OpenGL4RenderStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4RenderStateTest>();
}
