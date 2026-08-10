// SPDX-License-Identifier: MS-PL
// Task 696: Pixel test — BlendState::Opaque on SDL_Renderer.
//
// BlendState::Opaque uses factors (colorSrc=One, colorDst=Zero) -- i.e. dst = src*1 + dst*0 =
// src, completely ignoring the destination AND the source alpha entirely. Confirmed via Task
// 695's audit that this preset was already correctly mapped to SDL_BLENDMODE_NONE both before
// and after that task's 2 bug fixes (Opaque/Additive were the only 2 presets the old, incomplete
// mapping already special-cased correctly) -- this is a dedicated, focused verification of that
// specific preset now that BlendState::AlphaBlend/NonPremultiplied are correctly distinguished.
//
// Draws a quad with a LOW-alpha (64/255) Red tint over a solid Green background using
// BlendState::Opaque. If Opaque is genuinely honored, alpha must be completely ignored and the
// destination completely overwritten -- the result must be pure, fully-opaque Red, NOT any kind
// of blend with the Green background (which a "silently falls back to alpha-blending" bug would
// produce instead).
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

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool colourMatch(Color got, Color want, int tol)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

static const Color kGreen(0, 255, 0, 255);
static const Color kRed  (255, 0, 0, 255);

class SdlBlendStateOpaqueTest : public Game
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
        dev.Clear(kGreen);

        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        // Low-alpha (64/255) Red -- Opaque must ignore this alpha entirely.
        sb_->Draw(*whiteTex_, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 64));
        sb_->End();

        Color px(0, 0, 0, 0);
        Rectangle reg(8, 8, 1, 1);
        dev.GetBackBufferData(&reg, &px, 0, 1);

        const bool ok = colourMatch(px, kRed, 10);
        std::printf("[%s] BlendState::Opaque ignores alpha, fully overwrites destination: got=(%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL", px.getRProperty(), px.getGProperty(), px.getBProperty());
        if (!ok) result_ = 1;

        Exit();
    }

public:
    SdlBlendStateOpaqueTest()
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
    SdlBlendStateOpaqueTest game;
    game.Run();
    return game.getResult();
}
