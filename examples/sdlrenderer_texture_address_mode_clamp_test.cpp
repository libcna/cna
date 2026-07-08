// SPDX-License-Identifier: MS-PL
// Task 685: Pixel test — TextureAddressMode::Clamp via SpriteBatch on SDL_Renderer.
// Direct port of Task 269's EasyGL test (easygl_texture_address_mode_test.cpp).
//
// SpriteBatch.cpp calls backend_->SetSamplerAddressMode(addressU, addressV) once per Begin().
// ISpriteBatchBackend::SetSamplerAddressMode defaults to a silent no-op, and SdlSpriteBatchBackend
// never overrode it. Investigated fixing this via SDL3's real SDL_SetRenderTextureAddressMode
// (renderer, uMode, vMode) API -- but its own doc comment says it configures addressing "used in
// SDL_RenderGeometry()" specifically; SdlSpriteBatchBackend::Draw uses SDL_RenderTexture /
// SDL_RenderTextureRotated / SDL_RenderTextureAffine instead (texel-space sourceRectangle, not
// raw UV geometry), which are UNAFFECTED by it -- confirmed empirically: wiring up the override
// and calling it produced byte-for-byte identical results to not calling it at all.
//
// So for THIS Draw() implementation, TextureAddressMode::Clamp/Wrap/Mirror is not actually
// selectable at all -- SDL_RenderTexture's srcrect handling has ONE fixed behavior regardless of
// requested SamplerState. Empirically, that fixed behavior already matches Clamp semantics
// (out-of-bounds source-rect sampling clamps to the texture's edge texel, not wrap or mirror) --
// this task's actual finding: TextureAddressMode::Clamp already produces the correct result on
// SDL_Renderer, but as an accidental side effect of SDL_RenderTexture's fixed edge-clamping
// behavior, not because any SamplerState is genuinely being honored.
//
// TextureAddressMode::Wrap is consequently NOT correctly honored (same fixed clamp-like behavior
// applies regardless of the Wrap request) -- a real, separate, already-tracked gap for Task 686
// to resolve (its own row already anticipated needing an emulate-via-tiled-draws-or-throw
// decision; confirmed here that a native SDL_Renderer fix isn't available for this Draw() path).
// This test does not assert on Wrap -- only prints it for context -- since fixing it is Task
// 686's job, not this one.
//
// Draws a 2x1 (Red|Blue) texture via SpriteBatch with a sourceRectangle twice the texture's
// width, so the sampled source-rect spans texel range [0,4] on a 2-texel-wide texture --
// exercising sampling past the texture edge, the classic XNA scrolling/tiling-background
// technique (FNA's real SpriteBatch never clamps sourceRectangle to the texture's own bounds).
// Reads back the pixel at the destination x-fraction corresponding to source position 1.25:
//   - PointClamp: clamps to the texture's last texel -> Blue. (This task's actual subject --
//                 confirmed correct with zero production code changes.)
//   - PointWrap:  printed for context only, not asserted -- currently also produces Blue instead
//                 of the XNA-correct Red (Task 686's open gap, not fixed here).
// Point filtering keeps samples crisp (no bilinear blending) for exact comparison.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = PointClamp check passes, 1 = it fails.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
}

class SdlTextureAddressModeClampTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             tex_; // 2x1: [Red, Blue]

    bool done_   = false;
    int  result_ = 0;

    Color SampleAtUOnePointTwoFive(SamplerState* sampler)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 0, 0, 255));
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, sampler, nullptr, nullptr);
        sb_->Draw(*tex_, Rectangle(0, 0, W, H), Rectangle(0, 0, 4, 1), Color::White);
        sb_->End();

        const Rectangle region(W * 5 / 8, H / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);

        const std::vector<std::uint8_t> px = {
            kRed.getRProperty(),  kRed.getGProperty(),  kRed.getBProperty(),  kRed.getAProperty(),
            kBlue.getRProperty(), kBlue.getGProperty(), kBlue.getBProperty(), kBlue.getAProperty(),
        };
        tex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 2, 1, px));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        const Color wrapPixel  = SampleAtUOnePointTwoFive(const_cast<SamplerState*>(&SamplerState::PointWrap));
        const Color clampPixel = SampleAtUOnePointTwoFive(const_cast<SamplerState*>(&SamplerState::PointClamp));

        const bool wrapMatchesXna  = (wrapPixel.getRProperty()  == 255 && wrapPixel.getBProperty()  == 0);
        const bool clampPass       = (clampPixel.getRProperty() == 0   && clampPixel.getBProperty() == 255);

        // Informational only -- Task 686's open gap, not asserted here.
        std::printf("[INFO] PointWrap  @ src-x=1.25: got=(%d,%d,%d) XNA wants=(255,0,0) (%s -- Task 686, not this task)\n",
                    wrapPixel.getRProperty(), wrapPixel.getGProperty(), wrapPixel.getBProperty(),
                    wrapMatchesXna ? "matches" : "known gap, does not yet match");
        std::printf("[%s] PointClamp @ src-x=1.25: got=(%d,%d,%d) want=(0,0,255)\n",
                    clampPass ? "PASS" : "FAIL",
                    clampPixel.getRProperty(), clampPixel.getGProperty(), clampPixel.getBProperty());

        if (!clampPass) result_ = 1;
        Exit();
    }

public:
    SdlTextureAddressModeClampTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(16);
        gdm_->setPreferredBackBufferHeightProperty(4);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlTextureAddressModeClampTest game;
    game.Run();
    return game.getResult();
}
