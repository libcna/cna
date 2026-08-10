// SPDX-License-Identifier: MS-PL
// Task 880: GraphicsDevice.Viewport GPU wiring — Vulkan sub-region (split-screen) viewport.
//
// GraphicsDevice.Viewport previously had zero GPU renderer effect on Vulkan: the backbuffer pass
// in RecordCommandBuffer always hardcoded vkCmdSetViewport to the full swapchain extent
// regardless of what Viewport was set to. This test proves a genuine sub-region Viewport (e.g.
// the left half of the window, as in a split-screen layout) now actually clips/scales rendering
// to that region: a full-NDC-range quad (-1..1 on both axes) drawn under a custom Viewport should
// fill exactly that Viewport's own rectangle, not the whole window.
//
// Mirrors vulkan_scissor_test.cpp's structure: one scenario per frame (frame_ state machine),
// with a blank-frame retry loop (<=20 attempts) to skip the intermittent AMD/RADV driver flake
// that returns the clear colour on the first GetBackBufferData call after a swapchain present.
//
//   Frame 0 — sub-region Viewport (left half): clear black, draw full-NDC red quad.
//     Left-half sample point must be red (inside the custom Viewport).
//     Right-half sample point must stay black (outside the custom Viewport — not drawn there).
//
//   Frame 1 — full Viewport restored: clear black, draw full-NDC green quad.
//     Both left-half and right-half sample points must be green.
//     Also verifies Viewport getter/setter round-trip.
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
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

class VulkanViewportSubregionTest : public Game
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

    // Draw a full-NDC-range quad using DrawUserPrimitives.
    static void drawQuad(GraphicsDevice& dev, BasicEffect& fx, const Color& col)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.f,  1.f, 0.f), col },
            { Vector3( 1.f,  1.f, 0.f), col },
            { Vector3(-1.f, -1.f, 0.f), col },
            { Vector3(-1.f, -1.f, 0.f), col },
            { Vector3( 1.f,  1.f, 0.f), col },
            { Vector3( 1.f, -1.f, 0.f), col },
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

        static const Color kRed(255, 0, 0, 255);
        static const Color kGreen(0, 255, 0, 255);

        const Viewport fullVp = dev.getViewportProperty();
        const int W = fullVp.getWidthProperty();
        const int H = fullVp.getHeightProperty();
        const int leftHalfW = W / 2;

        // Sample points: well inside the left half and well inside the right half.
        const int leftX  = leftHalfW / 2;
        const int rightX = leftHalfW + (W - leftHalfW) / 2;
        const int midY   = H / 2;

        if (frame_ == 0)
        {
            // Frame 0: sub-region Viewport (left half) — draw must only reach the left half.
            Color left(0,0,0,0), right(0,0,0,0);
            for (int i = 0; i < 20; ++i)
            {
                dev.Clear(Color::Black);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                dev.setViewportProperty(Viewport(0, 0, leftHalfW, H));
                fx.Apply();
                drawQuad(dev, fx, kRed);

                left  = readAt(dev, leftX, midY);
                right = readAt(dev, rightX, midY);
                if (!isBlack(left) || !isBlack(right)) break;
            }
            check(isRed(left),    "sub-region Viewport: left-half (drawn)",       left,  "RED");
            check(isBlack(right), "sub-region Viewport: right-half (not drawn)",  right, "BLACK");
            ++frame_;
            return;
        }

        if (frame_ == 1)
        {
            // Frame 1: restore full Viewport — draw must reach both halves.
            dev.setViewportProperty(fullVp);

            Color left(0,0,0,0), right(0,0,0,0);
            for (int i = 0; i < 20; ++i)
            {
                dev.Clear(Color::Black);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                fx.Apply();
                drawQuad(dev, fx, kGreen);

                left  = readAt(dev, leftX, midY);
                right = readAt(dev, rightX, midY);
                if (!isBlack(left) || !isBlack(right)) break;
            }
            check(isGreen(left),  "full Viewport restored: left-half",  left,  "GREEN");
            check(isGreen(right), "full Viewport restored: right-half", right, "GREEN");

            const Viewport testVp(10, 20, 300, 200);
            dev.setViewportProperty(testVp);
            const Viewport got = dev.getViewportProperty();
            const bool roundTripOk =
                got.getXProperty() == testVp.getXProperty() &&
                got.getYProperty() == testVp.getYProperty() &&
                got.getWidthProperty() == testVp.getWidthProperty() &&
                got.getHeightProperty() == testVp.getHeightProperty();
            std::printf("[%s] Viewport get/set round-trip preserves all fields\n",
                        roundTripOk ? "PASS" : "FAIL");
            if (roundTripOk) ++pass_; else ++fail_;

            std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
            Exit();
        }
    }

public:
    VulkanViewportSubregionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanViewportSubregionTest game;
    game.Run();
    return game.getResult();
}
