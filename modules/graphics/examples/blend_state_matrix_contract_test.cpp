// SPDX-License-Identifier: MS-PL
// SOFTWARE-152: renderer-neutral public BlendState factor/function matrix.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 16;
    constexpr int kTolerance = 2;
    const Color kSource(153, 51, 204, 102);
    const Color kDestination(51, 128, 26, 178);
    const Color kFactor(64, 192, 128, 32);

    constexpr std::array<Blend, 13> kBlendFactors = {
        Blend::One, Blend::Zero, Blend::SourceColor, Blend::InverseSourceColor,
        Blend::SourceAlpha, Blend::InverseSourceAlpha, Blend::DestinationColor,
        Blend::InverseDestinationColor, Blend::DestinationAlpha,
        Blend::InverseDestinationAlpha, Blend::BlendFactor,
        Blend::InverseBlendFactor, Blend::SourceAlphaSaturation,
    };

    constexpr std::array<const char*, 13> kBlendNames = {
        "One", "Zero", "SourceColor", "InverseSourceColor", "SourceAlpha",
        "InverseSourceAlpha", "DestinationColor", "InverseDestinationColor",
        "DestinationAlpha", "InverseDestinationAlpha", "BlendFactor",
        "InverseBlendFactor", "SourceAlphaSaturation",
    };

    constexpr std::array<BlendFunction, 5> kFunctions = {
        BlendFunction::Add, BlendFunction::Subtract, BlendFunction::ReverseSubtract,
        BlendFunction::Max, BlendFunction::Min,
    };

    constexpr std::array<const char*, 5> kFunctionNames = {
        "Add", "Subtract", "ReverseSubtract", "Max", "Min",
    };

    std::array<float, 4> ToFloat(const Color& value)
    {
        return {
            value.getRProperty() / 255.0f,
            value.getGProperty() / 255.0f,
            value.getBProperty() / 255.0f,
            value.getAProperty() / 255.0f,
        };
    }

    float FactorComponent(Blend factor, int channel,
                          const std::array<float, 4>& source,
                          const std::array<float, 4>& destination,
                          const std::array<float, 4>& constant)
    {
        switch (factor)
        {
            case Blend::One: return 1.0f;
            case Blend::Zero: return 0.0f;
            case Blend::SourceColor: return source[channel];
            case Blend::InverseSourceColor: return 1.0f - source[channel];
            case Blend::SourceAlpha: return source[3];
            case Blend::InverseSourceAlpha: return 1.0f - source[3];
            case Blend::DestinationColor: return destination[channel];
            case Blend::InverseDestinationColor: return 1.0f - destination[channel];
            case Blend::DestinationAlpha: return destination[3];
            case Blend::InverseDestinationAlpha: return 1.0f - destination[3];
            case Blend::BlendFactor: return constant[channel];
            case Blend::InverseBlendFactor: return 1.0f - constant[channel];
            case Blend::SourceAlphaSaturation:
                return channel == 3 ? 1.0f
                                    : std::min(source[3], 1.0f - destination[3]);
        }
        return 0.0f;
    }

    float ApplyFunction(BlendFunction function, float source, float destination)
    {
        switch (function)
        {
            case BlendFunction::Add: return source + destination;
            case BlendFunction::Subtract: return source - destination;
            case BlendFunction::ReverseSubtract: return destination - source;
            case BlendFunction::Max: return std::max(source, destination);
            case BlendFunction::Min: return std::min(source, destination);
        }
        return 0.0f;
    }

    int ToByte(float value)
    {
        return std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255);
    }
}

class BlendStateMatrixContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<RenderTarget2D> target_;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void CheckChannel(int actual, int expected, const std::string& label)
    {
        const bool ok = std::abs(actual - expected) <= kTolerance;
        std::printf("[%s] %s: got=%d expected=%d tolerance=%d\n",
                    ok ? "PASS" : "FAIL", label.c_str(), actual, expected, kTolerance);
        ++total_;
        if (ok)
            ++passed_;
    }

    Color Render(GraphicsDevice& device, const BlendState& state)
    {
        device.SetRenderTarget(target_.get());
        device.setViewportProperty(Viewport(0, 0, kSize, kSize));
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(kDestination);
        device.setBlendStateProperty(state);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
        const VertexPositionColor vertices[6] = {
            {Vector3(-1,  1, 0), kSource}, {Vector3(-1, -1, 0), kSource},
            {Vector3( 1, -1, 0), kSource}, {Vector3(-1,  1, 0), kSource},
            {Vector3( 1, -1, 0), kSource}, {Vector3( 1,  1, 0), kSource},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Rectangle center(kSize / 2, kSize / 2, 1, 1);
        Color pixel;
        target_->GetData(0, &center, &pixel, 0, 1);
        return pixel;
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        target_ = std::make_unique<RenderTarget2D>(
            device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto source = ToFloat(kSource);
        const auto destination = ToFloat(kDestination);
        const auto constant = ToFloat(kFactor);

        for (std::size_t i = 0; i < kBlendFactors.size(); ++i)
        {
            BlendState state;
            state.setColorSourceBlendProperty(kBlendFactors[i]);
            state.setColorDestinationBlendProperty(Blend::Zero);
            state.setAlphaSourceBlendProperty(Blend::One);
            state.setAlphaDestinationBlendProperty(Blend::Zero);
            state.setBlendFactorProperty(kFactor);
            const Color actual = Render(device, state);
            const float factor = FactorComponent(
                kBlendFactors[i], 0, source, destination, constant);
            CheckChannel(actual.getRProperty(), ToByte(source[0] * factor),
                         std::string("RGB source factor ") + kBlendNames[i]);
        }

        // SourceAlphaSaturation is the fixed-function source-only factor in GL/FNA. The other
        // twelve values are valid destination factors and cover source-, destination- and
        // constant-dependent vectors from the opposite side of the equation.
        for (std::size_t i = 0; i + 1 < kBlendFactors.size(); ++i)
        {
            BlendState state;
            state.setColorSourceBlendProperty(Blend::Zero);
            state.setColorDestinationBlendProperty(kBlendFactors[i]);
            state.setAlphaSourceBlendProperty(Blend::One);
            state.setAlphaDestinationBlendProperty(Blend::Zero);
            state.setBlendFactorProperty(kFactor);
            const Color actual = Render(device, state);
            const float factor = FactorComponent(
                kBlendFactors[i], 0, source, destination, constant);
            CheckChannel(actual.getRProperty(), ToByte(destination[0] * factor),
                         std::string("RGB destination factor ") + kBlendNames[i]);
        }

        for (std::size_t i = 0; i < kFunctions.size(); ++i)
        {
            BlendState state;
            state.setColorSourceBlendProperty(Blend::One);
            state.setColorDestinationBlendProperty(Blend::One);
            state.setColorBlendFunctionProperty(kFunctions[i]);
            state.setAlphaSourceBlendProperty(Blend::One);
            state.setAlphaDestinationBlendProperty(Blend::Zero);
            const Color actual = Render(device, state);
            CheckChannel(actual.getRProperty(),
                         ToByte(ApplyFunction(kFunctions[i], source[0], destination[0])),
                         std::string("independent RGB function ") + kFunctionNames[i]);
        }

        for (std::size_t i = 0; i < kFunctions.size(); ++i)
        {
            BlendState state;
            state.setColorSourceBlendProperty(Blend::One);
            state.setColorDestinationBlendProperty(Blend::Zero);
            state.setAlphaSourceBlendProperty(Blend::One);
            state.setAlphaDestinationBlendProperty(Blend::One);
            state.setAlphaBlendFunctionProperty(kFunctions[i]);
            const Color actual = Render(device, state);
            CheckChannel(actual.getAProperty(),
                         ToByte(ApplyFunction(kFunctions[i], source[3], destination[3])),
                         std::string("independent alpha function ") + kFunctionNames[i]);
        }

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    BlendStateMatrixContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(32);
        graphics_->setPreferredBackBufferHeightProperty(32);
    }

    int Result() const { return result_; }
};

int main()
{
    BlendStateMatrixContractTest game;
    game.Run();
    return game.Result();
}
