// SPDX-License-Identifier: MS-PL
// Pixel oracle for SKIA-33: tint must multiply each source-alpha convention before compositing.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cmath>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBlue(0, 0, 255, 255);
    // Multiplies red and alpha by about one half.  Its alpha is intentionally not opaque: an
    // implementation that tints after Skia has interpreted the source alpha gets a distinct error.
    const Color kHalfRedTint(128, 255, 255, 128);

    [[nodiscard]] bool Matches(Color got, Color want, int tolerance = 5)
    {
        return std::abs(static_cast<int>(got.getRProperty()) - static_cast<int>(want.getRProperty())) <= tolerance
            && std::abs(static_cast<int>(got.getGProperty()) - static_cast<int>(want.getGProperty())) <= tolerance
            && std::abs(static_cast<int>(got.getBProperty()) - static_cast<int>(want.getBProperty())) <= tolerance
            && std::abs(static_cast<int>(got.getAProperty()) - static_cast<int>(want.getAProperty())) <= tolerance;
    }
}

class SkiaSpriteBatchTintAlphaTest final : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);

        premultipliedTexture_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color premultipliedRed(128, 0, 0, 128);
        premultipliedTexture_->SetData(&premultipliedRed, 1);

        straightTexture_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color straightRed(255, 0, 0, 128);
        straightTexture_->SetData(&straightRed, 1);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        const Rectangle fullScreen(0, 0, 16, 16);
        const Rectangle source(0, 0, 1, 1);
        auto drawAndRead = [&](Texture2D& texture, BlendState state)
        {
            device.Clear(kBlue);
            spriteBatch_->Begin(SpriteSortMode::Deferred, state,
                                const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                                nullptr, Matrix::getIdentityProperty());
            spriteBatch_->Draw(texture, fullScreen, source, kHalfRedTint);
            spriteBatch_->End();

            Color pixel(0, 0, 0, 0);
            const Rectangle centre(8, 8, 1, 1);
            device.GetBackBufferData(&centre, &pixel, 0, 1);
            return pixel;
        };

        // Premultiplied source (128,0,0,128) multiplied by tint (128,255,255,128) becomes
        // (64,0,0,64).  AlphaBlend then gives (64,0,191,255) over opaque blue.
        const Color premultiplied = drawAndRead(*premultipliedTexture_, BlendState::AlphaBlend);
        Check(Matches(premultiplied, Color(64, 0, 191, 255)),
              "AlphaBlend premultiplied source with semi-transparent tint", premultiplied);

        // Straight source (255,0,0,128) gets the same tint before NonPremultiplied's source-alpha
        // factor: (128,0,0,64) * (64/255) over blue -> (32,0,191,255).
        const Color straight = drawAndRead(*straightTexture_, BlendState::NonPremultiplied);
        Check(Matches(straight, Color(32, 0, 191, 255)),
              "NonPremultiplied straight source with semi-transparent tint", straight);

        Exit();
    }

public:
    SkiaSpriteBatchTintAlphaTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(16);
        graphics_->setPreferredBackBufferHeightProperty(16);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return result_; }

private:
    void Check(bool ok, const char* label, Color got)
    {
        std::printf("[%s] %s: got=(%d,%d,%d,%d)\n", ok ? "PASS" : "FAIL", label,
                    got.getRProperty(), got.getGProperty(), got.getBProperty(), got.getAProperty());
        if (!ok)
            result_ = 1;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> premultipliedTexture_;
    std::unique_ptr<Texture2D> straightTexture_;
    bool finished_ = false;
    int result_ = 0;
};

int main()
{
    SkiaSpriteBatchTintAlphaTest game;
    game.Run();
    return game.Result();
}
