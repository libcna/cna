// SPDX-License-Identifier: MS-PL
// Task 336: verify RenderTarget2D's mip chain is genuinely GL-complete after being auto-generated
// on unbind, on EasyGL.
//
// A RenderTarget2D is populated by real GPU draws, not SetData — Texture2D::GetData reads a
// CPU-side mirror buffer that is never touched by rendering, so it cannot verify a render
// target's actual GPU mip content. Instead this test reuses the exact methodology already
// established by Task 299 (easygl_texture_anisotropic_effect_test.cpp) and the underlying Task
// 867 finding: sampling with a *Mip*-suffixed TextureFilter (here: Anisotropic) on EasyGL renders
// **solid black** if the bound texture's mip chain is not GL-complete (missing levels or
// undefined storage) — a reliable, already-proven "is this texture mip-complete" probe that
// doesn't require raw GL access from the test.
//
// Method: fill a mipMap=true RenderTarget2D with solid blue at level 0, unbind it (this is where
// EasyGLRenderTargetRenderer::UnbindAsRenderTarget now calls generate_mipmap — the Task 336 fix),
// then sample the unbound RT via SpriteBatch with TextureFilter::Anisotropic. If the fix works,
// the result is blue (mip-complete). If the mip chain were still absent/incomplete (the
// pre-Task-336 state — LevelCount claims >1 mip levels exist, but no storage was ever allocated
// past level 0 and no generation step ever ran), the result would be solid black, exactly
// matching Task 867's documented symptom.
//
// Exit code 0 = PASS (blue, mip chain complete), 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kRTSize = 64;

class RenderTarget2DMipTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<RenderTarget2D>        rt_;
    int result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
        rt_ = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize, /*mipMap=*/true,
                                                SurfaceFormat::Color, DepthFormat::None);
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.setBlendStateProperty(BlendState::Opaque);

        // --- Phase 1: fill level 0 with solid blue, then unbind (triggers mip regeneration) ---
        device.SetRenderTarget(rt_.get());
        device.Clear(Color(0, 0, 255, 255));
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // --- Phase 2: sample the unbound RT with a mip-requiring filter ---
        device.Clear(Color(0, 0, 0, 255));

        SamplerState aniso;
        aniso.setFilterProperty(TextureFilter::Anisotropic);
        aniso.setAddressUProperty(TextureAddressMode::Clamp);
        aniso.setAddressVProperty(TextureAddressMode::Clamp);

        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &aniso, nullptr, nullptr);
        sb_->Draw(*rt_,
                  Rectangle(0, 0, W, H),
                  Rectangle(0, 0, kRTSize, kRTSize),
                  Color::White);
        sb_->End();

        const Rectangle centReg(W / 2, H / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);

        const bool isBlue  = centPx.getRProperty() <= 10 && centPx.getGProperty() <= 10 &&
                             centPx.getBProperty() >= 200;
        const bool isBlack = centPx.getRProperty() <= 10 && centPx.getGProperty() <= 10 &&
                             centPx.getBProperty() <= 10;

        if (isBlue)
        {
            std::printf("[PASS] RenderTarget2D mip chain is GL-complete: centre=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] RenderTarget2D mip chain: centre=(%d,%d,%d), expected blue "
                        "(R<=10,G<=10,B>=200)%s\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty(),
                        isBlack ? " — solid black, the Task 867 mip-incomplete-texture symptom"
                                : "");
        }
        Exit();
    }

public:
    RenderTarget2DMipTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    RenderTarget2DMipTest game;
    game.Run();
    return game.getResult();
}
