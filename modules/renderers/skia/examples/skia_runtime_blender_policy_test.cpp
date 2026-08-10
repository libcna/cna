// SPDX-License-Identifier: MS-PL
// SKIA-54: public SpriteBatch prototype for the proven SkRuntimeEffect blender route. The custom
// state has an independent alpha branch and reads the current destination; validate backbuffer,
// RenderTarget2D readback, and sampling before widening the accepted matrix in SKIA-55.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kDestination(200, 0, 0, 255);
    const Color kTint(128, 255, 128, 255);

    [[nodiscard]] BlendState DestinationColorState()
    {
        BlendState state;
        // RGB = source * destination-colour. Alpha has its own source-copy branch, proving this
        // is not a fixed SkBlendMode masquerading as the four known presets.
        state.setColorSourceBlendProperty(Blend::DestinationColor);
        state.setColorDestinationBlendProperty(Blend::Zero);
        state.setAlphaSourceBlendProperty(Blend::One);
        state.setAlphaDestinationBlendProperty(Blend::Zero);
        return state;
    }

    [[nodiscard]] bool IsExpected(const Color& color)
    {
        return color.getRProperty() >= 98 && color.getRProperty() <= 102
            && color.getGProperty() <= 1 && color.getBProperty() <= 1
            && color.getAProperty() == 255;
    }
}

class SkiaRuntimeBlenderPolicyTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> whiteTexture_;
    bool finished_ = false;
    int failures_ = 0;

    void Check(bool condition, const char* label, const Color& color)
    {
        std::printf("[%s] %s: got=(%d,%d,%d,%d)\n", condition ? "PASS" : "FAIL", label,
                    color.getRProperty(), color.getGProperty(), color.getBProperty(), color.getAProperty());
        if (!condition) ++failures_;
    }

    void DrawDestinationColor(const Rectangle& destination)
    {
        spriteBatch_->Begin(SpriteSortMode::Deferred, DestinationColorState(),
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr);
        spriteBatch_->Draw(*whiteTexture_, destination, Rectangle(0, 0, 1, 1), kTint);
        spriteBatch_->End();
    }

    [[nodiscard]] Color ReadBackbuffer(GraphicsDevice& device, int x, int y)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Initialize() override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        whiteTexture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        if (finished_) return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.Clear(kDestination);
        DrawDestinationColor(Rectangle(0, 0, 8, 8));
        const Color backbuffer = ReadBackbuffer(device, 3, 3);
        Check(IsExpected(backbuffer), "runtime blender reads and modulates the backbuffer destination", backbuffer);

        RenderTarget2D target(device, 4, 4, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&target);
        device.Clear(kDestination);
        DrawDestinationColor(Rectangle(0, 0, 4, 4));
        device.SetRenderTarget(nullptr);

        std::vector<Color> targetPixels(16, Color(0, 0, 0, 0));
        target.GetData(targetPixels.data(), 0, static_cast<int>(targetPixels.size()));
        Check(IsExpected(targetPixels[5]), "runtime-blended RenderTarget2D round-trips through public GetData",
              targetPixels[5]);

        device.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr);
        spriteBatch_->Draw(target, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->End();
        const Color sampled = ReadBackbuffer(device, 3, 3);
        Check(IsExpected(sampled), "runtime-blended RenderTarget2D samples back through SpriteBatch", sampled);
        Exit();
    }

public:
    SkiaRuntimeBlenderPolicyTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(8);
        graphics_->setPreferredBackBufferHeightProperty(8);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SkiaRuntimeBlenderPolicyTest game;
    game.Run();
    return game.Result();
}
