// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-24: real occlusion queries for the OpenGL4 graphics renderer --
// OpenGL4OcclusionQueryRenderer wraps a genuine GL 1.5 core query object using GL_SAMPLES_PASSED
// (an exact passed-sample count), unlike EasyGLOcclusionQueryRenderer's GLES3
// GL_ANY_SAMPLES_PASSED (0/1-only) query -- matching real XNA's own desktop OcclusionQuery
// semantics (see IOcclusionQueryRenderer's own doc comment contrasting the two). Ported test
// structure/scenarios from vulkan_occlusionquery_pixelcount_test.cpp (Tasks 447/854), the closest
// other desktop/exact-count renderer with an already cross-verified oracle.
//
// Scenario A (visible) -- a full 64x64 quad with nothing hiding it: PixelCount() should be
//   positive once IsComplete() is true (exact count would be 4096 on an ideal rasterizer; a
//   generous >0 check is used since a software rasterizer's exact edge-pixel coverage can vary).
// Scenario B (occluded) -- a nearer opaque occluder quad drawn first (no query attached), then
//   the target quad wrapped in Begin()/End()): the real depth test should reject every one of the
//   target's fragments, so PixelCount() should read exactly 0.
// Scenario C (multi-draw span) -- two non-overlapping half-quads drawn between a SINGLE
//   Begin()/End() pair: a correct implementation must sum both draws' contributions, so
//   PixelCount() should equal the SAME total (64*64) as a single full-quad draw would -- a
//   discriminating check against a bug that only counted the last draw in the query's span.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kMaxPollFrames = 30;

    const Color kBlack(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);

    bool ColourMatch(Color got, Color want, int tol = 40)
    {
        return std::abs(static_cast<int>(got.getRProperty()) - static_cast<int>(want.getRProperty())) <= tol
            && std::abs(static_cast<int>(got.getGProperty()) - static_cast<int>(want.getGProperty())) <= tol
            && std::abs(static_cast<int>(got.getBProperty()) - static_cast<int>(want.getBProperty())) <= tol;
    }

    void DrawQuad(GraphicsDevice& dev, float z, const Color& colour)
    {
        const Vector3 tl(-1.0f, 1.0f, z), bl(-1.0f, -1.0f, z);
        const Vector3 br(1.0f, -1.0f, z), tr(1.0f, 1.0f, z);
        const VertexPositionColor q[6] = {
            {tl, colour}, {bl, colour}, {br, colour},
            {tl, colour}, {br, colour}, {tr, colour},
        };
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }

    void DrawHalfQuad(GraphicsDevice& dev, float xMin, float xMax, float z, const Color& colour)
    {
        const Vector3 tl(xMin, 1.0f, z), bl(xMin, -1.0f, z);
        const Vector3 br(xMax, -1.0f, z), tr(xMax, 1.0f, z);
        const VertexPositionColor q[6] = {
            {tl, colour}, {bl, colour}, {br, colour},
            {tl, colour}, {br, colour}, {tr, colour},
        };
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }
}

class OpenGL4OcclusionQueryTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int result_ = 1;

    struct ScenarioResult { Color centre; bool complete; int pixelCount; };

    ScenarioResult RunScenario(GraphicsDevice& dev, bool occluded)
    {
        std::unique_ptr<OcclusionQuery> query;
        int frame = 0;
        bool complete = false;
        int pixelCount = -12345;
        Color centre(0, 0, 0, 0);

        while (true)
        {
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            DepthStencilState ds;
            ds.setDepthBufferEnableProperty(true);
            ds.setDepthBufferWriteEnableProperty(true);
            dev.setDepthStencilStateProperty(ds);
            fx.Apply();

            if (frame == 0)
            {
                query = std::make_unique<OcclusionQuery>(dev);
                if (occluded)
                    DrawQuad(dev, 0.1f, kBlue); // nearer opaque occluder, no query attached
                query->Begin();
                DrawQuad(dev, 0.9f, kRed); // target quad -- farther away
                query->End();
                ++frame;
                continue;
            }

            if (occluded) DrawQuad(dev, 0.1f, kBlue);
            DrawQuad(dev, 0.9f, kRed);

            const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
            dev.GetBackBufferData(&reg, &centre, 0, 1);

            complete = query->getIsCompleteProperty();
            pixelCount = query->getPixelCountProperty();

            if (complete || frame >= kMaxPollFrames)
                break;
            ++frame;
        }

        return {centre, complete, pixelCount};
    }

    ScenarioResult RunMultiDrawScenario(GraphicsDevice& dev)
    {
        std::unique_ptr<OcclusionQuery> query;
        int frame = 0;
        bool complete = false;
        int pixelCount = -12345;
        Color centre(0, 0, 0, 0);

        while (true)
        {
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            DepthStencilState ds;
            ds.setDepthBufferEnableProperty(true);
            ds.setDepthBufferWriteEnableProperty(true);
            dev.setDepthStencilStateProperty(ds);
            fx.Apply();

            if (frame == 0)
            {
                query = std::make_unique<OcclusionQuery>(dev);
                query->Begin();
                DrawHalfQuad(dev, -1.0f, 0.0f, 0.9f, kRed);
                DrawHalfQuad(dev, 0.0f, 1.0f, 0.9f, kRed);
                query->End();
                ++frame;
                continue;
            }

            DrawHalfQuad(dev, -1.0f, 0.0f, 0.9f, kRed);
            DrawHalfQuad(dev, 0.0f, 1.0f, 0.9f, kRed);

            const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
            dev.GetBackBufferData(&reg, &centre, 0, 1);

            complete = query->getIsCompleteProperty();
            pixelCount = query->getPixelCountProperty();

            if (complete || frame >= kMaxPollFrames)
                break;
            ++frame;
        }

        return {centre, complete, pixelCount};
    }

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        result_ = 0;

        auto& dev = getGraphicsDeviceProperty();

        const ScenarioResult visible = RunScenario(dev, /*occluded=*/false);
        Check(ColourMatch(visible.centre, kRed), "Check A1: fully visible quad renders Red at centre");
        Check(visible.complete && visible.pixelCount > 0,
              "Check A2: visible quad -- OcclusionQuery becomes complete with PixelCount() > 0");

        const ScenarioResult occluded = RunScenario(dev, /*occluded=*/true);
        Check(ColourMatch(occluded.centre, kBlue),
              "Check B1: occluder's own colour still shows -- target quad genuinely hidden behind it");
        Check(occluded.complete && occluded.pixelCount == 0,
              "Check B2: occluded quad -- OcclusionQuery becomes complete with PixelCount() == 0");

        const ScenarioResult multi = RunMultiDrawScenario(dev);
        Check(ColourMatch(multi.centre, kRed), "Check C1: multi-draw scenario -- both half-quads visible at centre");
        // 2 non-overlapping 32x64 halves = 4096 total, same as one full 64x64 quad -- a
        // discriminating check: if only the LAST draw in the query's span were counted (a
        // multi-draw-span bug), this would read ~2048 instead.
        Check(multi.complete && multi.pixelCount == kSize * kSize,
              "Check C2: multi-draw scenario -- PixelCount() sums BOTH draws within one Begin()/End() span");

        Exit();
    }

public:
    OpenGL4OcclusionQueryTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    OpenGL4OcclusionQueryTest game;
    game.Run();
    return game.getResult();
}
