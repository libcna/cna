// SPDX-License-Identifier: MS-PL
// SKIA-123: minimized public pixels cover every generated factor position and function branch.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    using Float4 = std::array<float, 4>;

    struct Selectors final
    {
        int colorSource = 0;
        int alphaSource = 0;
        int colorDestination = 1;
        int alphaDestination = 1;
        int colorFunction = 0;
        int alphaFunction = 0;
    };

    constexpr int kFactorCount = 13;
    constexpr int kFunctionCount = 5;
    const Color kSourceBytes(153, 102, 51, 204);
    const Color kDestinationBytes(64, 128, 32, 128);
    const Color kConstantBytes(64, 128, 192, 153);

    [[nodiscard]] Float4 ToFloat(Color value)
    {
        return {
            value.getRProperty() / 255.0f,
            value.getGProperty() / 255.0f,
            value.getBProperty() / 255.0f,
            value.getAProperty() / 255.0f,
        };
    }

    [[nodiscard]] Float4 Factor(int selector, const Float4& source,
                                const Float4& destination, const Float4& constant)
    {
        Float4 result{};
        for (std::size_t channel = 0; channel < result.size(); ++channel)
        {
            switch (selector)
            {
                case 0: result[channel] = 1.0f; break;
                case 1: result[channel] = 0.0f; break;
                case 2: result[channel] = source[channel]; break;
                case 3: result[channel] = 1.0f - source[channel]; break;
                case 4: result[channel] = source[3]; break;
                case 5: result[channel] = 1.0f - source[3]; break;
                case 6: result[channel] = destination[channel]; break;
                case 7: result[channel] = 1.0f - destination[channel]; break;
                case 8: result[channel] = destination[3]; break;
                case 9: result[channel] = 1.0f - destination[3]; break;
                case 10: result[channel] = constant[channel]; break;
                case 11: result[channel] = 1.0f - constant[channel]; break;
                case 12:
                    result[channel] = channel == 3u
                        ? 1.0f
                        : std::min(source[3], 1.0f - destination[3]);
                    break;
                default: break;
            }
        }
        return result;
    }

    [[nodiscard]] Float4 Equation(int function, const Float4& source,
                                  const Float4& destination,
                                  const Float4& weightedSource,
                                  const Float4& weightedDestination)
    {
        Float4 result{};
        for (std::size_t channel = 0; channel < result.size(); ++channel)
        {
            switch (function)
            {
                case 0: result[channel] = weightedSource[channel] + weightedDestination[channel]; break;
                case 1: result[channel] = weightedSource[channel] - weightedDestination[channel]; break;
                case 2: result[channel] = weightedDestination[channel] - weightedSource[channel]; break;
                case 3: result[channel] = std::max(source[channel], destination[channel]); break;
                case 4: result[channel] = std::min(source[channel], destination[channel]); break;
                default: break;
            }
            result[channel] = std::clamp(result[channel], 0.0f, 1.0f);
        }
        return result;
    }

    [[nodiscard]] Float4 Reference(const Selectors& selectors)
    {
        const Float4 source = ToFloat(kSourceBytes);
        const Float4 destination = ToFloat(kDestinationBytes);
        const Float4 constant = ToFloat(kConstantBytes);
        const Float4 colorSourceFactor = Factor(
            selectors.colorSource, source, destination, constant);
        const Float4 colorDestinationFactor = Factor(
            selectors.colorDestination, source, destination, constant);
        const Float4 alphaSourceFactor = Factor(
            selectors.alphaSource, source, destination, constant);
        const Float4 alphaDestinationFactor = Factor(
            selectors.alphaDestination, source, destination, constant);
        Float4 weightedColorSource{};
        Float4 weightedColorDestination{};
        Float4 weightedAlphaSource{};
        Float4 weightedAlphaDestination{};
        for (std::size_t channel = 0; channel < source.size(); ++channel)
        {
            weightedColorSource[channel] = source[channel] * colorSourceFactor[channel];
            weightedColorDestination[channel] = destination[channel] * colorDestinationFactor[channel];
            weightedAlphaSource[channel] = source[channel] * alphaSourceFactor[channel];
            weightedAlphaDestination[channel] = destination[channel] * alphaDestinationFactor[channel];
        }
        const Float4 color = Equation(selectors.colorFunction, source, destination,
                                      weightedColorSource, weightedColorDestination);
        const Float4 alpha = Equation(selectors.alphaFunction, source, destination,
                                      weightedAlphaSource, weightedAlphaDestination);
        return {color[0], color[1], color[2], alpha[3]};
    }

    [[nodiscard]] BlendState MakeState(const Selectors& selectors)
    {
        BlendState state;
        state.setColorSourceBlendProperty(static_cast<Blend>(selectors.colorSource));
        state.setAlphaSourceBlendProperty(static_cast<Blend>(selectors.alphaSource));
        state.setColorDestinationBlendProperty(static_cast<Blend>(selectors.colorDestination));
        state.setAlphaDestinationBlendProperty(static_cast<Blend>(selectors.alphaDestination));
        state.setColorBlendFunctionProperty(static_cast<BlendFunction>(selectors.colorFunction));
        state.setAlphaBlendFunctionProperty(static_cast<BlendFunction>(selectors.alphaFunction));
        state.setBlendFactorProperty(kConstantBytes);
        return state;
    }

    [[nodiscard]] bool Matches(Color actual, const Float4& expected, bool alphaOnly = false)
    {
        const int expectedAlpha = static_cast<int>(expected[3] * 255.0f + 0.5f);
        if (std::abs(actual.getAProperty() - expectedAlpha) > 3)
            return false;
        if (alphaOnly || expectedAlpha == 0)
            return true;
        const int actualRgb[] = {
            actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
        };
        for (std::size_t channel = 0; channel < 3u; ++channel)
        {
            const int expectedByte = static_cast<int>(expected[channel] * 255.0f + 0.5f);
            if (std::abs(actualRgb[channel] - expectedByte) > 4)
                return false;
        }
        return true;
    }
}

class SkiaGeneratedBlendPublicCorpusTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> sourceTexture_;
    bool finished_ = false;
    int checks_ = 0;
    int failures_ = 0;

    [[nodiscard]] Color Render(const Selectors& selectors)
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(kDestinationBytes);
        spriteBatch_->Begin(SpriteSortMode::Deferred, MakeState(selectors),
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr);
        spriteBatch_->Draw(*sourceTexture_, Rectangle(0, 0, 1, 1),
                           Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        Color result(0, 0, 0, 0);
        const Rectangle pixel(0, 0, 1, 1);
        device.GetBackBufferData(&pixel, &result, 0, 1);
        return result;
    }

    void CheckScene(const Selectors& selectors, const std::string& label, bool alphaOnly = false)
    {
        const Float4 expected = Reference(selectors);
        const Color actual = Render(selectors);
        const bool pass = Matches(actual, expected, alphaOnly);
        ++checks_;
        std::printf("[%s] %s got=(%d,%d,%d,%d)\n", pass ? "PASS" : "FAIL", label.c_str(),
                    actual.getRProperty(), actual.getGProperty(),
                    actual.getBProperty(), actual.getAProperty());
        if (!pass)
            ++failures_;
    }

protected:
    void Initialize() override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        sourceTexture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{153, 102, 51, 204}));
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        for (int factor = 0; factor < kFactorCount; ++factor)
        {
            Selectors selectors;
            selectors.colorSource = factor;
            selectors.colorDestination = 1;
            selectors.alphaFunction = 3; // Max forces the generic route without changing factor RGB.
            CheckScene(selectors, "color source factor " + std::to_string(factor));

            selectors = {};
            selectors.colorSource = 1;
            selectors.colorDestination = factor;
            selectors.alphaFunction = 3;
            CheckScene(selectors, "color destination factor " + std::to_string(factor));

            selectors = {};
            selectors.alphaSource = factor;
            selectors.alphaDestination = 1;
            selectors.colorFunction = 3; // Max similarly keeps this outside established mappings.
            CheckScene(selectors, "alpha source factor " + std::to_string(factor), true);

            selectors = {};
            selectors.alphaSource = 1;
            selectors.alphaDestination = factor;
            selectors.colorFunction = 3;
            CheckScene(selectors, "alpha destination factor " + std::to_string(factor), true);
        }

        for (int function = 0; function < kFunctionCount; ++function)
        {
            Selectors selectors;
            selectors.colorSource = 0;
            selectors.colorDestination = 0;
            selectors.colorFunction = function;
            CheckScene(selectors, "color function " + std::to_string(function));

            selectors = {};
            selectors.alphaSource = 0;
            selectors.alphaDestination = 0;
            selectors.alphaFunction = function;
            CheckScene(selectors, "alpha function " + std::to_string(function), true);
        }

        std::printf("=== %d/%d PASS ===\n", checks_ - failures_, checks_);
        Exit();
    }

public:
    SkiaGeneratedBlendPublicCorpusTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(1);
        graphics_->setPreferredBackBufferHeightProperty(1);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SkiaGeneratedBlendPublicCorpusTest game;
    game.Run();
    return game.Result();
}
