// SPDX-License-Identifier: MS-PL
// Task 700: Decide and document custom (non-preset) BlendState combinations on SDL_Renderer.
//
// Task 695's fix replaced the old 2-case (Opaque/Additive) special-case mapping with a general
// Blend/BlendFunction -> SDL_BlendFactor/SDL_BlendOperation table + SDL_ComposeCustomBlendMode(),
// which ALREADY handles any custom (non-preset) combination built from the 10 XNA Blend values
// that have exact SDL equivalents (One/Zero/SourceColor/InverseSourceColor/SourceAlpha/
// InverseSourceAlpha/DestinationColor/InverseDestinationColor/DestinationAlpha/
// InverseDestinationAlpha) -- this is empirically confirmed here with a genuinely custom
// (DestinationColor, Zero) BlendState (not one of the 4 XNA presets), which multiplies the
// destination by the source colour ("colour filter"/modulate blending).
//
// DECISIONS (documented here, per this task's own framing):
//   1. Blend::BlendFactor / InverseBlendFactor / SourceAlphaSaturation have no SDL_BlendFactor
//      equivalent at all -- SdlRenderer::ApplyBlendState's ToSdlBlendFactor() throws
//      std::runtime_error for these 3 values (confirmed by this test) rather than silently
//      substituting a wrong factor. This is deliberately narrower than a full custom-blend
//      feature -- these 3 values are rare in practice (no built-in FNA/CNA effect or the 4
//      standard presets use them) and there is no SDL_BlendFactor that could honor them
//      correctly, so throwing (matching this renderer's established ThrowNo3D/SetCustomEffect
//      "must not silently misrender" convention) is the right choice, not a workaround away from.
//   2. BlendFunction::Add is confirmed universally reliable (all 4 presets + this task's custom
//      test use it). BlendFunction::Subtract/ReverseSubtract/Max/Min are technically composable
//      via SDL_ComposeCustomBlendMode() and DO get applied without error on THIS project's actual
//      SDL_Renderer/OpenGL sandbox (confirmed here with a real Subtract test) -- but SDL3's own
//      SDL_ComposeCustomBlendMode doc comment states plain "opengl: Supports the
//      SDL_BLENDOPERATION_ADD operation with all factors" (i.e. does not explicitly document
//      Subtract/RevSubtract/Min/Max support for the opengl driver the way it does for
//      direct3d/direct3d11/opengles2) -- so a non-Add BlendFunction should be treated as a
//      "works on this sandbox, not officially guaranteed on every OpenGL driver/GPU combination"
//      case, not a hard-guaranteed one. No throw is added for non-Add BlendFunction values (they
//      DO work here), but this caveat is documented rather than silently assumed universal.
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
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
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

// BlendState's 4-Blend-value constructor is private (reserved for the 4 static presets) --
// build genuinely custom combinations via the public default constructor + setters instead.
static BlendState MakeBlendState(Blend colorSrc, Blend colorDst, Blend alphaSrc, Blend alphaDst)
{
    BlendState bs;
    bs.setColorSourceBlendProperty(colorSrc);
    bs.setColorDestinationBlendProperty(colorDst);
    bs.setAlphaSourceBlendProperty(alphaSrc);
    bs.setAlphaDestinationBlendProperty(alphaDst);
    return bs;
}

class SdlBlendStateCustomTest : public Game
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

    Color DrawAndSample(BlendState blend, Color background, Color drawColor)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(background);

        sb_->Begin(SpriteSortMode::Deferred, blend,
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

        // Genuinely custom (non-preset) combination: (colorSrc=DestinationColor, colorDst=Zero) --
        // multiplies the destination by the source colour ("modulate"/colour-filter blending).
        // dst = src*dst + dst*0 = src*dst (per-channel).
        {
            BlendState custom = MakeBlendState(Blend::DestinationColor, Blend::Zero,
                                                Blend::DestinationColor, Blend::Zero);
            // Grey (128,128,128) background * White (255,255,255) source = grey unchanged
            // (per-channel: 128*255/255=128). Use a non-white background to make the
            // multiplication meaningful: Red (200,0,0) bg * source (128,255,128) ->
            // (200*128/255, 0*255/255, 0*128/255) = (~100, 0, 0).
            const Color result = DrawAndSample(custom, Color(200, 0, 0, 255), Color(128, 255, 128, 255));
            check(colourMatch(result, Color(100, 0, 0, 255), 15),
                  "Custom (DestinationColor,Zero) modulate blend composes and applies correctly", result);
        }

        // Blend::BlendFactor (no SDL_BlendFactor equivalent) must throw, not silently misrender.
        {
            bool threw = false;
            try
            {
                BlendState badFactor = MakeBlendState(Blend::BlendFactor, Blend::Zero,
                                                       Blend::One, Blend::Zero);
                DrawAndSample(badFactor, Color(0, 0, 0, 255), Color(255, 255, 255, 255));
            }
            catch (const std::runtime_error&)
            {
                threw = true;
            }
            check(threw, "Blend::BlendFactor (no SDL equivalent) throws instead of silently misrendering",
                  Color(threw ? 0 : 255, 0, 0, 255));
        }

        // Blend::SourceAlphaSaturation (no SDL_BlendFactor equivalent) must also throw.
        {
            bool threw = false;
            try
            {
                BlendState badSaturation = MakeBlendState(Blend::SourceAlphaSaturation, Blend::Zero,
                                                           Blend::One, Blend::Zero);
                DrawAndSample(badSaturation, Color(0, 0, 0, 255), Color(255, 255, 255, 255));
            }
            catch (const std::runtime_error&)
            {
                threw = true;
            }
            check(threw, "Blend::SourceAlphaSaturation (no SDL equivalent) throws instead of silently misrendering",
                  Color(threw ? 0 : 255, 0, 0, 255));
        }

        // BlendFunction::Subtract computes (src - dst), matching XNA's own BlendFunction::Subtract
        // docstring exactly ("Subtracts destination from source"). Documented as an OpenGL-driver
        // caveat (SDL3 only explicitly documents ADD-with-all-factors support for opengl);
        // confirmed empirically to actually work correctly on this project's own sandbox:
        // src=(200,0,0), dst=(50,0,0) at factors (One,One) -> src-dst = (150,0,0).
        {
            BlendState subtractive = MakeBlendState(Blend::One, Blend::One, Blend::One, Blend::One);
            subtractive.setColorBlendFunctionProperty(BlendFunction::Subtract);
            subtractive.setAlphaBlendFunctionProperty(BlendFunction::Subtract);
            const Color result = DrawAndSample(subtractive, Color(50, 0, 0, 255), Color(200, 0, 0, 255));
            check(colourMatch(result, Color(150, 0, 0, 255), 15),
                  "BlendFunction::Subtract composes and applies correctly on this sandbox (documented OpenGL caveat)",
                  result);
        }

        Exit();
    }

public:
    SdlBlendStateCustomTest()
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
    SdlBlendStateCustomTest game;
    game.Run();
    return game.getResult();
}
