// SPDX-License-Identifier: MS-PL
// REMED-GFX-011: orientation oracle / calibration.
//
// Every other orientation test in this family asserts an absolute screen orientation. That
// assertion is only meaningful if the convention it encodes is itself pinned down, so this test
// establishes it against a shader family that is already known to apply the Vulkan NDC Y-flip
// correctly (colored3d, via BasicEffect with vertex colours).
//
// Two independent conventions are pinned at once:
//   1. XNA clip space is +Y up, so a vertex at y=+0.5 belongs in the UPPER half of the image.
//   2. GetBackBufferData returns rows top-first, so the upper half is the LOW pixel-row range.
//
// The geometry is a single quad confined to the +X/+Y quadrant, i.e. asymmetric on BOTH axes.
// A scene with any vertical symmetry cannot detect a vertical mirror at all — the blind spot that
// let the missing Y-flip survive every pre-existing Vulkan test, all of which sample the centre
// row. Sampling all four quadrant centres distinguishes: correct, vertically mirrored,
// horizontally mirrored, and both.
//
// Expected, with the quad in the +X/+Y quadrant:
//   top-right    = red   (the quad)
//   top-left     = green (background)
//   bottom-right = green (background)   <-- red here means a vertical mirror
//   bottom-left  = green (background)
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

// Matches the Vulkan colored3d stride=16 vertex format.
struct GpuVPC { float x, y, z; uint8_t r, g, b, a; };
static_assert(sizeof(GpuVPC) == 16);

// Quadrant the quad occupies in XNA clip space: x in [+0.2,+0.9], y in [+0.2,+0.9].
static constexpr float kLo = 0.2f;
static constexpr float kHi = 0.9f;

class VulkanOrientationCalibrationTest : public Game
{
    bool done_   = false;
    int  result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 255, 0, 255));   // green background
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        // Red quad confined to the +X/+Y quadrant (upper-right in XNA terms).
        const GpuVPC verts[6] = {
            { kLo, kHi, 0.0f, 255, 0, 0, 255 },
            { kLo, kLo, 0.0f, 255, 0, 0, 255 },
            { kHi, kLo, 0.0f, 255, 0, 0, 255 },
            { kLo, kHi, 0.0f, 255, 0, 0, 255 },
            { kHi, kLo, 0.0f, 255, 0, 0, 255 },
            { kHi, kHi, 0.0f, 255, 0, 0, 255 },
        };
        VertexBuffer vb(device, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(GpuVPC)));

        BasicEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        device.SetVertexBuffer(&vb);
        // Winding here is back-facing under CNA's real default RasterizerState (REMED-GFX-052).
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        // Sample all four quadrant centres.
        const Rectangle trReg(3 * W / 4,     H / 4, 1, 1);
        const Rectangle tlReg(    W / 4,     H / 4, 1, 1);
        const Rectangle brReg(3 * W / 4, 3 * H / 4, 1, 1);
        const Rectangle blReg(    W / 4, 3 * H / 4, 1, 1);
        Color tr(0,0,0,0), tl(0,0,0,0), br(0,0,0,0), bl(0,0,0,0);
        device.GetBackBufferData(&trReg, &tr, 0, 1);
        device.GetBackBufferData(&tlReg, &tl, 0, 1);
        device.GetBackBufferData(&brReg, &br, 0, 1);
        device.GetBackBufferData(&blReg, &bl, 0, 1);

        auto isRed   = [](const Color& c) { return c.getRProperty() >= 200 && c.getGProperty() <= 60; };
        auto isGreen = [](const Color& c) { return c.getGProperty() >= 200 && c.getRProperty() <= 60; };

        const bool ok = isRed(tr) && isGreen(tl) && isGreen(br) && isGreen(bl);

        std::printf("[%s] VulkanOrientationCalibration (colored3d oracle): "
                    "TR=(%d,%d,%d) TL=(%d,%d,%d) BR=(%d,%d,%d) BL=(%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL",
                    tr.getRProperty(), tr.getGProperty(), tr.getBProperty(),
                    tl.getRProperty(), tl.getGProperty(), tl.getBProperty(),
                    br.getRProperty(), br.getGProperty(), br.getBProperty(),
                    bl.getRProperty(), bl.getGProperty(), bl.getBProperty());
        if (!ok)
        {
            std::printf("       expected TR=red, TL/BR/BL=green "
                        "(quad at XNA +X/+Y must land upper-right)\n");
            if (isRed(br))
                std::printf("       BR is red -> VERTICAL MIRROR\n");
            if (isRed(tl))
                std::printf("       TL is red -> HORIZONTAL MIRROR\n");
        }
        result_ = ok ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    VulkanOrientationCalibrationTest game;
    game.Run();
    return game.getResult();
}
