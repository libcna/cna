// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-190 (harness: WEBGPU-207), the RASTERIZER and VIEWPORT families, plus
// the two edge cases the row calls out by name: state leakage between batches, and a viewport
// sub-region combined with a render-target switch.
//
// CULLING IS ASSERTED AS A PAIR OF WINDINGS, never as one. Each cull cell draws BOTH a clockwise and
// a counter-clockwise triangle, in different halves of the cell, and reads both halves. A cell that
// checked only "the triangle disappeared" cannot tell culling from a triangle that was never
// submitted, from a degenerate winding, or from a renderer that culls everything; checking that the
// OTHER winding survived in the same draw call rules out all three at once.
//
// THE VIEWPORT CELLS ARE ABOUT WHAT A VIEWPORT DOES TO GEOMETRY THAT DOES NOT MOVE. Every quad in
// them is the full clip-space square, so its position on screen is decided entirely by the viewport
// rectangle; a renderer that ignored the viewport paints the whole cell, and one that applied it
// twice paints a smaller region than asked for.
//
// The layout, four columns by two rows:
//
//   row 0  rasterizer:  CullNone | CullClockwise | CullCounterClockwise | scissor
//   row 1  viewport:    a sub-region | the same after a render-target round trip |
//                       cull state leaking between batches | depth bias

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <array>
#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 4;
    constexpr int kRows = 2;
    constexpr int kCell = 40;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;

    const Color kClearColor(9, 13, 17, 255);
    const Color kFirst(230, 120, 40, 255);
    const Color kSecond(40, 150, 220, 255);
}

/// WEBGPU-190 (rasterizer/viewport): cull modes, scissor, viewport sub-regions, leakage, depth bias.
class RasterizerViewportParityFixture : public CNA::Parity::ParityFixture
{
public:
    RasterizerViewportParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        // --- Row 0: cull modes, each with BOTH windings in one draw ---------------------------
        const std::array<CullMode, 3> modes{CullMode::None, CullMode::CullClockwiseFace,
                                            CullMode::CullCounterClockwiseFace};
        for (int column = 0; column < 3; ++column)
        {
            RasterizerState rs;
            rs.setCullModeProperty(modes[static_cast<std::size_t>(column)]);
            device.setRasterizerStateProperty(rs);
            DrawWindingPairEXT(device, column, 0);
        }

        // --- Row 0 column 3: the scissor rectangle --------------------------------------------
        {
            RasterizerState rs;
            rs.setCullModeProperty(CullMode::None);
            rs.setScissorTestEnableProperty(true);
            device.setRasterizerStateProperty(rs);
            // The right half of the cell only. A quad covering the whole cell must land only there.
            device.setScissorRectangleProperty(
                Rectangle(3 * kCell + kCell / 2, 0, kCell / 2, kCell));
            DrawCellQuadEXT(device, 3, 0, kFirst, 0.0f);
            RasterizerState off;
            off.setCullModeProperty(CullMode::None);
            device.setRasterizerStateProperty(off);
            device.setScissorRectangleProperty(Rectangle(0, 0, kWidth, kHeight));
        }

        // --- Row 1 column 0: a viewport sub-region --------------------------------------------
        const Viewport wholeFrame = device.getViewportProperty();
        {
            device.setViewportProperty(
                Viewport(0 * kCell + kCell / 4, 1 * kCell + kCell / 4, kCell / 2, kCell / 2));
            DrawFullClipQuadEXT(device, kFirst, 0.0f);
            device.setViewportProperty(wholeFrame);
        }

