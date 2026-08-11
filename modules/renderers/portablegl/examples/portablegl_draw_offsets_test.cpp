// SPDX-License-Identifier: MS-PL
// Draw-offset regression test for the PortableGL renderer.
//
// GraphicsDevice's ordinary draw routes hand every renderer a GpuDrawParams carrying vertexStart,
// startIndex, baseVertex and the bound VertexBufferBinding's own VertexOffset. PortableGL used to
// override only the legacy colored methods, so the IGraphicsRenderer defaults forwarded the draw
// with all four discarded and every submission started at vertex zero / index zero. This file is
// the oracle for that: the vertex buffer holds three full-viewport quads in three different
// colours, so any dropped or mis-scaled offset paints a colour the check can name.
//
// Slot 0 (vertices 0-3)  = RED    decoy
// Slot 1 (vertices 4-7)  = GREEN
// Slot 2 (vertices 8-11) = BLUE
//
// The index buffer holds two quads' worth of indices: [0,6) references slot 0 and [6,12)
// references slot 1, so startIndex and baseVertex are independently observable.
//
// Check A -- non-indexed vertexStart selects the right vertices.
// Check B -- VertexBufferBinding.VertexOffset alone selects the right vertices.
// Check C -- VertexOffset and vertexStart combine (they are added, never applied twice or once).
// Check D -- indexed startIndex alone (16-bit indices).
// Check E -- indexed baseVertex alone (16-bit indices).
// Check F -- indexed startIndex and baseVertex together (16-bit indices).
// Check G -- indexed VertexOffset and baseVertex together (16-bit indices).
// Check H -- the whole startIndex/baseVertex matrix again with 32-bit indices, which additionally
//   proves startIndex is scaled by the buffer's own index width rather than a hardcoded one.
// Check I -- a draw whose range leaves the bound buffer is refused, and the renderer still draws
//   correctly afterwards.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "portablegl_test_support.hpp"

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <exception>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace portablegl_test;

namespace
{
    constexpr int kSize = 32;
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);
}

class PortableGLDrawOffsetsTest : public PortableGLTestGame
{
    std::vector<VertexPositionColor> vertices_;
    std::vector<std::uint16_t> indices16_;
    std::vector<std::uint32_t> indices32_;

    /// The whole 32x32 backbuffer, so a check reads every pixel the quad covers.
    Rectangle Whole() const { return Rectangle(0, 0, kSize, kSize); }

    void BuildGeometry()
    {
        for (const Color& c : {kRed, kGreen, kBlue})
        {
            const auto corners = FullQuadCorners(c);
            vertices_.insert(vertices_.end(), corners.begin(), corners.end());
        }
        // [0,6) -> slot 0's corners, [6,12) -> slot 1's corners.
        indices16_ = {0, 1, 2, 0, 2, 3,  4, 5, 6, 4, 6, 7};
        indices32_ = {0, 1, 2, 0, 2, 3,  4, 5, 6, 4, 6, 7};
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        BuildGeometry();

        // Depth and culling are not what this file measures; state them explicitly so a change to
        // either cannot silently turn an offset check into a culling check.
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);

        VertexBuffer vb(dev, PosColorDecl(), static_cast<int>(vertices_.size()), BufferUsage::None);
        vb.SetData(vertices_.data(), 0, static_cast<int>(vertices_.size()));

        IndexBuffer ib16(dev, IndexElementSize::SixteenBits,
                         static_cast<int>(indices16_.size()), BufferUsage::None);
        ib16.SetData(indices16_.data(), 0, static_cast<int>(indices16_.size()));

        IndexBuffer ib32(dev, IndexElementSize::ThirtyTwoBits,
                         static_cast<int>(indices32_.size()), BufferUsage::None);
        ib32.SetData(indices32_.data(), 0, static_cast<int>(indices32_.size()));

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;

        // --- Non-indexed ------------------------------------------------------------------
        // The non-indexed route consumes six consecutive vertices, so a quad is submitted as the
        // corner list read straight through: 0,1,2 then 3(=BL) closes nothing. Build a dedicated
        // six-vertex triangle-list buffer per slot instead, so vertexStart is measured against
        // real triangle-list geometry rather than an indexed layout.
        std::vector<VertexPositionColor> listVerts;
        for (const Color& c : {kRed, kGreen, kBlue})
        {
            const auto quad = FullQuad(c);
            listVerts.insert(listVerts.end(), quad.begin(), quad.end());
        }
        VertexBuffer listVb(dev, PosColorDecl(), static_cast<int>(listVerts.size()), BufferUsage::None);
        listVb.SetData(listVerts.data(), 0, static_cast<int>(listVerts.size()));

