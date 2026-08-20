// SPDX-License-Identifier: MS-PL
// Task 701: Audit SamplerState -> SDL_ScaleMode + address-mode mapping on SDL_Renderer.
//
// Consolidates and extends findings already established across Tasks 685/688: TextureFilter's
// Point-vs-Linear mapping to SDL_ScaleMode was already verified correct for the exact 0/1 case
// (Task 688), and TextureAddressMode's Clamp/Wrap/Mirror situation was already thoroughly audited
// (Task 685/686/687 -- Clamp is correct by accident of SDL_RenderTexture's fixed edge behavior,
// Wrap/Mirror are BLOCKED pending a project-owner decision, see plans/plan_graphics.md rows 686/687).
//
// REAL BUG FOUND AND FIXED here: TextureFilter has 9 values encoding separate min/mag/mip filter
// components (see TextureFilter.hpp's doc comments: "shrink" = minification, "expand" =
// magnification), but the old SetSamplerFilter mapping only treated textureFilter==0 (Linear) as
// SDL_SCALEMODE_LINEAR, silently downgrading EVERY other value -- including Anisotropic,
// LinearMipPoint, MinPointMagLinearMipLinear, and MinPointMagLinearMipPoint, all 4 of which
// specify a LINEAR magnification ("expand") filter -- to SDL_SCALEMODE_NEAREST. Since
// SDL_Renderer's 2D blit pipeline has no separate min/mag/mip control and no LOD/mipmap-level
// sampling at all, and SpriteBatch draws are near-universally magnification-dominant (sprites
// scaled up or 1:1), the magnification component is the one that visibly matters and is now used
// as the effective filter for all 9 values.
//
// This test uses SamplerState::AnisotropicClamp (a real, existing XNA preset using
// TextureFilter::Anisotropic) and confirms it now produces LINEAR (blended) sampling at a texel
// boundary, mirroring Task 688's own methodology exactly (2-texel Red|Green texture stretched
// wide, sampled at the exact boundary between the two texel centers).
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // True if both channels are in a genuine mid-range blend (~50/50 of 255).
    bool IsBlended(const Color& c)
    {
        return c.getRProperty() >= 90 && c.getRProperty() <= 165
            && c.getGProperty() >= 90 && c.getGProperty() <= 165;
    }
}

class SdlSamplerStateFilterAuditTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             tex_; // 2x1: [Red, Green]

    bool done_   = false;
    int  result_ = 0;

    Color DrawAndSampleAtBoundary(SamplerState* sampler)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, sampler, nullptr, nullptr,
                   nullptr, Matrix::getIdentityProperty());
        sb_->Draw(*tex_, Rectangle(0, 0, 64, 8), Rectangle(0, 0, 2, 1), Color::White);
        sb_->End();

        Color px(0, 0, 0, 0);
        Rectangle reg(32, 4, 1, 1); // exact texel boundary of the stretched 2-texel texture
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        const std::vector<std::uint8_t> px = {
            255,   0,   0, 255,   // texel 0: red
              0, 255,   0, 255    // texel 1: green
        };
        tex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 2, 1, px));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        const Color anisotropicSample = DrawAndSampleAtBoundary(
            const_cast<SamplerState*>(&SamplerState::AnisotropicClamp));

        const bool ok = IsBlended(anisotropicSample);
        std::printf("[%s] SamplerState::AnisotropicClamp (TextureFilter::Anisotropic) @ texel "
                    "boundary -> blended (LINEAR mag), not pure (NEAREST): got=(%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL",
                    anisotropicSample.getRProperty(), anisotropicSample.getGProperty(),
                    anisotropicSample.getBProperty());
        if (!ok) result_ = 1;

        Exit();
    }

public:
    SdlSamplerStateFilterAuditTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(8);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlSamplerStateFilterAuditTest game;
    game.Run();
    return game.getResult();
}
