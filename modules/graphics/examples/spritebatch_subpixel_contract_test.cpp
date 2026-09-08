// SPDX-License-Identifier: MS-PL
//
// SOFTWARE-137: renderer-independent XNA SpriteBatch sub-pixel destination contract.
//
// XNA/FNA keep Vector2 positions and scaled destination sizes as floats until vertex generation.
// Each direct-float draw below is therefore required to produce the exact same render-target bytes
// as an equivalent integer draw followed by SpriteBatch.Begin's transform matrix. This comparison
// needs no renderer-specific golden image and no tolerance: both legs enter the same rasterizer with
// mathematically identical dyadic coordinates. A backend that inherits ISpriteBatchRenderer's
// compatibility fallback truncates the direct leg and fails, while a real float implementation
// makes the images byte-identical.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 32;
    constexpr int kHeight = 24;
    const Color kClear(13, 27, 39, 255);

    bool SameColor(const Color& a, const Color& b)
    {
        return a.getPackedValueProperty() == b.getPackedValueProperty();
    }
}

class SpriteBatchSubpixelContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<Texture2D> fontTexture_;
    std::unique_ptr<SpriteFont> font_;
    bool done_ = false;
    int passed_ = 0;
    int total_ = 0;

    void Check(bool condition, const std::string& label)
    {
        ++total_;
        if (condition) ++passed_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
    }

    std::vector<Color> Render(const Matrix& transform,
                              const std::function<void(SpriteBatch&)>& draw)
    {
        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, kWidth, kHeight, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&target);
        device.setViewportProperty(Viewport(0, 0, kWidth, kHeight));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.Clear(kClear);

        SpriteBatch batch(device);
        const SamplerState sampler = SamplerState::PointClamp;
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler,
                    &DepthStencilState::None, &RasterizerState::CullCounterClockwise,
                    nullptr, transform);
        draw(batch);
        batch.End();

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> result(static_cast<std::size_t>(kWidth * kHeight));
        target.GetData(result.data(), 0, static_cast<int>(result.size()));
        return result;
    }

    void CheckEquivalent(const std::string& label,
                         const std::vector<Color>& direct,
                         const std::vector<Color>& transformed)
    {
        int different = 0;
        int drawn = 0;
        for (std::size_t i = 0; i < direct.size(); ++i)
        {
            if (!SameColor(direct[i], transformed[i])) ++different;
            if (!SameColor(direct[i], kClear)) ++drawn;
        }
        Check(drawn > 0, label + ": direct leg rasterizes visible pixels");
        Check(different == 0,
              label + ": direct floats equal the transform oracle byte-for-byte (different=" +
                  std::to_string(different) + ")");
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        texture_ = std::make_unique<Texture2D>(device, 4, 4);
        const std::vector<Color> pixels{
            Color::Red, Color::Green, Color::Blue, Color::White,
            Color::Yellow, Color::Magenta, Color::Cyan, Color(80, 120, 160, 255),
            Color(10, 200, 30, 255), Color(220, 40, 90, 255),
            Color(50, 70, 210, 255), Color(180, 130, 20, 255),
            Color(15, 35, 55, 255), Color(75, 95, 115, 255),
            Color(135, 155, 175, 255), Color(195, 215, 235, 255),
        };
        texture_->SetData(pixels.data(), static_cast<int>(pixels.size()));

        fontTexture_ = std::make_unique<Texture2D>(device, 4, 4);
        const std::vector<Color> fontPixels(16, Color::White);
        fontTexture_->SetData(fontPixels.data(), static_cast<int>(fontPixels.size()));

        std::vector<Rectangle> glyphs{Rectangle(0, 0, 4, 4)};
        std::vector<Rectangle> cropping{Rectangle(0, 0, 4, 4)};
        std::vector<SharpRuntime::charcs> characters{u'A'};
        std::vector<Vector3> kerning{Vector3(0.0f, 4.0f, 0.0f)};
        font_ = std::make_unique<SpriteFont>(
            *fontTexture_, glyphs, cropping, characters, 4, 0.0f, kerning,
            std::optional<SharpRuntime::charcs>(std::nullopt));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        const std::vector<Color> directPosition = Render(
            Matrix::getIdentityProperty(), [this](SpriteBatch& batch)
            {
                batch.Draw(*texture_, Vector2(5.75f, 6.75f), Color::White);
            });
        const std::vector<Color> transformedPosition = Render(
            Matrix::CreateTranslation(0.75f, 0.75f, 0.0f), [this](SpriteBatch& batch)
            {
                batch.Draw(*texture_, Vector2(5.0f, 6.0f), Color::White);
            });
        CheckEquivalent("Vector2 fractional position", directPosition, transformedPosition);

        constexpr float uniformScale = 1.375f;
        const std::vector<Color> directUniformScale = Render(
            Matrix::getIdentityProperty(), [this](SpriteBatch& batch)
            {
                batch.Draw(*texture_, Vector2::Zero, std::nullopt, Color::White, 0.0f,
                           Vector2::Zero, uniformScale, SpriteEffects::None, 0.0f);
            });
        const std::vector<Color> transformedUniformScale = Render(
            Matrix::CreateScale(uniformScale, uniformScale, 1.0f), [this](SpriteBatch& batch)
            {
                batch.Draw(*texture_, Vector2::Zero, Color::White);
            });
        CheckEquivalent("scalar fractional scale", directUniformScale, transformedUniformScale);

        const Vector2 scale(1.375f, 1.625f);
        const std::vector<Color> directScale = Render(
            Matrix::getIdentityProperty(), [this, scale](SpriteBatch& batch)
            {
                batch.Draw(*texture_, Vector2::Zero, std::nullopt, Color::White, 0.0f,
                           Vector2::Zero, scale, SpriteEffects::None, 0.0f);
            });
        const std::vector<Color> transformedScale = Render(
            Matrix::CreateScale(scale.X, scale.Y, 1.0f), [this](SpriteBatch& batch)
            {
                batch.Draw(*texture_, Vector2::Zero, Color::White);
            });
        CheckEquivalent("Vector2 fractional scale", directScale, transformedScale);

        const Vector2 fontPosition(4.75f, 3.25f);
        const Vector2 fontScale(1.375f, 1.625f);
        const std::vector<Color> directFont = Render(
            Matrix::getIdentityProperty(), [this, fontPosition, fontScale](SpriteBatch& batch)
            {
                batch.DrawString(*font_, "A", fontPosition, Color::White, 0.0f,
                                 Vector2::Zero, fontScale, SpriteEffects::None, 0.0f);
            });
        const std::vector<Color> transformedFont = Render(
            Matrix::CreateScale(fontScale.X, fontScale.Y, 1.0f) *
                Matrix::CreateTranslation(fontPosition.X, fontPosition.Y, 0.0f),
            [this](SpriteBatch& batch)
            {
                batch.DrawString(*font_, "A", Vector2::Zero, Color::White);
            });
        CheckEquivalent("DrawString fractional position and scale", directFont, transformedFont);

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        Exit();
    }

public:
    SpriteBatchSubpixelContractTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int Result() const { return passed_ == total_ ? 0 : 1; }
};

int main()
{
    SpriteBatchSubpixelContractTest game;
    game.Run();
    return game.Result();
}
