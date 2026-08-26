// SPDX-License-Identifier: MS-PL
// WEBGPU-91/92: verify WebGPURenderer::ReadBackbuffer()/GraphicsDevice::GetBackBufferData()
// actually return the pixels that were just drawn, including within a single Draw() call with no
// intervening real Present() -- WebGPU's swapchain-backed render target means a naive
// implementation can only ever observe the *previous* frame's content (nothing is readable until
// the frame has been submitted), so ReadBackbuffer() renders any pending Clear()/SpriteBatch work
// on demand before mapping the readback buffer, matching the Vulkan/Bgfx renderers' own
// on-demand-submit readback semantics (see WebGPURenderer::EnsureFrameRendered()).
//
// Check A -- Clear(Red) is visible via GetBackBufferData() in the same Draw() call.
// Check B -- a second Clear(Blue) right after replaces it (no stale/cached frame served).
// Check C -- a SpriteBatch-drawn solid green quad is visible inside its destination rectangle...
// Check D -- ...and the still-black background outside that rectangle is unaffected (partial
//   coverage, not an accidental full-screen overwrite).
// Check E -- a fully-transparent (alpha=0) sprite must not modify the destination at all.
// Check F -- a sourceRectangle selecting only the right half of a 2x1 (red|blue) texture must
//   sample blue, not red or an unpredictable blend -- proves source-rectangle cropping, not just
//   whole-texture sampling.
// Check G -- a 50%-alpha sprite drawn with BlendState::NonPremultiplied over black must land
//   strictly between "fully transparent" (black) and "alpha ignored" (full brightness) -- proves
//   alpha genuinely attenuates colour. WEBGPU-141 (B): this MUST use NonPremultiplied
//   (SrcAlpha/InvSrcAlpha), the blend that attenuates a straight-alpha texture. SpriteBatch's DEFAULT
//   blend is AlphaBlend, which is PREMULTIPLIED in XNA/FNA (One/InverseSourceAlpha,
//   FNA/src/Graphics/States/BlendState.cs), and FNA's sprite shader outputs straight colour -- so a
//   straight-alpha texture under default AlphaBlend gives FULL brightness, not partway (WebGPU,
//   EasyGL and SOFTWARE all honour the device BlendState and agree). An earlier version of this check
//   used the default Begin() and expected partway, which asserted non-FNA behaviour; it passed only
//   while WEBGPU-132 hardcoded a straight sprite blend, and correctly began failing once
//   REMED-GFX-102 made WebGPU honour the BlendState. The exact partway value depends on linear-vs-sRGB
//   swapchain blending, so this asserts direction and magnitude, not an exact value.
//
// Checks H/I/J (WEBGPU-92's remaining scope) -- sampler address-mode (Wrap/Clamp/Mirror) pixel
//   assertions. A sourceRectangle wider than the texture (4 texels requested against a 2x1
//   red|blue texture) makes the sprite's own UV genuinely exceed 1.0 without any extra plumbing
//   (SpriteBatch never clamps UV to the source rectangle itself); TextureFilter=Point makes the
//   sampled colour a crisp single-texel lookup with no bilinear blur at the wrap/mirror seam, so
//   each check reads back an exact, deterministic colour instead of an ambiguous blended one.
// Check H -- AddressMode=Wrap at u~1.14: wraps back to the fractional part (0.14, inside the red
//   texel's [0, 0.5) range) -- samples red.
// Check I -- AddressMode=Clamp at the same u~1.14 (>=1.0): clamps to the last texel (blue) --
//   proves Wrap and Clamp are genuinely different, not both defaulting to the same behaviour.
// Check J -- AddressMode=Mirror at u~1.77: the [1,2] range is a flipped repeat of [0,1], so
//   u=1.77 mirrors to u'=0.23 (red) -- Wrap or Clamp at this same u would both sample blue
//   (u mod 1 = 0.77, or clamped to the last texel), so this uniquely discriminates Mirror.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool colorNear(Color a, Color b, int tol = 8)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    Color readPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }
}

class WebGpuClearReadbackTest : public Game
{
    static constexpr int kChecks = 10;

