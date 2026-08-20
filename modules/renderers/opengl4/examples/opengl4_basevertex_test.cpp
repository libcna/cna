// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-27: real GpuDrawParams::baseVertex support for the OpenGL4 graphics
// renderer -- DrawIndexedPrimitivesEx now uses the real GL 3.2 core glDrawElementsBaseVertex
// entry point (newly loaded via GL4Loader) instead of plain glDrawElements, adding
// params.baseVertex to every fetched index before it indexes into the currently bound vertex
// buffer -- letting multiple sub-meshes share one large vertex buffer with per-draw
// index-space-relative indices, matching every effect's own
// DrawIndexedPrimitivesEx(..., baseVertex, ...) contract. Previously baseVertex was silently
// ignored entirely (always treated as 0).
//
// A single shared 8-vertex VertexPositionColor buffer holds two quads: vertices[0..3] = a RED
// quad on the LEFT half of NDC space (x: -1..0), vertices[4..7] = a BLUE quad on the RIGHT half
// (x: 0..1). A single shared 6-index buffer holds one quad's worth of LOCAL indices {0,1,2,0,2,3}
// -- reused for BOTH draws, unchanged.
//
// Check A -- baseVertex=0: indices fetch vertices[0..3] -> the left half renders RED.
// Check B -- baseVertex=4 (same local indices): if genuinely honored, +4 is added to every
//   fetched index before the vertex fetch, so it now fetches vertices[4..7] -> the right half
//   renders BLUE. If baseVertex were silently ignored (the old bug), this second draw would
//   incorrectly re-fetch vertices[0..3] again -- since those vertices sit on the LEFT half in
//   NDC space, nothing would render on the right at all, leaving it at the background colour
//   instead of blue -- a decisive, unambiguous distinction between "honored" and "ignored".
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
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

class OpenGL4BaseVertexTest : public CNA::Examples::PixelTestGame
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

        // A single quad's worth of LOCAL indices, reused unchanged for both draws.
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        IndexBuffer ib(dev, 6);
        ib.SetData(indices, 6);

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
        // Draw B: baseVertex=4, SAME local indices -> must fetch vertices[4..7] (BLUE, right half).
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, /*baseVertex=*/4,
                                  /*minVertexIndex=*/0, /*numVertices=*/4, /*startIndex=*/0, /*primitiveCount=*/2);

        const Color left = ReadPixel(dev, kSize / 4, kSize / 2);
        const Color right = ReadPixel(dev, 3 * kSize / 4, kSize / 2);

        Check(left.getRProperty() > 200 && left.getGProperty() < 50 && left.getBProperty() < 50,
              "Check A: baseVertex=0 fetches vertices[0..3] -- left half renders RED");
        Check(right.getBProperty() > 200 && right.getRProperty() < 50 && right.getGProperty() < 50,
              "Check B: baseVertex=4 (same local indices) fetches vertices[4..7] -- right half renders BLUE");
    }

public:
    OpenGL4BaseVertexTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4BaseVertexTest>();
}
