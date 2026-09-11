// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareCompiledEffect.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using CNA::Internal::Renderers::CompiledEffectDeviceState;
using CNA::Internal::Renderers::CompiledEffectPassStateChanges;
using CNA::Internal::Renderers::ICompiledEffectRuntime;
using CNA::Internal::Renderers::Software::SoftwareCompiledEffect;
using CNA::Internal::Renderers::Software::SoftwareRenderer;
using CNA::Internal::Renderers::Software::SoftwareShaderProgramEXT;
using CNA::Internal::Renderers::Software::SoftwareShaderStageEXT;
using Microsoft::Xna::Framework::Graphics::Blend;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::CullMode;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
using Microsoft::Xna::Framework::Graphics::TextureFilter;

namespace
{
    int failures = 0;

    void Check(bool condition, const std::string& message)
    {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }

    std::vector<std::uint8_t> Load(const std::string& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
            throw std::runtime_error("Cannot open compiled effect: " + path);
        const std::streamsize size = input.tellg();
        if (size <= 0)
            throw std::runtime_error("Compiled effect is empty: " + path);
        input.seekg(0);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (!input.read(reinterpret_cast<char*>(bytes.data()), size))
            throw std::runtime_error("Cannot read compiled effect: " + path);
        return bytes;
    }

    std::unique_ptr<ICompiledEffectRuntime>
    LoadRuntime(SoftwareRenderer& renderer, const std::string& directory, const char* name)
    {
        const auto bytes = Load(directory + "/" + name);
        return renderer.CreateCompiledEffect(bytes.data(), bytes.size());
    }

    void CheckProgram(const SoftwareShaderProgramEXT* program, SoftwareShaderStageEXT expectedStage,
                      const std::string& label)
    {
        Check(program != nullptr, label + " was not selected");
        if (program == nullptr)
            return;
        Check(program->stage == expectedStage, label + " has the wrong stage");
        Check(program->majorVersion >= 1 && program->majorVersion <= 3,
              label + " has a non-D3D9 shader model");
        Check(program->tokens.size() >= 2, label + " lost its token stream");
        Check(!program->instructions.empty(), label + " has no decoded instructions");
        Check(program->tokens.back() == 0x0000FFFFu, label + " has no final END token");

        std::size_t previousEnd = 1;
        for (const auto& instruction : program->instructions)
        {
            Check(!instruction.tokens.empty(), label + " contains an empty instruction");
            Check(instruction.tokenOffset >= previousEnd,
                  label + " instruction offsets are not monotonic");
            Check(instruction.tokenOffset + instruction.tokens.size() < program->tokens.size(),
                  label + " instruction slice reaches beyond END");
            if (!instruction.tokens.empty() && instruction.tokenOffset < program->tokens.size())
            {
                Check(instruction.tokens.front() == program->tokens[instruction.tokenOffset],
                      label + " instruction slice does not preserve source tokens");
            }
            previousEnd = instruction.tokenOffset + instruction.tokens.size();
        }
    }

    bool ContainsFloat4(std::span<const float> registers, float x, float y, float z, float w)
    {
        constexpr float epsilon = 0.00001f;
        for (std::size_t index = 0; index + 4u <= registers.size(); ++index)
        {
            if (std::abs(registers[index] - x) <= epsilon &&
                std::abs(registers[index + 1u] - y) <= epsilon &&
                std::abs(registers[index + 2u] - z) <= epsilon &&
                std::abs(registers[index + 3u] - w) <= epsilon)
            {
                return true;
            }
        }
        return false;
    }
} // namespace

