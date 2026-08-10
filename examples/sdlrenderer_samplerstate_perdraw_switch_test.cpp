// SPDX-License-Identifier: MS-PL
// Task 703: Verify per-draw sampler state changes take effect on the next SpriteBatch::Begin
// on SDL_Renderer.
//
// SdlSpriteBatchRenderer::SetSamplerFilter (called once per SpriteBatch::Begin(), from
// SpriteBatch.cpp's own Begin() sequence) stores its result into a single `scaleMode` member,
// which every Draw() overload re-applies via SDL_SetTextureScaleMode() immediately before
// rendering. This test proves that mechanism is genuinely LIVE per Begin() -- not applied once
// at construction/first-use and then stuck -- by drawing the SAME 2x1 (Red|Blue) texture 3 times
// in sequence, each via its own Begin()/Draw()/End() cycle, alternating SamplerState:
//   1. PointClamp  -> exact texel boundary must read a PURE colour (no blend).
//   2. LinearClamp -> the SAME boundary must now read a genuine BLEND.
//   3. PointClamp again -> must revert to PURE -- rules out a one-directional/sticky bug where
//      switching TO Linear works but switching BACK to Point does not.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = all 3 draws show the correct filter for their SamplerState, 1 = at least one does not.

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
    bool IsPure(const Color& c)
    {
        // Either near-pure Red or near-pure Blue -- Point filtering must never blend.
        const bool pureRed  = c.getRProperty() >= 230 && c.getBProperty() <= 25;
        const bool pureBlue = c.getBProperty() >= 230 && c.getRProperty() <= 25;
        return pureRed || pureBlue;
    }

    bool IsBlended(const Color& c)
    {
        return c.getRProperty() >= 90 && c.getRProperty() <= 165
            && c.getBProperty() >= 90 && c.getBProperty() <= 165;
    }
}

class SdlSamplerStatePerDrawSwitchTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             tex_; // 2x1: [Red, Blue]

    bool done_   = false;
    int  result_ = 0;

    Color DrawWithSamplerAndSampleBoundary(SamplerState* sampler)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 0, 0, 255));
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, sampler, nullptr, nullptr);
        // Exact-size sourceRectangle stretched across the full viewport width -- the boundary
        // between texel 0 (Red) and texel 1 (Blue) sits at destination x = W/2.
        sb_->Draw(*tex_, Rectangle(0, 0, W, H), Rectangle(0, 0, 2, 1), Color::White);
        sb_->End();

        Color pixel(0, 0, 0, 0);
        const Rectangle region(W / 2, H / 2, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    void check(bool ok, const char* label, Color got)
    {
        std::printf("[%s] %s: got=(%d,%d,%d)\n", ok ? "PASS" : "FAIL", label,
                    got.getRProperty(), got.getGProperty(), got.getBProperty());
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        const std::vector<std::uint8_t> px = {
            255,   0,   0, 255,   // texel 0: red
              0,   0, 255, 255    // texel 1: blue
        };
        tex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 2, 1, px));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        const Color point1  = DrawWithSamplerAndSampleBoundary(const_cast<SamplerState*>(&SamplerState::PointClamp));
        const Color linear  = DrawWithSamplerAndSampleBoundary(const_cast<SamplerState*>(&SamplerState::LinearClamp));
        const Color point2  = DrawWithSamplerAndSampleBoundary(const_cast<SamplerState*>(&SamplerState::PointClamp));

        check(IsPure(point1),    "1st Begin() with PointClamp: boundary reads a pure colour", point1);
        check(IsBlended(linear), "2nd Begin() with LinearClamp: boundary now reads a genuine blend", linear);
        check(IsPure(point2),    "3rd Begin() with PointClamp again: boundary reverts to a pure colour", point2);

        Exit();
    }

public:
    SdlSamplerStatePerDrawSwitchTest()
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
    SdlSamplerStatePerDrawSwitchTest game;
    game.Run();
    return game.getResult();
}
