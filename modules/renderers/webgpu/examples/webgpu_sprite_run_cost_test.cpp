// SPDX-License-Identifier: MS-PL
// Consecutive stock sprites with identical native state share one draw over their contiguous
// vertex range. Texture, sampler and ordered-stream boundaries remain observable run barriers.

#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using CNA::Internal::Renderers::WebGPU::WebGPURenderer;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSprites = 8;
    constexpr int kCell = 8;
    constexpr int kWidth = kSprites * kCell;
    constexpr int kHeight = kCell;

    bool SameRgb(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() &&
               a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty();
    }
}

class WebGpuSpriteRunCostTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sprites_;
    std::unique_ptr<RenderTarget2D> target_;
    Texture2D red_;
    Texture2D blue_;
    int passed_ = 0;
    int result_ = 1;
    bool done_ = false;

    WebGPURenderer& Renderer()
    {
        return static_cast<WebGPURenderer&>(getGraphicsDeviceProperty().GetRenderer());
    }

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (condition) ++passed_;
    }

    std::size_t DrawSprites(SpriteSortMode mode, bool alternateTextures,
                            const SamplerState* sampler = &SamplerState::PointClamp)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(target_.get());
        device.Clear(Color::Black);
        const std::size_t before = Renderer().GetNativeDrawIssueCountEXT();
        sprites_->Begin(mode, BlendState::Opaque, sampler, nullptr, nullptr);
        for (int i = 0; i < kSprites; ++i)
        {
            Texture2D& texture = alternateTextures && (i % 2 != 0) ? blue_ : red_;
            sprites_->Draw(texture, Rectangle(i * kCell, 0, kCell, kCell), Color::White);
        }
        sprites_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return Renderer().GetNativeDrawIssueCountEXT() - before;
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        sprites_ = std::make_unique<SpriteBatch>(device);
        target_ = std::make_unique<RenderTarget2D>(device, kWidth, kHeight, false,
                                                   SurfaceFormat::Color, DepthFormat::None);
        red_ = Texture2D::CreateFromPixels(device, 1, 1,
                                           std::vector<std::uint8_t>{255, 0, 0, 255});
        blue_ = Texture2D::CreateFromPixels(device, 1, 1,
                                            std::vector<std::uint8_t>{0, 0, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        Check(DrawSprites(SpriteSortMode::Deferred, false) == 1,
              "eight consecutive same-state sprites issue one native draw");
        Check(DrawSprites(SpriteSortMode::Deferred, true) == kSprites,
              "alternating textures remain eight ordered runs in Deferred mode");
        Check(DrawSprites(SpriteSortMode::Texture, true) == 2,
              "Texture sort collapses the alternating input to two native runs");

        SamplerState linear = SamplerState::LinearClamp;
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(target_.get());
        device.Clear(Color::Black);
        const std::size_t beforeSamplerBoundary = Renderer().GetNativeDrawIssueCountEXT();
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                        &SamplerState::PointClamp, nullptr, nullptr);
        for (int i = 0; i < kSprites / 2; ++i)
            sprites_->Draw(red_, Rectangle(i * kCell, 0, kCell, kCell), Color::White);
        sprites_->End();
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &linear, nullptr, nullptr);
        for (int i = kSprites / 2; i < kSprites; ++i)
            sprites_->Draw(red_, Rectangle(i * kCell, 0, kCell, kCell), Color::White);
        sprites_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(Renderer().GetNativeDrawIssueCountEXT() - beforeSamplerBoundary == 2,
              "a sampler change remains a native run boundary");

        DrawSprites(SpriteSortMode::Texture, true);
        std::vector<Color> pixels(static_cast<std::size_t>(kWidth) * kHeight);
        target_->GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        bool pixelsCorrect = true;
        for (int i = 0; i < kSprites; ++i)
        {
            const Color got = pixels[static_cast<std::size_t>(kHeight / 2) * kWidth +
                                     i * kCell + kCell / 2];
            const Color want = i % 2 == 0 ? Color::Red : Color::Blue;
            pixelsCorrect = pixelsCorrect && SameRgb(got, want);
        }
        Check(pixelsCorrect, "coalesced texture-sorted runs retain every sprite's pixels");

        std::printf("=== %d/5 PASS ===\n", passed_);
        result_ = passed_ == 5 ? 0 : 1;
        Exit();
    }

public:
    WebGpuSpriteRunCostTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kWidth);
        gdm_->setPreferredBackBufferHeightProperty(kHeight);
    }

    [[nodiscard]] int Result() const noexcept { return result_; }
};

int main()
{
    WebGpuSpriteRunCostTest game;
    game.Run();
    return game.Result();
}
