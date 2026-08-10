// SPDX-License-Identifier: MS-PL
// Pixel contract for SKIA-34: a negative SpriteBatch scale mirrors about the supplied position.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <cstdio>
#include <memory>
#include <optional>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kBlack(0, 0, 0, 255);

    [[nodiscard]] bool Same(Color left, Color right)
    {
        return left.getRProperty() == right.getRProperty()
            && left.getGProperty() == right.getGProperty()
            && left.getBProperty() == right.getBProperty()
            && left.getAProperty() == right.getAProperty();
    }
}

class SkiaSpriteBatchNegativeScaleTest final : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);

        horizontal_ = std::make_unique<Texture2D>(device, 2, 1);
        const Color horizontalPixels[] {kRed, kBlue};
        horizontal_->SetData(horizontalPixels, 2);

        vertical_ = std::make_unique<Texture2D>(device, 1, 2);
        const Color verticalPixels[] {kRed, kBlue};
        vertical_->SetData(verticalPixels, 2);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.Clear(kBlack);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        // With origin zero, the left/top edge moves away from position when the corresponding
        // scale is negative.  The distinct texels make the implied mirror observable.
        spriteBatch_->Draw(*horizontal_, Vector2(150.0f, 50.0f), std::optional<Rectangle>{}, Color::White,
                           0.0f, Vector2::Zero, Vector2(-50.0f, 50.0f), SpriteEffects::None, 0.0f);
        spriteBatch_->Draw(*vertical_, Vector2(200.0f, 120.0f), std::optional<Rectangle>{}, Color::White,
                           0.0f, Vector2::Zero, Vector2(50.0f, -50.0f), SpriteEffects::None, 0.0f);
        spriteBatch_->End();

        CheckPixel(75, 75, kBlue, "negative X scale mirrors the left half to Blue");
        CheckPixel(125, 75, kRed, "negative X scale mirrors the right half to Red");
        CheckPixel(225, 45, kBlue, "negative Y scale mirrors the top half to Blue");
        CheckPixel(225, 95, kRed, "negative Y scale mirrors the bottom half to Red");
        CheckPixel(20, 20, kBlack, "negative scale creates no coverage outside the reflected sprites");
        Exit();
    }

public:
    SkiaSpriteBatchNegativeScaleTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(240);
        graphics_->setPreferredBackBufferHeightProperty(140);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

private:
    void CheckPixel(int x, int y, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&region, &actual, 0, 1);
        const bool pass = Same(actual, expected);
        std::printf("[%s] %s: got=(%d,%d,%d,%d)\n", pass ? "PASS" : "FAIL", label,
                    actual.getRProperty(), actual.getGProperty(), actual.getBProperty(), actual.getAProperty());
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> horizontal_;
    std::unique_ptr<Texture2D> vertical_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaSpriteBatchNegativeScaleTest game;
    game.Run();
    return game.Result();
}
