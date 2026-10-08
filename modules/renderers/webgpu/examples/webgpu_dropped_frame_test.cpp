// SPDX-License-Identifier: MS-PL
//
// plans/plan_apple_m4.md AM4-078/089: a backbuffer frame on an occluded WebGPU surface.
//
// A surface reports Occluded for a macOS window that is not on screen -- every window of a locked
// session. AM4-078 found that dropping such a frame dropped only its queued SpriteBatch draws: a 3D
// draw queued for the backbuffer survived, and the next bind cycle replayed it into whatever target
// that cycle opened. A post-process sample -- draw a render target to the screen, then render into
// that same target next frame -- then sampled the target inside its own pass, and wgpu-native's
// validation aborted the process ("conflicting usages ... COLOR_TARGET is an exclusive usage").
// AM4-089 then stopped dropping it: XNA's back buffer exists whether or not the window is shown, so
// the frame renders into an offscreen stand-in and only the present is skipped.
//
// Registered with CNA_WEBGPU_TEST_SURFACE_OCCLUDED, so every backbuffer frame here is occluded:
//   frame 1 -- render red into target A; draw a textured quad sampling A to the backbuffer and read
//              the backbuffer back: red, the frame rendered although nothing could be presented.
//   frame 2 -- render blue into A, unbind, read A back: blue, untouched by frame 1's draws, and the
//              process alive to say so.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    int failures = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failures;
    }
}

class WebGPUDroppedFrameTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<RenderTarget2D> target_;
    int frame_ = 0;

protected:
    void Draw(const GameTime& gameTime) override
    {
        auto& device = getGraphicsDeviceProperty();
        ++frame_;
        if (frame_ == 1)
        {
            target_ = std::make_unique<RenderTarget2D>(device, 32, 32, false, SurfaceFormat::Color,
                                                       DepthFormat::None);
            device.SetRenderTarget(target_.get());
            device.Clear(Color(255, 0, 0, 255));
            device.SetRenderTarget(nullptr);

            device.Clear(Color(0, 0, 0, 255));
            BasicEffect effect(device);
            effect.setTextureEnabledProperty(true);
            effect.setTextureProperty(target_.get());
            effect.Apply();
            const VertexPositionTexture quad[] = {
                {Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)},
                {Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)},
                {Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)},
                {Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 0.0f)},
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleStrip, quad, 0, 2);
            // XNA refuses SetData/rendering hazards on a texture left set on the device.
            device.getTexturesProperty()(0, nullptr);
            Color shown(0, 0, 0, 0);
            const Rectangle centre(64, 64, 1, 1);
            device.GetBackBufferData(&centre, &shown, 0, 1);
            check(shown.getRProperty() == 255 && shown.getGProperty() == 0 && shown.getBProperty() == 0,
                  "an occluded backbuffer frame renders: the quad sampling the red target reads back red");
        }
        else if (frame_ == 2)
        {
            device.SetRenderTarget(target_.get());
            device.Clear(Color(0, 0, 255, 255));
            device.SetRenderTarget(nullptr);
            Color pixel(0, 0, 0, 0);
            const Rectangle one(16, 16, 1, 1);
            target_->GetData(0, &one, &pixel, 0, 1);
            check(pixel.getRProperty() == 0 && pixel.getGProperty() == 0 && pixel.getBProperty() == 255,
                  "a render target drawn by a dropped backbuffer frame renders and reads back next "
                  "frame, untouched by that frame's draws");
            Exit();
        }
        Game::Draw(gameTime);
    }

public:
    WebGPUDroppedFrameTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        manager_->setPreferredBackBufferWidthProperty(128);
        manager_->setPreferredBackBufferHeightProperty(128);
    }
};

int main()
{
    if (std::getenv("CNA_WEBGPU_TEST_SURFACE_OCCLUDED") == nullptr)
    {
        std::printf("[FAIL] CNA_WEBGPU_TEST_SURFACE_OCCLUDED must be set for this test\n");
        return 1;
    }
    {
        WebGPUDroppedFrameTest game;
        game.Run();
    }
    std::printf("=== %s ===\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
