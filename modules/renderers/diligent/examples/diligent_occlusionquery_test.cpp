// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-41: real-device proof that OcclusionQuery on the Diligent renderer
// reports a genuine GPU visible-sample result, through the public XNA API only.
//
// The checks below only ever assert "zero" vs. "greater than zero", deliberately, rather than an
// exact expected count: DiligentOcclusionQueryRenderer transparently falls back from
// QUERY_TYPE_OCCLUSION (an exact count) to QUERY_TYPE_BINARY_OCCLUSION (0 or 1) on a device without
// the `occlusionQueryPrecise` feature -- which includes Mesa's `lavapipe` software rasterizer, the
// only device this test actually runs against in this sandbox -- so a check that expected a
// specific fraction of the viewport's pixel count would be renderer-correct but environment-flaky.
//
// Check A -- a query that was constructed but never Begin()/End()'d is not complete and reports
//   zero pixels: the baseline state before any GPU work has happened.
// Check B -- a Begin()/End() span with NO draws inside it reports zero visible samples: proves the
//   query measures real rendered work rather than always reporting "something".
// Check C -- a quad covering the whole viewport with the depth test off reports a NON-zero visible
//   count, in contrast with check B's empty span.
// Check D -- the actual point of an occlusion query: a far quad fully hidden behind a nearer,
//   depth-tested quad reports zero visible samples, exactly like check B's empty span but this
//   time real geometry was submitted and the depth test rejected every one of its fragments. A
//   renderer that counted submitted fragments regardless of the depth test would pass B/C but fail
//   this one.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <thread>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kChecks = 4;

    BasicEffect& SharedEffect(GraphicsDevice& device)
    {
        static BasicEffect* effect = nullptr;
        if (effect == nullptr)
            effect = new BasicEffect(device);
        effect->setWorldProperty(Matrix::getIdentityProperty());
        effect->setViewProperty(Matrix::getIdentityProperty());
        effect->setProjectionProperty(Matrix::getIdentityProperty());
        return *effect;
    }

    void DrawQuad(GraphicsDevice& device, float left, float right, float top, float bottom, float z,
                  const Color& color)
    {
        const VertexPositionColor vertices[6] = {
            {Vector3(left, top, z), color},     {Vector3(left, bottom, z), color},
            {Vector3(right, bottom, z), color}, {Vector3(left, top, z), color},
            {Vector3(right, bottom, z), color}, {Vector3(right, top, z), color},
        };
        BasicEffect& effect = SharedEffect(device);
        effect.VertexColorEnabled = true;
        effect.setTextureEnabledProperty(false);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    /// Polls getIsCompleteProperty() against a wall-clock deadline rather than an iteration count.
    /// The query is a genuinely asynchronous GPU object -- even though
    /// DiligentOcclusionQueryRenderer::End() flushes the immediate context so the result doesn't
    /// require waiting for a frame boundary, a real (if software) GPU still takes real time,
    /// especially the first draw of a given pipeline state, which also has to compile it. A tight
    /// CPU spin loop bounded only by iteration count can burn through thousands of iterations
    /// before the GPU is anywhere close to done.
    bool WaitForQuery(OcclusionQuery& query)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (query.getIsCompleteProperty())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
}

class DiligentOcclusionQueryTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(Color::Black);

        {
            OcclusionQuery query(device);
            Check(!query.getIsCompleteProperty() && query.getPixelCountProperty() == 0,
                  "a fresh query that was never Begin()/End()'d is not complete and reports 0 pixels");
        }

        {
            OcclusionQuery query(device);
            query.Begin();
            query.End();
            const bool complete = WaitForQuery(query);
            const int pixels = query.getPixelCountProperty();
            std::printf("    (empty Begin/End span, no draws -> %d pixels)\n", pixels);
            Check(complete && pixels == 0,
                  "a Begin()/End() span with no draws inside it reports zero visible samples");
        }

        {
            OcclusionQuery query(device);
            query.Begin();
            DrawQuad(device, -1.0f, 1.0f, 1.0f, -1.0f, 0.5f, Color::Red);
            query.End();
            const bool complete = WaitForQuery(query);
            const int pixels = query.getPixelCountProperty();
            std::printf("    (full-viewport quad -> %d pixels)\n", pixels);
            Check(complete && pixels > 0,
                  "a quad covering the whole viewport reports a non-zero visible-sample result");
        }

        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
        DrawQuad(device, -1.0f, 1.0f, 1.0f, -1.0f, 0.1f, Color::Lime);
        {
            OcclusionQuery query(device);
            query.Begin();
            DrawQuad(device, -1.0f, 1.0f, 1.0f, -1.0f, 0.9f, Color::Red);
            query.End();
            const bool complete = WaitForQuery(query);
            const int pixels = query.getPixelCountProperty();
            std::printf("    (fully depth-occluded quad -> %d pixels, expected 0)\n", pixels);
            Check(complete && pixels == 0,
                  "a quad fully hidden behind a nearer depth-tested quad reports zero visible "
                  "samples, not just \"something was drawn\"");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentOcclusionQueryTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(kSize);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(kSize);
        graphicsDeviceManager_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentOcclusionQueryTest game;
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
