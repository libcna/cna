// SPDX-License-Identifier: MS-PL
// OcclusionQuery.PixelCount is XNA's Direct3D 9 tally of the pixels that passed the depth test:
// an exact count for a partly occluded draw, not a did-anything-pass flag. Moved from the retired
// VULKAN suite (VULKAN-370, finding F-14).
//
// An occluder covers the left half of a 64x64 frame at a nearer depth; the queried quad covers the
// whole frame at a farther one, so under CompareFunction.Less exactly the right half survives:
// 64 x 32 = 2048. The lensflare idiom -- PixelCount divided by an area -- needs that number.
//
//   A  the query completes (polled across ordinary Game frames, with a deadline);
//   B  when the renderer reports the count as precise (CNA's isPixelCountPreciseEXT), PixelCount is
//      exactly 2048; a renderer that says it is not precise is held only to "some pixels passed";
//   C  a precise report is not a disguised flag (neither 0 nor 1).
//
// Exit code 0 = PASS, 1 = FAIL, 77 = no OcclusionQuery on this renderer.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "CNA/GraphicsCapability.hpp"

#include "common/PixelTestGame.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kExpectedVisible = kSize * (kSize / 2);
    constexpr int kMaxPollFrames = 600;
    constexpr auto kPollDeadline = std::chrono::seconds(5);

    void DrawQuad(GraphicsDevice& dev, float xMin, float xMax, float z, const Color& colour)
    {
        const VertexPositionColor q[6] = {
            {Vector3(xMin, 1.0f, z), colour}, {Vector3(xMin, -1.0f, z), colour},
            {Vector3(xMax, -1.0f, z), colour}, {Vector3(xMin, 1.0f, z), colour},
            {Vector3(xMax, -1.0f, z), colour}, {Vector3(xMax, 1.0f, z), colour},
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }
}

class OcclusionQueryPrecisionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<BasicEffect> effect_;
    std::unique_ptr<OcclusionQuery> query_;
    int result_ = 0;
    bool skipped_ = false;
    bool done_ = false;
    bool submitted_ = false;
    bool precise_ = true;
    int polls_ = 0;
    std::chrono::steady_clock::time_point pollStart_{};

    void Check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        std::fflush(stdout);
        if (!ok) result_ = 1;
    }

    void Finish()
    {
        done_ = true;
        Exit();
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        if (!dev.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery))
        {
            std::printf("[SKIP] GraphicsCapability::OcclusionQuery is false on this renderer\n");
            skipped_ = true;
            return;
        }
        effect_ = std::make_unique<BasicEffect>(dev);
        effect_->setVertexColorEnabledProperty(true);
        query_ = std::make_unique<OcclusionQuery>(dev);
        precise_ = query_->isPixelCountPreciseEXT();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        if (skipped_)
        {
            Finish();
            return;
        }
        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        DepthStencilState less;
        less.setDepthBufferFunctionProperty(CompareFunction::Less);
        dev.setDepthStencilStateProperty(less);
        dev.Clear(Color(0, 0, 0, 255));

        // Queue ONE query once, then let ordinary Game frames submit and poll it.
        if (!submitted_)
        {
            effect_->Apply();
            DrawQuad(dev, -1.0f, 0.0f, 0.25f, Color(255, 0, 0, 255));  // occluder, not queried
            query_->Begin();
            DrawQuad(dev, -1.0f, 1.0f, 0.75f, Color(0, 255, 0, 255));
            query_->End();
            submitted_ = true;
            pollStart_ = std::chrono::steady_clock::now();
            return;
        }

        ++polls_;
        const auto elapsedMs = [this] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - pollStart_)
                .count();
        };
        if (!query_->getIsCompleteProperty())
        {
            if (polls_ < kMaxPollFrames &&
                std::chrono::steady_clock::now() - pollStart_ <= kPollDeadline)
                return;
            Check(false, "A the query completes",
                  "never completed: " + std::to_string(polls_) + " polls over " +
                      std::to_string(elapsedMs()) + " ms");
            Finish();
            return;
        }

        int counted = -1;
        try
        {
            counted = query_->getPixelCountProperty();
        }
        catch (const std::exception& e)
        {
            Check(false, "A the query completes", std::string("PixelCount after IsComplete threw: ") +
                                                      e.what());
            Finish();
            return;
        }
        Check(true, "A the query completes",
              std::string("precise=") + (precise_ ? "true" : "false") + ", PixelCount=" +
                  std::to_string(counted) + ", after " + std::to_string(polls_) + " polls / " +
                  std::to_string(elapsedMs()) + " ms");

        if (precise_)
        {
            Check(counted == kExpectedVisible, "B the count is the exact number of surviving pixels",
                  std::to_string(counted) + ", expected exactly " +
                      std::to_string(kExpectedVisible));
            Check(counted != 1 && counted != 0, "C a precise report is not a disguised flag",
                  std::to_string(counted));
        }
        else
        {
            Check(counted > 0,
                  "B the renderer reports an imprecise count, so only 'some pixels passed' is "
                  "promised",
                  std::to_string(counted));
        }
        Finish();
    }

public:
    OcclusionQueryPrecisionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24);
        gdm_->setPreferMultiSamplingProperty(false);
    }

    int getResult() const
    {
        if (result_ != 0) return 1;
        return skipped_ ? CNA::Examples::kSkipExitCode : 0;
    }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("occlusion_query_precision"))
        return CNA::Examples::Conformance::kSkipExitCode;
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    OcclusionQueryPrecisionTest game;
    game.Run();
    return game.getResult();
}
