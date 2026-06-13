// SPDX-License-Identifier: MS-PL
// Task 87: EasyGL integration test — RenderTarget2D pixel readback.
//
// Renders a solid-green clear into a 64×64 RenderTarget2D, then uses the RT
// as a texture to fill the default framebuffer, reads back the centre pixel,
// and asserts G=255, R=0, B=0.
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kRTSize = 64;

class RenderTargetTest : public Game
{
    std::unique_ptr<SpriteBatch>   sb_;
    std::unique_ptr<RenderTarget2D> rt_;
    bool                            done_ = false;
    int                             result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
        rt_ = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        // --- Pass 1: render solid green into the RT ---
        device.SetRenderTarget(rt_.get());
        device.SetDepthTestEnabled(false);
        device.Clear(Color(0, 255, 0, 255));
        device.SetRenderTarget(nullptr);

        // --- Pass 2: blit RT as a full-screen texture ---
        device.Clear(Color(0, 0, 0, 255));
        device.SetDepthTestEnabled(false);

        sb_->Begin();
        sb_->Draw(*rt_,
                  Rectangle(0, 0, W, H),
                  Rectangle(0, 0, kRTSize, kRTSize),
                  Color::White);
        sb_->End();

        // Read back the centre pixel from the default framebuffer.
        const Rectangle region(W / 2, H / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);

        const bool pass = (pixel.getRProperty() == 0   &&
                           pixel.getGProperty() == 255 &&
                           pixel.getBProperty() == 0);

        if (pass)
        {
            std::printf("[PASS] RenderTarget2D: pixel=(%d,%d,%d,%d)\n",
                        pixel.getRProperty(), pixel.getGProperty(),
                        pixel.getBProperty(), pixel.getAProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] RenderTarget2D: pixel=(%d,%d,%d,%d), expected (0,255,0,*)\n",
                        pixel.getRProperty(), pixel.getGProperty(),
                        pixel.getBProperty(), pixel.getAProperty());
            result_ = 1;
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    RenderTargetTest game;
    game.Run();
    return game.getResult();
}
