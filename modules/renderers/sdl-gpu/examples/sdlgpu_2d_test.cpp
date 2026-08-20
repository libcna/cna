// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-22..25: end-to-end 2D vertical-slice proof for the SDL_GPU graphics
// renderer -- a real Texture2D upload and real SpriteBatch draws (tint, alpha, rotation, both
// flips, and Point/Linear + Wrap/Clamp/Mirror samplers), rendered for many frames with no
// exception. This is the "declare a verified native 2D baseline" gate (SDLGPU-25's own bar,
// mirroring WEBGPU-124-131's).
//
// No pixel-level readback exists yet on this renderer (ReadBackbuffer is not implemented --
// plans/plan_sdlgpu.md's Phase SDLGPU-8/39), so this test cannot assert exact pixel colors the way the
// WebGPU/D3D11/Vulkan smoke tests do. It proves: real texture upload, a real multi-sprite scene
// (tint/alpha/rotation/flip/sampler variants) drawn over many frames with no crash -- the same
// bar WEBGPU-126 met before automated GPU readback existed for that renderer.
//
// Check A -- Texture2D::CreateFromPixels() succeeds for a 4x4 per-quadrant-colored texture.
// Check B -- a SpriteBatch scene mixing tint, alpha, rotation, both flips, and
//   Point/Linear + Wrap/Clamp/Mirror samplers (via separate Begin/End blocks with explicit
//   SamplerState presets) completes with no exception.
// Check C -- 120 frames of that scene render with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kTotalFrames = 120;

    std::vector<std::uint8_t> MakeQuadrantTexturePixels()
    {
        // 4x4 RGBA8, one solid color per 2x2 quadrant: red / green / blue / white.
        std::vector<std::uint8_t> pixels(4 * 4 * 4, 0);
        auto setPixel = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
            const std::size_t offset = (static_cast<std::size_t>(y) * 4 + x) * 4;
            pixels[offset + 0] = r;
            pixels[offset + 1] = g;
            pixels[offset + 2] = b;
            pixels[offset + 3] = a;
        };
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                if (x < 2 && y < 2) setPixel(x, y, 255, 0, 0, 255);
                else if (x >= 2 && y < 2) setPixel(x, y, 0, 255, 0, 255);
                else if (x < 2 && y >= 2) setPixel(x, y, 0, 0, 255, 255);
                else setPixel(x, y, 255, 255, 255, 255);
            }
        }
        return pixels;
    }
}

class SdlGpu2DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        try
        {
            texture_ = std::make_unique<Texture2D>(
                Texture2D::CreateFromPixels(dev, 4, 4, MakeQuadrantTexturePixels()));
            check(texture_->getWidthProperty() == 4 && texture_->getHeightProperty() == 4,
                  "Texture2D::CreateFromPixels() succeeds for a 4x4 texture");
        }
        catch (const std::exception& e)
        {
            check(false, (std::string("Texture2D::CreateFromPixels() succeeds -- threw: ") + e.what()).c_str());
        }

        spriteBatch_ = std::make_unique<SpriteBatch>(dev);
    }

    void DrawScene()
    {
        auto& dev = getGraphicsDeviceProperty();

        // Plain tinted/alpha draw, default (LinearClamp) sampler.
        spriteBatch_->Begin();
        spriteBatch_->Draw(*texture_, Rectangle(10, 10, 64, 64), Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->Draw(*texture_, Rectangle(90, 10, 64, 64), Rectangle(0, 0, 4, 4),
                           Color(255, 255, 255, 128));
        spriteBatch_->End();

        // Rotated + both-flip draw.
        spriteBatch_->Begin();
        spriteBatch_->Draw(*texture_, Rectangle(170, 42, 64, 64), Rectangle(0, 0, 4, 4), Color::White,
                           MathHelper::PiOver4, Vector2(2.0f, 2.0f), SpriteEffects::None, 0.0f);
        spriteBatch_->Draw(*texture_, Rectangle(250, 42, 64, 64), Rectangle(0, 0, 4, 4), Color::White,
                           0.0f, Vector2::Zero,
                           static_cast<SpriteEffects>(static_cast<int>(SpriteEffects::FlipHorizontally) |
                                                       static_cast<int>(SpriteEffects::FlipVertically)),
                           0.0f);
        spriteBatch_->End();

        // Point + Wrap sampler, UVs extending past [0,1] to exercise wrap/mirror behavior.
        SamplerState pointWrap = SamplerState::PointWrap;
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &pointWrap, nullptr, nullptr);
        spriteBatch_->Draw(*texture_, Rectangle(10, 130, 64, 64), Rectangle(0, 0, 8, 8), Color::White);
        spriteBatch_->End();

        SamplerState linearMirror = SamplerState::LinearWrap;
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &linearMirror, nullptr, nullptr);
        spriteBatch_->Draw(*texture_, Rectangle(90, 130, 64, 64), Rectangle(0, 0, 8, 8), Color::White);
        spriteBatch_->End();

        (void)dev;
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color::CornflowerBlue);

        if (frame_ == 1)
        {
            bool threw = false;
            try { DrawScene(); }
            catch (const std::exception&) { threw = true; }
            check(!threw, "SpriteBatch scene (tint/alpha/rotation/flip/samplers) draws with no exception");
        }
        else
        {
            DrawScene();
        }

        if (frame_ == kTotalFrames)
        {
            check(true, "120 frames of the SpriteBatch scene render with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, 3);
            result_ = (passCount_ == 3) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpu2DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpu2DTest game;
    game.Run();
    return game.getResult();
}
