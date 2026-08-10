// SPDX-License-Identifier: MS-PL
// Pixel contract for SKIA-37: Linear filtering must not bleed texels outside a source rectangle.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBlack(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);

    [[nodiscard]] bool IsRed(Color color)
    {
        return color.getRProperty() >= 240 && color.getGProperty() <= 15 && color.getBProperty() <= 15;
    }
}

class SkiaSpriteBatchSourceRectLinearTest final : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        texture_ = std::make_unique<Texture2D>(device, 3, 3);

        // The source rectangle selects only the central red texel. Every adjacent texel is a
        // different non-red colour, so any filtering outside that rectangle is visible at one
        // of the four magnified destination edges.
        const Color pixels[] {
            Color(0, 255, 0, 255), Color(0, 0, 255, 255), Color(255, 255, 0, 255),
            Color(0, 255, 255, 255), kRed,                    Color(255, 0, 255, 255),
            Color(255, 128, 0, 255), Color(128, 0, 255, 255), Color(0, 255, 128, 255),
        };
        texture_->SetData(pixels, 9);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.Clear(kBlack);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::LinearClamp), nullptr, nullptr);
        spriteBatch_->Draw(*texture_, Rectangle(8, 4, 32, 24), Rectangle(1, 1, 1, 1), Color::White);
        spriteBatch_->End();

        CheckRed(8, 16, "left source edge remains red under LinearClamp");
        CheckRed(39, 16, "right source edge remains red under LinearClamp");
        CheckRed(24, 4, "top source edge remains red under LinearClamp");
        CheckRed(24, 27, "bottom source edge remains red under LinearClamp");
        CheckRed(8, 4, "top-left source corner remains red under LinearClamp");
        CheckRed(39, 27, "bottom-right source corner remains red under LinearClamp");
        CheckColor(7, 16, kBlack, "destination geometry does not cover the left exterior pixel");
        CheckColor(40, 16, kBlack, "destination geometry does not cover the right exterior pixel");
        Exit();
    }

public:
    SkiaSpriteBatchSourceRectLinearTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(48);
        graphics_->setPreferredBackBufferHeightProperty(32);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

private:
    void CheckRed(int x, int y, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&region, &actual, 0, 1);
        const bool pass = IsRed(actual);
        std::printf("[%s] %s: got=(%d,%d,%d)\n", pass ? "PASS" : "FAIL", label,
                    actual.getRProperty(), actual.getGProperty(), actual.getBProperty());
        if (!pass)
            ++failures_;
    }

    void CheckColor(int x, int y, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&region, &actual, 0, 1);
        const bool pass = actual == expected;
        std::printf("[%s] %s: got=(%d,%d,%d)\n", pass ? "PASS" : "FAIL", label,
                    actual.getRProperty(), actual.getGProperty(), actual.getBProperty());
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
    SkiaSpriteBatchSourceRectLinearTest game;
    game.Run();
    return game.Result();
}
