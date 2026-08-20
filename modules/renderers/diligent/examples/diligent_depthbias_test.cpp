// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-49: real-device proof that RasterizerState.DepthBias and
// SlopeScaleDepthBias visibly change a depth-test outcome through the Diligent renderer, not just
// "the fields are decoded into Dg::RasterizerStateDesc and never crash".
//
// Method (the same "shadow acne"-style coplanar test used by vulkan_depth_bias_test.cpp, Task 328):
// depth is cleared to 1.0, DepthStencilState.DepthBufferFunction is forced to CompareFunction::Less
// (real XNA's own default is LessEqual, under which an equal-depth redraw always passes regardless
// of bias and could never discriminate anything here). A red triangle A is drawn first, writing its
// depth. A green triangle B with EXACTLY the same geometry is drawn second with the scenario's
// RasterizerState. Under LESS, a second draw at equal depth fails (centre stays red) -- unless a
// negative bias pulls B's depth toward the camera so it passes (centre turns green).
//
// plans/plan_diligent.md DILIGENT-64 note: DiligentRenderer::ApplyRasterizerState() used to pack
// DepthBias/SlopeScaleDepthBias into a single signed byte each inside PipelineKey::raster, which
// silently wrapped sign once a scaled value left [-128, 127] (see
// DiligentDeviceSelectionTests.cpp's DepthBiasRawUnits* tests for that boundary/sign case, provable
// with no GPU). PipelineKey now stores DepthBias as its own lossless Int32 field
// (ComputeDiligentDepthBiasRawUnits()) and SlopeScaledDepthBias as an exact Float32, matching
// Dg::RasterizerStateDesc's own field types. This test's own values (-0.128 and -8.0) still work
// unchanged -- they were always exactly representable, byte-packed or not -- so checks 0-3 below are
// a pure non-regression check for the storage change, not new coverage.
//
// Two scenarios: flat geometry (constant DepthBias) and tilted geometry with a real depth slope
// (SlopeScaleDepthBias). Both rendered side by side in one frame, read back with one
// GetBackBufferData capture.
//
//   Strip 0 — flat,   B DepthBias = 0           → RED   (B fails the equal-depth test)
//   Strip 1 — flat,   B DepthBias = -0.128      → GREEN (constant bias pulls B in front)
//   Strip 2 — tilted, B SlopeScaleDepthBias = 0 → RED   (B fails the equal-depth test)
//   Strip 3 — tilted, B SlopeScaleDepthBias=-8.0 → GREEN (slope bias pulls B in front)
//
// Checks 4-6 are DILIGENT-64's own "A -> B -> A" pipeline-cache-identity acceptance criterion:
// SlopeScaleDepthBias is the only one of the two bias fields with an observable pixel effect on
// this software device (DepthBias's constant term shows none, on any sign, at any magnitude -- see
// below), so it stands in for both fields to prove GetOrCreatePipeline() still creates and reuses
// the right cached pipeline across a bias -> no-bias -> bias sequence at a fixed draw position, now
// that the two fields moved out of the shared byte-packed `raster` key member into their own.
//
//   Strip 4 — tilted, B SlopeScaleDepthBias=-8.0 (A)       → GREEN
//   Strip 5 — tilted, B SlopeScaleDepthBias=0     (B)      → RED (not stuck at strip 4's cached PSO)
//   Strip 6 — tilted, B SlopeScaleDepthBias=-8.0 (A again) → GREEN (re-visiting A works again)
//
// If this renderer's constant DepthBias term turns out to produce no visible effect on the software
// (lavapipe) device under test -- the same open-gap shape as D3D9's own still-unresolved D9-62 --
// that is itself the recorded finding; this test does not silently report PASS for that.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
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
#include <exception>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 320;
    constexpr int kHeight = 240;
    constexpr int kChecks = 7;

    bool IsRed(const Color& px)
    {
        return px.getRProperty() >= 200 && px.getGProperty() <= 60 && px.getBProperty() <= 60;
    }

    bool IsGreen(const Color& px)
    {
        return px.getGProperty() >= 200 && px.getRProperty() <= 60 && px.getBProperty() <= 60;
    }

    bool IsBlack(const Color& px)
    {
        return px.getRProperty() < 30 && px.getGProperty() < 30 && px.getBProperty() < 30;
    }
}

class DiligentDepthBiasTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

    // CW-winding triangle (front face under default CullCounterClockwiseFace), centred at NDC
    // x = cx. tilted=false -> all vertices at z=0 (depth 0.5, zero slope); tilted=true -> top at
    // z=-0.6 (depth 0.2), base at z=+0.6 (depth 0.8): a real depth slope top-to-bottom.
    static void DrawTriangle(GraphicsDevice& device, float cx, bool tilted, const Color& color)
    {
        const float zTop = tilted ? -0.6f : 0.0f;
        const float zBottom = tilted ? 0.6f : 0.0f;
        const VertexPositionColor verts[3] = {
            {Vector3(cx, 0.8f, zTop), color},
            {Vector3(cx + 0.15f, -0.8f, zBottom), color},
            {Vector3(cx - 0.15f, -0.8f, zBottom), color},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 1);
    }

    // Red A with no bias (writes depth), then green B with the scenario's RasterizerState.
    static void DrawPair(GraphicsDevice& device, float cx, bool tilted,
                          const RasterizerState& rasterizerStateB)
    {
        device.setRasterizerStateProperty(RasterizerState());
        DrawTriangle(device, cx, tilted, Color(255, 0, 0, 255));

        device.setRasterizerStateProperty(rasterizerStateB);
        DrawTriangle(device, cx, tilted, Color(0, 255, 0, 255));
    }

    static Color ReadAt(GraphicsDevice& device, float ndcX)
    {
        const auto& viewport = device.getViewportProperty();
        const int px = static_cast<int>((ndcX + 1.0f) * 0.5f * viewport.getWidthProperty());
        Rectangle region(px, viewport.getHeightProperty() / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        const float cx[4] = {-0.6f, -0.2f, 0.2f, 0.6f};

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.VertexColorEnabled = true;

        // Real XNA's own DepthStencilState.Default is LessEqual, under which this coplanar-redraw
        // trick can never discriminate anything (an equal-depth redraw always passes). Force Less.
        DepthStencilState depthStencilState;
        depthStencilState.setDepthBufferFunctionProperty(CompareFunction::Less);
        device.setDepthStencilStateProperty(depthStencilState);

        RasterizerState rasterizerNoBias;                            // DepthBias = 0
        RasterizerState rasterizerConstBias;
        rasterizerConstBias.setDepthBiasProperty(-0.128f);            // packs to the byte -128
        RasterizerState rasterizerNoSlope;                            // SlopeScaleDepthBias = 0
        RasterizerState rasterizerSlopeBias;
        rasterizerSlopeBias.setSlopeScaleDepthBiasProperty(-8.0f);    // packs to the byte -128

        Color p0(0, 0, 0, 0), p1(0, 0, 0, 0), p2(0, 0, 0, 0), p3(0, 0, 0, 0);
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            device.Clear(Color(0, 0, 0, 255));
            device.setBlendStateProperty(BlendState::Opaque);
            effect.Apply();
            DrawPair(device, cx[0], false, rasterizerNoBias);    // expect RED
            DrawPair(device, cx[1], false, rasterizerConstBias); // expect GREEN
            DrawPair(device, cx[2], true, rasterizerNoSlope);    // expect RED
            DrawPair(device, cx[3], true, rasterizerSlopeBias);  // expect GREEN

            p0 = ReadAt(device, cx[0]);
            p1 = ReadAt(device, cx[1]);
            p2 = ReadAt(device, cx[2]);
            p3 = ReadAt(device, cx[3]);

            // A produced frame always has rendered content (strip 1 is green); an all-clear-colour
            // read is a blank-frame capture flake independent of depth-bias logic. Retry it.
            if (!(IsBlack(p0) && IsBlack(p1) && IsBlack(p2) && IsBlack(p3)))
                break;
        }

        char buf[160];
        std::snprintf(buf, sizeof(buf), "DepthBias=0 (flat): (%d,%d,%d) expected RED",
                      p0.getRProperty(), p0.getGProperty(), p0.getBProperty());
        Check(IsRed(p0), buf);
        std::snprintf(buf, sizeof(buf), "DepthBias=-0.128 (flat): (%d,%d,%d) expected GREEN",
                      p1.getRProperty(), p1.getGProperty(), p1.getBProperty());
        Check(IsGreen(p1), buf);
        std::snprintf(buf, sizeof(buf), "SlopeScaleDepthBias=0 (tilted): (%d,%d,%d) expected RED",
                      p2.getRProperty(), p2.getGProperty(), p2.getBProperty());
        Check(IsRed(p2), buf);
        std::snprintf(buf, sizeof(buf), "SlopeScaleDepthBias=-8.0 (tilted): (%d,%d,%d) expected GREEN",
                      p3.getRProperty(), p3.getGProperty(), p3.getBProperty());
        Check(IsGreen(p3), buf);

        // DILIGENT-64's own "A -> B -> A" pipeline-cache-identity acceptance check: with everything
        // else about the draw held fixed (same position, same variant/topology/blend/depth-stencil),
        // only SlopeScaleDepthBias changes across three sequential frames. If PipelineKey's own
        // depthBias/slopeScaledDepthBias fields were somehow still aliasing between distinct values
        // (the class of bug this task's storage change guards against), the second or third step
        // would silently reuse the wrong cached pipeline.
        struct SlopeStep { float slope; bool expectGreen; const char* label; };
        const SlopeStep steps[3] = {
            {-8.0f, true, "SlopeScaleDepthBias=-8.0 (A)"},
            {0.0f, false, "SlopeScaleDepthBias=0 (B)"},
            {-8.0f, true, "SlopeScaleDepthBias=-8.0 (A again)"},
        };
        for (const SlopeStep& step : steps)
        {
            RasterizerState rasterizerStep;
            rasterizerStep.setSlopeScaleDepthBiasProperty(step.slope);

            Color result(0, 0, 0, 0);
            for (int attempt = 0; attempt < 20; ++attempt)
            {
                device.Clear(Color(0, 0, 0, 255));
                device.setBlendStateProperty(BlendState::Opaque);
                effect.Apply();
                DrawPair(device, cx[3], true, rasterizerStep);
                result = ReadAt(device, cx[3]);
                if (!IsBlack(result))
                    break;
            }

            std::snprintf(buf, sizeof(buf), "%s: (%d,%d,%d) expected %s", step.label,
                          result.getRProperty(), result.getGProperty(), result.getBProperty(),
                          step.expectGreen ? "GREEN" : "RED");
            Check(step.expectGreen ? IsGreen(result) : IsRed(result), buf);
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentDepthBiasTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(kWidth);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(kHeight);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentDepthBiasTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const bool noDevice = message.find("no device type could be created") != std::string::npos ||
                              message.find("unsupported SDL video driver") != std::string::npos ||
                              message.find("SDL_Init") != std::string::npos ||
                              message.find("live SDL window") != std::string::npos;
        if (noDevice)
        {
            std::printf("[SKIP] CNA Diligent smoke: no usable device (%s)\n", error.what());
            return 77;
        }
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        return 1;
    }
}
