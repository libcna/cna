// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-31: real 32-bit index buffer support for the OpenGL4 graphics renderer --
// discovered as a separate, newly-found gap while scoping GL4-27 (`baseVertex`), not attempted
// there since it needed real new work: `OpenGL4IndexBufferRenderer::IsThirtyTwoBit()` was
// unconditionally `false`, `CreateIndexBuffer32` wasn't overridden (silently fell back to a
// 16-bit buffer via the `IGraphicsRenderer` default), and every index-buffer draw path hardcoded
// `GL_UNSIGNED_SHORT`/`sizeof(uint16_t)` -- this renderer had no 32-bit index buffer support at
// all, unlike every other established renderer.
//
// Fixed by: `OpenGL4IndexBufferRenderer` gained a `thirtyTwoBit_` flag (set at construction),
// real `SetData32`/`SetData32WithOptions` overrides (`GL_UNSIGNED_INT` storage instead of
// `GL_UNSIGNED_SHORT`), and `IsThirtyTwoBit()` now reports the real flag instead of a hardcoded
// `false`. `OpenGL4Renderer::CreateIndexBuffer32()` is now overridden (previously fell
// back to `CreateIndexBuffer16`). Every index-buffer draw call site
// (`DrawIndexedPrimitivesEx`'s `customEffectRenderer` branch and its normal
// `BindProgramForStride` branch, `DrawIndexedColoredPrimitives`) now selects
// `GL_UNSIGNED_INT`/`sizeof(uint32_t)` vs `GL_UNSIGNED_SHORT`/`sizeof(uint16_t)` from
// `IIndexBufferRenderer::IsThirtyTwoBit()` instead of hardcoding the 16-bit path.
//
// Combines the same shared-vertex-buffer/two-draws methodology `OpenGL4_BaseVertex`
// (`opengl4_basevertex_test.cpp`, GL4-27) already established, but with a real
// `IndexElementSize::ThirtyTwoBits` `IndexBuffer` instead of a 16-bit one -- a genuinely
// discriminating proof: if 32-bit indices were still silently reinterpreted as 16-bit data (the
// old bug), the 6 uint32_t indices' raw bytes would be misread as 12 uint16_t values, producing
// wildly wrong/out-of-range vertex fetches instead of the intended `{0,1,2,0,2,3}` triangle list
// -- the quad would NOT render as a clean two-triangle shape at all, unlike an honored 32-bit
// buffer which renders it exactly.
//
// vertices[0..3]: RED quad, NDC x: -1..0 (left half). vertices[4..7]: BLUE quad, NDC x: 0..1
// (right half). A single shared 32-bit index buffer holds one quad's worth of LOCAL indices
// {0,1,2,0,2,3}, reused unchanged for both draws (only baseVertex differs).
//
// Check A -- GetIndexElementSizeProperty()/IsThirtyTwoBit(): the IndexBuffer reports
//   ThirtyTwoBits, not silently downgraded to SixteenBits.
// Check B -- baseVertex=0: indices fetch vertices[0..3] -> the left half renders RED.
// Check C -- baseVertex=4 (same local 32-bit indices): if genuinely honored (both baseVertex AND
//   the 32-bit index data itself), fetches vertices[4..7] -> the right half renders BLUE. Any
//   corruption in either mechanism (baseVertex ignored, or index data misread as 16-bit) would
//   leave the right half at the background colour instead of blue.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
}

class OpenGL4IndexBuffer32Test : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle reg(x, y, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const Color kGreenBg(0, 255, 0, 255);
        const Color kRed(255, 0, 0, 255);
        const Color kBlue(0, 0, 255, 255);

        // vertices[0..3]: RED quad, NDC x: -1..0 (left half).
        // vertices[4..7]: BLUE quad, NDC x: 0..1 (right half).
        const VertexPositionColor verts[8] = {
            {Vector3(-1.0f, 1.0f, 0.0f), kRed}, {Vector3(-1.0f, -1.0f, 0.0f), kRed},
            {Vector3(0.0f, -1.0f, 0.0f), kRed}, {Vector3(0.0f, 1.0f, 0.0f), kRed},
            {Vector3(0.0f, 1.0f, 0.0f), kBlue}, {Vector3(0.0f, -1.0f, 0.0f), kBlue},
            {Vector3(1.0f, -1.0f, 0.0f), kBlue}, {Vector3(1.0f, 1.0f, 0.0f), kBlue},
        };
        VertexBuffer vb(dev, 8);
        vb.SetData(verts, 8);

        // A single quad's worth of LOCAL indices as real 32-bit data, reused unchanged for both draws.
        IndexBuffer ib(dev, IndexElementSize::ThirtyTwoBits, 6, BufferUsage::None);
        const std::uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
        ib.SetData(indices, 6);

        Check(ib.getIndexElementSizeProperty() == IndexElementSize::ThirtyTwoBits,
              "Check A: IndexBuffer reports ThirtyTwoBits, not silently downgraded to SixteenBits");

        dev.Clear(kGreenBg);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        dev.SetVertexBuffer(&vb);
        dev.setIndicesProperty(&ib);

        // Draw A: baseVertex=0 -> fetches vertices[0..3] (RED, left half).
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, /*baseVertex=*/0,
                                  /*minVertexIndex=*/0, /*numVertices=*/4, /*startIndex=*/0, /*primitiveCount=*/2);
        // Draw B: baseVertex=4, SAME local 32-bit indices -> must fetch vertices[4..7] (BLUE, right half).
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, /*baseVertex=*/4,
                                  /*minVertexIndex=*/0, /*numVertices=*/4, /*startIndex=*/0, /*primitiveCount=*/2);

        const Color left = ReadPixel(dev, kSize / 4, kSize / 2);
        const Color right = ReadPixel(dev, 3 * kSize / 4, kSize / 2);

        Check(left.getRProperty() > 200 && left.getGProperty() < 50 && left.getBProperty() < 50,
              "Check B: baseVertex=0 with 32-bit local indices fetches vertices[0..3] -- left half renders RED");
        Check(right.getBProperty() > 200 && right.getRProperty() < 50 && right.getGProperty() < 50,
              "Check C: baseVertex=4 with the SAME 32-bit local indices fetches vertices[4..7] -- right half renders BLUE");
    }

public:
    OpenGL4IndexBuffer32Test()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4IndexBuffer32Test>();
}
