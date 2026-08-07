// SPDX-License-Identifier: MS-PL
// SKIA-121: public generated BlendState integration for constants, blend enable, and write masks.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    [[nodiscard]] BlendState ConstantSourceState(Color constant)
    {
        BlendState state;
        state.setColorSourceBlendProperty(Blend::BlendFactor);
        state.setColorDestinationBlendProperty(Blend::Zero);
        state.setAlphaSourceBlendProperty(Blend::One);
        state.setAlphaDestinationBlendProperty(Blend::Zero);
        state.setBlendFactorProperty(constant);
        return state;
    }

    [[nodiscard]] BlendState ReverseSubtractState(int mask = 15)
    {
        BlendState state;
        state.setColorSourceBlendProperty(Blend::One);
        state.setColorDestinationBlendProperty(Blend::One);
        state.setColorBlendFunctionProperty(BlendFunction::ReverseSubtract);
        state.setAlphaSourceBlendProperty(Blend::One);
        state.setAlphaDestinationBlendProperty(Blend::Zero);
        state.setAlphaBlendFunctionProperty(BlendFunction::Add);
        state.setColorWriteChannelsProperty(static_cast<ColorWriteChannels>(mask));
        return state;
    }

    [[nodiscard]] Color Masked(Color destination, Color blended, int mask)
    {
        return Color((mask & 1) ? blended.getRProperty() : destination.getRProperty(),
                     (mask & 2) ? blended.getGProperty() : destination.getGProperty(),
                     (mask & 4) ? blended.getBProperty() : destination.getBProperty(),
                     (mask & 8) ? blended.getAProperty() : destination.getAProperty());
    }

    [[nodiscard]] bool Near(Color actual, Color expected, int tolerance = 1)
    {
        const auto channel = [tolerance](int left, int right)
        {
            return std::abs(left - right) <= tolerance;
        };
        return channel(actual.getRProperty(), expected.getRProperty())
            && channel(actual.getGProperty(), expected.getGProperty())
            && channel(actual.getBProperty(), expected.getBProperty())
            && channel(actual.getAProperty(), expected.getAProperty());
    }
}

class SkiaGeneratedBlendStatePolicyTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> whiteTexture_;
    bool finished_ = false;
    int failures_ = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition)
            ++failures_;
    }

    [[nodiscard]] Color Read(int x = 0)
    {
        Color result(0, 0, 0, 0);
        const Rectangle region(x, 0, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&region, &result, 0, 1);
        return result;
    }

    void DrawOnce(const BlendState& state, Color tint, int x = 0)
    {
        spriteBatch_->Begin(SpriteSortMode::Deferred, state,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr);
        spriteBatch_->Draw(*whiteTexture_, Rectangle(x, 0, 1, 1), Rectangle(0, 0, 1, 1), tint);
        spriteBatch_->End();
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
        if (finished_)
            return;
        finished_ = true;
        auto& device = getGraphicsDeviceProperty();

        device.Clear(Color::Black);
        DrawOnce(ConstantSourceState(Color(200, 100, 40, 255)), Color::White);
        Check(Near(Read(), Color(200, 100, 40, 255)),
              "BlendState.BlendFactor reaches the generated source-factor route");

        device.Clear(Color::Black);
        const BlendState liveConstant = ConstantSourceState(Color(255, 0, 0, 255));
        spriteBatch_->Begin(SpriteSortMode::Immediate, liveConstant,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr);
        spriteBatch_->Draw(*whiteTexture_, Rectangle(0, 0, 1, 1), Color::White);
        device.setBlendFactorProperty(Color(0, 255, 0, 255));
        spriteBatch_->Draw(*whiteTexture_, Rectangle(1, 0, 1, 1), Color::White);
        device.setBlendFactorProperty(Color(255, 0, 0, 255));
        spriteBatch_->Draw(*whiteTexture_, Rectangle(2, 0, 1, 1), Color::White);
        spriteBatch_->End();
        Check(Near(Read(0), Color(255, 0, 0, 255))
                  && Near(Read(1), Color(0, 255, 0, 255))
                  && Near(Read(2), Color(255, 0, 0, 255)),
              "standalone BlendFactor changes rebuild without stale A-to-B-to-A state");

        const Color destination(192, 160, 128, 255);
        const Color source(64, 96, 128, 255);
        const Color blended(128, 64, 0, 255);
        bool allMasks = true;
        for (int mask = 0; mask < 16; ++mask)
        {
            device.Clear(destination);
            DrawOnce(ReverseSubtractState(mask), source);
            const Color actual = Read();
            const Color expected = Masked(destination, blended, mask);
            if (!Near(actual, expected))
            {
                std::printf("[INFO] mask=%d got=(%d,%d,%d,%d) want=(%d,%d,%d,%d)\n",
                            mask, actual.getRProperty(), actual.getGProperty(),
                            actual.getBProperty(), actual.getAProperty(), expected.getRProperty(),
                            expected.getGProperty(), expected.getBProperty(), expected.getAProperty());
                allMasks = false;
            }
        }
        Check(allMasks, "all 16 target-0 masks preserve destination bytes after generated blending");

        device.Clear(destination);
        device.SetBlendEnabled(false);
        DrawOnce(ReverseSubtractState(9), source);
        Check(Near(Read(), Masked(destination, source, 9)),
              "disabled blending performs masked source replacement for a generated state");

        device.SetBlendEnabled(true);
        device.Clear(destination);
        DrawOnce(ReverseSubtractState(), source);
        Check(Near(Read(), blended),
              "re-enabling blending restores the configured generated equation");

        Exit();
    }

public:
    SkiaGeneratedBlendStatePolicyTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(4);
        graphics_->setPreferredBackBufferHeightProperty(1);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SkiaGeneratedBlendStatePolicyTest game;
    game.Run();
    return game.Result();
}
