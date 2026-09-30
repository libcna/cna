// SPDX-License-Identifier: MS-PL
// cna-killer KF-2: with PreferMultiSampling on, resizing the back buffer while the window was
// minimized threw "EasyGL: multisample backbuffer is incomplete" out of ApplyChanges. The
// multisample renderbuffers are sized from the drawable, which is 0x0 while minimized; after the
// throw the back buffer could not be bound again, SetRenderTarget(nullptr) failed and every
// Present threw "Cannot present while render targets are bound". XNA treats PreferMultiSampling
// as a preference and never fails a reset over it.
//
// The game minimizes its window, resizes the back buffer while minimized, restores the window,
// and then must clear and read back the back buffer normally.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"

#include <cstdio>
#include <exception>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class MsaaBackBufferMinimizedResizeTest final : public Game
{
public:
    MsaaBackBufferMinimizedResizeTest()
        : graphics_(std::make_unique<GraphicsDeviceManager>(this))
    {
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferMultiSamplingProperty(true);
        graphics_->setPreferredBackBufferWidthProperty(320);
        graphics_->setPreferredBackBufferHeightProperty(240);
    }

    [[nodiscard]] int Result() const noexcept { return result_; }

protected:
    void Update(GameTime& gameTime) override
    {
        ++updates_;
        try
        {
            if (updates_ == 3)
            {
                getWindowProperty().MinimizeEXT();
            }
            else if (updates_ == 8)
            {
                graphics_->setPreferredBackBufferWidthProperty(400);
                graphics_->setPreferredBackBufferHeightProperty(300);
                graphics_->ApplyChanges();
            }
            else if (updates_ == 12)
            {
                getWindowProperty().RestoreEXT();
            }
            else if (updates_ > 600)
            {
                std::printf("[FAIL] the frame after restoring the window never came\n");
                result_ = 1;
                Exit();
            }
        }
        catch (const std::exception& error)
        {
            std::printf("[FAIL] update %d threw: %s\n", updates_, error.what());
            result_ = 1;
            Exit();
        }
        Game::Update(gameTime);
    }

    void Draw(const GameTime& gameTime) override
    {
        Game::Draw(gameTime);
        if (updates_ < 20 || result_ != 0)
            return;
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(nullptr);
        device.Clear(Color::Orange);
        Color pixel(0, 0, 0, 0);
        const Rectangle corner(1, 1, 1, 1);
        device.GetBackBufferData(&corner, &pixel, 0, 1);
        const bool pass = pixel == Color::Orange;
        std::printf("[%s] after a resize while minimized with MSAA the back buffer clears and reads back: "
                    "(%d,%d,%d), %d samples\n",
                    pass ? "PASS" : "FAIL", pixel.getRProperty(), pixel.getGProperty(), pixel.getBProperty(),
                    device.getPresentationParametersProperty().getMultiSampleCountProperty());
        result_ = pass ? 0 : 1;
        Exit();
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int updates_ = 0;
    int result_ = 0;
};

int main()
{
    try
    {
        MsaaBackBufferMinimizedResizeTest game;
        game.Run();
        return game.Result();
    }
    catch (const std::exception& error)
    {
        std::printf("[FAIL] the game ended with an exception: %s\n", error.what());
        return 1;
    }
}
