// SPDX-License-Identifier: MS-PL
// Pixel contract for SKIA-46: each SpriteBatch::Begin applies its own sampler without state leaks.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
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
    [[nodiscard]] bool IsPure(Color color)
    {
        const bool red = color.getRProperty() >= 235 && color.getGProperty() <= 20;
        const bool green = color.getGProperty() >= 235 && color.getRProperty() <= 20;
        return red || green;
    }

    [[nodiscard]] bool IsBlended(Color color)
    {
        return color.getRProperty() >= 90 && color.getRProperty() <= 165
            && color.getGProperty() >= 90 && color.getGProperty() <= 165;
    }
}

class SkiaSpriteBatchSamplerTransitionTest final : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        const std::vector<std::uint8_t> pixels {
            255, 0, 0, 255,
            0, 255, 0, 255,
        };
        texture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 2, 1, pixels));
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color(0, 0, 0, 255));
        DrawBlock(0, const_cast<SamplerState*>(&SamplerState::PointClamp));
        DrawBlock(64, const_cast<SamplerState*>(&SamplerState::LinearClamp));
        DrawBlock(128, const_cast<SamplerState*>(&SamplerState::PointClamp));

        const Color firstPoint = Read(32);
        const Color linear = Read(96);
        const Color secondPoint = Read(160);
        Check(IsPure(firstPoint), "first PointClamp block selects a pure texel", firstPoint);
        Check(IsBlended(linear), "middle LinearClamp block interpolates", linear);
        Check(IsPure(secondPoint), "second PointClamp block restores point sampling", secondPoint);
        Exit();
    }

public:
    SkiaSpriteBatchSamplerTransitionTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(192);
        graphics_->setPreferredBackBufferHeightProperty(8);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

private:
    void DrawBlock(int x, SamplerState* sampler)
    {
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, sampler, nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        spriteBatch_->Draw(*texture_, Rectangle(x, 0, 64, 8), Rectangle(0, 0, 2, 1), Color::White);
        spriteBatch_->End();
    }

    Color Read(int x)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle sample(x, 4, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&sample, &pixel, 0, 1);
        return pixel;
    }

    void Check(bool pass, const char* label, Color color)
    {
        std::printf("[%s] %s: got=(%d,%d,%d)\n", pass ? "PASS" : "FAIL", label,
                    color.getRProperty(), color.getGProperty(), color.getBProperty());
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaSpriteBatchSamplerTransitionTest game;
    game.Run();
    return game.Result();
}
