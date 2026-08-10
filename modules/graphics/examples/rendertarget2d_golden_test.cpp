// SPDX-License-Identifier: MS-PL
// SKIA-75: shared RenderTarget2D 2D golden for SKIA, EASYGL, and SDL_RENDERER.
//
// This source contains the checked-in level-0 RGBA oracle rather than a driver-dependent image
// file. It deliberately limits itself to the exact common 2D contract: an opaque Point sprite
// pattern rendered into a preserve target, public target readback, target unbind, and Point
// sampling back to a larger default backbuffer. The oracle has four distinct corners, so any
// vertical/horizontal orientation or stale-snapshot defect is visible.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kTargetSize = 4;
    constexpr int kBackbufferSize = 8;

    struct Rgba final
    {
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;
    };

    // Top-row-first CNA readback oracle: red / green / blue / yellow quadrants.
    constexpr std::array<Rgba, kTargetSize * kTargetSize> kTargetGolden {{
        {255, 0,   0,   255}, {255, 0,   0,   255}, {0,   255, 0,   255}, {0,   255, 0,   255},
        {255, 0,   0,   255}, {255, 0,   0,   255}, {0,   255, 0,   255}, {0,   255, 0,   255},
        {0,   0,   255, 255}, {0,   0,   255, 255}, {255, 255, 0,   255}, {255, 255, 0,   255},
        {0,   0,   255, 255}, {0,   0,   255, 255}, {255, 255, 0,   255}, {255, 255, 0,   255},
    }};

    [[nodiscard]] bool Matches(const Color& actual, const Rgba& expected)
    {
        return actual.getRProperty() == expected.r && actual.getGProperty() == expected.g
            && actual.getBProperty() == expected.b && actual.getAProperty() == expected.a;
    }
}

class RenderTarget2DGoldenTest final : public Game
{
public:
    RenderTarget2DGoldenTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kBackbufferSize);
        graphics_->setPreferredBackBufferHeightProperty(kBackbufferSize);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(device, 1, 1, false, SurfaceFormat::Color);
        const Color white = Color::White;
        white_->SetData(&white, 1);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&target);
        device.Clear(Color(0, 0, 0, 255));
        BeginPointOpaque();
        DrawQuadrant(0, 0, Color(255, 0, 0, 255));
        DrawQuadrant(2, 0, Color(0, 255, 0, 255));
        DrawQuadrant(0, 2, Color(0, 0, 255, 255));
        DrawQuadrant(2, 2, Color(255, 255, 0, 255));
        spriteBatch_->End();

        std::vector<Color> targetReadback(kTargetSize * kTargetSize, Color(0, 0, 0, 0));
        target.GetData(targetReadback.data(), 0, static_cast<int>(targetReadback.size()));
        CheckMatches(targetReadback, false, "RenderTarget2D level-0 readback matches the checked-in oracle");

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.Clear(Color(0, 0, 0, 255));
        BeginPointOpaque();
        spriteBatch_->Draw(target, Rectangle(0, 0, kBackbufferSize, kBackbufferSize),
                           Rectangle(0, 0, kTargetSize, kTargetSize), Color::White);
        spriteBatch_->End();

        std::vector<Color> sampledReadback(kBackbufferSize * kBackbufferSize, Color(0, 0, 0, 0));
        device.GetBackBufferData(sampledReadback.data(), 0, static_cast<int>(sampledReadback.size()));
        CheckMatches(sampledReadback, true,
                     "unbound RenderTarget2D Point sampling matches the 2x checked-in oracle");
        Exit();
    }

private:
    void BeginPointOpaque()
    {
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
    }

    void DrawQuadrant(int x, int y, const Color& color)
    {
        spriteBatch_->Draw(*white_, Rectangle(x, y, 2, 2), Rectangle(0, 0, 1, 1), color);
    }

    void CheckMatches(const std::vector<Color>& actual, bool sampled, const char* label)
    {
        int mismatches = 0;
        for (std::size_t index = 0; index < actual.size(); ++index)
        {
            const int x = static_cast<int>(index % (sampled ? kBackbufferSize : kTargetSize));
            const int y = static_cast<int>(index / (sampled ? kBackbufferSize : kTargetSize));
            const Rgba& expected = kTargetGolden[static_cast<std::size_t>(y / (sampled ? 2 : 1))
                                                   * kTargetSize + x / (sampled ? 2 : 1)];
            if (!Matches(actual[index], expected))
                ++mismatches;
        }
        std::printf("[%s] %s (%zu/%zu exact RGBA pixels)\n", mismatches == 0 ? "PASS" : "FAIL", label,
                    actual.size() - static_cast<std::size_t>(mismatches), actual.size());
        if (mismatches != 0)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> white_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    RenderTarget2DGoldenTest game;
    game.Run();
    return game.Result();
}