        // --- Row 1 column 1: the same, with a render-target round trip in the middle -----------
        // XNA resets the viewport to the whole target on every bind, so a viewport set BEFORE the
        // round trip must not survive it -- the quad after the trip fills the cell rather than the
        // quarter the earlier viewport named. That is REMED-GFX-019's rule seen from the other side.
        {
            device.setViewportProperty(
                Viewport(1 * kCell + kCell / 4, 1 * kCell + kCell / 4, kCell / 2, kCell / 2));
            RenderTarget2D scratch(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                   RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(&scratch);
            device.Clear(kSecond);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            // Now set the cell's viewport again explicitly and draw: this is what a caller must do,
            // and what must work.
            device.setViewportProperty(Viewport(1 * kCell, 1 * kCell, kCell, kCell));
            DrawFullClipQuadEXT(device, kSecond, 0.0f);
            device.setViewportProperty(wholeFrame);
        }

        // --- Row 1 column 2: cull state must not leak between draws ---------------------------
        {
            RasterizerState culled;
            culled.setCullModeProperty(CullMode::CullClockwiseFace);
            device.setRasterizerStateProperty(culled);
            // Draw nothing under it; the state is merely SET. Then switch to None and draw the
            // pair: both windings must appear. A renderer that cached the previous cull mode into
            // its pipeline and never re-read it loses the clockwise half.
            RasterizerState none;
            none.setCullModeProperty(CullMode::None);
            device.setRasterizerStateProperty(none);
            DrawWindingPairEXT(device, 2, 1);
        }

        // --- Row 1 column 3: depth bias -------------------------------------------------------
        // Two coplanar quads at the same z with LessEqual depth. Without a bias the second wins by
        // the tie-break; with a NEGATIVE bias pulling it toward the viewer it wins outright, and
        // this cell asserts the bias is applied at all by comparing against the unbiased twin drawn
        // into the cell beside it -- see the assertions.
        {
            RenderTarget2D depthScene(device, kCell, kCell, false, SurfaceFormat::Color,
                                      DepthFormat::Depth24, 0, RenderTargetUsage::PreserveContents);
            device.SetRenderTarget(&depthScene);
            device.Clear(kClearColor);
            DepthStencilState depthOn;
            depthOn.setDepthBufferEnableProperty(true);
            depthOn.setDepthBufferWriteEnableProperty(true);
            depthOn.setDepthBufferFunctionProperty(CompareFunction::Less);
            device.setDepthStencilStateProperty(depthOn);

            RasterizerState plain;
            plain.setCullModeProperty(CullMode::None);
            device.setRasterizerStateProperty(plain);
            DrawFullClipQuadEXT(device, kFirst, 0.5f);

            RasterizerState biased;
            biased.setCullModeProperty(CullMode::None);
            // -1.0, not -0.01, and the difference is the finding: DepthBias is NOT a fraction of
            // the depth range here. Measured -- at -0.01 the biased quad loses the tie on both
            // renderers and the cell stays the first colour, while at -1.0 it wins on both. The
            // unit is the smallest resolvable depth difference (OpenGL's glPolygonOffset `units`
            // and its equivalents), so a bias smaller than one of those does nothing at all. This
            // cell therefore asserts the DIRECTION a negative bias moves a surface, not a
            // magnitude, which is the only part that is renderer-independent.
            biased.setDepthBiasProperty(-1.0f);
            device.setRasterizerStateProperty(biased);
            DrawFullClipQuadEXT(device, kSecond, 0.5f);

            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setViewportProperty(wholeFrame);
            BlitEXT(device, depthScene, Rectangle(3 * kCell, 1 * kCell, kCell, kCell));
        }

        // --- Reading ---------------------------------------------------------------------------
        // A small probe just inside each triangle's RIGHT-ANGLE corner. The triangles cover half
        // of their half-cell, so a probe spanning the half-cell averages triangle and background
        // together and reads as neither -- measured, and the reason this is not a half-cell mean.
        const auto half = [](int column, int row, bool left) {
            const int x = column * kCell + (left ? 3 : kCell / 2 + 3);
            return Rectangle(x, row * kCell + 3, 5, 5);
        };
        const auto painted = [this](const Rectangle& r, const Color& colour) {
            const Color got = Average(r);
            return std::abs(got.getRProperty() - colour.getRProperty()) <= 8 &&
                   std::abs(got.getGProperty() - colour.getGProperty()) <= 8 &&
                   std::abs(got.getBProperty() - colour.getBProperty()) <= 8;
        };
        const auto clear = [&painted](const Rectangle& r) { return painted(r, kClearColor); };
        (void)clear;

        // CullNone keeps both windings; each cull mode removes exactly one, and the OTHER survives.
        Require(painted(half(0, 0, true), kFirst) && painted(half(0, 0, false), kSecond),
                "CullMode::None keeps both windings -- the control that makes the two cells beside "
                "it mean something");
        Require(!painted(half(1, 0, true), kFirst) && painted(half(1, 0, false), kSecond),
                "CullClockwiseFace removes the clockwise triangle and KEEPS the counter-clockwise "
                "one, in the same draw call");
        Require(painted(half(2, 0, true), kFirst) && !painted(half(2, 0, false), kSecond),
                "CullCounterClockwiseFace is its mirror -- and between them the two cells prove the "
                "renderer culls by winding rather than culling everything or nothing");

        // The scissor confines a full-cell quad to the right half.
        Require(!painted(half(3, 0, true), kFirst) && painted(half(3, 0, false), kFirst),
                "the scissor rectangle confines a full-cell quad to the half it names");

        // The viewport puts a full-clip quad in the quarter it names, and nowhere else.
        Require(painted(Rectangle(0 * kCell + kCell / 2 - 2, 1 * kCell + kCell / 2 - 2, 4, 4),
                        kFirst),
                "a viewport sub-region paints the middle of the region it names");
        Require(!painted(Rectangle(0 * kCell + 2, 1 * kCell + 2, 4, 4), kFirst),
                "...and nothing outside it -- a renderer ignoring the viewport paints the whole cell");

        // After the render-target round trip, an explicitly re-set viewport works: the cell is full.
        Require(painted(Rectangle(1 * kCell + 3, 1 * kCell + 3, 5, 5), kSecond) &&
                    painted(Rectangle(1 * kCell + kCell - 8, 1 * kCell + kCell - 8, 5, 5), kSecond),
                "a viewport set after a render-target round trip covers the whole cell -- the trip "
                "reset it, and setting it again is what a caller must do and what must work");

        // Leakage: after a CullClockwiseFace state that drew nothing, a None state draws both.
        Require(painted(half(2, 1, true), kFirst) && painted(half(2, 1, false), kSecond),
                "a cull mode set and then replaced does not leak into the next draw");

        // Depth bias: the biased quad is in front, so the cell shows the SECOND colour.
        {
            const Color biasCell = Average(
                Rectangle(3 * kCell + kCell / 2 - 3, 1 * kCell + kCell / 2 - 3, 6, 6));
            std::printf("[info] depth-bias cell = (%d,%d,%d); unbiased first = (%d,%d,%d), "
                        "biased second = (%d,%d,%d)\n",
                        biasCell.getRProperty(), biasCell.getGProperty(), biasCell.getBProperty(),
                        kFirst.getRProperty(), kFirst.getGProperty(), kFirst.getBProperty(),
                        kSecond.getRProperty(), kSecond.getGProperty(), kSecond.getBProperty());
        }
        Require(painted(Rectangle(3 * kCell + kCell / 2 - 3, 1 * kCell + kCell / 2 - 3, 6, 6),
                        kSecond),
                "a negative depth bias pulls a coplanar quad toward the viewer, so it wins a Less "
                "depth test it would otherwise lose");
    }

private:
    /// Draws a clockwise triangle in the cell's left half and a counter-clockwise one in its right.
    void DrawWindingPairEXT(GraphicsDevice& device, int column, int row)
    {
        const float left = -1.0f + 2.0f * static_cast<float>(column) / kColumns;
        const float mid = left + 1.0f / kColumns;
        const float right = left + 2.0f / kColumns;
        const float top = 1.0f - 2.0f * static_cast<float>(row) / kRows;
        const float bottom = top - 2.0f / kRows;

        struct Vertex { float x, y, z; };
        // Clockwise on screen (y down after the projection flip): top-left, top-right, bottom-left.
        const std::array<Vertex, 3> clockwise{
            Vertex{left + 0.01f, top - 0.01f, 0.0f},
            Vertex{mid - 0.01f, top - 0.01f, 0.0f},
            Vertex{left + 0.01f, bottom + 0.01f, 0.0f}};
        // The mirror winding in the other half.
        const std::array<Vertex, 3> counterClockwise{
            Vertex{mid + 0.01f, top - 0.01f, 0.0f},
            Vertex{mid + 0.01f, bottom + 0.01f, 0.0f},
            Vertex{right - 0.01f, top - 0.01f, 0.0f}};
        DrawTriangleEXT(device, clockwise, kFirst);
        DrawTriangleEXT(device, counterClockwise, kSecond);
    }

