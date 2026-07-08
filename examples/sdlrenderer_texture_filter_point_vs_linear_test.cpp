// SPDX-License-Identifier: MS-PL
// Task 688: Pixel test — TextureFilter::Point vs Linear via SDL_ScaleMode on SDL_Renderer.
// SpriteBatch-based counterpart to Task 297's 3D DualTextureEffect test
// (easygl_texture_filter_point_vs_linear_test.cpp), scoped to magnification (the natural
// SpriteBatch use case) as this task's own row specifically names "via SDL_ScaleMode" --
// the exact SDL3 API SdlSpriteBatchBackend::SetSamplerFilter maps TextureFilter onto
// (SDL_SetTextureScaleMode(SDL_SCALEMODE_NEAREST/LINEAR)).
//
// Draws a 2x1 (Red|Green) texture stretched wide via SpriteBatch, once with
// SamplerState::PointClamp and once with SamplerState::LinearClamp, then samples each at its
// texel boundary (the midpoint between the two texel centers, guaranteed not to coincide with
// either texel center):
//   Point:  a hard edge -- the sample must be a pure, unblended texel colour.
//   Linear: a genuine blend -- both R and G channels must be significant at that exact point.
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
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // True if exactly one of R/G is a pure, unblended channel value.
    bool IsPure(const Color& c)
    {
        const bool rHigh = c.getRProperty() >= 235, rLow = c.getRProperty() <= 20;
        const bool gHigh = c.getGProperty() >= 235, gLow = c.getGProperty() <= 20;
        return (rHigh && gLow) || (rLow && gHigh);
    }

    // True if both channels are in a genuine mid-range blend (~50/50 of 255).
    bool IsBlended(const Color& c)
    {
        return c.getRProperty() >= 90 && c.getRProperty() <= 165
            && c.getGProperty() >= 90 && c.getGProperty() <= 165;
    }
}

class SdlTextureFilterPointVsLinearTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             tex_; // 2x1: [Red, Green]

    bool done_   = false;
    int  result_ = 0;

    Color DrawAndSampleAtBoundary(SamplerState* sampler, int destX, int destW, int destH)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, sampler, nullptr, nullptr,
                   nullptr, Matrix::getIdentityProperty());
        sb_->Draw(*tex_, Rectangle(destX, 0, destW, destH), Rectangle(0, 0, 2, 1), Color::White);
        sb_->End();

        // Texel boundary is at the horizontal centre of the stretched quad.
        const Rectangle reg(destX + destW / 2, destH / 2, 1, 1);
        Color c(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &c, 0, 1);
        return c;
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

        const Color pointSample  = DrawAndSampleAtBoundary(const_cast<SamplerState*>(&SamplerState::PointClamp),  0, 64, 8);
        const Color linearSample = DrawAndSampleAtBoundary(const_cast<SamplerState*>(&SamplerState::LinearClamp), 0, 64, 8);

        const bool pointOk  = IsPure(pointSample);
        const bool linearOk = IsBlended(linearSample);

        std::printf("[%s] PointClamp  @ texel boundary: got=(%d,%d,%d) (expect pure)\n",
                    pointOk ? "PASS" : "FAIL",
                    pointSample.getRProperty(), pointSample.getGProperty(), pointSample.getBProperty());
        std::printf("[%s] LinearClamp @ texel boundary: got=(%d,%d,%d) (expect blend)\n",
                    linearOk ? "PASS" : "FAIL",
                    linearSample.getRProperty(), linearSample.getGProperty(), linearSample.getBProperty());

        if (!pointOk || !linearOk) result_ = 1;
        Exit();
    }

public:
    SdlTextureFilterPointVsLinearTest()
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
    SdlTextureFilterPointVsLinearTest game;
    game.Run();
    return game.getResult();
}
