// SPDX-License-Identifier: MS-PL
// Task 699: Pixel test — BlendState::Additive saturation behavior on SDL_Renderer.
//
// BlendState::Additive uses factors (colorSrc=SourceAlpha, colorDst=One) -- i.e.
// dst = src*srcA + dst*1, adding the (alpha-scaled) source colour on top of the destination
// rather than blending/replacing it. Confirmed correct by Task 695's own audit (`Additive` was
// one of the 2 presets the old, incomplete mapping already special-cased correctly via
// SDL_BLENDMODE_ADD, and remains correctly mapped under the new general
// SDL_ComposeCustomBlendMode-based mapping too) -- this is a dedicated, focused verification.
//
// Two checks:
//   1. Non-saturating: half-alpha (128/255) Red (100,0,0) drawn over a dim Red (50,0,0)
//      background -- dst = (100,0,0)*(128/255) + (50,0,0) ≈ (100,0,0), confirming the
//      SourceAlpha colour-scaling and the additive combination both work correctly when the
//      result stays within the representable [0,255] range.
//   2. Saturating: full-alpha, bright Red (200,0,0) drawn over an already-bright Red (200,0,0)
//      background -- dst = (200,0,0)*1 + (200,0,0) = (400,0,0) mathematically, which must
//      CLAMP/saturate to (255,0,0), not wrap around or otherwise misbehave -- the specific
//      "saturation behavior" this task's own row names.
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

class SdlBlendStateAdditiveTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             whiteTex_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label, Color got)
    {
        std::printf("[%s] %s: got=(%d,%d,%d)\n", ok ? "PASS" : "FAIL", label,
                    got.getRProperty(), got.getGProperty(), got.getBProperty());
        if (!ok) result_ = 1;
    }

    Color DrawAndSample(Color background, Color drawColor)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(background);

        sb_->Begin(SpriteSortMode::Deferred, BlendState::Additive,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        sb_->Draw(*whiteTex_, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 1, 1), drawColor);
        sb_->End();

        Color px(0, 0, 0, 0);
        Rectangle reg(8, 8, 1, 1);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

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

        const Color nonSaturating = DrawAndSample(Color(50, 0, 0, 255), Color(100, 0, 0, 128));
        check(colourMatch(nonSaturating, Color(100, 0, 0, 255), 15),
              "Additive non-saturating: half-alpha src + dim bg -> proper sum (~100,0,0)", nonSaturating);

        const Color saturating = DrawAndSample(Color(200, 0, 0, 255), Color(200, 0, 0, 255));
        check(colourMatch(saturating, Color(255, 0, 0, 255), 5),
              "Additive saturating: bright src + bright bg -> clamps to (255,0,0), no wraparound", saturating);

        Exit();
    }

public:
    SdlBlendStateAdditiveTest()
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
    SdlBlendStateAdditiveTest game;
    game.Run();
    return game.getResult();
}
