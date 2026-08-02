// SPDX-License-Identifier: MS-PL
// SKIA-27 / SKIA-70 / SKIA-126 / SKIA-131: transition policy for complete 2D mip storage.
//
// SkImage::withDefaultMipmaps() is public in the pinned Skia revision, but it returns an immutable
// image snapshot.  Its public API neither exposes per-level raster readback nor defines the
// invalidation/resolve contract CNA needs when a mutable SkSurface target is rebound or uploaded.
// SKIA-126 allocates a CNA-owned Texture2D chain, SKIA-129 samples it, and SKIA-131 gives
// RenderTarget2D the same stable per-level addressing. Automatic target descendant generation is
// deliberately the next transition, SKIA-132.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);

    [[nodiscard]] bool same(Color left, Color right)
    {
        return left.getRProperty() == right.getRProperty()
            && left.getGProperty() == right.getGProperty()
            && left.getBProperty() == right.getBProperty()
            && left.getAProperty() == right.getAProperty();
    }

}

class SkiaMipmapPolicyTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool finished_ = false;
    int failures_ = 0;

    void check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures_;
    }

    void checkPixel(int x, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle pixel(x, 1, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&pixel, &actual, 0, 1);
        check(same(actual, expected), label);
    }

    void checkTargetPixel(RenderTarget2D& target, int x, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle pixel(x, 0, 1, 1);
        target.GetData(0, &pixel, &actual, 0, 1);
        check(same(actual, expected), label);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        Texture2D mippedTexture(device, 4, 4, true, SurfaceFormat::Color);
        check(mippedTexture.getLevelCountProperty() == 3,
              "Texture2D mipMap=true allocates and reports the complete 4x4 chain");
        RenderTarget2D mippedTarget(device, 4, 4, true, SurfaceFormat::Color,
                                    DepthFormat::None, 0,
                                    RenderTargetUsage::PreserveContents);
        check(mippedTarget.getLevelCountProperty() == 3,
              "RenderTarget2D mipMap=true allocates and reports the complete 4x4 chain");

        const std::vector<Color> pixels = {
            kRed, kRed, kBlue, kBlue,
            kRed, kRed, kBlue, kBlue,
            kRed, kRed, kBlue, kBlue,
            kRed, kRed, kBlue, kBlue,
        };
        mippedTexture.SetData(pixels.data(), static_cast<int>(pixels.size()));
        device.Clear(Color(0, 0, 0, 255));
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        spriteBatch_->Draw(mippedTexture, Rectangle(0, 0, 4, 2),
                           Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->End();
        checkPixel(1, kRed, "mipped Texture2D level-zero upload remains sampleable (red half)");
        checkPixel(3, kBlue, "mipped Texture2D level-zero upload remains sampleable (blue half)");

        RenderTarget2D levelZeroTarget(device, 2, 1, false, SurfaceFormat::Color,
                                       DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&levelZeroTarget);
        device.Clear(kRed);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        checkTargetPixel(levelZeroTarget, 0, kRed,
                         "level-0 target remains readable after mip-target construction");

        device.Clear(Color(0, 0, 0, 255));
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        spriteBatch_->Draw(levelZeroTarget, Rectangle(0, 0, 4, 2), Rectangle(0, 0, 2, 1), Color::White);
        spriteBatch_->End();
        checkPixel(1, kRed,
                   "level-0 target remains sampleable after mip-target construction");
        Exit();
    }

public:
    SkiaMipmapPolicyTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(4);
        graphics_->setPreferredBackBufferHeightProperty(2);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    SkiaMipmapPolicyTest game;
    game.Run();
    return game.Result();
}