    std::unique_ptr<GraphicsDeviceManager> gdm_;
    SpriteBatch* spriteBatch_ = nullptr;
    Texture2D greenTex_;
    Texture2D redBlueTex_;
    bool done_ = false;
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
        spriteBatch_ = new SpriteBatch(getGraphicsDeviceProperty());
        // Color::Lime, not Color::Green (XNA's Color::Green is the darker (0,128,0) "web green").
        greenTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                 std::vector<std::uint8_t>{0, 255, 0, 255});
        // 2x1: left pixel red, right pixel blue -- for the source-rectangle crop check.
        redBlueTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 2, 1,
                                                   std::vector<std::uint8_t>{255, 0, 0, 255,
                                                                              0, 0, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // Check A: Clear(Red) observed without an intervening real Present().
        dev.Clear(Color::Red);
        check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::Red),
              "Clear(Red) visible via GetBackBufferData() in the same Draw() call");

        // Check B: a second Clear() right after replaces it -- no stale cached frame.
        dev.Clear(Color::Blue);
        check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::Blue),
              "Clear(Blue) right after Clear(Red) is observed, not the stale Red frame");

        // Check C/D: Clear(Black), draw a green quad over the left half only, then verify both
        // sides.
        dev.Clear(Color::Black);
        const Rectangle destination(0, 0, kSize / 2, kSize);
        spriteBatch_->Begin();
        spriteBatch_->Draw(greenTex_, destination, Color::White);
        spriteBatch_->End();

        check(colorNear(readPixel(dev, kSize / 4, kSize / 2), Color::Lime),
              "SpriteBatch-drawn green (Lime) quad is visible inside its destination rectangle");
        check(colorNear(readPixel(dev, kSize - kSize / 4, kSize / 2), Color::Black),
              "background outside the quad's destination rectangle stays black");

        // Check E: alpha=0 sprite must not modify the destination.
        dev.Clear(Color::White);
        spriteBatch_->Begin();
        spriteBatch_->Draw(greenTex_, Rectangle(0, 0, kSize, kSize), Color(255, 255, 255, 0));
        spriteBatch_->End();
        check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::White),
              "alpha=0 sprite leaves the destination unmodified");

        // Check F: sourceRectangle selecting only the right (blue) texel of a 2x1 texture.
        dev.Clear(Color::Black);
        const Rectangle rightTexel(1, 0, 1, 1);
        spriteBatch_->Begin();
        spriteBatch_->Draw(redBlueTex_, Rectangle(0, 0, kSize, kSize), rightTexel, Color::White);
        spriteBatch_->End();
        check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::Blue),
              "sourceRectangle selecting the right texel samples blue, not red");

        // Check G: 50%-alpha green sprite over black must land strictly between black and full
        // green -- proves alpha genuinely attenuates colour, not just an on/off gate. WEBGPU-141 (B):
        // this uses BlendState::NonPremultiplied (SrcAlpha/InvSrcAlpha), the blend that attenuates a
        // straight-alpha texture. The DEFAULT SpriteBatch blend is AlphaBlend, which is PREMULTIPLIED
        // in XNA/FNA (One/InverseSourceAlpha) -- a straight-alpha texture under it gives FULL
        // brightness, not partway (every renderer + FNA agree), so testing attenuation requires
        // NonPremultiplied here, not the default.
        dev.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied);
        spriteBatch_->Draw(greenTex_, Rectangle(0, 0, kSize, kSize), Color(255, 255, 255, 128));
        spriteBatch_->End();
        {
            Color c = readPixel(dev, kSize / 2, kSize / 2);
            const bool ok = c.getGProperty() >= 60 && c.getGProperty() <= 220 &&
                            c.getRProperty() <= 20 && c.getBProperty() <= 20;
            std::printf("    (50%%-alpha green over black -> R=%d G=%d B=%d)\n",
                        c.getRProperty(), c.getGProperty(), c.getBProperty());
            check(ok, "50%-alpha sprite blends partway between black and full colour");
        }

        // Checks H/I/J: sampler address-mode (Wrap/Clamp/Mirror). sourceRectangle is 4 texels wide
        // against a 2x1 texture, so u genuinely ranges over [0, 2) -- SpriteBatch never clamps UV
        // to the source rectangle, it just divides by the texture's real width.
        const Rectangle wideSource(0, 0, 4, 1);
        const Rectangle fullDestination(0, 0, kSize, kSize);

        // Check H: AddressMode=Wrap, sampled at x=36 (u ~= 1.14 -> wraps to 0.14, the red texel).
        {
            dev.Clear(Color::Black);
            SamplerState wrap;
            wrap.setFilterProperty(TextureFilter::Point);
            wrap.setAddressUProperty(TextureAddressMode::Wrap);
            wrap.setAddressVProperty(TextureAddressMode::Wrap);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &wrap, nullptr, nullptr);
            spriteBatch_->Draw(redBlueTex_, fullDestination, wideSource, Color::White);
            spriteBatch_->End();
            check(colorNear(readPixel(dev, 36, kSize / 2), Color::Red),
                  "AddressMode=Wrap at u~1.14 wraps back to the red texel");
        }

        // Check I: AddressMode=Clamp, same sample point (u ~= 1.14, past 1.0) -- clamps to the
        // last texel (blue), proving Wrap and Clamp are genuinely different sampler states.
        {
            dev.Clear(Color::Black);
            SamplerState clamp;
            clamp.setFilterProperty(TextureFilter::Point);
            clamp.setAddressUProperty(TextureAddressMode::Clamp);
            clamp.setAddressVProperty(TextureAddressMode::Clamp);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &clamp, nullptr, nullptr);
            spriteBatch_->Draw(redBlueTex_, fullDestination, wideSource, Color::White);
            spriteBatch_->End();
            check(colorNear(readPixel(dev, 36, kSize / 2), Color::Blue),
                  "AddressMode=Clamp at the same u~1.14 clamps to the last (blue) texel instead");
        }

        // Check J: AddressMode=Mirror at u~1.77 -- the [1,2] range mirrors [0,1], so this samples
        // red; Wrap or Clamp at the same u would both sample blue, uniquely discriminating Mirror.
        {
            dev.Clear(Color::Black);
            SamplerState mirror;
            mirror.setFilterProperty(TextureFilter::Point);
            mirror.setAddressUProperty(TextureAddressMode::Mirror);
            mirror.setAddressVProperty(TextureAddressMode::Mirror);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &mirror, nullptr, nullptr);
            spriteBatch_->Draw(redBlueTex_, fullDestination, wideSource, Color::White);
            spriteBatch_->End();
            check(colorNear(readPixel(dev, 56, kSize / 2), Color::Red),
                  "AddressMode=Mirror at u~1.77 mirrors back to the red texel (Wrap/Clamp would be blue)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = (passCount_ == kChecks) ? 0 : 1;
        Exit();
    }

public:
    WebGpuClearReadbackTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuClearReadbackTest game;
    game.Run();
    return game.getResult();
}
