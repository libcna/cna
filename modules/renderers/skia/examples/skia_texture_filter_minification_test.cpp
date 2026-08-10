// SPDX-License-Identifier: MS-PL
// Pixel contract for SKIA-43: Point and Linear remain distinct while a sprite is minified.

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

class SkiaTextureFilterMinificationTest final : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        const std::vector<std::uint8_t> pixels {
            255, 0, 0, 255,  // red
            0, 255, 0, 255,  // green
        };
        texture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 2, 1, pixels));
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        const Color point = DrawAndRead(const_cast<SamplerState*>(&SamplerState::PointClamp));
        const Color linear = DrawAndRead(const_cast<SamplerState*>(&SamplerState::LinearClamp));
        Check(IsPure(point), "PointClamp minification selects one stored texel", point);
        Check(IsBlended(linear), "LinearClamp minification interpolates source texels", linear);
        Exit();
    }

public:
    SkiaTextureFilterMinificationTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(4);
        graphics_->setPreferredBackBufferHeightProperty(4);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

private:
    Color DrawAndRead(SamplerState* sampler)
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color(0, 0, 0, 255));
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, sampler, nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        // A two-texel source into one destination pixel is an unambiguous minification case.
        spriteBatch_->Draw(*texture_, Rectangle(1, 0, 1, 4), Rectangle(0, 0, 2, 1), Color::White);
        spriteBatch_->End();

        Color pixel(0, 0, 0, 0);
        const Rectangle sample(1, 2, 1, 1);
        device.GetBackBufferData(&sample, &pixel, 0, 1);
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
    SkiaTextureFilterMinificationTest game;
    game.Run();
    return game.Result();
}
