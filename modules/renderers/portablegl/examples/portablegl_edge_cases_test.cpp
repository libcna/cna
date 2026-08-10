// SPDX-License-Identifier: MS-PL
// Adversarial edge-case pixel-oracle test for the PortableGL (rswinkle/PortableGL) graphics
// renderer: the odd-but-legal inputs a real game produces, each verified by GetBackBufferData()
// readback rather than by "did not throw".
//
// Check A -- a 1x1 texture drawn through SpriteBatch paints its single texel exactly.
// Check B -- a non-power-of-two (3x5) texture drawn 1:1 paints the right texel at the right
//   pixel, including the top-left one, so the source-row orientation of the textured path is
//   pinned down and not merely assumed from a uniformly coloured texture.
// Check C -- a zero-size destination rectangle draws nothing at all, while a NEGATIVE-size one
//   mirrors the sprite across its own origin and is genuinely drawn (XNA/FNA scale the source quad
//   by the destination width/height) -- covering exactly its reversed bounds and nothing else.
// Check D -- Clear() with out-of-range float colour components saturates instead of wrapping.
// Check E -- the same texture handle re-drawn several times in one frame keeps sampling correctly
//   (no per-draw texture state is consumed or invalidated).
// Check F -- churning many textures/vertex buffers/index buffers through create-and-destroy
//   leaves the renderer able to draw correctly afterwards; under the ASan/UBSan build this same
//   loop is the leak and use-after-free oracle for those handle destructors.
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

class PortableGLEdgeCasesTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> onePixel_;
    std::unique_ptr<Texture2D> npot_;
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

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(dev);

        const std::vector<std::uint8_t> single = {200, 30, 90, 255};
        onePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, single));

        // 3x5: every texel cyan except the top-left one, which is orange. Non-power-of-two in both
        // dimensions, and asymmetric top-to-bottom so a flipped source row would be visible.
        std::vector<std::uint8_t> npot(3 * 5 * 4);
        for (std::size_t i = 0; i < 3u * 5u; ++i)
        {
            npot[i * 4 + 0] = 0;
            npot[i * 4 + 1] = 255;
            npot[i * 4 + 2] = 255;
            npot[i * 4 + 3] = 255;
        }
        npot[0] = 255; npot[1] = 128; npot[2] = 0; npot[3] = 255;
        npot_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 3, 5, npot));
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        // Check A -- 1x1 texture.
        dev.Clear(Color::Black);
        spriteBatch_->Begin();
        spriteBatch_->Draw(*onePixel_, Rectangle(0, 0, 16, 16), Color::White);
        spriteBatch_->End();
        check(RegionIs(Rectangle(2, 2, 12, 12), Color(200, 30, 90, 255)),
              "check A: a 1x1 texture is sampled correctly across a scaled destination rectangle");

        // Check B -- non-power-of-two texture, drawn at its own size so texels and pixels line up.
        dev.Clear(Color::Black);
        spriteBatch_->Begin();
        spriteBatch_->Draw(*npot_, Rectangle(0, 0, 3, 5), Color::White);
        spriteBatch_->End();
        check(RegionIs(Rectangle(0, 0, 1, 1), Color(255, 128, 0, 255)),
              "check B: a 3x5 non-power-of-two texture puts its top-left texel at the top-left pixel");
        check(RegionIs(Rectangle(1, 4, 1, 1), Color(0, 255, 255, 255)) &&
                  RegionIs(Rectangle(2, 0, 1, 1), Color(0, 255, 255, 255)),
              "check B: the remaining non-power-of-two texels sample their own colour");
        check(RegionIs(Rectangle(3, 0, 4, 5), Color::Black),
              "check B: nothing is drawn beyond the non-power-of-two destination rectangle");

        // Check C -- degenerate destination rectangles.
        const Color backdrop(7, 7, 7, 255);
        dev.Clear(backdrop);
        bool degenerateThrew = false;
        try
        {
            spriteBatch_->Begin();
            spriteBatch_->Draw(*onePixel_, Rectangle(20, 20, 0, 0), Color::White);
            spriteBatch_->Draw(*onePixel_, Rectangle(50, 50, 8, 0), Color::White);
            spriteBatch_->End();
        }
        catch (...) { degenerateThrew = true; }
        check(!degenerateThrew,
              "check C: zero-size destination rectangles do not throw");
        check(RegionIs(Rectangle(0, 0, kSize, kSize), backdrop),
              "check C: a zero-width or zero-height destination rectangle draws nothing at all");

        // A NEGATIVE-size destination rectangle is not degenerate in XNA/FNA: the width and height
        // scale the source quad, so a reversed rectangle mirrors the sprite back across its own
        // origin and really is drawn. Pin down that it covers exactly [30,40) x [30,40) and
        // nothing else -- a renderer that culled the reversed winding, or that let the quad escape
        // its own bounds, fails one half of this pair.
        dev.Clear(backdrop);
        spriteBatch_->Begin();
        spriteBatch_->Draw(*onePixel_, Rectangle(40, 40, -10, -10), Color::White);
        spriteBatch_->End();
        check(RegionIs(Rectangle(30, 30, 10, 10), Color(200, 30, 90, 255)),
              "check C: a negative-size destination rectangle mirrors the sprite across its origin");
        check(RegionIs(Rectangle(0, 0, kSize, 30), backdrop) &&
                  RegionIs(Rectangle(0, 40, kSize, kSize - 40), backdrop) &&
                  RegionIs(Rectangle(0, 30, 30, 10), backdrop) &&
                  RegionIs(Rectangle(40, 30, kSize - 40, 10), backdrop),
              "check C: that mirrored quad touches no pixel outside its own reversed bounds");

        // Check D -- out-of-range clear components saturate.
        dev.Clear(2.0f, -1.0f, 5.0f, 1.0f);
        check(RegionIs(Rectangle(0, 0, kSize, kSize), Color(255, 0, 255, 255)),
              "check D: Clear() saturates out-of-range colour components instead of wrapping");

        // Check E -- the same texture drawn repeatedly in one frame.
        dev.Clear(Color::Black);
        spriteBatch_->Begin();
        for (int i = 0; i < 4; ++i)
            spriteBatch_->Draw(*onePixel_, Rectangle(i * 8, 0, 8, 8), Color::White);
        spriteBatch_->End();
        check(RegionIs(Rectangle(0, 0, 32, 8), Color(200, 30, 90, 255)),
              "check E: re-drawing one texture handle several times in a frame samples it every time");
        check(RegionIs(Rectangle(32, 0, 8, 8), Color::Black),
              "check E: those repeated draws stop exactly where the last destination rectangle ends");

        // Check F -- resource churn, then draw again.
        {
            const std::vector<std::uint8_t> scratch(4 * 4 * 4, 128);
            const std::uint16_t scratchIndices[6] = {0, 1, 2, 0, 2, 3};
            const VertexPositionColor scratchVerts[4] = {
                { Vector3(-1.0f, -1.0f, 0.0f), Color::White },
                { Vector3(1.0f, -1.0f, 0.0f),  Color::White },
                { Vector3(1.0f, 1.0f, 0.0f),   Color::White },
                { Vector3(-1.0f, 1.0f, 0.0f),  Color::White },
            };
            bool churnThrew = false;
            try
            {
                for (int i = 0; i < 128; ++i)
                {
                    Texture2D t = Texture2D::CreateFromPixels(dev, 4, 4, scratch);
                    VertexBuffer vb(dev, PosColorDecl(), 4, BufferUsage::None);
                    vb.SetData(scratchVerts, 0, 4);
                    IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
                    ib.SetData(scratchIndices, 0, 6);
                }
            }
            catch (...) { churnThrew = true; }
            check(!churnThrew, "check F: repeated resource create/destroy cycles do not throw");

            dev.Clear(Color::Black);
            VertexBuffer vb(dev, PosColorDecl(), 6, BufferUsage::None);
            const VertexPositionColor verts[6] = {
                { Vector3(-1.0f, -1.0f, 0.0f), Color(0, 0, 255, 255) },
                { Vector3(1.0f, -1.0f, 0.0f),  Color(0, 0, 255, 255) },
                { Vector3(1.0f, 1.0f, 0.0f),   Color(0, 0, 255, 255) },
                { Vector3(-1.0f, -1.0f, 0.0f), Color(0, 0, 255, 255) },
                { Vector3(1.0f, 1.0f, 0.0f),   Color(0, 0, 255, 255) },
                { Vector3(-1.0f, 1.0f, 0.0f),  Color(0, 0, 255, 255) },
            };
            vb.SetData(verts, 0, 6);
            BasicEffect fx(dev);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(RegionIs(Rectangle(28, 28, 8, 8), Color(0, 0, 255, 255)),
                  "check F: the renderer still draws correctly after the churn");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 13);
        result_ = (passCount_ == 13) ? 0 : 1;
        Exit();
    }

public:
    PortableGLEdgeCasesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    PortableGLEdgeCasesTest game;
    game.Run();
    return game.getResult();
}
