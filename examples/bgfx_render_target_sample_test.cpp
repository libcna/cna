// SPDX-License-Identifier: MS-PL
// Task 333: RenderTarget2D sampled as Texture2D after unbinding, on Bgfx.
//
// EasyGL (Task 87, easygl_render_target_test.cpp) and Vulkan (Task 148, vulkan_rt2d_test.cpp)
// both already pixel-verify this exact scenario: render into a RenderTarget2D, unbind it, then
// draw the RT through SpriteBatch as a Texture2D, and read back the result. Bgfx has no usable
// GPU pixel-readback path in this project (see bgfx_render_target_usage_test.cpp, Task 179), so
// this cannot be pixel-verified the same way. Task 179's own comment already suspected the reason:
// "SpriteBatch casts textures to BgfxTextureBackend, not BgfxRenderTargetBackend".
//
// This test confirms that suspicion directly: BgfxRenderTargetBackend (defined in
// BgfxGraphicsBackend.hpp) inherits ONLY IRenderTargetBackend, NOT BgfxTextureBackend - they are
// unrelated sibling classes with different memory layouts. BgfxSpriteBatchBackend::Draw
// unconditionally does static_cast<const BgfxTextureBackend&>(texture) on whatever ITextureBackend
// it's handed. When handed a RenderTarget2D's backend (a BgfxRenderTargetBackend), this is a
// static_cast between unrelated types - undefined behaviour, not just "no-op".
//
// Exit code 0 = did not crash (no assertion about pixel correctness is possible on Bgfx).
// A crash/abort here is itself the finding - it upgrades "cast looks suspicious" (Task 179) to
// "confirmed UB that corrupts a real draw call", tracked as Task 873.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kRTSize = 64;

class BgfxRenderTargetSampleTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>    sb_;
    std::unique_ptr<RenderTarget2D> rt_;
    int  frame_  = 0;

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
        if (frame_ > 0) return;
        ++frame_;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        // --- Pass 1: render solid green into the RT, then unbind ---
        device.SetRenderTarget(rt_.get());
        device.Clear(Color(0, 255, 0, 255));
        device.SetRenderTarget(nullptr);

        // --- Pass 2: sample the unbound RT as a Texture2D via SpriteBatch ---
        // If BgfxSpriteBatchBackend::Draw's static_cast<BgfxTextureBackend&> on a
        // BgfxRenderTargetBackend is truly unrelated-type UB, this call is where it manifests.
        device.Clear(Color(0, 0, 0, 255));
        sb_->Begin();
        sb_->Draw(*rt_,
                  Rectangle(0, 0, W, H),
                  Rectangle(0, 0, kRTSize, kRTSize),
                  Color::White);
        sb_->End();

        std::printf("[PASS] Bgfx RenderTarget2D-as-Texture2D SpriteBatch::Draw did not crash\n");
        Exit();
    }

public:
    BgfxRenderTargetSampleTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kRTSize);
        gdm_->setPreferredBackBufferHeightProperty(kRTSize);
    }
};

int main()
{
    BgfxRenderTargetSampleTest game;
    game.Run();
    return 0;
}
