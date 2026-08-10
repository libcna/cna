// SPDX-License-Identifier: MS-PL
// SKIA-73: Present after zero draws, Clear only, and a rejected SpriteBatch draw must leave the
// raster backbuffer deterministic and must not resurrect a previously presented frame.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
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
    const Color kBlue(0, 0, 255, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kTransparentBlack(0, 0, 0, 0);

    [[nodiscard]] bool Same(Color actual, Color expected)
    {
        return actual.getRProperty() == expected.getRProperty()
            && actual.getGProperty() == expected.getGProperty()
            && actual.getBProperty() == expected.getBProperty()
            && actual.getAProperty() == expected.getAProperty();
    }
}

class SkiaPresentationEdgeTest final : public Game
{
public:
    SkiaPresentationEdgeTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(8);
        graphics_->setPreferredBackBufferHeightProperty(8);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        batch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.Present();
        CheckBackbuffer(kTransparentBlack, "zero-draw Present reads deterministic transparent black");

        device.Clear(kBlue);
        device.Present();
        CheckBackbuffer(kBlue, "Clear-only Present preserves the newly cleared frame");

        device.Clear(kGreen);
        Texture2D disposed(device, 1, 1);
        disposed.Dispose();
        bool rejected = false;
        batch_->Begin();
        try
        {
            batch_->Draw(disposed, Rectangle(0, 0, 1, 1), Color::White);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        batch_->End();
        Check(rejected, "disposed texture draw is rejected before it changes the backbuffer");
        device.Present();
        CheckBackbuffer(kGreen, "Present after rejected draw keeps the current clear instead of a stale frame");
        Exit();
    }

private:
    void CheckBackbuffer(Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle centre(4, 4, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&centre, &actual, 0, 1);
        Check(Same(actual, expected), label);
    }

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> batch_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaPresentationEdgeTest game;
    game.Run();
    return game.Result();
}
