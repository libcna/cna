// SPDX-License-Identifier: MS-PL
// Pixel contract for SKIA-44: Wrap, Clamp, and Mirror address texture axes independently.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    const Color kBlack(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kYellow(255, 255, 0, 255);

    [[nodiscard]] bool Same(Color left, Color right)
    {
        return left.getRProperty() == right.getRProperty()
            && left.getGProperty() == right.getGProperty()
            && left.getBProperty() == right.getBProperty()
            && left.getAProperty() == right.getAProperty();
    }

    [[nodiscard]] SamplerState MakePointSampler(TextureAddressMode u, TextureAddressMode v)
    {
        SamplerState result;
        result.setFilterProperty(TextureFilter::Point);
        result.setAddressUProperty(u);
        result.setAddressVProperty(v);
        return result;
    }
}

class SkiaTextureAddressAxesTest final : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);

        horizontal_ = std::make_unique<Texture2D>(device, 2, 1);
        const Color horizontal[] {kRed, kBlue};
        horizontal_->SetData(horizontal, 2);

        vertical_ = std::make_unique<Texture2D>(device, 1, 2);
        const Color vertical[] {kRed, kBlue};
        vertical_->SetData(vertical, 2);

        grid_ = std::make_unique<Texture2D>(device, 2, 2);
        const Color grid[] {kRed, kGreen, kBlue, kYellow};
        grid_->SetData(grid, 4);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        const Rectangle fullDestination(0, 0, kSize, kSize);

        // U crosses the right edge; Wrap reaches red again while Clamp remains blue.
        Check(Sample(*horizontal_, MakePointSampler(TextureAddressMode::Wrap, TextureAddressMode::Clamp),
                     fullDestination, Rectangle(0, 0, 4, 1), 36, 32),
              kRed, "U Wrap repeats the first texel after a positive source overflow");
        Check(Sample(*horizontal_, MakePointSampler(TextureAddressMode::Clamp, TextureAddressMode::Clamp),
                     fullDestination, Rectangle(0, 0, 4, 1), 36, 32),
              kBlue, "U Clamp holds the final texel after the same positive overflow");

        // The negative source coordinate is observable: -0.5 wraps to the blue half but clamps
        // to red. This rejects an implementation that handles only positive overflows.
        Check(Sample(*horizontal_, MakePointSampler(TextureAddressMode::Wrap, TextureAddressMode::Clamp),
                     fullDestination, Rectangle(-2, 0, 4, 1), 24, 32),
              kBlue, "U Wrap handles a negative source coordinate");
        Check(Sample(*horizontal_, MakePointSampler(TextureAddressMode::Clamp, TextureAddressMode::Clamp),
                     fullDestination, Rectangle(-2, 0, 4, 1), 24, 32),
              kRed, "U Clamp handles the same negative source coordinate");

        // V mirror reaches the first texel in the reflected [1,2] repeat rather than wrapping to
        // the second texel or clamping at the bottom edge.
        Check(Sample(*vertical_, MakePointSampler(TextureAddressMode::Clamp, TextureAddressMode::Mirror),
                     fullDestination, Rectangle(0, 0, 1, 4), 32, 56),
              kRed, "V Mirror reflects the second repeat independently of U");

        // Distinct grid corners prove address modes are independent per axis: (u,v) outside both
        // dimensions resolves to blue for Wrap/Clamp and green for Clamp/Wrap.
        Check(Sample(*grid_, MakePointSampler(TextureAddressMode::Wrap, TextureAddressMode::Clamp),
                     fullDestination, Rectangle(0, 0, 4, 4), 36, 36),
              kBlue, "mixed U Wrap and V Clamp resolve their own grid axes");
        Check(Sample(*grid_, MakePointSampler(TextureAddressMode::Clamp, TextureAddressMode::Wrap),
                     fullDestination, Rectangle(0, 0, 4, 4), 36, 36),
              kGreen, "mixed U Clamp and V Wrap resolve their own grid axes");
        Exit();
    }

public:
    SkiaTextureAddressAxesTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kSize);
        graphics_->setPreferredBackBufferHeightProperty(kSize);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

private:
    Color Sample(Texture2D& texture, SamplerState sampler, Rectangle destination, Rectangle source,
                 int sampleX, int sampleY)
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(kBlack);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr);
        spriteBatch_->Draw(texture, destination, source, Color::White);
        spriteBatch_->End();

        Color result(0, 0, 0, 0);
        const Rectangle pixel(sampleX, sampleY, 1, 1);
        device.GetBackBufferData(&pixel, &result, 0, 1);
        return result;
    }

    void Check(Color actual, Color expected, const char* label)
    {
        const bool pass = Same(actual, expected);
        std::printf("[%s] %s: got=(%d,%d,%d)\n", pass ? "PASS" : "FAIL", label,
                    actual.getRProperty(), actual.getGProperty(), actual.getBProperty());
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> horizontal_;
    std::unique_ptr<Texture2D> vertical_;
    std::unique_ptr<Texture2D> grid_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaTextureAddressAxesTest game;
    game.Run();
    return game.Result();
}