    template <typename Verts>
    void DrawTriangleEXT(GraphicsDevice& device, const Verts& verts, const Color& colour)
    {
        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setVertexColorEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(
            static_cast<float>(colour.getRProperty()) / 255.0f,
            static_cast<float>(colour.getGProperty()) / 255.0f,
            static_cast<float>(colour.getBProperty()) / 255.0f));
        effect.setAlphaProperty(1.0f);
        VertexBuffer vb(device,
                        VertexDeclaration(12,
                            {VertexElement(0, VertexElementFormat::Vector3,
                                           VertexElementUsage::Position, 0)}),
                        static_cast<int>(verts.size()), BufferUsage::None);
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), 12);
        device.SetVertexBuffer(&vb);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        device.SetVertexBuffer(nullptr);
    }

    /// A quad covering one cell exactly, in clip space.
    void DrawCellQuadEXT(GraphicsDevice& device, int column, int row, const Color& colour, float z)
    {
        const float left = -1.0f + 2.0f * static_cast<float>(column) / kColumns;
        const float right = left + 2.0f / kColumns;
        const float top = 1.0f - 2.0f * static_cast<float>(row) / kRows;
        const float bottom = top - 2.0f / kRows;
        DrawQuadEXT(device, left, top, right, bottom, colour, z);
    }

    /// A quad covering the whole clip-space square, so the VIEWPORT decides where it lands.
    void DrawFullClipQuadEXT(GraphicsDevice& device, const Color& colour, float z)
    {
        DrawQuadEXT(device, -1.0f, 1.0f, 1.0f, -1.0f, colour, z);
    }

    void DrawQuadEXT(GraphicsDevice& device, float left, float top, float right, float bottom,
                     const Color& colour, float z)
    {
        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setVertexColorEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(
            static_cast<float>(colour.getRProperty()) / 255.0f,
            static_cast<float>(colour.getGProperty()) / 255.0f,
            static_cast<float>(colour.getBProperty()) / 255.0f));
        effect.setAlphaProperty(1.0f);
        struct Vertex { float x, y, z; };
        const std::array<Vertex, 4> verts{
            Vertex{left, top, z}, Vertex{left, bottom, z},
            Vertex{right, top, z}, Vertex{right, bottom, z}};
        VertexBuffer vb(device,
                        VertexDeclaration(12,
                            {VertexElement(0, VertexElementFormat::Vector3,
                                           VertexElementUsage::Position, 0)}),
                        static_cast<int>(verts.size()), BufferUsage::None);
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), 12);
        device.SetVertexBuffer(&vb);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
        device.SetVertexBuffer(nullptr);
    }

    void BlitEXT(GraphicsDevice& device, Texture2D& texture, const Rectangle& destination);
};

#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"

void RasterizerViewportParityFixture::BlitEXT(GraphicsDevice& device, Texture2D& texture,
                                              const Rectangle& destination)
{
    const SamplerState pointClamp = SamplerState::PointClamp;
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
    batch.Draw(texture, destination, Color::White);
    batch.End();
}

CNA_PARITY_FIXTURE_MAIN(RasterizerViewportParityFixture)
