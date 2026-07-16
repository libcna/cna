// SPDX-License-Identifier: MS-PL
// Ad-hoc diagnostic (not a CTest): draws exactly one, non-rotated, non-flipped, full-texture
// sprite with NativeBackBuffer presentation (no logical scaling ambiguity) to isolate whether
// basic Texture2D sampling/UV mapping is correct, independent of viewport scaling, rotation,
// flip, or sampler-address-mode variation.
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::SdlGpu;

class DiagSingleSprite : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    int frame_ = 0;

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        std::vector<std::uint8_t> pixels(4 * 4 * 4, 0);
        auto setPixel = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            const std::size_t o = (static_cast<std::size_t>(y) * 4 + x) * 4;
            pixels[o+0]=r; pixels[o+1]=g; pixels[o+2]=b; pixels[o+3]=255;
        };
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
            {
                if (x<2 && y<2) setPixel(x,y,255,0,0);
                else if (x>=2 && y<2) setPixel(x,y,0,255,0);
                else if (x<2 && y>=2) setPixel(x,y,0,0,255);
                else setPixel(x,y,255,255,255);
            }
        texture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 4, 4, pixels));
        spriteBatch_ = std::make_unique<SpriteBatch>(dev);
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color::Black);
        spriteBatch_->Begin();
        spriteBatch_->Draw(*texture_, Rectangle(20, 20, 200, 200), Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->End();
        if (frame_ == 300)
            Exit();
    }

public:
    DiagSingleSprite()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(240);
        gdm_->setPreferredBackBufferHeightProperty(240);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    DiagSingleSprite game;
    game.Run();
    return 0;
}
