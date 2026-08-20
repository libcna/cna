// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-11/GL4-12: real 3D proof for the OpenGL4 graphics renderer -- a colored
// (VertexPositionColor, BasicEffect.VertexColorEnabled=true) triangle/quad through
// DrawColoredPrimitives, a real depth-test occlusion proof (a nearer quad drawn BEFORE a farther
// one still occludes it, verified by an actual pixel readback rather than "didn't throw"), and an
// indexed draw through DrawIndexedColoredPrimitives.
//
// This renderer does not yet implement DrawPrimitivesEx (plans/plan_opengl4.md remaining work) -- it
// inherits IGraphicsRenderer's own default fallback to DrawColoredPrimitives, which ignores
// texture/lighting and only reproduces vertex color correctly for the stride-16
// VertexPositionColor layout. This test therefore only exercises that stride, matching what is
// genuinely implemented -- it deliberately does not claim textured/lit 3D coverage this renderer
// does not have yet.
//
// Check A -- a solid-red VertexPositionColor quad, drawn via DrawPrimitives, is visible at its
//   center pixel.
// Check B -- real depth test: a nearer BLUE quad drawn first, then a farther RED quad drawn over
//   the same screen-space area, with SetDepthTestEnabled(true)/SetDepthWriteEnabled(true) -- the
//   overlap pixel must read BLUE (the far quad was correctly occluded), not red.
// Check C -- with depth test left ON but SIMULATING NO PRIOR CLEAR (a second overlap probe on a
//   fresh clear), drawing far-then-near instead shows the NEAR (blue) quad on top, proving the
//   depth buffer discriminates by depth, not draw order.
// Check D -- DrawIndexedPrimitives (a solid-green indexed quad, 4 vertices/6 indices) is visible
//   at its center pixel.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdint>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    std::array<VertexPositionColor, 6> MakeColorQuad(const Color& c)
    {
        return {
            VertexPositionColor{Vector3(-0.6f, 0.6f, 0.0f), c},
            VertexPositionColor{Vector3(-0.6f, -0.6f, 0.0f), c},
            VertexPositionColor{Vector3(0.6f, -0.6f, 0.0f), c},
            VertexPositionColor{Vector3(-0.6f, 0.6f, 0.0f), c},
            VertexPositionColor{Vector3(0.6f, -0.6f, 0.0f), c},
            VertexPositionColor{Vector3(0.6f, 0.6f, 0.0f), c},
        };
    }
}

class OpenGL4ThreeDTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();

        // plans/plan_opengl4.md GL4-16: RasterizerState.CullMode now really applies, and
        // GraphicsDevice's own default RasterizerState (CullCounterClockwiseFace, XNA's real
        // default) would otherwise cull this file's own quad winding -- irrelevant to what this
        // test actually checks (colour/depth, not winding), so opt out explicitly. Same idiom
        // already established by e.g. bgfx_basiceffect_texture_enabled_test.cpp for the identical
        // reason.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 10.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 20.0f);

        // Check A: a solid-red quad is visible at its center.
        {
            dev.Clear(Color::Black);
            const auto quad = MakeColorQuad(Color::Red);
            VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(quad.data(), 0, 6);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(view);
            fx.setProjectionProperty(projection);
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);

            ExpectPixel("solid red quad center", Rectangle(kSize / 2, kSize / 2, 1, 1), Color::Red);
        }

        // Check B: real depth test -- nearer BLUE quad drawn first, farther RED quad drawn
        // second over the same area. With depth test genuinely on, the farther quad must not
        // overwrite the nearer one.
        {
            dev.Clear(Color::Black);
            dev.SetDepthTestEnabled(true);
            dev.SetDepthWriteEnabled(true);

            const auto nearQuad = MakeColorQuad(Color::Blue);
            VertexBuffer nearVb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
            nearVb.SetData(nearQuad.data(), 0, 6);
            const auto farQuad = MakeColorQuad(Color::Red);
            VertexBuffer farVb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
            farVb.SetData(farQuad.data(), 0, 6);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setViewProperty(view);
            fx.setProjectionProperty(projection);

            fx.setWorldProperty(Matrix::CreateTranslation(0.0f, 0.0f, 0.5f));
            fx.Apply();
            dev.SetVertexBuffer(&nearVb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            fx.setWorldProperty(Matrix::CreateTranslation(0.0f, 0.0f, -0.5f));
            fx.Apply();
            dev.SetVertexBuffer(&farVb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            dev.SetVertexBuffer(nullptr);

            ExpectPixel("near-then-far: nearer BLUE quad occludes the farther RED one",
                        Rectangle(kSize / 2, kSize / 2, 1, 1), Color::Blue);

            // Check C: draw order reversed (far first, near second) -- the depth test must still
            // resolve to the NEARER quad on top, proving occlusion is depth-driven, not just
            // "last write wins".
            dev.Clear(Color::Black);
            fx.setWorldProperty(Matrix::CreateTranslation(0.0f, 0.0f, -0.5f));
            fx.Apply();
            dev.SetVertexBuffer(&farVb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            fx.setWorldProperty(Matrix::CreateTranslation(0.0f, 0.0f, 0.5f));
            fx.Apply();
            dev.SetVertexBuffer(&nearVb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            dev.SetVertexBuffer(nullptr);

            ExpectPixel("far-then-near: nearer BLUE quad still wins regardless of draw order",
                        Rectangle(kSize / 2, kSize / 2, 1, 1), Color::Blue);

            dev.SetDepthTestEnabled(false);
            dev.SetDepthWriteEnabled(false);
        }

        // Check D: DrawIndexedPrimitives -- a solid-green quad built from 4 vertices + 6 indices.
        {
            dev.Clear(Color::Black);

            const VertexPositionColor verts[4] = {
                {Vector3(-0.6f, 0.6f, 0.0f), Color::Green},
                {Vector3(-0.6f, -0.6f, 0.0f), Color::Green},
                {Vector3(0.6f, -0.6f, 0.0f), Color::Green},
                {Vector3(0.6f, 0.6f, 0.0f), Color::Green},
            };
            VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 4, BufferUsage::None);
            vb.SetData(verts, 0, 4);

            const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
            ib.SetData(indices, 0, 6);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(view);
            fx.setProjectionProperty(projection);
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.setIndicesProperty(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            dev.setIndicesProperty(nullptr);
            dev.SetVertexBuffer(nullptr);

            ExpectPixel("indexed solid green quad center", Rectangle(kSize / 2, kSize / 2, 1, 1), Color::Green);
        }
    }

public:
    OpenGL4ThreeDTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4ThreeDTest>();
}
