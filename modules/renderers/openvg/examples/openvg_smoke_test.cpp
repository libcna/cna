// SPDX-License-Identifier: MS-PL
// Vertical-slice smoke test for the OPENVG (ShivaVG) renderer: selection -> initialization ->
// resource creation -> real rendering -> present -> readback -> cleanup, all through genuine
// `vg*` OpenVG entry points (see docs/openvg-renderer.md).
//
// 1. Clear() to a distinctive colour and read it back exactly.
// 2. Create a small solid-colour Texture2D, draw it with SpriteBatch at a known position with
//    BlendState.Opaque, and verify both the sprite's own pixel and a pixel outside it (still the
//    clear colour, proving the draw did not paint the whole surface).
//
// Exit code 0 = all PASS, 1 = at least one FAIL, 77 = SKIPPED (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 128;
    constexpr int kHeight = 96;
}

class OpenVgSmokeTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> sprite_;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        const Color green(0, 255, 0, 255);
        std::vector<uint8_t> pixels(16 * 16 * 4);
        for (std::size_t i = 0; i < pixels.size(); i += 4)
        {
            pixels[i + 0] = green.getRProperty();
            pixels[i + 1] = green.getGProperty();
            pixels[i + 2] = green.getBProperty();
            pixels[i + 3] = green.getAProperty();
        }
        sprite_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 16, 16, pixels));
    }

    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        const Color clearColor(20, 40, 60, 255);
        dev.Clear(clearColor);

        ExpectPixel("clear-color", Rectangle(4, 4, 1, 1), clearColor);

        SpriteBatch sb(dev);
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr, nullptr,
                Matrix::getIdentityProperty());
        sb.Draw(*sprite_, Rectangle(40, 30, 16, 16), Rectangle(0, 0, 16, 16), Color::White);
        sb.End();

        ExpectPixel("sprite-interior", Rectangle(47, 37, 1, 1), Color(0, 255, 0, 255));
        ExpectPixel("outside-sprite-still-clear", Rectangle(10, 10, 1, 1), clearColor);
    }

public:
    OpenVgSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kWidth);
        gdm_->setPreferredBackBufferHeightProperty(kHeight);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenVgSmokeTest>();
}
