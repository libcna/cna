// SPDX-License-Identifier: MS-PL
// MTH-EXIT-002 R1-01: XNA SpriteBatch applies (x,y,layerDepth,1) * transform * projection.
// The sprite projection preserves Z. These expectations are authored from that equation,
// independently of any CNA rendering: .25 + .5 = .75 fails LessEqual against red's .5.

#include "CNA/GraphicsRendererCategory.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "common/PixelTestGame.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 16;
}

class SpriteBatchTransformDepthTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    bool done_ = false;
    int passed_ = 0;
    int total_ = 0;

    void Check(const std::array<Color, kSize * kSize>& pixels, int x, int y,
               const Color& expected, const char* label, SpriteSortMode mode)
    {
        const Color& actual = pixels[static_cast<std::size_t>(y * kSize + x)];
        const bool ok = actual.getPackedValueProperty() == expected.getPackedValueProperty();
        ++total_;
        if (ok) ++passed_;
        std::printf("[%s] %s %s at (%d,%d): RGBA=(%u,%u,%u,%u) expected=(%u,%u,%u,%u)\n",
                    ok ? "PASS" : "FAIL", mode == SpriteSortMode::Immediate ? "Immediate" : "Deferred",
                    label, x, y, actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                    actual.getAProperty(), expected.getRProperty(), expected.getGProperty(),
                    expected.getBProperty(), expected.getAProperty());
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        Texture2D white(device, 1, 1);
        const Color texel = Color::White;
        white.SetData(&texel, 1);
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::Depth24, 0, RenderTargetUsage::PreserveContents);
        SpriteBatch batch(device);
        const SamplerState point = SamplerState::PointClamp;
        const auto draw = [&](SpriteSortMode mode, const Matrix& transform, const Color& color,
                              float depth)
        {
            batch.Begin(mode, BlendState::Opaque, &point, &DepthStencilState::Default,
                        &RasterizerState::CullNone, nullptr, transform);
            batch.Draw(white, Rectangle(0, 0, kSize, kSize), std::nullopt, color, 0.0f,
                       Vector2::Zero, SpriteEffects::None, depth);
            batch.End();
        };
        for (const SpriteSortMode mode : {SpriteSortMode::Immediate, SpriteSortMode::Deferred})
        {
            const auto render = [&](const Matrix& transform, float depth = 0.25f)
            {
                device.SetRenderTarget(&target);
                device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
                draw(mode, Matrix::getIdentityProperty(), Color::Red, 0.5f);
                draw(mode, transform, Color::Green, depth);
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                std::array<Color, kSize * kSize> pixels{};
                target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
                return pixels;
            };

            Check(render(Matrix::getIdentityProperty()), 8, 8, Color::Green,
                  "identity control: .25 passes .5", mode);
            Check(render(Matrix::CreateTranslation(0.0f, 0.0f, 0.5f)), 8, 8, Color::Red,
                  "Z translation: .75 fails .5", mode);
            Check(render(Matrix::CreateScale(1.0f, 1.0f, 3.0f)), 8, 8, Color::Red,
                  "Z scale: .25 * 3 fails .5", mode);
            Check(render(Matrix::CreateTranslation(0.0f, 0.0f, -0.5f)), 8, 8, Color::Red,
                  "negative transformed Z clips instead of clamping to zero", mode);
            Check(render(Matrix::getIdentityProperty(), -0.25f), 8, 8, Color::Red,
                  "negative layerDepth clips instead of clamping to zero", mode);

            Matrix slope = Matrix::getIdentityProperty();
            slope.M13 = 1.0f / 32.0f;
            const auto xSlope = render(slope);
            Check(xSlope, 2, 8, Color::Green, "X contributes to Z: left passes", mode);
            Check(xSlope, 13, 8, Color::Red, "X contributes to Z: right fails", mode);
            slope.M13 = 0.0f;
            slope.M23 = 1.0f / 32.0f;
            const auto ySlope = render(slope);
            Check(ySlope, 8, 2, Color::Green, "Y contributes to Z: top passes", mode);
            Check(ySlope, 8, 13, Color::Red, "Y contributes to Z: bottom fails", mode);
        }
        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        Exit();
    }

public:
    SpriteBatchTransformDepthTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(32);
        manager_->setPreferredBackBufferHeightProperty(32);
    }

    int Result() const { return passed_ == total_ ? 0 : 1; }
};

int main()
{
    const auto renderer = CNA::GraphicsRendererSelection::GetSelected();
    if (CNA::getGraphicsRendererCategory(renderer) == CNA::GraphicsRendererCategory::Diagnostic)
    {
        std::printf("[SKIP] spritebatch_transform_depth: the %s renderer cannot read a render "
                    "target's colour attachment back to the CPU\n",
                    std::string(CNA::getGraphicsRendererName(renderer)).c_str());
        return CNA::Examples::kSkipExitCode;
    }
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    SpriteBatchTransformDepthTest game;
    game.Run();
    return game.Result();
}
