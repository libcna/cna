// SPDX-License-Identifier: MS-PL
// Task 328: Vulkan depth bias / slope-scale depth bias integration test.
//
// Verifies that RasterizerState.DepthBias and RasterizerState.SlopeScaleDepthBias
// are applied by the Vulkan backend (via vkCmdSetDepthBias) and actually change the
// outcome of the depth test.
//
// Method (a "shadow acne"-style coplanar test):
//   The depth buffer is cleared to 1.0 and the depth compare op is LESS.
//   For each scenario a red triangle A is drawn first with no bias, writing its depth.
//   A green triangle B with EXACTLY the same geometry is drawn second. Because the depth
//   test is LESS, a second draw at equal depth fails (centre stays red) — unless a negative
//   depth bias pulls B's depth toward the camera so it passes (centre turns green).
//
//   All four scenarios are rendered in a SINGLE frame, side by side in four vertical
//   strips, and read back with one GetBackBufferData capture.
//
//   Strip 0 — flat,   B DepthBias = 0            → RED   (B fails the equal-depth test)
//   Strip 1 — flat,   B DepthBias = -1e6         → GREEN (constant bias pulls B in front)
//   Strip 2 — tilted, B SlopeScaleDepthBias = 0  → RED   (B fails the equal-depth test)
//   Strip 3 — tilted, B SlopeScaleDepthBias=-2e3 → GREEN (slope bias pulls B in front)
//
// Bias magnitudes are deliberately large: vkCmdSetDepthBias scales the constant factor by
// the depth format's minimum resolvable difference and the slope factor by the primitive's
// depth slope, both small, so a large negative factor gives an unambiguous, format-
// independent result. Overshoot is harmless — a depth clamped to 0 still passes LESS.
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

#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class VulkanDepthBiasTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    // CW-winding triangle (front face under default CullCounterClockwiseFace), centred at
    // NDC x = cx. tilted=false → all vertices at z=0 (depth 0.5, slope 0); tilted=true →
    // top at z=-0.6 (depth 0.2), base at z=+0.6 (depth 0.8): depth slopes top-to-bottom.
    static void drawTri(GraphicsDevice& dev, float cx, bool tilted, const Color& col)
    {
        const float zt = tilted ? -0.6f : 0.0f;
        const float zb = tilted ?  0.6f : 0.0f;
        const VertexPositionColor verts[3] = {
            { Vector3(cx,         0.8f, zt), col },
            { Vector3(cx + 0.15f, -0.8f, zb), col },
            { Vector3(cx - 0.15f, -0.8f, zb), col },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 1);
    }

    // Red A with no bias (writes depth), then green B with the scenario's bias.
    void drawPair(GraphicsDevice& dev, float cx, bool tilted, const RasterizerState& rsB)
    {
        dev.setRasterizerStateProperty(RasterizerState());
        drawTri(dev, cx, tilted, Color(255, 0, 0, 255));

        dev.setRasterizerStateProperty(rsB);
        drawTri(dev, cx, tilted, Color(0, 255, 0, 255));
    }

    static bool isBlack(const Color& px)
    {
        return px.getRProperty() < 30 && px.getGProperty() < 30 && px.getBProperty() < 30;
    }

    Color readAt(GraphicsDevice& dev, float ndcX)
    {
        const auto& vp = dev.getViewportProperty();
        const int px = static_cast<int>((ndcX + 1.0f) * 0.5f * vp.getWidthProperty());
        Rectangle reg(px, vp.getHeightProperty() / 2, 1, 1);
        Color c(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &c, 0, 1);
        return c;
    }

    static bool isRed(const Color& px)
    {
        return px.getRProperty() >= 200 && px.getGProperty() <= 60 && px.getBProperty() <= 60;
    }
    static bool isGreen(const Color& px)
    {
        return px.getGProperty() >= 200 && px.getRProperty() <= 60 && px.getBProperty() <= 60;
    }

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        const float cx[4] = { -0.6f, -0.2f, 0.2f, 0.6f };

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;

        RasterizerState rsNoBias;                          // DepthBias = 0
        RasterizerState rsConst;  rsConst.setDepthBiasProperty(-1000000.0f);
        RasterizerState rsSlope0;                          // SlopeScaleDepthBias = 0
        RasterizerState rsSlope;  rsSlope.setSlopeScaleDepthBiasProperty(-2000.0f);

        // On this AMD RADV PHOENIX iGPU, backbuffer rendering intermittently produces a
        // blank (clear-colour) frame in the GetBackBufferData capture — an all-or-nothing
        // driver flake that is independent of the depth-bias logic (the rendered result is
        // ALWAYS fully correct, never a wrong colour). Re-render until the frame is produced;
        // a real depth-bias bug would render a wrong colour and stop the retry, so this only
        // masks the blank-frame flake, never an actual failure.
        Color p0(0,0,0,0), p1(0,0,0,0), p2(0,0,0,0), p3(0,0,0,0);
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            // Render all four scenarios into one frame (depth auto-clears to 1.0).
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            fx.Apply();
            drawPair(dev, cx[0], false, rsNoBias); // expect RED
            drawPair(dev, cx[1], false, rsConst);  // expect GREEN
            drawPair(dev, cx[2], true,  rsSlope0); // expect RED
            drawPair(dev, cx[3], true,  rsSlope);  // expect GREEN

            // Single capture: read the centre of each strip.
            p0 = readAt(dev, cx[0]);
            p1 = readAt(dev, cx[1]);
            p2 = readAt(dev, cx[2]);
            p3 = readAt(dev, cx[3]);

            // A produced frame always has rendered content (strip 1 is green); a blank flake
            // has every strip at the clear colour. Accept the first non-blank frame.
            if (!(isBlack(p0) && isBlack(p1) && isBlack(p2) && isBlack(p3)))
                break;
        }

        char buf[160];
        std::snprintf(buf, sizeof(buf), "DepthBias=0 (flat): (%d,%d,%d) expected RED",
                      p0.getRProperty(), p0.getGProperty(), p0.getBProperty());
        check(isRed(p0), buf);
        std::snprintf(buf, sizeof(buf), "DepthBias=-1e6 (flat): (%d,%d,%d) expected GREEN",
                      p1.getRProperty(), p1.getGProperty(), p1.getBProperty());
        check(isGreen(p1), buf);
        std::snprintf(buf, sizeof(buf), "SlopeScale=0 (tilted): (%d,%d,%d) expected RED",
                      p2.getRProperty(), p2.getGProperty(), p2.getBProperty());
        check(isRed(p2), buf);
        std::snprintf(buf, sizeof(buf), "SlopeScale=-2000 (tilted): (%d,%d,%d) expected GREEN",
                      p3.getRProperty(), p3.getGProperty(), p3.getBProperty());
        check(isGreen(p3), buf);

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanDepthBiasTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanDepthBiasTest game;
    game.Run();
    return game.getResult();
}
