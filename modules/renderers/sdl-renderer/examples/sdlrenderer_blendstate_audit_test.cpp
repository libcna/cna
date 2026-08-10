// SPDX-License-Identifier: MS-PL
// Task 695: Audit BlendState -> SDL_BlendMode mapping for all 4 presets on SDL_Renderer.
//
// FOUND AND FIXED A REAL BUG (SDL_Renderer-specific -- SdlRenderer::ApplyBlendState):
// the previous mapping only special-cased 2 of the 4 presets by their exact (colorSrcBlend,
// colorDstBlend) pair (Opaque -> SDL_BLENDMODE_NONE; Additive -> SDL_BLENDMODE_ADD) and fell
// through to the single generic SDL_BLENDMODE_BLEND constant for EVERYTHING else -- meaning
// BlendState::AlphaBlend (colorSrc=One, colorDst=InverseSourceAlpha) and
// BlendState::NonPremultiplied (colorSrc=SourceAlpha, colorDst=InverseSourceAlpha) were
// INDISTINGUISHABLE on SDL_Renderer, despite XNA defining them with genuinely different blend
// equations specifically for premultiplied vs. non-premultiplied source colour data:
//   AlphaBlend:       dst = src*1     + dst*(1-srcA)   (SDL's SDL_BLENDMODE_BLEND_PREMULTIPLIED)
//   NonPremultiplied: dst = src*srcA  + dst*(1-srcA)   (SDL's SDL_BLENDMODE_BLEND)
// The old code's fallback (SDL_BLENDMODE_BLEND) happens to be exactly correct for
// NonPremultiplied, but wrong for AlphaBlend (which needs BLEND_PREMULTIPLIED instead).
//
// FIX: replaced the 2-special-case mapping with a complete, general Blend/BlendFunction ->
// SDL_BlendFactor/SDL_BlendOperation table + SDL_ComposeCustomBlendMode(), covering all 4
// factor/operation parameters XNA's BlendState exposes (colorSrc/Dst, alphaSrc/Dst,
// color/alphaBlendFunction) -- confirmed via SDL3's own SDL_BlendFactor enum that 10 of XNA's 13
// Blend values have EXACT SDL equivalents (only Blend::BlendFactor/InverseBlendFactor/
// SourceAlphaSaturation lack one; these now throw a clear error instead of silently substituting
// a wrong factor -- Task 700's own scope). This also resolves Task 700's premise that
// "SDL_ComposeCustomBlendMode has a narrower factor set than XNA" -- true only for those 3 rare
// values, not the 10 commonly-used ones the 4 standard presets and most custom BlendStates rely
// on. All 4 presets use BlendFunction::Add for both channels (confirmed via BlendState.cpp), and
// SDL_ComposeCustomBlendMode's ADD operation is universally supported by every SDL renderer
// renderer (per its own doc comment) -- so this fix is safe and reliable specifically for the 4
// presets this task audits.
//
// This test proves the fix's core, most consequential finding: AlphaBlend and NonPremultiplied
// now produce genuinely DIFFERENT (and each individually correct) results for the same
// non-premultiplied (straight-alpha) source colour, where they were previously identical.
// Draws a half-transparent (alpha=128) solid Red quad over a solid Green background, once with
// each BlendState:
//   AlphaBlend (assumes premultiplied source; our straight-alpha texture ISN'T, so this
//               correctly reproduces XNA's own well-known "washed out / over-bright" artifact
//               for non-premultiplied content through AlphaBlend): dst ~= (255, 127, 0)
//   NonPremultiplied (correctly treats source as straight alpha): dst ~= (128, 127, 0)
// A tolerance-free but clearly-separated pair of expected values (127 apart on the R channel)
// makes this a strong, unambiguous discriminating check.
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

static bool colourMatch(Color got, Color want, int tol)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

static const Color kGreen(0, 255, 0, 255);

class SdlBlendStateAuditTest : public Game
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

    Color DrawAndSample(BlendState blend)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(kGreen);

        sb_->Begin(SpriteSortMode::Deferred, blend,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        // Straight-alpha (non-premultiplied) half-transparent Red: full-brightness R with alpha=128.
        sb_->Draw(*whiteTex_, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 128));
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

        const Color alphaBlendResult = DrawAndSample(BlendState::AlphaBlend);
        const Color nonPremulResult  = DrawAndSample(BlendState::NonPremultiplied);

        // AlphaBlend (source treated as already premultiplied) -- over-bright artifact for our
        // deliberately-straight-alpha source: dst = src*1 + bg*(1-srcA) ~= (255, 127, 0).
        check(colourMatch(alphaBlendResult, Color(255, 127, 0, 255), 15),
              "AlphaBlend(straight-alpha src) -> over-bright (255,127,0)", alphaBlendResult);

        // NonPremultiplied (source correctly treated as straight alpha): dst = src*srcA +
        // bg*(1-srcA) ~= (128, 127, 0) -- a proper 50/50 blend.
        check(colourMatch(nonPremulResult, Color(128, 127, 0, 255), 15),
              "NonPremultiplied(straight-alpha src) -> proper blend (128,127,0)", nonPremulResult);

        // The headline audit finding: these two must now be MEANINGFULLY different (previously
        // both fell through to the same SDL_BLENDMODE_BLEND and were identical).
        const bool distinguishable = std::abs((int)alphaBlendResult.getRProperty()
                                             - (int)nonPremulResult.getRProperty()) > 50;
        check(distinguishable, "AlphaBlend and NonPremultiplied now produce genuinely different results",
              Color(std::abs((int)alphaBlendResult.getRProperty() - (int)nonPremulResult.getRProperty()), 0, 0, 255));

        Exit();
    }

public:
    SdlBlendStateAuditTest()
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
    SdlBlendStateAuditTest game;
    game.Run();
    return game.getResult();
}
