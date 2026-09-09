// SPDX-License-Identifier: MS-PL
//
// SOFTWARE-251: classic XNA SpriteBatch submissions are chunked rather than allowing the
// 16-bit quad index pattern to wrap. Microsoft XNA and FNA both use a 2,048-sprite native batch.
// The 16,385th same-texture sprite is the first one whose four-vertex base overflows UInt16 in an
// unbounded implementation, so this test leaves every earlier sprite offscreen and makes that
// exact sprite the only observable output.

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
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kTargetWidth = 8;
    constexpr int kTargetHeight = 8;
    constexpr int kUInt16OverflowSprite = 16385;
    const Color kClear(11, 23, 37, 255);
}

class SpriteBatchLargeBatchContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<RenderTarget2D> target_;
    bool done_ = false;
    bool passed_ = false;

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        texture_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color texel = Color::Red;
        texture_->SetData(&texel, 1);

        target_ = std::make_unique<RenderTarget2D>(
            device, kTargetWidth, kTargetHeight, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(target_.get());
        device.setViewportProperty(Viewport(0, 0, kTargetWidth, kTargetHeight));
        device.Clear(kClear);

        SpriteBatch batch(device);
        const SamplerState sampler = SamplerState::PointClamp;
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler,
                    &DepthStencilState::None, &RasterizerState::CullCounterClockwise);
        for (int sprite = 1; sprite < kUInt16OverflowSprite; ++sprite)
        {
            batch.Draw(*texture_, Rectangle(32, 32, 1, 1), Color::White);
        }
        batch.Draw(*texture_, Rectangle(3, 3, 1, 1), Color::White);
        batch.End();

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> pixels(kTargetWidth * kTargetHeight);
        target_->GetData(pixels.data(), 0, static_cast<int>(pixels.size()));

        int redPixels = 0;
        int changedPixels = 0;
        for (const Color& pixel : pixels)
        {
            if (pixel == Color::Red) ++redPixels;
            if (pixel != kClear) ++changedPixels;
        }
        const Color observed = pixels[3 * kTargetWidth + 3];
        passed_ = observed == Color::Red && redPixels == 1 && changedPixels == 1;
        std::printf(
            "[%s] sprite 16,385 survives UInt16 batch boundaries "
            "(observed=%u,%u,%u,%u red=%d changed=%d)\n",
            passed_ ? "PASS" : "FAIL",
            static_cast<unsigned>(observed.getRProperty()),
            static_cast<unsigned>(observed.getGProperty()),
            static_cast<unsigned>(observed.getBProperty()),
            static_cast<unsigned>(observed.getAProperty()), redPixels, changedPixels);
        Exit();
    }

public:
    SpriteBatchLargeBatchContractTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int Result() const { return passed_ ? 0 : 1; }
};

int main()
{
    SpriteBatchLargeBatchContractTest game;
    game.Run();
    return game.Result();
}
