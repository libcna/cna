// SPDX-License-Identifier: MS-PL
// Task 328: Vulkan depth bias / slope-scale depth bias integration test.
//
// plans/plan_vulkan.md VULKAN-091 corrected this file's DEPTH CONVENTION, which had been
// OpenGL's rather than XNA's, and had been failing the renderer for it.
//
// The flat scenarios used to place their triangles at z = 0 under an identity projection and
// describe that as "depth 0.5". That is the OpenGL mapping: GL clips z to [-1,1] and maps it to
// [0,1], so z = 0 lands mid-range and a negative depth bias has somewhere to go. XNA uses
// Direct3D 9's convention -- clip z in [0,w], depth in [0,1] -- and so does Vulkan, so under both
// of them z = 0 is the NEAR PLANE. Nothing can be biased in front of it: the viewport depth range
// clamps at 0, the coplanar redraw stays at exactly 0, and the LESS test fails. The test then read
// that as "Vulkan drops the constant depth bias".
//
// It does not. spikes/vulkan-depth-bias-spike/ runs the same coplanar experiment off screen with
// no surface, on EVERY device the loader offers, and vkCmdSetDepthBias's constant factor behaves
// exactly as Vulkan specifies on BOTH of this machine's drivers -- llvmpipe with the same
// D24_UNORM_S8_UINT format this renderer picks, and AMD RADV, which the renderer itself cannot
// reach under Xvfb because RADV refuses presentation without DRI3. Two drivers, which is what
// plan_vulkan.md requires before a result may be attributed to one.
//
// The flat scenarios therefore sit at z = 0.5 now, and the tilted ones span 0.2 to 0.8 instead of
// straddling the near plane and being clipped in half. Leg E is new and is the guard: a flat
// triangle AT z = 0 with the same -1e6 bias must stay RED, because that is what XNA's depth range
// means. Without it this file could drift back to the OpenGL premise unnoticed.
//
// Verifies that RasterizerState.DepthBias and RasterizerState.SlopeScaleDepthBias
// are applied by the Vulkan renderer (via vkCmdSetDepthBias) and actually change the
// outcome of the depth test.
//
// Method (a "shadow acne"-style coplanar test):
//   The depth buffer is cleared to 1.0 and DepthStencilState.DepthBufferFunction is explicitly
//   set to CompareFunction::Less (Task 870: real per-DepthBufferFunction pipeline selection
//   landed on Vulkan, so this can no longer rely on incidental renderer-hardcoded LESS behavior
//   the way it did before -- XNA's real DepthStencilState.Default is actually LessEqual, under
//   which this coplanar-redraw trick can never discriminate anything, since an equal-depth
//   redraw always passes regardless of bias).
//   For each scenario a red triangle A is drawn first with no bias, writing its depth.
//   A green triangle B with EXACTLY the same geometry is drawn second. Because the depth
//   test is LESS, a second draw at equal depth fails (centre stays red) — unless a negative
//   depth bias pulls B's depth toward the camera so it passes (centre turns green).
//
//   All four scenarios are rendered in a SINGLE frame, side by side in four vertical
//   strips, and read back with one GetBackBufferData capture.
//
//   Strip 0 — flat z=0.5,   B DepthBias = 0            → RED   (B fails the equal-depth test)
//   Strip 1 — flat z=0.5,   B DepthBias = -1e6         → GREEN (constant bias pulls B in front)
//   Strip 2 — tilted 0.2..0.8, B SlopeScaleDepthBias = 0  → RED
//   Strip 3 — tilted 0.2..0.8, B SlopeScaleDepthBias=-2e3 → GREEN (slope bias pulls B in front)
//   Strip 4 — flat z=0.0,   B DepthBias = -1e6         → RED   (VULKAN-091: z=0 is the near
//                                                        plane under XNA's D3D9 depth range, and
//                                                        the clamp there is not a missing bias)
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
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
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
    // NDC x = cx. VULKAN-091: these z values are XNA's Direct3D 9 depth range, [0,1] with 0 at
    // the near plane, which is also Vulkan's. `flatZ` places a zero-slope triangle at that depth;
    // `tilted` runs 0.2 at the apex to 0.8 at the base, so the slope term has something to scale
    // and no part of the primitive is clipped against the near plane.
    static void drawTri(GraphicsDevice& dev, float cx, bool tilted, float flatZ, const Color& col)
    {
        const float zt = tilted ? 0.2f : flatZ;
        const float zb = tilted ? 0.8f : flatZ;
        const VertexPositionColor verts[3] = {
            { Vector3(cx,         0.8f, zt), col },
            { Vector3(cx + 0.13f, -0.8f, zb), col },
            { Vector3(cx - 0.13f, -0.8f, zb), col },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 1);
    }

    // Red A with no bias (writes depth), then green B with the scenario's bias.
    void drawPair(GraphicsDevice& dev, float cx, bool tilted, float flatZ,
                  const RasterizerState& rsB)
    {
        dev.setRasterizerStateProperty(RasterizerState());
        drawTri(dev, cx, tilted, flatZ, Color(255, 0, 0, 255));

        dev.setRasterizerStateProperty(rsB);
        drawTri(dev, cx, tilted, flatZ, Color(0, 255, 0, 255));
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

        const float cx[5] = { -0.72f, -0.36f, 0.0f, 0.36f, 0.72f };

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;

        // Task 870: explicitly request strict Less -- see this file's header comment for why
        // relying on the default (real XNA DepthBufferFunction=LessEqual) can't work here.
        DepthStencilState dss;
        dss.setDepthBufferFunctionProperty(CompareFunction::Less);
        dev.setDepthStencilStateProperty(dss);

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
        Color p0(0,0,0,0), p1(0,0,0,0), p2(0,0,0,0), p3(0,0,0,0), p4(0,0,0,0);
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            // Render all five scenarios into one frame (depth auto-clears to 1.0).
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            fx.Apply();
            drawPair(dev, cx[0], false, 0.5f, rsNoBias); // expect RED
            drawPair(dev, cx[1], false, 0.5f, rsConst);  // expect GREEN
            drawPair(dev, cx[2], true,  0.5f, rsSlope0); // expect RED
            drawPair(dev, cx[3], true,  0.5f, rsSlope);  // expect GREEN
            drawPair(dev, cx[4], false, 0.0f, rsConst);  // expect RED -- the near plane

            // Single capture: read the centre of each strip.
            p0 = readAt(dev, cx[0]);
            p1 = readAt(dev, cx[1]);
            p2 = readAt(dev, cx[2]);
            p3 = readAt(dev, cx[3]);
            p4 = readAt(dev, cx[4]);

            // A produced frame always has rendered content (strip 1 is green); a blank flake
            // has every strip at the clear colour. Accept the first non-blank frame.
            if (!(isBlack(p0) && isBlack(p1) && isBlack(p2) && isBlack(p3) && isBlack(p4)))
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
        // VULKAN-091's guard leg. Nothing can be biased in front of the near plane, so this one
        // must stay RED -- and if a future change ever makes it GREEN, the depth range has stopped
        // being XNA's. This is the leg whose absence let the whole file be written against
        // OpenGL's mapping and read a correct renderer as broken.
        std::snprintf(buf, sizeof(buf),
                      "DepthBias=-1e6 at the near plane z=0 (flat): (%d,%d,%d) expected RED "
                      "-- XNA depth starts at 0 and the clamp there is not a dropped bias",
                      p4.getRProperty(), p4.getGProperty(), p4.getBProperty());
        check(isRed(p4), buf);

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
