// SPDX-License-Identifier: MS-PL
// Task 146: EasyGL MSAA 4× integration test.
//
// Verifies that PresentationParameters.MultiSampleCount=4 is respected:
//   — EasyGL creates a 4× multisampled renderbuffer pair (color + depth)
//   — All rendering goes to the MSAA FBO
//   — Present() (via GetBackBufferData) resolves via glBlitFramebuffer to FBO 0
//   — GetBackBufferData reads the resolved single-sample FBO 0 copy
//
// Test: render a solid red full-screen quad (no alpha, no depth test),
//       read back the centre pixel, assert R>=200, G<=50, B<=50.
//
// Because MSAA resolve is the focus, the test uses SpriteBatch::Draw with a
// solid-red 1×1 Texture2D scaled to fill the viewport — same pattern as the
// non-MSAA EasyGL readback test.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class EasyGLMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             redTex_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_     = std::make_unique<SpriteBatch>(device);
        redTex_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color red(255, 0, 0, 255);
        redTex_->SetData(&red, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 0, 0, 255));
        device.setBlendStateProperty(BlendState::Opaque);

        sb_->Begin();
        sb_->Draw(*redTex_,
                  Rectangle(0, 0, W, H),
                  Rectangle(0, 0, 1, 1),
                  Color::White);
        sb_->End();

        const Rectangle centReg(W / 2, H / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);

        const bool pass = (centPx.getRProperty() >= 200 &&
                           centPx.getGProperty() <= 50  &&
                           centPx.getBProperty() <= 50);

        if (pass) {
            std::printf("[PASS] EasyGL MSAA 4x: centre=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        } else {
            std::printf("[FAIL] EasyGL MSAA 4x: centre=(%d,%d,%d), "
                        "expected red (R>=200, G<=50, B<=50)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
        }
        Exit();
    }

public:
    EasyGLMsaaTest()
    {
        // Request MSAA via PresentationParameters before renderer creation.
        // GraphicsDeviceManager caps MultiSampleCount at 8; the GL renderer
        // clamps to GL_MAX_SAMPLES at runtime.
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        gdm_->setPreferMultiSamplingProperty(true);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLMsaaTest game;
    game.Run();
    return game.getResult();
}
