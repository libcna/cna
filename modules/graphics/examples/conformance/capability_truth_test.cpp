// SPDX-License-Identifier: MS-PL
// DX-211: a capability answer must agree with what the device actually does. MultiSampleAntiAliasing
// is true exactly when a four-sample Reset applies more than one sample; each entry GraphicsDevice
// derives (compiled effects, float render targets, half-float filtering) agrees with its source;
// and every other entry is the renderer's own answer (MRT and Texture3D additionally gated by the
// profile).

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    struct CapabilityCase
    {
        CNA::GraphicsCapability capability;
        const char* name;
    };

    constexpr std::array<CapabilityCase, 17> kCapabilities{{
        {CNA::GraphicsCapability::ThreeD, "ThreeD"},
        {CNA::GraphicsCapability::DepthStencilBuffer, "DepthStencilBuffer"},
        {CNA::GraphicsCapability::MultiSampleAntiAliasing, "MultiSampleAntiAliasing"},
        {CNA::GraphicsCapability::MultipleRenderTargets, "MultipleRenderTargets"},
        {CNA::GraphicsCapability::AnisotropicFiltering, "AnisotropicFiltering"},
        {CNA::GraphicsCapability::WireFrame, "WireFrame"},
        {CNA::GraphicsCapability::OcclusionQuery, "OcclusionQuery"},
        {CNA::GraphicsCapability::CustomEffects, "CustomEffects"},
        {CNA::GraphicsCapability::Texture3D, "Texture3D"},
        {CNA::GraphicsCapability::MultiStreamVertexInput, "MultiStreamVertexInput"},
        {CNA::GraphicsCapability::Instancing, "Instancing"},
        {CNA::GraphicsCapability::StencilBuffer, "StencilBuffer"},
        {CNA::GraphicsCapability::AdditiveBlending, "AdditiveBlending"},
        {CNA::GraphicsCapability::CompiledEffects, "CompiledEffects"},
        {CNA::GraphicsCapability::FloatRenderTargets, "FloatRenderTargets"},
        {CNA::GraphicsCapability::HalfFloatRenderTargets, "HalfFloatRenderTargets"},
        {CNA::GraphicsCapability::HalfFloatTextureLinearFiltering,
         "HalfFloatTextureLinearFiltering"},
    }};

    enum class Expectation
    {
        True,
        False,
        RendererAnswer,
        RendererAnswerOrProfileLimit,
    };

    Expectation ExpectedCapability(GraphicsDevice& device,
                                   CNA::Internal::Renderers::IGraphicsRenderer& renderer,
                                   CNA::GraphicsCapability capability,
                                   int appliedSamples)
    {
        const auto fromBool = [](bool value) { return value ? Expectation::True : Expectation::False; };
        switch (capability)
        {
        case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            return fromBool(appliedSamples > 1);
        case CNA::GraphicsCapability::CompiledEffects:
            return fromBool(renderer.SupportsCompiledEffects());
        case CNA::GraphicsCapability::FloatRenderTargets:
            return fromBool(device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4));
        case CNA::GraphicsCapability::HalfFloatRenderTargets:
            return fromBool(device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable));
        case CNA::GraphicsCapability::HalfFloatTextureLinearFiltering:
            return fromBool(renderer.SupportsHalfFloatTextureLinearFilteringEXT());
        case CNA::GraphicsCapability::MultipleRenderTargets:
        case CNA::GraphicsCapability::Texture3D:
            return Expectation::RendererAnswerOrProfileLimit;
        default:
            return Expectation::RendererAnswer;
        }
    }
}

class RendererCapabilityTruthTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int passed_ = 0;
    int failed_ = 0;
    bool done_ = false;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        condition ? ++passed_ : ++failed_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        GraphicsDevice& device = getGraphicsDeviceProperty();
        auto& renderer = device.GetRenderer();

        PresentationParameters parameters =
            device.getPresentationParametersProperty().Clone();
        parameters.setMultiSampleCountProperty(4);
        device.Reset(parameters);
        const int appliedSamples =
            device.getPresentationParametersProperty().getMultiSampleCountProperty();

        for (const CapabilityCase& item : kCapabilities)
        {
            const bool publicAnswer = device.SupportsCapability(item.capability);
            const bool rendererAnswer = renderer.SupportsCapability(item.capability);
            bool agrees = false;
            switch (ExpectedCapability(device, renderer, item.capability, appliedSamples))
            {
            case Expectation::True: agrees = publicAnswer; break;
            case Expectation::False: agrees = !publicAnswer; break;
            case Expectation::RendererAnswer: agrees = publicAnswer == rendererAnswer; break;
            case Expectation::RendererAnswerOrProfileLimit: agrees = !publicAnswer || rendererAnswer; break;
            }
            Check(agrees, std::string(item.name) + " public=" + (publicAnswer ? "true" : "false") +
                              " renderer=" + (rendererAnswer ? "true" : "false") +
                              " appliedSamples=" + std::to_string(appliedSamples));
        }

        std::printf("=== %d/%d PASS ===\n", passed_, passed_ + failed_);
        Exit();
    }

public:
    RendererCapabilityTruthTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(96);
        graphics_->setPreferredBackBufferHeightProperty(64);
        graphics_->setPreferMultiSamplingProperty(true);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
    }

    [[nodiscard]] int Result() const { return failed_ == 0 ? 0 : 1; }
};

int main()
{
    RendererCapabilityTruthTest game;
    game.Run();
    return game.Result();
}
