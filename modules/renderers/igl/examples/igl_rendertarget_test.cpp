// SPDX-License-Identifier: MS-PL
// plan_igl.md IGL-24: off-screen rendering end to end -- a real RenderTarget2D with a real
// depth/stencil attachment, drawn into with SpriteBatch, read back with GetData(), and then sampled
// as an ordinary texture while drawing to the back buffer.
//
// The orientation checks are the point: this renderer draws off-screen targets with a flipped
// projection precisely so a target reads back and samples in the same orientation as an uploaded
// texture. A target whose Y was left unflipped passes the "centre" check and fails these.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kTargetSize = 32;

    [[nodiscard]] Color Rgba(const int r, const int g, const int b, const int a = 255)
    {
        return Color(static_cast<bytecs>(r), static_cast<bytecs>(g), static_cast<bytecs>(b),
                     static_cast<bytecs>(a));
    }

    [[nodiscard]] bool ColourMatches(const Color& got, const Color& want, const int tolerance = 4)
    {
        const auto close = [tolerance](const int a, const int b) {
            return (a > b ? a - b : b - a) <= tolerance;
        };
        return close(got.getRProperty(), want.getRProperty()) &&
               close(got.getGProperty(), want.getGProperty()) &&
               close(got.getBProperty(), want.getBProperty());
    }
}

class IglRenderTargetTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        auto white = std::make_unique<Texture2D>(device, 1, 1);
        const Color solid = Rgba(255, 255, 255);
        white->SetData(&solid, 1);

        auto target = std::make_unique<RenderTarget2D>(device, kTargetSize, kTargetSize, false,
                                                       SurfaceFormat::Color,
                                                       DepthFormat::Depth24Stencil8);
        if (!ExpectTrue("the render target was created", target != nullptr))
            return;
        ExpectTrue("the render target reports a real depth buffer",
                   target->getDepthStencilFormatProperty() == DepthFormat::Depth24Stencil8);

        // ---- Render into the target -----------------------------------------------------------
        device.SetRenderTarget(target.get());
        device.Clear(Rgba(0, 0, 255));

        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        // A yellow band across the TOP half of the target, in XNA's top-left origin.
        batch.Draw(*white, Rectangle(0, 0, kTargetSize, kTargetSize / 2), Rgba(255, 255, 0));
        batch.End();

        device.SetRenderTarget(nullptr);

        // ---- Read the target back --------------------------------------------------------------
        std::vector<Color> pixels(static_cast<std::size_t>(kTargetSize * kTargetSize));
        target->GetData(pixels.data(), 0, static_cast<int>(pixels.size()));

        const Color topRow = pixels[static_cast<std::size_t>(2) * kTargetSize + 2];
        const Color bottomRow =
            pixels[static_cast<std::size_t>(kTargetSize - 3) * kTargetSize + 2];
        ExpectTrue("GetData() returns the yellow band in the FIRST rows (top-first order)",
                   ColourMatches(topRow, Rgba(255, 255, 0)));
        ExpectTrue("GetData() returns the blue clear in the LAST rows",
                   ColourMatches(bottomRow, Rgba(0, 0, 255)));

        // ---- Sample the target while drawing to the back buffer --------------------------------
        device.Clear(Rgba(255, 0, 0));
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        batch.Draw(*target, Rectangle(0, 0, kSize, kSize), Rgba(255, 255, 255));
        batch.End();

        ExpectPixel("the sampled target keeps the yellow band at the top", Rectangle(32, 8, 1, 1),
                    Rgba(255, 255, 0), /*tolerance=*/4);
        ExpectPixel("the sampled target keeps the blue clear at the bottom", Rectangle(32, 56, 1, 1),
                    Rgba(0, 0, 255), /*tolerance=*/4);
    }

public:
    IglRenderTargetTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglRenderTargetTest>();
}
