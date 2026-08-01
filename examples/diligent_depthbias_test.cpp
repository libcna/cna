// SPDX-License-Identifier: MS-PL
//
// plan_diligent.md DILIGENT-49: real-device proof that RasterizerState.DepthBias and
// SlopeScaleDepthBias visibly change a depth-test outcome through the Diligent backend, not just
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
// DiligentGraphicsBackend::ApplyRasterizerState() packs DepthBias/SlopeScaleDepthBias into a single
// signed byte each (PipelineKey::raster bits 16-23 / 24-31, DiligentGraphicsBackend.cpp
// ApplyRasterizerState()/GetOrCreatePipeline()): DepthBias round-trips as
// lround(depthBias * 1000) clamped to an int8_t, decoded directly into Dg::RasterizerStateDesc's
// raw Int32 DepthBias units; SlopeScaleDepthBias round-trips as lround(value * 16) clamped the same
// way, decoded back by dividing by 16. That packing's representable range is only
// [-128, 127] / scale, i.e. DepthBias in [-0.128, 0.127] and SlopeScaleDepthBias in [-8.0, 7.9375]
// -- unlike the native Vulkan backend's own depth-bias test (Task 328), which drives
// vkCmdSetDepthBias directly and can use an arbitrarily large factor. This test therefore uses the
// most extreme values this packing can exactly represent (-0.128 and -8.0, both round-tripping to
// the packing's minimum byte, -128) rather than an arbitrarily large input, since anything larger
// would silently alias to a different byte instead of clamping.
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
// If this backend's narrow 8-bit packing turns out too coarse to produce a visible effect on the
// software (lavapipe) device under test -- the same open-gap shape as D3D9's own still-unresolved
// D9-62 -- that is itself the recorded finding; this test does not silently report PASS for that.
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
    constexpr int kChecks = 4;

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
