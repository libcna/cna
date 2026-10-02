// SPDX-License-Identifier: MS-PL
// Applying a BlendState on an OpenGL ES 3.0 context (WebGL 2's API level; ctest runs this with
// MESA_GLES_VERSION_OVERRIDE=3.0) must leave no GL error behind. EasyGL used to enable
// GL_SAMPLE_MASK whenever a glSampleMaski entry point resolved -- which Mesa and the WebGL layer
// both do on ES 3.0 -- and the INVALID_ENUM it left made the next multiple-render-target bind
// fail (Bubble Bound in a browser). Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SampleMaskEs30Test : public Game
{
    std::unique_ptr<SpriteBatch> batch_;
    Texture2D texture_;
    bool done_ = false;

public:
    int result = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        batch_ = std::make_unique<SpriteBatch>(device);
        texture_ = Texture2D::CreateFromPixels(device, 1, 1, std::vector<uint8_t>{255, 0, 0, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        try
        {
            device.Clear(Color(0, 0, 0, 255));
            batch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend);
            batch_->Draw(texture_, Rectangle(0, 0, 16, 16), Color(255, 255, 255, 255));
            batch_->End();

            // Two targets: the multiple-render-target bind is what checks for a pending GL error.
            RenderTarget2D first(device, 16, 16);
            RenderTarget2D second(device, 16, 16);
            device.SetRenderTargets({RenderTargetBinding(&first), RenderTargetBinding(&second)});
            device.Clear(Color(0, 255, 0, 255));
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            result = 0;
            std::printf("[PASS] a blend state and a render-target bind left no GL error\n");
        }
        catch (const std::exception& error)
        {
            std::printf("[FAIL] %s\n", error.what());
        }
        Exit();
    }
};

int main()
{
    SampleMaskEs30Test game;
    // Multiple render targets are HiDef, as Bubble Bound's renderer is.
    game.getGraphicsDeviceProperty().SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    game.Run();
    return game.result;
}
