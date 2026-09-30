// SPDX-License-Identifier: MS-PL
// cna-killer KF-6: GetBackBufferData addresses the back buffer, which XNA keeps at the
// PresentationParameters size whatever the window does. EasyGL draws the back buffer scaled into
// the window's drawable (Letterbox by default), but read it back 1:1 from the drawable at the back
// buffer's coordinates -- so once the window no longer had the back buffer's size (a user resize,
// a window the screen could not fit, a display scale of 2) the read returned the letterbox bars,
// a different part of the picture, or zeros past the drawable's edge.
//
// A 320x240 back buffer in a window resized to 640x360: the picture is scaled by 1.5 and centred
// with 80-pixel bars. The left half is drawn red over a green clear; the whole back buffer is
// read back.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BackBufferReadLetterboxedTest final : public Game
{
public:
    BackBufferReadLetterboxedTest()
        : graphics_(std::make_unique<GraphicsDeviceManager>(this))
    {
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferredBackBufferWidthProperty(kWidth);
        graphics_->setPreferredBackBufferHeightProperty(kHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::Letterbox);
    }

    [[nodiscard]] int Result() const noexcept { return result_; }

protected:
    void LoadContent() override
    {
        white_ = std::make_unique<Texture2D>(getGraphicsDeviceProperty(), 1, 1);
        const Color pixel = Color::White;
        white_->SetData(&pixel, 1);
        batch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
    }

    void Update(GameTime& gameTime) override
    {
        Game::Update(gameTime);
        GameWindow& window = getWindowProperty();
        if (++frames_ == 2)
        {
            // XNA's own way to size the client area without touching the back buffer.
            window.BeginScreenDeviceChange(false);
            window.EndScreenDeviceChange(window.getScreenDeviceNameProperty(), 640, 360);
        }
        const Rectangle client = window.getClientBoundsProperty();
        if (frames_ > 2 && client.Width == 640 && client.Height == 360)
            ++settledFrames_;
        if (frames_ > 600)
        {
            std::printf("[FAIL] the window never became 640x360 (it is %dx%d)\n", client.Width, client.Height);
            Exit();
        }
    }

    void Draw(const GameTime& gameTime) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color::Lime);
        batch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        batch_->Draw(*white_, Rectangle(0, 0, kWidth / 2, kHeight), Color::Red);
        batch_->End();
        Game::Draw(gameTime);
        if (settledFrames_ < 3)
            return;

        std::vector<Color> pixels(static_cast<std::size_t>(kWidth * kHeight));
        device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
        int wrong = 0;
        for (int y = 0; y < kHeight; ++y)
        {
            for (int x = 0; x < kWidth; ++x)
            {
                const Color expected = x < kWidth / 2 ? Color::Red : Color::Lime;
                const Color& actual = pixels[static_cast<std::size_t>(y * kWidth + x)];
                if (actual != expected)
                {
                    if (wrong < 4)
                        std::printf("  (%d,%d) reads (%d,%d,%d,%d)\n", x, y, actual.getRProperty(),
                                    actual.getGProperty(), actual.getBProperty(), actual.getAProperty());
                    ++wrong;
                }
            }
        }
        std::printf("[%s] a 320x240 back buffer letterboxed into a 640x360 window reads back as drawn "
                    "(%d of %d pixels wrong)\n",
                    wrong == 0 ? "PASS" : "FAIL", wrong, kWidth * kHeight);
        result_ = wrong == 0 ? 0 : 1;
        Exit();
    }

    void UnloadContent() override
    {
        batch_.reset();
        white_.reset();
    }

private:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 240;

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<SpriteBatch> batch_;
    int frames_ = 0;
    int settledFrames_ = 0;
    int result_ = 1;
};

int main()
{
    BackBufferReadLetterboxedTest game;
    game.Run();
    return game.Result();
}
