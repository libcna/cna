// SPDX-License-Identifier: MS-PL
// Cross-program pipeline-state and 32-bit-index regression test for the PortableGL
// (rswinkle/PortableGL) graphics renderer. Both existing PortableGL tests exercise exactly one
// draw path per process; this one interleaves the colored-3D program and the textured SpriteBatch
// program inside a single rendered frame, on one renderer instance, and reads back real pixels
// after every stage.
//
// Why this shape: PortableGL's vertex_stage walks ALL GL_MAX_VERTEX_ATTRIBS slots on every draw
// and fetches each slot whose `enabled` flag is set, using the buffer/offset/stride that slot's
// last glVertexAttribPointer recorded -- it has no notion of "the current program declares only N
// inputs". The colored program uses attrib slots 0-1 and the textured program uses 0-2, so a
// textured draw followed by a longer colored draw used to keep reading slot 2 out of the 6-vertex
// (192-byte) SpriteBatch quad buffer for as many vertices as the colored draw submitted -- a real
// out-of-bounds read from the seventh vertex on. Stage 3 below therefore submits 9 vertices right
// after a SpriteBatch draw: the ASan/UBSan build turns that stale fetch into a hard failure, and
// the pixel oracles here prove the fix did not change what any of the three stages actually draws.
//
// Check A -- a colored 3D full-viewport draw paints the exact vertex color.
// Check B -- a SpriteBatch textured draw over the left half paints the exact texture color there
//   and leaves the right half untouched (both programs' output coexists in one frame).
// Check C -- a second, longer colored 3D draw (3 triangles / 9 vertices) issued after the
//   textured draw paints its exact vertex colors, in both a full-viewport and a sub-triangle
//   region -- the stale-attrib regression case.
// Check D -- a real IndexBuffer(IndexElementSize::ThirtyTwoBits) with real 32-bit indices drives
//   DrawIndexedPrimitives end to end and paints the exact indexed-vertex color. Misreading the
//   32-bit index data as 16-bit yields {0,0,1,0,2,0}, i.e. only degenerate triangles and an
//   unchanged black backbuffer, so this readback genuinely discriminates the index width.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/PortableGL/PortableGLRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

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

class PortableGLPipelineInterleaveTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> blueTexture_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    /// Reads a w*h region and reports whether every pixel matches @p expected exactly (RGB).
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

    /// Draws @p vertexCount vertices of @p verts as a colored TriangleList through the real
    /// GraphicsDevice draw route (VertexBuffer + BasicEffect), exactly as game code would.
    /// VertexColorEnabled is set explicitly because BasicEffect's own default is false and this
    /// renderer honours that -- the packed vertex colour these checks read back is only consumed
    /// when the effect actually asks for it.
    void DrawColoredTriangles(const VertexPositionColor* verts, int vertexCount)
    {
        auto& dev = getGraphicsDeviceProperty();
        VertexBuffer vb(dev, PosColorDecl(), vertexCount, BufferUsage::None);
        vb.SetData(verts, 0, vertexCount);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, vertexCount / 3);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        const std::vector<std::uint8_t> bluePixels = {
            0, 0, 255, 255,  0, 0, 255, 255,
            0, 0, 255, 255,  0, 0, 255, 255,
        };
        blueTexture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 2, 2, bluePixels));
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        // Depth is deliberately out of the picture here -- this test is about vertex-attribute
        // pipeline identity, and a later stage must be able to overpaint an earlier one at the
        // same depth. (Depth testing has its own dedicated pixel-oracle test.)
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.Clear(Color::Black);

        // Stage 1 -- colored 3D program: a full-viewport red quad, wound clockwise in XNA's
        // top-left screen space (TL, TR, BR / TL, BR, BL) so it is front-facing under the default
        // RasterizerState.CullCounterClockwise that stage 2's SpriteBatch.Begin installs.
        const VertexPositionColor redQuad[6] = {
            { Vector3(-1.0f, 1.0f, 0.0f),  Color(255, 0, 0, 255) },
            { Vector3(1.0f, 1.0f, 0.0f),   Color(255, 0, 0, 255) },
            { Vector3(1.0f, -1.0f, 0.0f),  Color(255, 0, 0, 255) },
            { Vector3(-1.0f, 1.0f, 0.0f),  Color(255, 0, 0, 255) },
            { Vector3(1.0f, -1.0f, 0.0f),  Color(255, 0, 0, 255) },
            { Vector3(-1.0f, -1.0f, 0.0f), Color(255, 0, 0, 255) },
        };
        DrawColoredTriangles(redQuad, 6);
        check(RegionIs(Rectangle(28, 28, 8, 8), Color(255, 0, 0, 255)),
              "stage 1: the colored 3D program paints the exact vertex color");

        // Stage 2 -- textured SpriteBatch program over the LEFT half only. SpriteBatch::Begin
        // installs DepthStencilState::None itself, which is what this frame already wants.
        spriteBatch_->Begin();
        spriteBatch_->Draw(*blueTexture_, Rectangle(0, 0, kSize / 2, kSize), Color::White);
        spriteBatch_->End();
        check(RegionIs(Rectangle(8, 30, 4, 4), Color(0, 0, 255, 255)),
              "stage 2: the textured SpriteBatch program paints the exact texture color");
        check(RegionIs(Rectangle(48, 30, 4, 4), Color(255, 0, 0, 255)),
              "stage 2: the half the SpriteBatch quad does not cover keeps stage 1's pixels");

        // Stage 3 -- back to the colored 3D program, with MORE vertices (9) than the SpriteBatch
        // quad buffer holds (6). This is the stale-attrib regression case; see the file header.
        const VertexPositionColor greenQuadPlusCorner[9] = {
            // Two triangles tiling the whole viewport, green, clockwise on screen.
            { Vector3(-1.0f, 1.0f, 0.0f),  Color(0, 255, 0, 255) },
            { Vector3(1.0f, 1.0f, 0.0f),   Color(0, 255, 0, 255) },
            { Vector3(1.0f, -1.0f, 0.0f),  Color(0, 255, 0, 255) },
            { Vector3(-1.0f, 1.0f, 0.0f),  Color(0, 255, 0, 255) },
            { Vector3(1.0f, -1.0f, 0.0f),  Color(0, 255, 0, 255) },
            { Vector3(-1.0f, -1.0f, 0.0f), Color(0, 255, 0, 255) },
            // A third triangle covering the lower-right corner, yellow, drawn last so it wins.
            // Also clockwise on screen ((32,64) -> (64,32) -> (64,64) at this 64x64 backbuffer).
            { Vector3(0.0f, -1.0f, 0.0f),  Color(255, 255, 0, 255) },
            { Vector3(1.0f, 0.0f, 0.0f),   Color(255, 255, 0, 255) },
            { Vector3(1.0f, -1.0f, 0.0f),  Color(255, 255, 0, 255) },
        };
        DrawColoredTriangles(greenQuadPlusCorner, 9);
        check(RegionIs(Rectangle(8, 8, 4, 4), Color(0, 255, 0, 255)),
              "stage 3: a longer colored 3D draw after a textured draw paints its own vertex color");
        check(RegionIs(Rectangle(56, 56, 4, 4), Color(255, 255, 0, 255)),
              "stage 3: the trailing sub-triangle of that same draw paints its own vertex color");

        // Check D -- real 32-bit indices, end to end.
        {
            dev.Clear(Color::Black);

            VertexBuffer vb(dev, PosColorDecl(), 4, BufferUsage::None);
            const VertexPositionColor verts[4] = {
                { Vector3(-1.0f, 1.0f, 0.0f),  Color(255, 0, 255, 255) },
                { Vector3(1.0f, 1.0f, 0.0f),   Color(255, 0, 255, 255) },
                { Vector3(1.0f, -1.0f, 0.0f),  Color(255, 0, 255, 255) },
                { Vector3(-1.0f, -1.0f, 0.0f), Color(255, 0, 255, 255) },
            };
            vb.SetData(verts, 0, 4);

            IndexBuffer ib(dev, IndexElementSize::ThirtyTwoBits, 6, BufferUsage::None);
            const std::uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
            ib.SetData(indices, 0, 6);
            check(ib.getIndexElementSizeProperty() == IndexElementSize::ThirtyTwoBits,
                  "check D: the IndexBuffer reports the thirty-two-bit element size it was created with");

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);

            check(RegionIs(Rectangle(30, 30, 4, 4), Color(255, 0, 255, 255)),
                  "check D: a real 32-bit IndexBuffer drives glDrawElements to the exact vertex color");
            check(RegionIs(Rectangle(1, 1, 2, 2), Color(255, 0, 255, 255)),
                  "check D: the 32-bit indexed quad covers the viewport corner too");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 8);
        result_ = (passCount_ == 8) ? 0 : 1;
        Exit();
    }

public:
    PortableGLPipelineInterleaveTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    PortableGLPipelineInterleaveTest game;
    game.Run();
    return game.getResult();
}
