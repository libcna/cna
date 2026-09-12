// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-047: device-bound parse/reflection/state evidence before the draw path
// is allowed to advertise GraphicsCapability::CompiledEffects.

#if !defined(CNA_RLGL_COMPILED_EFFECTS)
#error "This fixture requires CNA_RLGL_COMPILED_EFFECTS"
#endif

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglCompiledEffect.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include "common/PixelTestGame.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::CompiledEffectDeviceState;
using CNA::Internal::Renderers::CompiledEffectPassStateChanges;
using CNA::Internal::Renderers::Rlgl::RlglCompiledEffect;
using CNA::Internal::Renderers::Rlgl::RlglRenderer;

namespace
{
    [[nodiscard]] std::vector<std::uint8_t> LoadConformanceEffect()
    {
        const std::filesystem::path path =
            std::filesystem::path(CNA_RLGL_SOURCE_DIR) /
            "modules/renderers/fna3d/effects/CnaConformanceEffect.fxb";
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
}

class RlglCompiledEffectRuntimeTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglCompiledEffectRuntimeTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(64);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        GraphicsDevice& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<RlglRenderer&>(device.GetRenderer());
        Check(!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects),
              "parse-only milestone does not advertise compiled-effect execution");

        const std::vector<std::uint8_t> bytes = LoadConformanceEffect();
        Check(!bytes.empty(), "committed XNA Effect Framework fixture is readable");
        if (bytes.empty()) return;

        bool emptyRejected = false;
        try
        {
            (void)renderer.CreateCompiledEffect(nullptr, 0);
        }
        catch (const std::invalid_argument&)
        {
            emptyRejected = true;
        }
        Check(emptyRejected, "empty compiled-effect bytecode is rejected before parsing");

        auto runtime = renderer.CreateCompiledEffect(bytes.data(), bytes.size());
        Check(runtime != nullptr, "MojoShader creates an RLGL device-bound runtime");
        if (runtime == nullptr) return;

        const auto& description = runtime->GetDescription();
        Check(description.parameters.size() == 6 &&
                  description.parameters.front().name == "Gain" &&
                  description.parameters.back().name == "FxTexture",
              "parameter reflection matches the conformance effect");
        Check(description.techniques.size() == 2 &&
                  description.techniques[0].passes.size() == 2 &&
                  description.techniques[1].passes.size() == 1 &&
                  description.techniques[0].passes[1].name == "StatePass",
              "technique and pass reflection preserves source order and names");

        bool badTechniqueRejected = false;
        try
        {
            runtime->SetTechnique(99);
        }
        catch (const std::out_of_range&)
        {
            badTechniqueRejected = true;
        }
        Check(badTechniqueRejected, "out-of-range technique selection is rejected");

        const std::array<float, 64> oversizedValue{};
        bool oversizedParameterRejected = false;
        try
        {
            runtime->SetParameterValue(0, oversizedValue.data(), sizeof(oversizedValue));
        }
        catch (const std::invalid_argument&)
        {
            oversizedParameterRejected = true;
        }
        Check(oversizedParameterRejected, "parameter writes cannot exceed reflected storage");

        runtime->SetTechnique(0);
        const BlendState blend = BlendState::Opaque;
        const DepthStencilState depth = DepthStencilState::Default;
        const RasterizerState raster = RasterizerState::CullCounterClockwise;
        CompiledEffectDeviceState deviceState;
        deviceState.blend = &blend;
        deviceState.depthStencil = &depth;
        deviceState.rasterizer = &raster;
        CompiledEffectPassStateChanges changes;
        runtime->ApplyPass(1, deviceState, changes);
        Check(changes.blendChanged &&
                  changes.blend.getColorSourceBlendProperty() == Blend::SourceAlpha &&
                  changes.blend.getColorDestinationBlendProperty() == Blend::InverseSourceAlpha,
              "legacy blend assignments translate from the applied pass");
        Check(changes.depthStencilChanged &&
                  !changes.depthStencil.getDepthBufferEnableProperty() &&
                  !changes.depthStencil.getDepthBufferWriteEnableProperty(),
              "legacy depth assignments translate from the applied pass");
        Check(changes.rasterizerChanged &&
                  changes.rasterizer.getCullModeProperty() == CullMode::None,
              "legacy rasterizer assignments translate from the applied pass");

        CompiledEffectPassStateChanges untouchedChanges;
        runtime->ApplyPass(0, deviceState, untouchedChanges);
        Check(!untouchedChanges.blendChanged && !untouchedChanges.depthStencilChanged &&
                  !untouchedChanges.rasterizerChanged,
              "a pass without state assignments preserves the device selections");

        const auto textureParameter = std::find_if(
            description.parameters.begin(), description.parameters.end(),
            [](const auto& parameter) { return parameter.name == "FxTexture"; });
        Check(textureParameter != description.parameters.end(),
              "texture parameter has a stable reflected runtime index");
        if (textureParameter != description.parameters.end())
        {
            Texture2D white = Texture2D::CreateFromPixels(
                device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
            bool textureAccepted = true;
            try
            {
                runtime->SetParameterTexture(textureParameter->runtimeIndex, &white);
            }
            catch (...)
            {
                textureAccepted = false;
            }
            Check(textureAccepted, "runtime accepts a texture owned by the same RLGL device");
        }

        const float gain = 0.375f;
        runtime->SetParameterValue(0, &gain, sizeof(gain));
        runtime->SetTechnique(1);
        auto clone = runtime->Clone();
        runtime.reset();
        Check(clone != nullptr && clone->GetDescription().techniques.size() == 2,
              "clone is independent and retains the reflected object graph");
        if (clone != nullptr)
        {
            CompiledEffectPassStateChanges cloneChanges;
            bool cloneApplies = true;
            try
            {
                clone->ApplyPass(0, deviceState, cloneChanges);
            }
            catch (...)
            {
                cloneApplies = false;
            }
            Check(cloneApplies, "clone retains parameter storage and selected technique");
        }
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    return CNA::Examples::RunPixelTest<RlglCompiledEffectRuntimeTest>();
}
