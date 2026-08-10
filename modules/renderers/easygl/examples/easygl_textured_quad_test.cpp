// SPDX-License-Identifier: MS-PL
// Task 86 / SKIA-106: renderer-neutral textured-quad pixel readback, first used by EasyGL.
//
// Renders a 1×1 solid-red texture as a full-screen sprite, reads back the
// centre pixel with GetBackBufferData, and asserts R=255, G=0, B=0.
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class TexturedQuadTest : public Game
{
    std::unique_ptr<SpriteBatch> sb_;
    Texture2D                    redTex_;
    bool                         done_ = false;
    int                          result_ = 1;  // 1 = fail until proven otherwise

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);

        // 1×1 solid-red texture (RGBA)
        const std::vector<uint8_t> px = {255, 0, 0, 255};
        redTex_ = Texture2D::CreateFromPixels(device, 1, 1, px);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        // Clear to solid red so we can check clear+readback before adding SpriteBatch.
        device.Clear(Color(255, 0, 0, 255));
        // Express the absence of depth work through the public state object. This is equivalent
        // on 3D renderers and remains valid on deliberately 2D-only renderers.
        device.setDepthStencilStateProperty(DepthStencilState::None);

        // Read back the centre pixel — should be the clear colour.
        const Rectangle region(W / 2, H / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);

        // Now draw the 1×1 red texture and read back again.
        device.Clear(Color(0, 0, 0, 255));
        sb_->Begin();
        sb_->Draw(redTex_,
                  Rectangle(0, 0, W, H),
                  Rectangle(0, 0, 1, 1),
                  Color::White);
        sb_->End();
        pixel = Color(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);

        const bool pass = (pixel.getRProperty() == 255 &&
                           pixel.getGProperty() == 0   &&
                           pixel.getBProperty() == 0);

        if (pass)
        {
            std::printf("[PASS] TexturedQuad: pixel=(%d,%d,%d,%d)\n",
                        pixel.getRProperty(), pixel.getGProperty(),
                        pixel.getBProperty(), pixel.getAProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] TexturedQuad: pixel=(%d,%d,%d,%d), expected (255,0,0,*)\n",
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
    TexturedQuadTest game;
    game.Run();
    return game.getResult();
}