        // Check A -- vertexStart alone. Slot 1 lives at vertex 6.
        dev.Clear(Color::Black);
        fx.Apply();
        dev.SetVertexBuffer(&listVb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);
        dev.SetVertexBuffer(nullptr);
        check(RegionIs(Whole(), kGreen),
              "check A: DrawPrimitives' vertexStart selects the vertices it names");

        // Check B -- VertexBufferBinding.VertexOffset alone. Slot 2 lives at vertex 12.
        dev.Clear(Color::Black);
        fx.Apply();
        dev.SetVertexBuffer(&listVb, 12);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        check(RegionIs(Whole(), kBlue),
              "check B: VertexBufferBinding.VertexOffset alone selects the vertices it names");

        // Check C -- both, added exactly once each: 6 + 6 = slot 2.
        dev.Clear(Color::Black);
        fx.Apply();
        dev.SetVertexBuffer(&listVb, 6);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);
        dev.SetVertexBuffer(nullptr);
        check(RegionIs(Whole(), kBlue),
              "check C: VertexOffset and vertexStart are added, each applied exactly once");

        // --- Indexed, 16-bit --------------------------------------------------------------
        const auto indexedDraw = [&](IndexBuffer& ib, int bindingOffset, int baseVertex,
                                     int minVertexIndex, int numVertices, int startIndex) {
            dev.Clear(Color::Black);
            fx.Apply();
            dev.SetVertexBuffer(&vb, bindingOffset);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, baseVertex, minVertexIndex,
                                      numVertices, startIndex, 2);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);
        };

        indexedDraw(ib16, 0, 0, 0, 4, 0);
        check(RegionIs(Whole(), kRed),
              "check D: the zero-offset indexed control draws the decoy slot (16-bit indices)");

        indexedDraw(ib16, 0, 0, 4, 4, 6);
        check(RegionIs(Whole(), kGreen),
              "check D: indexed startIndex alone selects the indices it names (16-bit)");

        indexedDraw(ib16, 0, 4, 0, 4, 0);
        check(RegionIs(Whole(), kGreen),
              "check E: indexed baseVertex alone displaces every decoded index (16-bit)");

        indexedDraw(ib16, 0, 4, 4, 4, 6);
        check(RegionIs(Whole(), kBlue),
              "check F: startIndex and baseVertex combine without cancelling each other (16-bit)");

        indexedDraw(ib16, 4, 4, 0, 4, 0);
        check(RegionIs(Whole(), kBlue),
              "check G: VertexOffset and baseVertex both displace the fetch (16-bit)");

        // --- Indexed, 32-bit --------------------------------------------------------------
        // Misreading the 32-bit stream as 16-bit yields {0,0,1,0,2,0}: degenerate triangles only,
        // so a black readback here would also catch a wrong index width. Misscaling startIndex by
        // 2 bytes instead of 4 lands mid-stream and paints the decoy or nothing.
        indexedDraw(ib32, 0, 0, 0, 4, 0);
        check(RegionIs(Whole(), kRed),
              "check H: the zero-offset indexed control draws the decoy slot (32-bit indices)");

        indexedDraw(ib32, 0, 0, 4, 4, 6);
        check(RegionIs(Whole(), kGreen),
              "check H: indexed startIndex is scaled by the buffer's own 32-bit index width");

        indexedDraw(ib32, 0, 4, 4, 4, 6);
        check(RegionIs(Whole(), kBlue),
              "check H: startIndex and baseVertex combine with 32-bit indices too");

        // --- Check I -- an out-of-range range is refused, and the renderer recovers ----------
        {
            dev.Clear(Color::Black);
            fx.Apply();
            dev.SetVertexBuffer(&listVb);
            bool threw = false;
            try
            {
                // Slot 2 ends at vertex 18; asking for six vertices from 14 leaves the buffer.
                dev.DrawPrimitives(PrimitiveType::TriangleList, 14, 2);
            }
            catch (const std::exception&) { threw = true; }
            dev.SetVertexBuffer(nullptr);
            check(threw, "check I: a draw whose vertex range leaves the bound buffer is refused");
            check(RegionIs(Whole(), Color::Black),
                  "check I: the refused draw wrote no pixels at all");

            dev.Clear(Color::Black);
            fx.Apply();
            dev.SetVertexBuffer(&listVb, 12);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(RegionIs(Whole(), kBlue),
                  "check I: a valid draw still works after the refused one");
        }

        Finish();
    }

public:
    PortableGLDrawOffsetsTest() : PortableGLTestGame(kSize) {}
};

int main()
{
    PortableGLDrawOffsetsTest game;
    game.Run();
    return game.getResult();
}
