// SPDX-License-Identifier: MS-PL
// DX-211: every DirectX capability answer must agree with its executable/runtime-backed oracle.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
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

    constexpr std::array<CapabilityCase, 19> kCapabilities{{
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
        {CNA::GraphicsCapability::ComputeShaders, "ComputeShaders"},
        {CNA::GraphicsCapability::IndirectDraw, "IndirectDraw"},
    }};

    bool ExpectedCapability(GraphicsDevice& device,
                            CNA::Internal::Renderers::IGraphicsRenderer& renderer,
                            CNA::GraphicsCapability capability,
                            int appliedSamples)
    {
        switch (capability)
        {
        case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            return appliedSamples > 1;
        case CNA::GraphicsCapability::CompiledEffects:
            return renderer.SupportsCompiledEffects();
        case CNA::GraphicsCapability::FloatRenderTargets:
            return device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4);
        case CNA::GraphicsCapability::HalfFloatRenderTargets:
            return device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable);
        case CNA::GraphicsCapability::HalfFloatTextureLinearFiltering:
            return renderer.SupportsHalfFloatTextureLinearFilteringEXT();
        case CNA::GraphicsCapability::ComputeShaders:
            return renderer.SupportsComputeShadersEXT();
        case CNA::GraphicsCapability::IndirectDraw:
            return renderer.SupportsIndirectDrawEXT();
        default:
            return true;
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
        Check(appliedSamples > 1,
              "four-sample request applies a real multisampled default render surface");

        for (const CapabilityCase& item : kCapabilities)
        {
            const bool expected =
                ExpectedCapability(device, renderer, item.capability, appliedSamples);
            const bool publicAnswer = device.SupportsCapability(item.capability);
            const bool rendererAnswer = renderer.SupportsCapability(item.capability);
            Check(publicAnswer == expected && rendererAnswer == expected,
                  std::string(item.name) + " public=" +
                      (publicAnswer ? "true" : "false") + " renderer=" +
                      (rendererAnswer ? "true" : "false") + " expected=" +
                      (expected ? "true" : "false"));
        }

        Check(!renderer.SupportsCapability(
                  static_cast<CNA::GraphicsCapability>(1000)),
              "unknown future capability defaults to false rather than being advertised");
        Check(!renderer.SupportsComputeShadersEXT() &&
                  !renderer.SupportsIndirectDrawEXT() &&
                  !renderer.SupportsHalfFloatTextureLinearFilteringEXT(),
              "unimplemented modern DirectX extensions remain explicitly unavailable");

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
    }

    [[nodiscard]] int Result() const { return failed_ == 0 ? 0 : 1; }
};

int main()
{
    RendererCapabilityTruthTest game;
    game.Run();
    return game.Result();
}
