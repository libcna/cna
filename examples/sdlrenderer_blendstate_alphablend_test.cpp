// SPDX-License-Identifier: MS-PL
// Task 697: Pixel test — BlendState::AlphaBlend premultiplied alpha on SDL_Renderer.
//
// BlendState::AlphaBlend uses factors (colorSrc=One, colorDst=InverseSourceAlpha) -- i.e.
// dst = src*1 + dst*(1-srcA). This equation assumes the SOURCE colour has already been
// premultiplied by its own alpha (as XNA's real content pipeline does at build time for
// standard texture imports) -- Task 695's own audit test proved the OPPOSITE case (what
// happens when AlphaBlend is fed non-premultiplied data: a well-known "over-bright" artifact).
// This test complements that by feeding AlphaBlend genuinely premultiplied source colour and
// confirming it produces the textbook-correct blended result.
//
// Draws a half-transparent (alpha=128) Red quad, correctly PRE-multiplied (colour scaled by its
// own alpha up front: R=255*128/255≈128, not the full 255), over a solid Blue background, with
// BlendState::AlphaBlend:
//   dst = (128,0,0)*1 + (0,0,255)*(1-128/255) ≈ (128, 0, 127) -- a proper 50/50 red/blue blend.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
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

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool colourMatch(Color got, Color want, int tol)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

static const Color kBlue(0, 0, 255, 255);

class SdlBlendStateAlphaBlendTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             tex_;

    bool done_   = false;
    int  result_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        // Genuinely premultiplied Red at alpha=128: colour scaled by its own alpha up front
        // (128/255 ≈ 0.502), so R = 255*0.502 ≈ 128, not the full 255.
        tex_ = std::make_unique<Texture2D>(dev, 1, 1);
        const Color premultipliedRed(128, 0, 0, 128);
        tex_->SetData(&premultipliedRed, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(kBlue);

        sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        sb_->Draw(*tex_, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();

        Color px(0, 0, 0, 0);
        Rectangle reg(8, 8, 1, 1);
        dev.GetBackBufferData(&reg, &px, 0, 1);

        const bool ok = colourMatch(px, Color(128, 0, 127, 255), 15);
        std::printf("[%s] AlphaBlend(genuinely premultiplied src) -> proper blend (128,0,127): got=(%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL", px.getRProperty(), px.getGProperty(), px.getBProperty());
        if (!ok) result_ = 1;

        Exit();
    }

public:
    SdlBlendStateAlphaBlendTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(16);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlBlendStateAlphaBlendTest game;
    game.Run();
    return game.getResult();
}
