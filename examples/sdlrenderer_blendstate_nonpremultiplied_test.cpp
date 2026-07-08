// SPDX-License-Identifier: MS-PL
// Task 698: Pixel test — BlendState::NonPremultiplied on SDL_Renderer.
//
// BlendState::NonPremultiplied uses factors (colorSrc=SourceAlpha, colorDst=InverseSourceAlpha)
// -- i.e. dst = src*srcA + dst*(1-srcA), the classic "straight alpha" blend equation for source
// colour that has NOT been premultiplied by its own alpha (the common case for most raw/imported
// image data, as opposed to XNA's content-pipeline-premultiplied default). Confirmed by Task 695's
// audit and fix that this preset is correctly mapped (and was ALREADY correctly mapped before
// Task 695's fix too, since its factors happen to exactly match SDL's plain SDL_BLENDMODE_BLEND,
// the old fallback default) -- this is a dedicated, focused verification of that specific preset.
//
// Draws a half-transparent (alpha=128), straight (non-premultiplied) Red quad -- full-brightness
// R=255 with alpha=128, exactly as a typical unprocessed image would store it -- over a solid
// Blue background with BlendState::NonPremultiplied:
//   dst = (255,0,0)*(128/255) + (0,0,255)*(1-128/255) ≈ (128, 0, 127) -- a proper 50/50 blend.
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

class SdlBlendStateNonPremultipliedTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             whiteTex_;

    bool done_   = false;
    int  result_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        whiteTex_ = std::make_unique<Texture2D>(dev, 1, 1);
        const Color white(255, 255, 255, 255);
        whiteTex_->SetData(&white, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(kBlue);

        sb_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        // Straight (non-premultiplied) half-transparent Red: full-brightness R with alpha=128.
        sb_->Draw(*whiteTex_, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 128));
        sb_->End();

        Color px(0, 0, 0, 0);
        Rectangle reg(8, 8, 1, 1);
        dev.GetBackBufferData(&reg, &px, 0, 1);

        const bool ok = colourMatch(px, Color(128, 0, 127, 255), 15);
        std::printf("[%s] NonPremultiplied(straight-alpha src) -> proper blend (128,0,127): got=(%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL", px.getRProperty(), px.getGProperty(), px.getBProperty());
        if (!ok) result_ = 1;

        Exit();
    }

public:
    SdlBlendStateNonPremultipliedTest()
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
    SdlBlendStateNonPremultipliedTest game;
    game.Run();
    return game.getResult();
}
