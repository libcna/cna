// SPDX-License-Identifier: MS-PL
// Task 179: RenderTargetUsage::DiscardContents vs PreserveContents in Bgfx.
//
// Bgfx uses a view-level clear model: setViewClear() on view 1 determines
// whether the framebuffer is cleared at the start of each submitted view.
// For DiscardContents, the XNA layer calls Clear(0,0,0,255) after binding,
// which sets BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH on view 1.
// For PreserveContents, BindAsRenderTarget() calls setViewClear(BGFX_CLEAR_NONE)
// to suppress any carry-over clear from previous frames.
//
// Pixel-level verification is not available in the Bgfx renderer (SpriteBatch
// casts textures to BgfxTextureRenderer, not BgfxRenderTargetRenderer, and the
// 3D state API throws for depth/blend changes).  This is a smoke test: create
// both RT types, bind/unbind them across two frames, and verify no crash.
//
// Exit code 0 = no crash.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

class BgfxRTUsageTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D>        rt_discard_;
    std::unique_ptr<RenderTarget2D>        rt_preserve_;
    int frame_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        // DiscardContents: BindAsRenderTarget leaves view clear flags as-is;
        //   the XNA-layer Clear(0,0,0,255) from GraphicsDevice::SetRenderTarget
        //   calls bgfx::setViewClear(1, BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH, black).
        rt_discard_ = std::make_unique<RenderTarget2D>(dev, kSize, kSize, false,
                        SurfaceFormat::Color, DepthFormat::None, 0,
                        RenderTargetUsage::DiscardContents);
        // PreserveContents: BindAsRenderTarget calls bgfx::setViewClear(1, BGFX_CLEAR_NONE)
        //   to suppress any carry-over clear from a previous DiscardContents frame.
        rt_preserve_ = std::make_unique<RenderTarget2D>(dev, kSize, kSize, false,
                        SurfaceFormat::Color, DepthFormat::None, 0,
                        RenderTargetUsage::PreserveContents);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        if (frame_ == 0)
        {
            // Bind both RTs; just exercise the bind/unbind path — no 3D draws
            // since the Bgfx renderer requires the 3D pipeline to be initialised.
            dev.SetRenderTarget(rt_discard_.get());
            dev.Clear(Color::Red);
            dev.SetRenderTarget(nullptr);

            dev.SetRenderTarget(rt_preserve_.get());
            dev.Clear(Color::Green);
            dev.SetRenderTarget(nullptr);

            ++frame_;
            return;
        }

        if (frame_ == 1)
        {
            // Re-bind DiscardContents RT: XNA Clear(0,0,0,255) sets view clear to black.
            dev.SetRenderTarget(rt_discard_.get());
            dev.SetRenderTarget(nullptr);

            // Re-bind PreserveContents RT: BindAsRenderTarget() resets clear to NONE.
            dev.SetRenderTarget(rt_preserve_.get());
            dev.SetRenderTarget(nullptr);

            std::printf("[PASS] Bgfx RenderTargetUsage smoke: DiscardContents and PreserveContents bind/unbind OK\n");
            Exit();
        }
    }

public:
    BgfxRTUsageTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    BgfxRTUsageTest game;
    game.Run();
    return 0;
}
