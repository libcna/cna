// SPDX-License-Identifier: MS-PL
//
// SOFTWARE-252: SpriteSortMode::Immediate submits each sprite from Draw(), not from End().
// Switching render targets between those calls is an exact public discriminator: the sprite must
// remain in the target that was active at Draw time. A renderer-private queue that drains at End
// instead moves the sprite into the later target even though the shared SpriteBatch front end made
// the correct immediate renderer call.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;
    const Color kFirstClear(17, 31, 47, 255);
    const Color kSecondClear(71, 53, 29, 255);

    int Count(const std::vector<Color>& pixels, const Color& wanted)
    {
        int count = 0;
        for (const Color& pixel : pixels)
            if (pixel == wanted) ++count;
        return count;
    }
}

class SpriteBatchImmediateSubmissionContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<RenderTarget2D> first_;
    std::unique_ptr<RenderTarget2D> second_;
    bool done_ = false;
    bool passed_ = false;

    std::vector<Color> Read(RenderTarget2D& target)
    {
        std::vector<Color> pixels(kSize * kSize);
        target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        texture_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color texel = Color::Red;
        texture_->SetData(&texel, 1);
        first_ = std::make_unique<RenderTarget2D>(
            device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        second_ = std::make_unique<RenderTarget2D>(
            device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(first_.get());
        device.Clear(kFirstClear);
        device.SetRenderTarget(second_.get());
        device.Clear(kSecondClear);

        device.SetRenderTarget(first_.get());
        SpriteBatch batch(device);
        const SamplerState sampler = SamplerState::PointClamp;
        batch.Begin(SpriteSortMode::Immediate, BlendState::Opaque, &sampler,
                    &DepthStencilState::None, &RasterizerState::CullCounterClockwise);
        batch.Draw(*texture_, Rectangle(3, 3, 1, 1), Color::White);
        device.SetRenderTarget(second_.get());
        batch.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const std::vector<Color> firstPixels = Read(*first_);
        const std::vector<Color> secondPixels = Read(*second_);
        const int firstRed = Count(firstPixels, Color::Red);
        const int firstClear = Count(firstPixels, kFirstClear);
        const int secondRed = Count(secondPixels, Color::Red);
        const int secondClear = Count(secondPixels, kSecondClear);
        passed_ = firstRed == 1 && firstClear == kSize * kSize - 1 &&
                  secondRed == 0 && secondClear == kSize * kSize;

        std::printf(
            "[%s] Immediate Draw submits before a render-target switch "
            "(first red=%d clear=%d; second red=%d clear=%d)\n",
            passed_ ? "PASS" : "FAIL", firstRed, firstClear, secondRed, secondClear);
        Exit();
    }

public:
    SpriteBatchImmediateSubmissionContractTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int Result() const { return passed_ ? 0 : 1; }
};

int main()
{
    SpriteBatchImmediateSubmissionContractTest game;
    game.Run();
    return game.Result();
}
