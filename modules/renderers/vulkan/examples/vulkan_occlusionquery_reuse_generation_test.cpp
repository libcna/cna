// SPDX-License-Identifier: MS-PL
// MSR-026: a reused Vulkan query pool must publish only the generation just submitted.
//
// The renderer resets its one-slot pool inside the next GPU command buffer. Before that queued
// reset executes, vkGetQueryPoolResults can still report the preceding generation as available.
// Alternate full- and half-frame query rectangles so stale availability returns the wrong exact
// count, and call PixelCount immediately after IsComplete to catch availability that changes
// between the two public property reads. This is the frame-paced reuse pattern from LensFlare.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <exception>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kCycles = 32;
    constexpr int kMaxPollFrames = 600;

    void DrawRectangle(GraphicsDevice& device, float left, float right)
    {
        const Color green(0, 255, 0, 255);
        const VertexPositionColor vertices[6] = {
            {Vector3(left, 1.0f, 0.5f), green},
            {Vector3(left, -1.0f, 0.5f), green},
            {Vector3(right, -1.0f, 0.5f), green},
            {Vector3(left, 1.0f, 0.5f), green},
            {Vector3(right, -1.0f, 0.5f), green},
            {Vector3(right, 1.0f, 0.5f), green},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }
}

class VulkanOcclusionQueryReuseGenerationTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<BasicEffect> effect_;
    std::unique_ptr<OcclusionQuery> query_;
    int cycle_ = 0;
    int pollFrames_ = 0;
    int result_ = 1;
    bool waiting_ = false;
    bool precise_ = true;

    void QueueGeneration(GraphicsDevice& device)
    {
        const bool full = (cycle_ % 2) == 0;
        effect_->Apply();
        query_->Begin();
        // A precise renderer alternates two different exact positive counts. A renderer that can
        // only promise any-samples semantics alternates visible and fully clipped work instead,
        // retaining a generation discriminator without demanding unsupported precision.
        if (precise_)
            DrawRectangle(device, full ? -1.0f : 0.0f, 1.0f);
        else
            DrawRectangle(device, full ? -1.0f : 2.0f, full ? 1.0f : 3.0f);
        query_->End();
        waiting_ = true;
        pollFrames_ = 0;
    }

    void Fail(const char* reason, int count = -1)
    {
        std::fprintf(stderr, "[FAIL] cycle=%d %s count=%d\n", cycle_, reason, count);
        result_ = 1;
        Exit();
    }

protected:
    void LoadContent() override
    {
        GraphicsDevice& device = getGraphicsDeviceProperty();
        effect_ = std::make_unique<BasicEffect>(device);
        effect_->VertexColorEnabled = true;
        query_ = std::make_unique<OcclusionQuery>(device);
        precise_ = query_->isPixelCountPreciseEXT();
    }

    void Draw(const GameTime&) override
    {
        GraphicsDevice& device = getGraphicsDeviceProperty();
        device.Clear(Color(0, 0, 0, 255));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        if (!waiting_)
        {
            QueueGeneration(device);
            return;
        }

        if (!query_->getIsCompleteProperty())
        {
            if (++pollFrames_ > kMaxPollFrames)
                Fail("query did not complete");
            return;
        }

        int count = -1;
        try
        {
            count = query_->getPixelCountProperty();
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "[FAIL] cycle=%d PixelCount after IsComplete threw: %s\n",
                         cycle_, e.what());
            result_ = 1;
            Exit();
            return;
        }

        const bool full = (cycle_ % 2) == 0;
        const int expected = full ? kSize * kSize : kSize * (kSize / 2);
        const bool correct = precise_ ? count == expected : (full ? count > 0 : count == 0);
        if (!correct)
        {
            Fail("result came from the wrong query generation", count);
            return;
        }

        ++cycle_;
        if (cycle_ == kCycles)
        {
            std::printf("[PASS] %d alternating query generations completed with stable results\n",
                        kCycles);
            result_ = 0;
            Exit();
            return;
        }

        waiting_ = false;
        QueueGeneration(device);
    }

public:
    VulkanOcclusionQueryReuseGenerationTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferredBackBufferWidthProperty(kSize);
        graphics_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int getResult() const noexcept { return result_; }
};

int main()
{
    VulkanOcclusionQueryReuseGenerationTest game;
    game.Run();
    return game.getResult();
}
