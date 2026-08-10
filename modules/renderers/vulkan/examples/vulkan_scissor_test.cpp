// SPDX-License-Identifier: MS-PL
// Task 329: Vulkan ScissorTestEnable + ScissorRectangle interaction test.
//
// Verifies that enabling RasterizerState.ScissorTestEnable and setting
// GraphicsDevice.ScissorRectangle correctly clips 3D draw calls to the
// specified rectangle, leaving pixels outside the rect at the clear colour.
//
// Two scenarios, one per frame:
//
//   Frame 0 — no scissor (ScissorTestEnable=false, default):
//     Clear to green, draw full-screen red quad.
//     Both the top-left centre (16,16) and bottom-right centre (48,48) must be red.
//
//   Frame 1 — scissor=top-left (ScissorTestEnable=true, rect=[0,0,32,32]):
//     Clear to green, enable scissor to top-left quadrant, draw full-screen red quad.
//     Top-left centre (16,16) must be red (inside scissor).
//     Bottom-right centre (48,48) must be green (outside scissor, clear colour preserved).
//
// Both frames use a blank-frame retry (≤20 attempts) to skip the intermittent AMD/RADV
// driver flake that returns the clear colour on the first GetBackBufferData call.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

class VulkanScissorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;
    int pass_  = 0;
    int fail_  = 0;

    static bool isRed(const Color& c)
    {
        return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60;
    }
    static bool isGreen(const Color& c)
    {
        return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
    }
    static bool isBlack(const Color& c)
    {
        return c.getRProperty() < 30 && c.getGProperty() < 30 && c.getBProperty() < 30;
    }

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: (%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: (%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

    Color readAt(GraphicsDevice& dev, int px, int py)
    {
        Color c(0, 0, 0, 0);
        Rectangle reg(px, py, 1, 1);
        dev.GetBackBufferData(&reg, &c, 0, 1);
        return c;
    }

    // Draw a full-screen red quad using DrawUserPrimitives (NDC coords).
    static void drawRedQuad(GraphicsDevice& dev, BasicEffect& fx)
    {
        static const Color red(255, 0, 0, 255);
        const VertexPositionColor verts[6] = {
            { Vector3(-1.f,  1.f, 0.f), red },
            { Vector3( 1.f,  1.f, 0.f), red },
            { Vector3(-1.f, -1.f, 0.f), red },
            { Vector3(-1.f, -1.f, 0.f), red },
            { Vector3( 1.f,  1.f, 0.f), red },
            { Vector3( 1.f, -1.f, 0.f), red },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;

        static const Color kGreen(0, 255, 0, 255);

        // Sample points: centre of top-left quadrant and centre of bottom-right quadrant.
        const int tlX = kSize / 4,       tlY = kSize / 4;        // (16, 16)
        const int brX = kSize * 3 / 4,   brY = kSize * 3 / 4;    // (48, 48)

        if (frame_ == 0)
        {
            // Frame 0: no scissor — full screen should be red.
            Color tl(0,0,0,0), br(0,0,0,0);
            for (int i = 0; i < 20; ++i)
            {
                dev.Clear(kGreen);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState()); // scissor disabled by default
                fx.Apply();
                drawRedQuad(dev, fx);

                tl = readAt(dev, tlX, tlY);
                br = readAt(dev, brX, brY);
                if (!isBlack(tl) || !isBlack(br)) break;
            }
            check(isRed(tl), "NoScissor: top-left (16,16)",    tl, "RED");
            check(isRed(br), "NoScissor: bottom-right (48,48)", br, "RED");
            ++frame_;
            return;
        }

        if (frame_ == 1)
        {
            // Frame 1: scissor to top-left 32×32.
            // Top-left centre should be red; bottom-right centre should stay green.
            RasterizerState rs;
            rs.setScissorTestEnableProperty(true);
            dev.setScissorRectangleProperty(Rectangle(0, 0, kSize / 2, kSize / 2));

            Color tl(0,0,0,0), br(0,0,0,0);
            for (int i = 0; i < 20; ++i)
            {
                dev.Clear(kGreen);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(rs);
                fx.Apply();
                drawRedQuad(dev, fx);

                tl = readAt(dev, tlX, tlY);
                br = readAt(dev, brX, brY);
                // A produced frame has br=green (not black); skip pure blank frames.
                if (!isBlack(tl) || !isBlack(br)) break;
            }
            check(isRed(tl),   "ScissorTopLeft: inside  (16,16)",  tl, "RED");
            check(isGreen(br), "ScissorTopLeft: outside (48,48)",  br, "GREEN");

            std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
            Exit();
        }
    }

public:
    VulkanScissorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanScissorTest game;
    game.Run();
    return game.getResult();
}
