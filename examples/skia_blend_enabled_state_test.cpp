// SPDX-License-Identifier: MS-PL
// SKIA-52: SetBlendEnabled gates SpriteBatch composition and Clear always replaces the surface.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBlue(0, 0, 255, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kRed(255, 0, 0, 255);

    [[nodiscard]] bool matches(Color actual, Color expected, int tolerance = 15)
    {
        const auto channelMatches = [tolerance](int a, int b) {
            return a >= b - tolerance && a <= b + tolerance;
        };
        return channelMatches(actual.getRProperty(), expected.getRProperty())
            && channelMatches(actual.getGProperty(), expected.getGProperty())
            && channelMatches(actual.getBProperty(), expected.getBProperty());
    }
}

class SkiaBlendEnabledStateTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> premultipliedRed_;
    bool finished_ = false;
    int failures_ = 0;

    void checkPixel(Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle centre(4, 4, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&centre, &actual, 0, 1);
        const bool pass = matches(actual, expected);
        std::printf("[%s] %s: got=(%d,%d,%d) want=(%d,%d,%d)\n", pass ? "PASS" : "FAIL", label,
                    actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                    expected.getRProperty(), expected.getGProperty(), expected.getBProperty());
        if (!pass)
            ++failures_;
    }

    void drawAlphaBlend()
    {
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        spriteBatch_->Draw(*premultipliedRed_, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        premultipliedRed_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color halfPremultipliedRed(128, 0, 0, 128);
        premultipliedRed_->SetData(&halfPremultipliedRed, 1);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.Clear(kBlue);
        device.SetBlendEnabled(false);
        drawAlphaBlend();
        checkPixel(kRed, "blend disabled makes AlphaBlend source replace destination");

        // SkCanvas::clear is unconditional: it must replace even after a blend-disable toggle.
        device.Clear(kGreen);
        checkPixel(kGreen, "Clear is unconditional while blending is disabled");

        device.Clear(kBlue);
        device.SetBlendEnabled(true);
        drawAlphaBlend();
        checkPixel(Color(128, 0, 127, 255), "blend re-enable restores the stored AlphaBlend state");
        Exit();
    }

public:
    SkiaBlendEnabledStateTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(8);
        graphics_->setPreferredBackBufferHeightProperty(8);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    SkiaBlendEnabledStateTest game;
    game.Run();
    return game.Result();
}
