// SPDX-License-Identifier: MS-PL
// cna-killer KF-6: the back buffer is an offscreen framebuffer that reaches the window only at
// Present -- it always was with multisampling on. Creating a render target bound the target's own
// framebuffer to set it up and then bound framebuffer 0, the window's surface, instead of whatever
// was bound before; with the back buffer offscreen, everything drawn after that went to the
// window's surface directly and was overwritten by the back buffer at Present, and
// GetBackBufferData no longer saw it.
//
// In one Draw, with and without multisampling: clear the back buffer red, create a RenderTarget2D
// and a RenderTargetCube, clear the back buffer green, read a pixel back.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BackBufferSurvivesResourceCreationTest final : public Game
{
public:
    BackBufferSurvivesResourceCreationTest()
        : graphics_(std::make_unique<GraphicsDeviceManager>(this))
    {
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferredBackBufferWidthProperty(160);
        graphics_->setPreferredBackBufferHeightProperty(120);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 && checks_ == 2 ? 0 : 1; }

protected:
    void Draw(const GameTime& gameTime) override
    {
        auto& device = getGraphicsDeviceProperty();
        ++draws_;
        if (draws_ == 3 || draws_ == 8)
        {
            const bool multisampled = device.getPresentationParametersProperty().getMultiSampleCountProperty() > 1;
            device.Clear(Color::Red);
            RenderTarget2D target(device, 16, 16);
            RenderTargetCube cube(device, 8, false, SurfaceFormat::Color, DepthFormat::None);
            device.Clear(Color::Lime);
            Color pixel(0, 0, 0, 0);
            const Rectangle centre(80, 60, 1, 1);
            device.GetBackBufferData(&centre, &pixel, 0, 1);
            const bool pass = pixel == Color::Lime;
            std::printf("[%s] %s: a clear after creating render targets mid-frame reaches the back buffer "
                        "(reads %d,%d,%d)\n",
                        pass ? "PASS" : "FAIL", multisampled ? "multisampled" : "single-sample",
                        pixel.getRProperty(), pixel.getGProperty(), pixel.getBProperty());
            failures_ += pass ? 0 : 1;
            ++checks_;
        }
        else
        {
            device.Clear(Color::Black);
        }
        Game::Draw(gameTime);
    }

    void Update(GameTime& gameTime) override
    {
        Game::Update(gameTime);
        if (draws_ == 5 && !switched_)
        {
            switched_ = true;
            graphics_->setPreferMultiSamplingProperty(true);
            graphics_->ApplyChanges();
        }
        if (draws_ >= 10)
            Exit();
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int draws_ = 0;
    int checks_ = 0;
    int failures_ = 0;
    bool switched_ = false;
};

int main()
{
    BackBufferSurvivesResourceCreationTest game;
    game.Run();
    return game.Result();
}