int main()
{
    try
    {
        SoftwareRenderer renderer(16, 16);
        Check(!renderer.SupportsCompiledEffects(),
              "parser-only SOFTWARE-162 must not advertise shader execution");

        const std::string stockDirectory = CNA_SOFTWARE_STOCK_EFFECT_DIRECTORY;
        for (const char* name :
             {"SpriteEffect.fxb", "BasicEffect.fxb", "AlphaTestEffect.fxb", "DualTextureEffect.fxb",
              "EnvironmentMapEffect.fxb", "SkinnedEffect.fxb"})
        {
            auto runtime = LoadRuntime(renderer, stockDirectory, name);
            Check(runtime != nullptr, std::string(name) + " did not parse");
            if (runtime != nullptr)
            {
                Check(!runtime->GetDescription().techniques.empty(),
                      std::string(name) + " has no reflected techniques");
            }
        }

        const auto authenticBytes =
            Load(std::string(CNA_SOFTWARE_COMPILED_EFFECT_FIXTURE_DIRECTORY) +
                 "/racing-normal-mapping-xna4.fxb");
        Check(authenticBytes.size() == 82656u, "authentic XNA 4 fixture size differs");
        auto authentic =
            renderer.CreateCompiledEffect(authenticBytes.data(), authenticBytes.size());
        Check(authentic->GetDescription().parameters.size() == 19u,
              "authentic XNA 4 parameter reflection differs: " +
                  std::to_string(authentic->GetDescription().parameters.size()));
        Check(authentic->GetDescription().techniques.size() == 14u,
              "authentic XNA 4 technique reflection differs: " +
                  std::to_string(authentic->GetDescription().techniques.size()));

        auto runtime = LoadRuntime(renderer, stockDirectory, "CnaConformanceEffect.fxb");
        const auto& description = runtime->GetDescription();
        Check(description.parameters.size() == 6u, "conformance parameter reflection differs");
        Check(description.techniques.size() == 2u, "conformance technique reflection differs");
        if (description.techniques.size() == 2u)
        {
            Check(description.techniques[0].name == "FirstTechnique",
                  "first technique name differs");
            Check(description.techniques[0].passes.size() == 2u,
                  "first technique pass count differs");
            Check(description.techniques[1].name == "SecondTechnique",
                  "second technique name differs");
        }

        auto* softwareRuntime = dynamic_cast<SoftwareCompiledEffect*>(runtime.get());
        Check(softwareRuntime != nullptr, "renderer returned a foreign runtime type");

        bool rejectedTechnique = false;
        try
        {
            runtime->SetTechnique(99);
        }
        catch (const std::out_of_range&)
        {
            rejectedTechnique = true;
        }
        Check(rejectedTechnique, "out-of-range technique was accepted");

        const float tooLarge[64] = {};
        bool rejectedParameter = false;
        try
        {
            runtime->SetParameterValue(0, tooLarge, sizeof(tooLarge));
        }
        catch (const std::invalid_argument&)
        {
            rejectedParameter = true;
        }
        Check(rejectedParameter, "oversized parameter write was accepted");

        bool rejectedTextureType = false;
        try
        {
            runtime->SetParameterTexture(0, nullptr);
        }
        catch (const std::invalid_argument&)
        {
            rejectedTextureType = true;
        }
        Check(rejectedTextureType, "scalar parameter accepted a texture value");
        runtime->SetParameterTexture(5, nullptr);

        runtime->SetTechnique(0);
        CompiledEffectPassStateChanges changes;
        runtime->ApplyPass(0, {}, changes);
        if (softwareRuntime != nullptr)
        {
            CheckProgram(softwareRuntime->GetVertexProgramEXT(), SoftwareShaderStageEXT::Vertex,
                         "conformance vertex shader");
            CheckProgram(softwareRuntime->GetPixelProgramEXT(), SoftwareShaderStageEXT::Pixel,
                         "conformance pixel shader");
        }
        Check(!changes.blendChanged && !changes.depthStencilChanged && !changes.rasterizerChanged,
              "shader-only pass unexpectedly changed render state");
        const auto sampler = std::find_if(
            changes.samplers.begin(), changes.samplers.end(), [](const auto& change)
            { return !change.vertexStage && change.slot == 0 && change.samplerChanged; });
        Check(sampler != changes.samplers.end(), "P0 did not publish FxSampler state");
        if (sampler != changes.samplers.end())
        {
            Check(sampler->sampler.getFilterProperty() == TextureFilter::MinPointMagLinearMipPoint,
                  "P0 sampler filter differs");
            Check(sampler->sampler.getAddressUProperty() == TextureAddressMode::Mirror,
                  "P0 AddressU differs");
            Check(sampler->sampler.getAddressVProperty() == TextureAddressMode::Clamp,
                  "P0 AddressV differs");
            Check(sampler->sampler.getMaxAnisotropyProperty() == 8, "P0 MaxAnisotropy differs");
        }

        const BlendState blend = BlendState::Opaque;
        const DepthStencilState depth = DepthStencilState::Default;
        const RasterizerState rasterizer = RasterizerState::CullCounterClockwise;
        CompiledEffectDeviceState deviceState;
        deviceState.blend = &blend;
        deviceState.depthStencil = &depth;
        deviceState.rasterizer = &rasterizer;
        runtime->ApplyPass(1, deviceState, changes);
        Check(changes.blendChanged, "state pass did not publish blend state");
        Check(changes.depthStencilChanged, "state pass did not publish depth state");
        Check(changes.rasterizerChanged, "state pass did not publish rasterizer state");
        if (changes.blendChanged)
        {
            Check(changes.blend.getColorSourceBlendProperty() == Blend::SourceAlpha,
                  "state pass source blend differs");
            Check(changes.blend.getColorDestinationBlendProperty() == Blend::InverseSourceAlpha,
                  "state pass destination blend differs");
        }
        if (changes.rasterizerChanged)
        {
            Check(changes.rasterizer.getCullModeProperty() == CullMode::None,
                  "state pass culling differs");
        }

        bool rejectedPass = false;
        try
        {
            runtime->ApplyPass(99, deviceState, changes);
        }
        catch (const std::out_of_range&)
        {
            rejectedPass = true;
        }
        Check(rejectedPass, "out-of-range pass was accepted");

        const float originalTint[4] = {0.5f, 0.25f, 0.75f, 1.0f};
        runtime->SetParameterValue(1, originalTint, sizeof(originalTint));
        runtime->SetTechnique(1);
        auto clone = runtime->Clone();

        const float changedTint[4] = {1.0f, 0.5f, 0.25f, 0.125f};
        runtime->SetParameterValue(1, changedTint, sizeof(changedTint));
        runtime->ApplyPass(0, {}, changes);
        Check(ContainsFloat4(softwareRuntime->GetFloatRegistersEXT(SoftwareShaderStageEXT::Pixel),
                             0.8f, 0.4f, 0.2f, 0.1f),
              "updated Tint did not reach FlatPixelShader's Weights preshader "
              "output");

        runtime.reset();
        clone->ApplyPass(0, {}, changes);
        auto* softwareClone = dynamic_cast<SoftwareCompiledEffect*>(clone.get());
        CheckProgram(softwareClone != nullptr ? softwareClone->GetVertexProgramEXT() : nullptr,
                     SoftwareShaderStageEXT::Vertex, "cloned vertex shader");
        CheckProgram(softwareClone != nullptr ? softwareClone->GetPixelProgramEXT() : nullptr,
                     SoftwareShaderStageEXT::Pixel, "cloned pixel shader");
        if (softwareClone != nullptr)
        {
            Check(ContainsFloat4(softwareClone->GetFloatRegistersEXT(SoftwareShaderStageEXT::Pixel),
                                 0.4f, 0.2f, 0.6f, 0.8f),
                  "clone did not preserve an independent Tint/preshader result");
        }

        const std::vector<std::uint8_t> garbage(512u, 0xABu);
        bool rejectedGarbage = false;
        try
        {
            static_cast<void>(renderer.CreateCompiledEffect(garbage.data(), garbage.size()));
        }
        catch (...)
        {
            rejectedGarbage = true;
        }
        Check(rejectedGarbage, "malformed Effect Framework bytes were accepted");

        bool rejectedNull = false;
        try
        {
            static_cast<void>(renderer.CreateCompiledEffect(nullptr, 0));
        }
        catch (const std::invalid_argument&)
        {
            rejectedNull = true;
        }
        Check(rejectedNull, "null/empty Effect Framework input was accepted");
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL: unexpected exception: " << exception.what() << '\n';
        ++failures;
    }

    if (failures == 0)
        std::cout << "Software compiled-Effect parser/token-IR contract passed\n";
    return failures == 0 ? 0 : 1;
}
