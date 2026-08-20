// SPDX-License-Identifier: MS-PL
// plans/plan_sokol.md SOKOL-23: RasterizerState.FillMode == WireFrame is implemented via the same
// CPU-side triangle-to-GL_LINES re-expansion EasyGLRenderer::DrawWireframe() uses --
// sokol_gfx exposes no native polygon-fill-mode API at all, but the technique needs none of one:
// it only changes which indices are drawn and with what primitive topology
// (BuildWireframeLineIndicesEXT()'s own doc comment). Previously accepted and silently ignored.
//
// A 200x160 solid-red quad (two triangles, split along the top-left-to-bottom-right diagonal) is
// drawn over a black background, with the SAME geometry drawn once indexed (exercising
// BuildWireframeLineIndicesEXT()'s GL-readback path for the source index buffer) and once
// non-indexed (exercising its sequential vertexStart+i fallback).
//
// The "interior" sample point is deliberately NOT the quad's geometric centre: the two triangles'
// shared diagonal runs from (60,40) to (260,200) (slope 0.8), which passes exactly through
// (160,120) -- sampling there would misreport a genuinely-empty wireframe interior as "filled"
// merely because the diagonal line itself happens to cross that exact pixel. (220,60) sits well
// inside the upper-right triangle's interior, comfortably clear of that diagonal (which is at
// y=168 when x=220).
//
// Check A -- sanity: FillMode::Solid genuinely fills the quad's interior red.
// Check B -- FillMode::WireFrame (indexed draw) leaves the quad's INTERIOR black: nothing but the
//   edges (including the shared diagonal) are drawn, proving this is a real line re-expansion, not
//   e.g. always falling back to filled triangles.
// Check C -- FillMode::WireFrame (indexed draw) DOES draw the quad's left edge red -- proving
//   lines are genuinely rasterized, not that check B passes merely because nothing renders at all.
// Check D/E -- the same pair (interior empty, edge drawn), for a NON-indexed draw.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = skipped (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBackBufferWidth = 320;
    constexpr int kBackBufferHeight = 240;

    const Color kRed(255, 0, 0, 255);
    const Color kBlack(0, 0, 0, 255);

    // Quad spans (60,40)-(260,200). Left edge at x=60 (y in [40,200]).
    const std::vector<VertexPositionColor> kQuadVerts = {
        { Vector3(60.0f, 40.0f, 0.0f), kRed },   // 0: top-left
        { Vector3(60.0f, 200.0f, 0.0f), kRed },  // 1: bottom-left
        { Vector3(260.0f, 200.0f, 0.0f), kRed }, // 2: bottom-right
        { Vector3(260.0f, 40.0f, 0.0f), kRed },  // 3: top-right
    };
    const std::vector<std::uint16_t> kQuadIndices = { 0, 1, 2, 0, 2, 3 };

    Matrix PixelSpaceProjection()
    {
        return Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kBackBufferWidth),
            static_cast<float>(kBackBufferHeight), 0.0f,
            -1.0f, 1.0f);
    }
}

class SokolWireframeTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<BasicEffect> effect_;

    void DrawQuad(GraphicsDevice& device, bool indexed, FillMode fillMode)
    {
        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        rs.setFillModeProperty(fillMode);
        device.setRasterizerStateProperty(rs);

        VertexBuffer vb(device, VertexPositionColor::getVertexDeclarationStatic(),
                        static_cast<int>(kQuadVerts.size()), BufferUsage::None);
        vb.SetData(kQuadVerts.data(), static_cast<int>(kQuadVerts.size()));
        device.SetVertexBuffer(&vb);

        effect_->Apply();

        if (indexed)
        {
            IndexBuffer ib(device, IndexElementSize::SixteenBits,
                          static_cast<int>(kQuadIndices.size()), BufferUsage::None);
            ib.SetData(kQuadIndices.data(), static_cast<int>(kQuadIndices.size()));
            device.SetIndexBuffer(&ib);
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                         static_cast<int>(kQuadVerts.size()), 0, 2);
            device.SetIndexBuffer(nullptr);
        }
        else
        {
            // Two triangles sharing the diagonal, laid out sequentially (no index buffer):
            // (0,1,2) and (0,2,3) from kQuadVerts, matching the indexed draw's own triangles.
            const std::vector<VertexPositionColor> sequential = {
                kQuadVerts[0], kQuadVerts[1], kQuadVerts[2],
                kQuadVerts[0], kQuadVerts[2], kQuadVerts[3],
            };
            VertexBuffer seqVb(device, VertexPositionColor::getVertexDeclarationStatic(),
                               static_cast<int>(sequential.size()), BufferUsage::None);
            seqVb.SetData(sequential.data(), static_cast<int>(sequential.size()));
            device.SetVertexBuffer(&seqVb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }
        device.SetVertexBuffer(nullptr);
    }

    // A 1px GL line at an exact integer boundary coordinate (x=60.0 here) can rasterize into
    // either the pixel column just left or just right of it depending on the driver's fill-rule
    // tie-break -- sampling one exact column is unreliable. Checks whether ANY pixel in a small
    // strip straddling the boundary is (close to) red, rather than asserting one exact column.
    void ExpectRedSomewhereNear(const char* label, int y, int xLo, int xHi)
    {
        auto& device = getGraphicsDeviceProperty();
        for (int x = xLo; x <= xHi; ++x)
        {
            Color px(0, 0, 0, 0);
            const Rectangle reg(x, y, 1, 1);
            device.GetBackBufferData(&reg, &px, 0, 1);
            if (px.getRProperty() >= 215 && px.getGProperty() <= 40 && px.getBProperty() <= 40)
            {
                std::printf("[PASS] %s: found red at x=%d, pixel=(%d,%d,%d)\n", label, x,
                            px.getRProperty(), px.getGProperty(), px.getBProperty());
                return;
            }
        }
        std::printf("[FAIL] %s: no red pixel in x=[%d,%d] at y=%d\n", label, xLo, xHi, y);
        MarkFailedEXT();
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        device.SetDepthTestEnabled(false);

        effect_ = std::make_unique<BasicEffect>(device);
        effect_->VertexColorEnabled = true;
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(Matrix::getIdentityProperty());
        effect_->setProjectionProperty(PixelSpaceProjection());

        // Well inside the upper-right triangle, clear of the shared diagonal (see this file's own
        // top comment for why the geometric centre itself is unsafe to sample).
        const Rectangle interior(220, 60, 1, 1);
        // The left edge sits at the exact integer boundary x=60 -- a 1px GL line there can
        // rasterize into the column just left or just right of the boundary depending on the
        // driver's fill-rule tie-break, so check C/E scan a small straddling strip instead of
        // asserting one exact column (see ExpectRedSomewhereNear()'s own doc comment).

        // Check A -- sanity: Solid genuinely fills the interior.
        device.Clear(kBlack);
        DrawQuad(device, /*indexed=*/true, FillMode::Solid);
        ExpectPixel("A: Solid fills the quad's interior", interior, kRed);

        // Check B/C -- WireFrame (indexed): interior empty, left edge drawn.
        device.Clear(kBlack);
        DrawQuad(device, /*indexed=*/true, FillMode::WireFrame);
        ExpectPixel("B: WireFrame (indexed) leaves the interior black -- only edges are drawn",
                    interior, kBlack);
        ExpectRedSomewhereNear("C: WireFrame (indexed) draws the left edge red", 120, 58, 62);

        // Check D/E -- WireFrame (non-indexed): interior empty, left edge drawn.
        device.Clear(kBlack);
        DrawQuad(device, /*indexed=*/false, FillMode::WireFrame);
        ExpectPixel("D: WireFrame (non-indexed) leaves the interior black", interior, kBlack);
        ExpectRedSomewhereNear("E: WireFrame (non-indexed) draws the left edge red", 120, 58, 62);
    }

public:
    SokolWireframeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackBufferWidth);
        gdm_->setPreferredBackBufferHeightProperty(kBackBufferHeight);
    }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SokolWireframeTest game;
    game.Run();
    return game.getResultProperty();
}
