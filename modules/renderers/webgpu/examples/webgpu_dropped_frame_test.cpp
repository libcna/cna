// SPDX-License-Identifier: MS-PL
//
// plans/plan_apple_m4.md AM4-078: a backbuffer frame WebGPU cannot acquire is dropped whole.
//
// When the surface reports Occluded (a macOS window that is not visible) or cannot be configured,
// the frame is skipped. That path used to drop only the queued SpriteBatch draws: a 3D draw queued
// for the backbuffer survived, and the next bind cycle replayed it into whatever target that cycle
// opened. A post-process sample -- draw a render target to the screen, then render into that same
// target next frame -- then sampled the target inside its own pass, and wgpu-native's validation
// aborted the process ("conflicting usages ... COLOR_TARGET is an exclusive usage").
//
// Registered with CNA_WEBGPU_TEST_SURFACE_OCCLUDED, so every backbuffer frame here is dropped:
//   frame 1 -- render red into target A; draw a textured quad sampling A to the (dropped) backbuffer.
//   frame 2 -- render blue into A, unbind, read A back: blue, and the process alive to say so.
// Before the fix frame 2's pass for A carried frame 1's quad and the process aborted in the submit.

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
