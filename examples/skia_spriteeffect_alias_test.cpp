// SPDX-License-Identifier: MS-PL
// SKIA-90: explicit stock SpriteEffect must be a pixel-exact alias of the built-in Skia path.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 18;
    constexpr int kHeight = 14;

    class DerivedSpriteEffect final : public SpriteEffect
    {
    public:
        explicit DerivedSpriteEffect(GraphicsDevice& device) : SpriteEffect(device) {}
    };
}

class SkiaSpriteEffectAliasTest final : public Game
{
public:
    SkiaSpriteEffectAliasTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kWidth);
        graphics_->setPreferredBackBufferHeightProperty(kHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        const std::vector<std::uint8_t> pixels{
            255,   0,   0, 255,   0, 255,   0, 255,
              0,   0, 255, 255, 255, 255,   0, 255,
        };
        texture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(device, 2, 2, pixels));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D defaultTarget(
            device, kWidth, kHeight, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        RenderTarget2D stockTarget(
            device, kWidth, kHeight, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        RenderTarget2D cloneTarget(
            device, kWidth, kHeight, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

        SpriteEffect stock(device);
        std::unique_ptr<Effect> clone(stock.Clone());
        Render(defaultTarget, nullptr);
        Render(stockTarget, &stock);
        Render(cloneTarget, clone.get());

        const auto defaultPixels = Read(defaultTarget);
        const auto stockPixels = Read(stockTarget);
        const auto clonePixels = Read(cloneTarget);
        Check(defaultPixels == stockPixels,
              "explicit exact SpriteEffect matches every default-path target pixel");
        Check(defaultPixels == clonePixels,
              "a cloned exact SpriteEffect remains the same stock alias");

        bool hasDrawnPixel = false;
        bool hasClearPixel = false;
        for (const Color& pixel : defaultPixels)
        {
            hasDrawnPixel = hasDrawnPixel || pixel != Color(17, 29, 43, 255);
            hasClearPixel = hasClearPixel || pixel == Color(17, 29, 43, 255);
        }
        Check(hasDrawnPixel && hasClearPixel,
              "comparison oracle contains both transformed sprite and untouched clear pixels");

        DerivedSpriteEffect derived(device);
        bool derivedRejected = false;
        std::string error;
        try
        {
            spriteBatch_->Begin(
                SpriteSortMode::Deferred, BlendState::Opaque,
                const_cast<SamplerState*>(&SamplerState::PointClamp),
                nullptr, nullptr, &derived, Matrix::getIdentityProperty());
        }
        catch (const std::exception& exception)
        {
            derivedRejected = true;
            error = exception.what();
        }
        Check(derivedRejected && error.find("exact stock SpriteEffect") != std::string::npos,
              "derived SpriteEffect rejects instead of losing custom behavior");

        // Begin did not publish a begun session when the renderer rejected the effect.
        device.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        spriteBatch_->Draw(
            *texture_, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        Color corner = Color::Transparent;
        const Rectangle cornerRegion(1, 1, 1, 1);
        device.GetBackBufferData(&cornerRegion, &corner, 0, 1);
        Check(corner == Color(255, 0, 0, 255),
              "batch remains reusable after rejecting a derived effect");

        std::printf("=== %d checks, %d failures ===\n", checks_, failures_);
        Exit();
    }

private:
    void Render(RenderTarget2D& target, Effect* effect)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(&target);
        device.Clear(Color(17, 29, 43, 255));
        spriteBatch_->Begin(
            SpriteSortMode::Deferred, BlendState::Opaque,
            const_cast<SamplerState*>(&SamplerState::PointClamp),
            nullptr, nullptr, effect, Matrix::CreateTranslation(1.0f, -1.0f, 0.0f));
        spriteBatch_->Draw(
            *texture_, Rectangle(5, 4, 9, 7), Rectangle(0, 0, 2, 2),
            Color(211, 181, 151, 255), 0.31f, Vector2(1.0f, 1.0f),
            SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically, 0.0f);
        spriteBatch_->End();
        device.SetRenderTarget(nullptr);
    }

    static std::vector<Color> Read(RenderTarget2D& target)
    {
        std::vector<Color> pixels(kWidth * kHeight, Color::Transparent);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels;
    }

    void Check(bool condition, const char* label)
    {
        ++checks_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaSpriteEffectAliasTest game;
    game.Run();
    return game.Result();
}
