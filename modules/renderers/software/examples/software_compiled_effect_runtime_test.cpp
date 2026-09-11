// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareCompiledEffect.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using CNA::Internal::Renderers::CompiledEffectDeviceState;
using CNA::Internal::Renderers::CompiledEffectPassStateChanges;
using CNA::Internal::Renderers::BlendWriteState;
using CNA::Internal::Renderers::GpuDrawParams;
using CNA::Internal::Renderers::GpuVertexStreamBinding;
using CNA::Internal::Renderers::ICompiledEffectRuntime;
using CNA::Internal::Renderers::IRenderTargetRenderer;
using CNA::Internal::Renderers::RenderTargetBindingDescriptor;
using CNA::Internal::Renderers::Software::SoftwareCompiledEffect;
using CNA::Internal::Renderers::Software::ISoftwarePixelSamplerEXT;
using CNA::Internal::Renderers::Software::SoftwareRenderTargetRenderer;
using CNA::Internal::Renderers::Software::SoftwarePixelSampleRequestEXT;
using CNA::Internal::Renderers::Software::SoftwareRenderer;
using CNA::Internal::Renderers::Software::SoftwareShaderInstructionEXT;
using CNA::Internal::Renderers::Software::SoftwareShaderSemanticValueEXT;
using CNA::Internal::Renderers::Software::SoftwareShaderProgramEXT;
using CNA::Internal::Renderers::Software::SoftwareShaderStageEXT;
using CNA::Internal::Renderers::Software::SoftwareShaderSamplerEXT;
using CNA::Internal::Renderers::Software::SoftwareShaderSamplerTypeEXT;
using CNA::Internal::Renderers::Software::SoftwareTextureLodModeEXT;
using CNA::Internal::Renderers::Software::ExecuteSoftwarePixelShaderEXT;
using CNA::Internal::Renderers::Software::ExecuteSoftwarePixelShaderQuadEXT;
using CNA::Internal::Renderers::Software::ExecuteSoftwareVertexShaderEXT;
using Microsoft::Xna::Framework::Graphics::Blend;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::ClearOptions;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::CullMode;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::SpriteBatch;
using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;
using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
using Microsoft::Xna::Framework::Graphics::TextureFilter;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

namespace CNA::TestSupport
{
    struct CompiledEffectTestAccess
    {
        static std::unique_ptr<Effect> Create(
            GraphicsDevice& device, const std::vector<std::uint8_t>& bytes)
        {
            auto runtime = device.GetRenderer().CreateCompiledEffect(bytes.data(), bytes.size());
            return std::unique_ptr<Effect>(new Effect(device, std::move(runtime), nullptr));
        }
    };
}

namespace
{
    int failures = 0;

    class RecordingPixelSampler final : public ISoftwarePixelSamplerEXT
    {
    public:
        [[nodiscard]] std::array<float, 4> SampleEXT(
            const SoftwarePixelSampleRequestEXT& request) const override
        {
            lastRequest = request;
            ++sampleCount;
            return result;
        }

        std::array<float, 4> result{0.125f, 0.25f, 0.5f, 1.0f};
        mutable SoftwarePixelSampleRequestEXT lastRequest{};
        mutable int sampleCount = 0;
    };

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

    std::uint32_t FindParameter(const ICompiledEffectRuntime& runtime, const std::string& name)
    {
        const auto& parameters = runtime.GetDescription().parameters;
        const auto found = std::find_if(parameters.begin(), parameters.end(), [&](const auto& value)
                                        { return value.name == name; });
        if (found == parameters.end())
            throw std::runtime_error("Missing compiled-effect parameter: " + name);
        return static_cast<std::uint32_t>(std::distance(parameters.begin(), found));
    }

    void ExerciseEveryVertexProgram(ICompiledEffectRuntime& runtime, const std::string& label)
    {
        auto* software = dynamic_cast<SoftwareCompiledEffect*>(&runtime);
        if (software == nullptr)
            throw std::runtime_error(label + " is not a Software compiled effect");
        const SoftwareShaderSemanticValueEXT candidates[] = {
            {MOJOSHADER_USAGE_POSITION, 0u, {0.125f, -0.25f, 0.5f, 1.0f}},
            {MOJOSHADER_USAGE_BLENDWEIGHT, 0u, {1.0f, 0.0f, 0.0f, 0.0f}},
            {MOJOSHADER_USAGE_BLENDINDICES, 0u, {0.0f, 0.0f, 0.0f, 0.0f}},
            {MOJOSHADER_USAGE_NORMAL, 0u, {0.0f, 0.0f, 1.0f, 0.0f}},
            {MOJOSHADER_USAGE_POINTSIZE, 0u, {1.0f, 0.0f, 0.0f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 0u, {0.25f, 0.75f, 0.5f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 1u, {0.75f, 0.25f, 0.5f, 1.0f}},
            {MOJOSHADER_USAGE_TANGENT, 0u, {1.0f, 0.0f, 0.0f, 0.0f}},
            {MOJOSHADER_USAGE_BINORMAL, 0u, {0.0f, 1.0f, 0.0f, 0.0f}},
            {MOJOSHADER_USAGE_COLOR, 0u, {0.25f, 0.5f, 0.75f, 1.0f}},
            {MOJOSHADER_USAGE_COLOR, 1u, {0.75f, 0.5f, 0.25f, 1.0f}},
        };
        const auto& description = runtime.GetDescription();
        int executed = 0;
        for (std::size_t technique = 0; technique < description.techniques.size(); ++technique)
        {
            runtime.SetTechnique(static_cast<std::uint32_t>(technique));
            for (std::size_t pass = 0; pass < description.techniques[technique].passes.size(); ++pass)
            {
                CompiledEffectPassStateChanges changes;
                runtime.ApplyPass(static_cast<std::uint32_t>(pass), {}, changes);
                if (software->GetVertexProgramEXT() == nullptr)
                    continue;
                static_cast<void>(software->ExecuteVertexEXT(candidates));
                ++executed;
            }
        }
        Check(executed > 0, label + " exposed no executable vertex program");
    }

    int ExerciseTextureFreePixelPrograms(ICompiledEffectRuntime& runtime,
                                         const std::string& label)
    {
        auto* software = dynamic_cast<SoftwareCompiledEffect*>(&runtime);
        if (software == nullptr)
            throw std::runtime_error(label + " is not a Software compiled effect");
        const SoftwareShaderSemanticValueEXT inputs[] = {
            {MOJOSHADER_USAGE_COLOR, 0u, {0.25f, 0.5f, 0.75f, 1.0f}},
            {MOJOSHADER_USAGE_COLOR, 1u, {0.75f, 0.5f, 0.25f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 0u, {0.25f, 0.75f, 0.5f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 1u, {0.75f, 0.25f, 0.5f, 1.0f}},
        };
        const auto& description = runtime.GetDescription();
        int executed = 0;
        for (std::size_t technique = 0; technique < description.techniques.size(); ++technique)
        {
            runtime.SetTechnique(static_cast<std::uint32_t>(technique));
            for (std::size_t pass = 0; pass < description.techniques[technique].passes.size(); ++pass)
            {
                CompiledEffectPassStateChanges changes;
                runtime.ApplyPass(static_cast<std::uint32_t>(pass), {}, changes);
                const SoftwareShaderProgramEXT* program = software->GetPixelProgramEXT();
                if (program == nullptr)
                    continue;
                const bool samplesTexture = std::any_of(
                    program->instructions.begin(), program->instructions.end(),
                    [](const SoftwareShaderInstructionEXT& instruction)
                    {
                        return instruction.opcode == 66u;
                    });
                if (samplesTexture)
                    continue;
                const auto result = software->ExecutePixelEXT(inputs);
                Check(result.discarded || result.colorWriteMask != 0u || result.depthWritten,
                      label + " executed a pixel program without an observable result");
                ++executed;
            }
        }
        return executed;
    }

    void ExerciseEveryPixelProgram(ICompiledEffectRuntime& runtime, const std::string& label)
    {
        auto* software = dynamic_cast<SoftwareCompiledEffect*>(&runtime);
        if (software == nullptr)
            throw std::runtime_error(label + " is not a Software compiled effect");
        const SoftwareShaderSemanticValueEXT inputs[] = {
            {MOJOSHADER_USAGE_COLOR, 0u, {0.25f, 0.5f, 0.75f, 1.0f}},
            {MOJOSHADER_USAGE_COLOR, 1u, {0.75f, 0.5f, 0.25f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 0u, {0.25f, 0.75f, 0.5f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 1u, {0.75f, 0.25f, 0.5f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 2u, {0.5f, 0.25f, 0.75f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 3u, {0.2f, 0.4f, 0.6f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 4u, {0.6f, 0.4f, 0.2f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 5u, {0.1f, 0.3f, 0.7f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 6u, {0.7f, 0.3f, 0.1f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 7u, {0.5f, 0.5f, 0.5f, 1.0f}},
        };
        RecordingPixelSampler sampler;
        const auto& description = runtime.GetDescription();
        int executed = 0;
        for (std::size_t technique = 0; technique < description.techniques.size(); ++technique)
        {
            runtime.SetTechnique(static_cast<std::uint32_t>(technique));
            for (std::size_t pass = 0; pass < description.techniques[technique].passes.size(); ++pass)
            {
                CompiledEffectPassStateChanges changes;
                runtime.ApplyPass(static_cast<std::uint32_t>(pass), {}, changes);
                if (software->GetPixelProgramEXT() == nullptr)
                    continue;
                try
                {
                    static_cast<void>(software->ExecutePixelEXT(inputs, &sampler));
                }
                catch (const std::exception& error)
                {
                    throw std::runtime_error(
                        label + " technique " + std::to_string(technique) + " pass " +
                        std::to_string(pass) + " pixel execution failed: " + error.what());
                }
                ++executed;
            }
        }
        Check(executed > 0, label + " exposed no executable pixel program");
    }

    int CountBackbufferColor(SoftwareRenderer& renderer,
                             const std::array<std::uint8_t, 4>& expected)
    {
        std::array<std::uint8_t, 16u * 16u * 4u> pixels{};
        renderer.ReadBackbuffer(0, 0, 16, 16, pixels.data());
        int count = 0;
        for (std::size_t offset = 0; offset < pixels.size(); offset += 4u)
        {
            if (std::equal(expected.begin(), expected.end(), pixels.begin() +
                                                               static_cast<std::ptrdiff_t>(offset)))
                ++count;
        }
        return count;
    }

    std::array<std::uint8_t, 16u * 16u * 4u> ReadBackbufferPixels(
        SoftwareRenderer& renderer)
    {
        std::array<std::uint8_t, 16u * 16u * 4u> pixels{};
        renderer.ReadBackbuffer(0, 0, 16, 16, pixels.data());
        return pixels;
    }

    void CheckVertexInstructionSemantics()
    {
        constexpr std::uint32_t temporary = 0u;
        constexpr std::uint32_t input = 1u;
        constexpr std::uint32_t constant = 2u;
        constexpr std::uint32_t address = 3u;
        constexpr std::uint32_t rasterOutput = 4u;
        constexpr std::uint32_t attributeOutput = 5u;
        constexpr std::uint32_t textureOutput = 6u;
        const auto registerBits = [](std::uint32_t type)
        {
            return ((type & 0x7u) << 28u) | ((type >> 3u) << 11u);
        };
        const auto destination = [&](std::uint32_t type, std::uint32_t number,
                                     std::uint32_t mask = 0xFu)
        {
            return 0x80000000u | registerBits(type) | number | (mask << 16u);
        };
        const auto source = [&](std::uint32_t type, std::uint32_t number,
                                std::uint32_t swizzle = 0xE4u,
                                std::uint32_t modifier = 0u, bool relative = false)
        {
            return 0x80000000u | registerBits(type) | number | (swizzle << 16u) |
                   (modifier << 24u) | (relative ? (1u << 13u) : 0u);
        };
        SoftwareShaderProgramEXT program;
        program.stage = SoftwareShaderStageEXT::Vertex;
        program.majorVersion = 2;
        program.minorVersion = 0;
        program.inputSemantics = {
            {MOJOSHADER_USAGE_POSITION, 0u, 0u},
            {MOJOSHADER_USAGE_TEXCOORD, 0u, 1u},
        };
        const auto add = [&](std::uint16_t opcode, std::initializer_list<std::uint32_t> tokens)
        {
            SoftwareShaderInstructionEXT instruction;
            instruction.opcode = opcode;
            instruction.tokens.assign(tokens);
            program.instructions.push_back(std::move(instruction));
        };

        add(81, {81u, destination(constant, 10), CNA::TestSupport::FloatBits(2.0f),
                 CNA::TestSupport::FloatBits(-1.0f), CNA::TestSupport::FloatBits(0.5f),
                 CNA::TestSupport::FloatBits(3.0f)});
        add(20, {20u, destination(rasterOutput, 0), source(input, 0), source(constant, 0)});
        add(4, {4u, destination(textureOutput, 0), source(input, 1), source(constant, 10),
                source(constant, 11)});
        add(8, {8u, destination(textureOutput, 1), source(input, 0), source(constant, 10)});
        add(7, {7u, destination(textureOutput, 2), source(constant, 12, 0u)});
        add(36, {36u, destination(textureOutput, 3), source(constant, 13)});
        add(46, {46u, destination(address, 0, 0x1u), source(constant, 14, 0u)});
        add(1, {1u, destination(textureOutput, 4),
                source(constant, 20, 0xE4u, 0u, true), source(address, 0, 0u)});
        add(14, {14u, destination(temporary, 0), source(constant, 15)});
        add(15, {15u, destination(textureOutput, 5), source(temporary, 0, 0x00u)});
        add(10, {10u, destination(temporary, 1), source(constant, 16), source(constant, 17)});
        add(11, {11u, destination(temporary, 2), source(temporary, 1), source(constant, 18)});
        add(13, {13u, destination(textureOutput, 6), source(temporary, 2), source(constant, 19)});
        add(1, {1u, destination(attributeOutput, 0), source(constant, 23)});
        add(1, {1u, destination(textureOutput, 7), source(constant, 10, 0xE4u, 1u)});
        add(37, {37u, destination(textureOutput, 7, 0x3u), source(constant, 24, 0x00u),
                 source(constant, 25), source(constant, 26)});
        add(78, {78u, destination(textureOutput, 7, 0x4u), source(constant, 30, 0x00u)});
        add(79, {79u, destination(textureOutput, 7, 0x8u), source(constant, 31, 0x00u)});

        std::array<float, 256u * 4u> floats{};
        const auto setConstant = [&](std::size_t index, std::array<float, 4> value)
        {
            std::copy(value.begin(), value.end(), floats.begin() +
                                                     static_cast<std::ptrdiff_t>(index * 4u));
        };
        setConstant(0, {1.0f, 0.0f, 0.0f, 0.0f});
        setConstant(1, {0.0f, 1.0f, 0.0f, 0.0f});
        setConstant(2, {0.0f, 0.0f, 1.0f, 0.0f});
        setConstant(3, {0.0f, 0.0f, 0.0f, 1.0f});
        setConstant(11, {1.0f, 2.0f, 3.0f, 4.0f});
        setConstant(12, {4.0f, 4.0f, 4.0f, 4.0f});
        setConstant(13, {3.0f, 4.0f, 0.0f, 8.0f});
        setConstant(14, {1.6f, 0.0f, 0.0f, 0.0f});
        setConstant(15, {1.0f, 2.0f, 3.0f, 4.0f});
        setConstant(16, {-2.0f, 5.0f, 1.0f, 8.0f});
        setConstant(17, {-1.0f, 4.0f, 2.0f, 7.0f});
        setConstant(18, {-3.0f, 6.0f, 0.0f, 9.0f});
        setConstant(19, {-2.0f, 7.0f, 0.0f, 9.0f});
        setConstant(22, {22.0f, 23.0f, 24.0f, 25.0f});
        setConstant(23, {-1.0f, 0.5f, 2.0f, 1.0f});
        setConstant(24, {0.5f, 0.0f, 0.0f, 0.0f});
        setConstant(25, {1.0f, 0.0f, 0.0f, 0.0f});
        setConstant(26, {0.0f, 1.0f, 0.0f, 0.0f});
        setConstant(30, {3.0f, 0.0f, 0.0f, 0.0f});
        setConstant(31, {8.0f, 0.0f, 0.0f, 0.0f});
        const std::array<int, 16u * 4u> integers{};
        const std::array<unsigned char, 16> booleans{};
        const SoftwareShaderSemanticValueEXT inputs[] = {
            {MOJOSHADER_USAGE_POSITION, 0u, {0.25f, -0.5f, 0.75f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 0u, {0.5f, 0.25f, -0.25f, 2.0f}},
        };
        const auto result =
            ExecuteSoftwareVertexShaderEXT(program, floats, integers, booleans, inputs);
        Check(result.position == inputs[0].value, "M4X4 identity result differs");
        const auto varying = [&](std::uint8_t index) -> const std::array<float, 4>&
        {
            const auto found = std::find_if(result.varyings.begin(), result.varyings.end(),
                                            [&](const auto& value)
                                            {
                                                return value.usage == MOJOSHADER_USAGE_TEXCOORD &&
                                                       value.usageIndex == index;
                                            });
            if (found == result.varyings.end())
                throw std::runtime_error("Missing interpreter TEXCOORD output");
            return found->value;
        };
        Check(varying(0) == std::array<float, 4>{2.0f, 1.75f, 2.875f, 10.0f},
              "MAD or DEF result differs");
        Check(varying(1) == std::array<float, 4>{1.375f, 1.375f, 1.375f, 1.375f},
              "DP3 result differs");
        Check(varying(2) == std::array<float, 4>{0.5f, 0.5f, 0.5f, 0.5f},
              "RSQ scalar replication differs");
        constexpr float normalizedLength = 5.0f;
        Check(std::abs(varying(3)[0] - 3.0f / normalizedLength) < 0.00001f &&
                  std::abs(varying(3)[1] - 4.0f / normalizedLength) < 0.00001f &&
                  varying(3)[2] == 0.0f &&
                  std::abs(varying(3)[3] - 8.0f / normalizedLength) < 0.00001f,
              "vertex NRM did not use the fixed XYZ source length");
        Check(varying(4) == std::array<float, 4>{22.0f, 23.0f, 24.0f, 25.0f},
              "MOVA/relative constant result differs");
        Check(varying(5) == std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f},
              "EXP/LOG scalar replication differs");
        Check(varying(6) == std::array<float, 4>{1.0f, 0.0f, 1.0f, 1.0f},
              "MIN/MAX/SGE result differs");
        Check(std::abs(varying(7)[0] - std::cos(0.5f)) < 0.00001f &&
                  std::abs(varying(7)[1] - std::sin(0.5f)) < 0.00001f &&
                  varying(7)[2] == 8.0f && varying(7)[3] == 3.0f,
              "vertex SINCOS/EXPP/LOGP or partial destination write differs");
        const auto color = std::find_if(result.varyings.begin(), result.varyings.end(),
                                        [](const auto& value)
                                        {
                                            return value.usage == MOJOSHADER_USAGE_COLOR &&
                                                   value.usageIndex == 0u;
                                        });
        Check(color != result.varyings.end() &&
                  color->value == std::array<float, 4>{0.0f, 0.5f, 1.0f, 1.0f},
              "D3D COLOR output saturation differs");

        SoftwareShaderProgramEXT legacyExpp;
        legacyExpp.stage = SoftwareShaderStageEXT::Vertex;
        legacyExpp.majorVersion = 1u;
        legacyExpp.minorVersion = 1u;
        legacyExpp.inputSemantics = {{MOJOSHADER_USAGE_POSITION, 0u, 0u}};
        legacyExpp.outputSemantics = {
            {MOJOSHADER_USAGE_POSITION, 0u, 0u, rasterOutput},
            {MOJOSHADER_USAGE_TEXCOORD, 0u, 0u, textureOutput},
        };
        SoftwareShaderInstructionEXT movePosition;
        movePosition.opcode = 1u;
        movePosition.tokens = {1u, destination(rasterOutput, 0u), source(input, 0u)};
        legacyExpp.instructions.push_back(std::move(movePosition));
        SoftwareShaderInstructionEXT expp;
        expp.opcode = 78u;
        expp.tokens = {78u, destination(textureOutput, 0u), source(constant, 30u, 0x00u)};
        legacyExpp.instructions.push_back(std::move(expp));
        setConstant(30u, {3.25f, 0.0f, 0.0f, 0.0f});
        const auto legacyResult =
            ExecuteSoftwareVertexShaderEXT(legacyExpp, floats, integers, booleans, inputs);
        const auto legacyVarying = std::find_if(
            legacyResult.varyings.begin(), legacyResult.varyings.end(), [](const auto& value)
            {
                return value.usage == MOJOSHADER_USAGE_TEXCOORD && value.usageIndex == 0u;
            });
        Check(legacyVarying != legacyResult.varyings.end() &&
                  legacyVarying->value[0] == 8.0f && legacyVarying->value[1] == 0.25f &&
                  std::abs(legacyVarying->value[2] - std::exp2(3.25f)) < 0.00001f &&
                  legacyVarying->value[3] == 1.0f,
              "Shader Model 1.1 EXPP did not emit its legacy four-part result");

        setConstant(35u, {-2.0f, 0.0f, 0.0f, 1.0f});
        setConstant(36u, {0.0f, 0.0f, 0.0f, 1.0f});
        SoftwareShaderProgramEXT logProgram;
        logProgram.stage = SoftwareShaderStageEXT::Vertex;
        logProgram.majorVersion = 3u;
        logProgram.outputSemantics = {
            {MOJOSHADER_USAGE_POSITION, 0u, 0u, textureOutput},
            {MOJOSHADER_USAGE_TEXCOORD, 0u, 1u, textureOutput},
        };
        const auto addLog = [&](std::uint16_t opcode,
                                std::initializer_list<std::uint32_t> tokens)
        {
            SoftwareShaderInstructionEXT instruction;
            instruction.opcode = opcode;
            instruction.tokens.assign(tokens);
            logProgram.instructions.push_back(std::move(instruction));
        };
        addLog(1u, {1u, destination(textureOutput, 0u), source(constant, 36u)});
        addLog(15u, {15u, destination(textureOutput, 1u, 0x1u),
                     source(constant, 35u, 0x00u)});
        addLog(15u, {15u, destination(temporary, 0u, 0x2u),
                     source(constant, 35u, 0x55u)});
        addLog(5u, {5u, destination(temporary, 0u, 0x2u),
                    source(temporary, 0u, 0x55u), source(constant, 36u, 0x00u)});
        addLog(13u, {13u, destination(textureOutput, 1u, 0x2u),
                     source(temporary, 0u, 0x55u), source(constant, 36u, 0x00u)});
        addLog(1u, {1u, destination(textureOutput, 1u, 0xCu), source(constant, 36u)});
        const auto logResult = ExecuteSoftwareVertexShaderEXT(
            logProgram, floats, integers, booleans, {});
        Check(logResult.varyings.size() == 1u &&
                  logResult.varyings[0].value ==
                      std::array<float, 4>{1.0f, 1.0f, 0.0f, 1.0f},
              "vertex LOG did not ignore sign or return finite -FLT_MAX for zero");

        SoftwareShaderProgramEXT textureProgram;
        textureProgram.stage = SoftwareShaderStageEXT::Vertex;
        textureProgram.majorVersion = 3u;
        textureProgram.inputSemantics = {
            {MOJOSHADER_USAGE_TEXCOORD, 0u, 0u, input},
        };
        textureProgram.outputSemantics = {
            {MOJOSHADER_USAGE_POSITION, 0u, 0u, textureOutput},
        };
        textureProgram.samplers.push_back(
            SoftwareShaderSamplerEXT{3u, SoftwareShaderSamplerTypeEXT::Volume});
        constexpr std::uint32_t sampler = 10u;
        constexpr std::uint32_t swizzleBgra =
            2u | (1u << 2u) | (0u << 4u) | (3u << 6u);
        SoftwareShaderInstructionEXT textureLookup;
        textureLookup.opcode = 95u;
        textureLookup.tokens = {
            95u, destination(textureOutput, 0u), source(input, 0u),
            source(sampler, 3u, swizzleBgra)};
        textureProgram.instructions.push_back(std::move(textureLookup));
        RecordingPixelSampler recordingSampler;
        const auto textureResult = ExecuteSoftwareVertexShaderEXT(
            textureProgram, floats, integers, booleans, inputs, &recordingSampler);
        Check(recordingSampler.sampleCount == 1 &&
                  recordingSampler.lastRequest.samplerRegister == 3u &&
                  recordingSampler.lastRequest.samplerType ==
                      SoftwareShaderSamplerTypeEXT::Volume &&
                  recordingSampler.lastRequest.lodMode ==
                      SoftwareTextureLodModeEXT::Explicit &&
                  recordingSampler.lastRequest.lod == inputs[1].value[3],
              "vertex TEXLDL lost its sampler, dimension, or explicit LOD");
        Check(textureResult.position ==
                  std::array<float, 4>{0.5f, 0.25f, 0.125f, 1.0f},
              "vertex TEXLDL did not apply the sampler source swizzle to its result");
    }

    void CheckPixelInstructionSemantics()
    {
        constexpr std::uint32_t temporary = 0u;
        constexpr std::uint32_t input = 1u;
        constexpr std::uint32_t constant = 2u;
        constexpr std::uint32_t texture = 3u;
        constexpr std::uint32_t colorOutput = 8u;
        constexpr std::uint32_t depthOutput = 9u;
        const auto registerBits = [](std::uint32_t type)
        {
            return ((type & 0x7u) << 28u) | ((type >> 3u) << 11u);
        };
        const auto destination = [&](std::uint32_t type, std::uint32_t number,
                                     std::uint32_t mask = 0xFu,
                                     std::uint32_t modifier = 0u,
                                     std::uint32_t shift = 0u)
        {
            return 0x80000000u | registerBits(type) | number | (mask << 16u) |
                   (modifier << 20u) | (shift << 24u);
        };
        const auto source = [&](std::uint32_t type, std::uint32_t number,
                                std::uint32_t swizzle = 0xE4u,
                                std::uint32_t modifier = 0u)
        {
            return 0x80000000u | registerBits(type) | number | (swizzle << 16u) |
                   (modifier << 24u);
        };
        SoftwareShaderProgramEXT program;
        program.stage = SoftwareShaderStageEXT::Pixel;
        program.majorVersion = 2;
        program.minorVersion = 0;
        program.inputSemantics = {
            {MOJOSHADER_USAGE_COLOR, 0u, 0u, 1u},
            {MOJOSHADER_USAGE_TEXCOORD, 0u, 0u, 3u},
        };
        const auto add = [&](std::uint16_t opcode, std::initializer_list<std::uint32_t> tokens)
        {
            SoftwareShaderInstructionEXT instruction;
            instruction.opcode = opcode;
            instruction.tokens.assign(tokens);
            program.instructions.push_back(std::move(instruction));
        };

        add(4, {4u, destination(temporary, 0), source(input, 0), source(constant, 0),
                source(texture, 0)});
        add(88, {88u, destination(colorOutput, 0, 0xFu, 1u, 1u),
                 source(constant, 1), source(temporary, 0), source(constant, 2)});
        add(1, {1u, destination(depthOutput, 0, 0x1u), source(constant, 3, 0u)});

        std::array<float, 256u * 4u> floats{};
        const auto setConstant = [&](std::size_t index, std::array<float, 4> value)
        {
            std::copy(value.begin(), value.end(),
                      floats.begin() + static_cast<std::ptrdiff_t>(index * 4u));
        };
        setConstant(0, {2.0f, -1.0f, 0.5f, 1.0f});
        setConstant(1, {1.0f, -1.0f, 0.0f, 2.0f});
        setConstant(2, {0.25f, 0.125f, 0.75f, -2.0f});
        setConstant(3, {0.375f, 0.0f, 0.0f, 0.0f});
        const std::array<int, 16u * 4u> integers{};
        const std::array<unsigned char, 16> booleans{};
        const SoftwareShaderSemanticValueEXT inputs[] = {
            {MOJOSHADER_USAGE_COLOR, 0u, {0.2f, 0.4f, 0.6f, 0.8f}},
            {MOJOSHADER_USAGE_TEXCOORD, 0u, {0.1f, 0.2f, 0.3f, 0.4f}},
        };
        const auto result =
            ExecuteSoftwarePixelShaderEXT(program, floats, integers, booleans, inputs);
        Check(result.colorWriteMask == 1u, "pixel COLOR0 write tracking differs");
        Check(result.colors[0] == std::array<float, 4>{1.0f, 0.25f, 1.0f, 1.0f},
              "pixel MAD/CMP/shift/saturate result differs");
        Check(result.depthWritten && result.depth == 0.375f,
              "pixel depth-output result differs");
        Check(!result.discarded, "ordinary pixel invocation was discarded");

        setConstant(20, {1.0f, 2.0f, 3.0f, 4.0f});
        setConstant(21, {1.0f, 0.0f, 0.0f, 0.0f});
        setConstant(22, {0.0f, 1.0f, 0.0f, 0.0f});
        setConstant(23, {0.0f, 0.0f, 1.0f, 0.0f});
        setConstant(24, {0.0f, 0.0f, 0.0f, 1.0f});
        setConstant(25, {5.0f, 6.0f, 0.0f, 0.0f});
        setConstant(26, {7.0f, 0.0f, 0.0f, 0.0f});
        setConstant(27, {0.5f, 0.0f, 0.0f, 0.0f});
        setConstant(28, {1.0f, 0.0f, 0.0f, 0.0f});
        setConstant(29, {0.0f, 1.0f, 0.0f, 0.0f});
        setConstant(30, {3.0f, 0.0f, 0.0f, 0.0f});
        setConstant(31, {8.0f, 0.0f, 0.0f, 0.0f});
        setConstant(32, {0.5f, 0.5001f, 0.2f, 1.0f});
        setConstant(33, {1.0f, 2.0f, 3.0f, 4.0f});
        setConstant(34, {5.0f, 6.0f, 7.0f, 8.0f});
        setConstant(35, {-2.0f, 0.0f, 0.0f, 0.0f});
        setConstant(36, {3.0f, 4.0f, 12.0f, 26.0f});
        setConstant(37, {0.0f, 0.0f, 0.0f, 1.0f});
        const auto executeArithmetic = [&](std::uint16_t opcode, std::uint32_t writeMask,
                                           std::initializer_list<std::uint32_t> sources,
                                           const std::string& label,
                                           std::uint8_t majorVersion = 2u)
        {
            SoftwareShaderProgramEXT arithmeticProgram;
            arithmeticProgram.stage = SoftwareShaderStageEXT::Pixel;
            arithmeticProgram.majorVersion = majorVersion;
            arithmeticProgram.minorVersion = 0;
            SoftwareShaderInstructionEXT operation;
            operation.opcode = opcode;
            operation.tokens = {opcode, destination(temporary, 0, writeMask)};
            operation.tokens.insert(operation.tokens.end(), sources.begin(), sources.end());
            arithmeticProgram.instructions.push_back(std::move(operation));
            SoftwareShaderInstructionEXT output;
            output.opcode = 1u;
            output.tokens = {1u, destination(colorOutput, 0), source(temporary, 0)};
            arithmeticProgram.instructions.push_back(std::move(output));
            try
            {
                return ExecuteSoftwarePixelShaderEXT(
                    arithmeticProgram, floats, integers, booleans, inputs);
            }
            catch (const std::runtime_error& error)
            {
                Check(false, label + " was rejected: " + error.what());
                return CNA::Internal::Renderers::Software::SoftwarePixelShaderResultEXT{};
            }
        };
        const auto checkArithmetic = [&](std::uint16_t opcode, std::uint32_t writeMask,
                                         std::initializer_list<std::uint32_t> sources,
                                         const std::array<float, 4>& expected,
                                         const std::string& label,
                                         std::uint8_t majorVersion = 2u)
        {
            const auto arithmetic =
                executeArithmetic(opcode, writeMask, sources, label, majorVersion);
            Check(arithmetic.colorWriteMask == 1u && arithmetic.colors[0] == expected,
                  label + " result differs");
        };
        checkArithmetic(20u, 0xFu, {source(constant, 20), source(constant, 21)},
                        {1.0f, 2.0f, 3.0f, 4.0f}, "pixel M4X4");
        checkArithmetic(21u, 0x7u, {source(constant, 20), source(constant, 21)},
                        {1.0f, 2.0f, 3.0f, 0.0f}, "pixel M4X3");
        checkArithmetic(22u, 0xFu, {source(constant, 20), source(constant, 21)},
                        {1.0f, 2.0f, 3.0f, 0.0f}, "pixel M3X4");
        checkArithmetic(23u, 0x7u, {source(constant, 20), source(constant, 21)},
                        {1.0f, 2.0f, 3.0f, 0.0f}, "pixel M3X3");
        checkArithmetic(24u, 0x3u, {source(constant, 20), source(constant, 21)},
                        {1.0f, 2.0f, 0.0f, 0.0f}, "pixel M3X2");
        checkArithmetic(90u, 0xFu,
                        {source(constant, 20), source(constant, 25),
                         source(constant, 26, 0x00u)},
                        {24.0f, 24.0f, 24.0f, 24.0f}, "pixel DP2ADD");
        const auto sincos = executeArithmetic(
            37u, 0x3u,
            {source(constant, 27, 0x00u), source(constant, 28), source(constant, 29)},
            "pixel SINCOS");
        Check(sincos.colorWriteMask == 1u &&
                  std::abs(sincos.colors[0][0] - std::cos(0.5f)) < 0.00001f &&
                  std::abs(sincos.colors[0][1] - std::sin(0.5f)) < 0.00001f &&
                  sincos.colors[0][2] == 0.0f && sincos.colors[0][3] == 0.0f,
              "pixel SINCOS result differs");
        const auto sincosSm3 = executeArithmetic(
            37u, 0x3u, {source(constant, 27, 0x00u)}, "pixel Shader Model 3 SINCOS", 3u);
        Check(sincosSm3.colorWriteMask == 1u &&
                  std::abs(sincosSm3.colors[0][0] - std::cos(0.5f)) < 0.00001f &&
                  std::abs(sincosSm3.colors[0][1] - std::sin(0.5f)) < 0.00001f,
              "pixel Shader Model 3 SINCOS result differs");
        checkArithmetic(78u, 0xFu, {source(constant, 30, 0x00u)},
                        {8.0f, 8.0f, 8.0f, 8.0f}, "pixel Shader Model 1 EXPP", 1u);
        checkArithmetic(79u, 0xFu, {source(constant, 31, 0x00u)},
                        {3.0f, 3.0f, 3.0f, 3.0f}, "pixel Shader Model 1 LOGP", 1u);
        checkArithmetic(79u, 0xFu, {source(constant, 35, 0x00u)},
                        {1.0f, 1.0f, 1.0f, 1.0f}, "pixel LOGP ignores the source sign", 1u);
        checkArithmetic(
            79u, 0xFu, {source(constant, 35, 0x55u)},
            {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
             -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()},
            "pixel LOGP zero result is finite -FLT_MAX", 1u);
        checkArithmetic(15u, 0xFu, {source(constant, 35, 0x00u)},
                        {1.0f, 1.0f, 1.0f, 1.0f}, "pixel LOG ignores the source sign");
        checkArithmetic(
            15u, 0xFu, {source(constant, 35, 0x55u)},
            {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
             -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()},
            "pixel LOG zero result is finite -FLT_MAX");
        const auto normalized = executeArithmetic(
            36u, 0x3u, {source(constant, 36)},
            "pixel NRM uses XYZ length despite an XY destination mask");
        Check(normalized.colorWriteMask == 1u &&
                  std::abs(normalized.colors[0][0] - 3.0f / 13.0f) < 0.000001f &&
                  std::abs(normalized.colors[0][1] - 4.0f / 13.0f) < 0.000001f &&
                  normalized.colors[0][2] == 0.0f && normalized.colors[0][3] == 0.0f,
              "pixel NRM uses XYZ length despite an XY destination mask result differs");
        checkArithmetic(36u, 0x8u, {source(constant, 37)},
                        {0.0f, 0.0f, 0.0f, std::numeric_limits<float>::max()},
                        "pixel NRM zero-XYZ factor scales W by finite FLT_MAX");
        checkArithmetic(80u, 0xFu,
                        {source(constant, 32), source(constant, 33), source(constant, 34)},
                        {5.0f, 2.0f, 7.0f, 4.0f}, "pixel Shader Model 1 CND", 1u);

        constexpr std::uint32_t samplerRegisterType = 10u;
        constexpr std::uint32_t predicateRegisterType = 19u;
        SoftwareShaderProgramEXT textureProgram;
        textureProgram.stage = SoftwareShaderStageEXT::Pixel;
        textureProgram.majorVersion = 3;
        textureProgram.minorVersion = 0;
        textureProgram.inputSemantics = {
            {MOJOSHADER_USAGE_TEXCOORD, 0u, 0u, 1u},
        };
        textureProgram.samplers.push_back(
            SoftwareShaderSamplerEXT{3u, SoftwareShaderSamplerTypeEXT::Volume});
        SoftwareShaderInstructionEXT textureLookup;
        textureLookup.opcode = 66u;
        textureLookup.controls = 2u;
        constexpr std::uint32_t swizzleBgra =
            2u | (1u << 2u) | (0u << 4u) | (3u << 6u);
        textureLookup.tokens = {
            66u, destination(temporary, 0), source(input, 0),
            source(samplerRegisterType, 3, swizzleBgra)};
        textureProgram.instructions.push_back(std::move(textureLookup));
        SoftwareShaderInstructionEXT textureOutput;
        textureOutput.opcode = 1u;
        textureOutput.tokens = {
            1u, destination(colorOutput, 0), source(temporary, 0)};
        textureProgram.instructions.push_back(std::move(textureOutput));
        RecordingPixelSampler recordingSampler;
        const auto sampled = ExecuteSoftwarePixelShaderEXT(
            textureProgram, floats, integers, booleans, inputs, &recordingSampler);
        Check(recordingSampler.sampleCount == 1,
              "TEX did not request exactly one renderer sample");
        Check(recordingSampler.lastRequest.samplerRegister == 3u &&
                  recordingSampler.lastRequest.coordinateRegister == 0u &&
                  recordingSampler.lastRequest.samplerType ==
                      SoftwareShaderSamplerTypeEXT::Volume,
              "TEX lost its sampler register, coordinate register, or dimension");
        Check(recordingSampler.lastRequest.coordinate == inputs[1].value &&
                  recordingSampler.lastRequest.lod == inputs[1].value[3],
              "TEX lost its coordinate or instruction LOD bias");
        const std::array<float, 4> swizzledSample{0.5f, 0.25f, 0.125f, 1.0f};
        Check(sampled.colorWriteMask == 1u && sampled.colors[0] == swizzledSample,
              "TEX did not apply the sampler source swizzle to its result");

        textureProgram.instructions[0].opcode = 95u;
        textureProgram.instructions[0].controls = 0u;
        textureProgram.instructions[0].tokens[0] = 95u;
        const auto sampledExplicit = ExecuteSoftwarePixelShaderEXT(
            textureProgram, floats, integers, booleans, inputs, &recordingSampler);
        Check(recordingSampler.lastRequest.lodMode == SoftwareTextureLodModeEXT::Explicit &&
                  recordingSampler.lastRequest.lod == inputs[1].value[3],
              "TEXLDL did not publish its explicit level");
        Check(sampledExplicit.colors[0] == swizzledSample,
              "TEXLDL did not apply the sampler source swizzle to its result");

        textureProgram.instructions[0].opcode = 93u;
        textureProgram.instructions[0].tokens = {
            93u, destination(temporary, 0), source(input, 0),
            source(samplerRegisterType, 3, swizzleBgra),
            source(input, 0), source(constant, 0)};
        const auto sampledGradients = ExecuteSoftwarePixelShaderEXT(
            textureProgram, floats, integers, booleans, inputs, &recordingSampler);
        Check(recordingSampler.lastRequest.lodMode == SoftwareTextureLodModeEXT::Gradients &&
                  recordingSampler.lastRequest.gradientX == inputs[1].value &&
                  recordingSampler.lastRequest.gradientY ==
                      std::array<float, 4>{2.0f, -1.0f, 0.5f, 1.0f},
              "TEXLDD did not publish both explicit gradients");
        Check(sampledGradients.colors[0] == swizzledSample,
              "TEXLDD did not apply the sampler source swizzle to its result");

        const int samplesBeforeSetp = recordingSampler.sampleCount;
        textureProgram.instructions[0].opcode = 94u;
        textureProgram.instructions[0].tokens = {
            94u, destination(predicateRegisterType, 0),
            source(constant, 0), source(constant, 1)};
        textureProgram.instructions[0].controls = 1u;
        bool setpExecuted = true;
        try
        {
            static_cast<void>(ExecuteSoftwarePixelShaderEXT(
                textureProgram, floats, integers, booleans, inputs, &recordingSampler));
        }
        catch (const std::runtime_error&)
        {
            setpExecuted = false;
        }
        Check(setpExecuted && recordingSampler.sampleCount == samplesBeforeSetp,
              "SETP opcode 94 was rejected or misrouted through TEXLDD sampling");

        SoftwareShaderProgramEXT killProgram;
        killProgram.stage = SoftwareShaderStageEXT::Pixel;
        killProgram.majorVersion = 1;
        killProgram.minorVersion = 1;
        SoftwareShaderInstructionEXT kill;
        kill.opcode = 65u;
        kill.tokens = {65u, destination(texture, 0)};
        killProgram.instructions.push_back(std::move(kill));
        const SoftwareShaderSemanticValueEXT killInputs[] = {
            {MOJOSHADER_USAGE_TEXCOORD, 0u, {0.1f, -0.2f, 0.3f, 0.4f}},
        };
        const auto killed = ExecuteSoftwarePixelShaderEXT(
            killProgram, floats, integers, booleans, killInputs);
        Check(killed.discarded && killed.colorWriteMask == 0u,
              "TEXKILL did not suppress the Shader Model 1 r0 output");
    }

    void CheckPixelDerivativeSemantics()
    {
        constexpr std::uint32_t temporary = 0u;
        constexpr std::uint32_t input = 1u;
        constexpr std::uint32_t colorOutput = 8u;
        const auto registerBits = [](std::uint32_t type)
        {
            return ((type & 0x7u) << 28u) | ((type >> 3u) << 11u);
        };
        const auto destination = [&](std::uint32_t type, std::uint32_t number)
        {
            return 0x80000000u | registerBits(type) | number | (0xFu << 16u);
        };
        const auto source = [&](std::uint32_t type, std::uint32_t number)
        {
            return 0x80000000u | registerBits(type) | number | (0xE4u << 16u);
        };
        SoftwareShaderProgramEXT program;
        program.stage = SoftwareShaderStageEXT::Pixel;
        program.majorVersion = 3u;
        program.inputSemantics.push_back(
            {MOJOSHADER_USAGE_TEXCOORD, 0u, 0u, static_cast<std::uint8_t>(input)});
        const auto append = [&](std::uint16_t opcode,
                                std::initializer_list<std::uint32_t> tokens)
        {
            SoftwareShaderInstructionEXT instruction;
            instruction.opcode = opcode;
            instruction.tokens.assign(tokens);
            program.instructions.push_back(std::move(instruction));
        };
        append(5u, {5u, destination(temporary, 0u), source(input, 0u), source(input, 0u)});
        append(91u, {91u, destination(temporary, 1u), source(temporary, 0u)});
        append(92u, {92u, destination(temporary, 2u), source(temporary, 0u)});
        append(5u, {5u, destination(temporary, 3u), source(temporary, 1u), source(input, 0u)});
        append(91u, {91u, destination(temporary, 4u), source(temporary, 3u)});
        append(1u, {1u, destination(colorOutput, 0u), source(temporary, 1u)});
        append(1u, {1u, destination(colorOutput, 1u), source(temporary, 2u)});
        append(1u, {1u, destination(colorOutput, 2u), source(temporary, 4u)});

        const std::array<std::array<float, 4>, 4> laneValues = {{
            {1.0f, 2.0f, 3.0f, 4.0f},
            {3.0f, 5.0f, 7.0f, 11.0f},
            {2.0f, 4.0f, 8.0f, 16.0f},
            {6.0f, 10.0f, 14.0f, 22.0f},
        }};
        std::array<std::array<SoftwareShaderSemanticValueEXT, 1>, 4> laneInputs{};
        std::array<std::span<const SoftwareShaderSemanticValueEXT>, 4> inputSpans{};
        for (std::size_t lane = 0; lane < laneInputs.size(); ++lane)
        {
            laneInputs[lane][0] =
                {MOJOSHADER_USAGE_TEXCOORD, 0u, laneValues[lane]};
            inputSpans[lane] = laneInputs[lane];
        }
        const std::array<float, 256u * 4u> floats{};
        const std::array<int, 16u * 4u> integers{};
        const std::array<unsigned char, 16u> booleans{};
        const auto result = ExecuteSoftwarePixelShaderQuadEXT(
            program, floats, integers, booleans, inputSpans);
        const std::array<std::array<float, 4>, 4> expectedDx = {{
            {8.0f, 21.0f, 40.0f, 105.0f}, {8.0f, 21.0f, 40.0f, 105.0f},
            {32.0f, 84.0f, 132.0f, 228.0f}, {32.0f, 84.0f, 132.0f, 228.0f},
        }};
        const std::array<std::array<float, 4>, 4> expectedDy = {{
            {3.0f, 12.0f, 55.0f, 240.0f}, {27.0f, 75.0f, 147.0f, 363.0f},
            {3.0f, 12.0f, 55.0f, 240.0f}, {27.0f, 75.0f, 147.0f, 363.0f},
        }};
        const std::array<std::array<float, 4>, 4> expectedDependentDx = {{
            {16.0f, 63.0f, 160.0f, 735.0f}, {16.0f, 63.0f, 160.0f, 735.0f},
            {128.0f, 504.0f, 792.0f, 1368.0f}, {128.0f, 504.0f, 792.0f, 1368.0f},
        }};
        for (std::size_t lane = 0; lane < result.size(); ++lane)
        {
            Check(result[lane].colorWriteMask == 0x7u,
                  "DSX/DSY quad did not write all three diagnostic outputs");
            Check(result[lane].colors[0] == expectedDx[lane],
                  "DSX did not use the adjacent lock-step x lane");
            Check(result[lane].colors[1] == expectedDy[lane],
                  "DSY did not use the adjacent lock-step y lane");
            Check(result[lane].colors[2] == expectedDependentDx[lane],
                  "a later DSX did not observe the earlier derivative result");
        }

        bool scalarRejected = false;
        try
        {
            static_cast<void>(ExecuteSoftwarePixelShaderEXT(
                program, floats, integers, booleans, laneInputs[0]));
        }
        catch (const std::runtime_error& error)
        {
            scalarRejected = std::string(error.what()).find("2x2 quad") != std::string::npos;
        }
        Check(scalarRejected,
              "scalar execution silently approximated an adjacent-lane derivative");
    }

    void CheckVertexConditionalSemantics()
    {
        constexpr std::uint32_t constant = 2u;
        constexpr std::uint32_t outputRegister = 6u;
        constexpr std::uint32_t booleanConstant = 14u;
        constexpr std::uint32_t predicate = 19u;
        const auto registerBits = [](std::uint32_t type)
        {
            return ((type & 0x7u) << 28u) | ((type >> 3u) << 11u);
        };
        const auto destination = [&](std::uint32_t type, std::uint32_t number,
                                     std::uint32_t mask = 0xFu)
        {
            return 0x80000000u | registerBits(type) | number | (mask << 16u);
        };
        const auto source = [&](std::uint32_t type, std::uint32_t number,
                                std::uint32_t swizzle = 0xE4u)
        {
            return 0x80000000u | registerBits(type) | number | (swizzle << 16u);
        };
        SoftwareShaderProgramEXT program;
        program.stage = SoftwareShaderStageEXT::Vertex;
        program.majorVersion = 3;
        program.outputSemantics = {
            {MOJOSHADER_USAGE_POSITION, 0u, 0u, outputRegister},
            {MOJOSHADER_USAGE_TEXCOORD, 0u, 1u, outputRegister},
        };
        for (std::uint8_t index = 0; index < 6u; ++index)
        {
            program.outputSemantics.push_back(
                {MOJOSHADER_USAGE_TEXCOORD, static_cast<std::uint8_t>(index + 1u),
                 static_cast<std::uint16_t>(index + 2u), outputRegister});
        }
        const auto add = [&](std::uint16_t opcode, std::initializer_list<std::uint32_t> tokens,
                             std::uint8_t controls = 0u)
        {
            SoftwareShaderInstructionEXT instruction;
            instruction.opcode = opcode;
            instruction.controls = controls;
            instruction.tokens.assign(tokens);
            program.instructions.push_back(std::move(instruction));
        };
        add(1u, {1u, destination(outputRegister, 0u), source(constant, 0u)});
        add(47u, {47u, destination(booleanConstant, 0u), 1u});
        add(47u, {47u, destination(booleanConstant, 1u), 0u});
        add(94u, {94u, destination(predicate, 0u, 0x3u), source(constant, 1u),
                  source(constant, 2u)}, 1u);
        add(40u, {40u, source(booleanConstant, 0u, 0x00u)});
        add(40u, {40u, source(predicate, 0u, 0x00u)});
        add(1u, {1u, destination(outputRegister, 1u, 0x3u), source(constant, 3u)});
        add(42u, {42u});
        add(1u, {1u, destination(outputRegister, 1u, 0x3u), source(constant, 4u)});
        add(43u, {43u});
        add(42u, {42u});
        add(1u, {1u, destination(outputRegister, 1u, 0x3u), source(constant, 4u)});
        add(43u, {43u});
        add(40u, {40u, source(booleanConstant, 1u, 0x00u)});
        add(40u, {40u, source(booleanConstant, 0u, 0x00u)});
        add(1u, {1u, destination(outputRegister, 1u, 0x1u), source(constant, 4u)});
        add(43u, {43u});
        add(43u, {43u});
        add(41u, {41u, source(constant, 1u, 0x00u), source(constant, 2u, 0x00u)}, 4u);
        add(1u, {1u, destination(outputRegister, 1u, 0xCu), source(constant, 4u)});
        add(42u, {42u});
        add(1u, {1u, destination(outputRegister, 1u, 0xCu), source(constant, 5u)});
        add(43u, {43u});
        for (std::uint8_t control = 1u; control <= 6u; ++control)
        {
            add(94u, {94u, destination(predicate, 0u, 0x1u), source(constant, 6u, 0x00u),
                      source(constant, 7u, 0x00u)}, control);
            add(1u, {1u, destination(outputRegister, static_cast<std::uint32_t>(control + 1u),
                                     0x1u),
                     source(predicate, 0u, 0x00u)});
        }

        std::array<float, 256u * 4u> floats{};
        const auto setConstant = [&](std::size_t index, std::array<float, 4> value)
        {
            std::copy(value.begin(), value.end(),
                      floats.begin() + static_cast<std::ptrdiff_t>(index * 4u));
        };
        setConstant(0u, {0.0f, 0.0f, 0.0f, 1.0f});
        setConstant(1u, {2.0f, 1.0f, 5.0f, 0.0f});
        setConstant(2u, {1.0f, 2.0f, 5.0f, 0.0f});
        setConstant(3u, {10.0f, 20.0f, 30.0f, 40.0f});
        setConstant(4u, {50.0f, 60.0f, 70.0f, 80.0f});
        setConstant(5u, {90.0f, 100.0f, 110.0f, 120.0f});
        setConstant(6u, {2.0f, 0.0f, 0.0f, 0.0f});
        setConstant(7u, {1.0f, 0.0f, 0.0f, 0.0f});
        const std::array<int, 16u * 4u> integers{};
        const std::array<unsigned char, 16> booleans{};
        const auto result = ExecuteSoftwareVertexShaderEXT(
            program, floats, integers, booleans, {});
        Check(result.position == std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f},
              "vertex conditional program lost POSITION0");
        const auto varying = [&](std::uint8_t index) -> const std::array<float, 4>&
        {
            const auto found = std::find_if(result.varyings.begin(), result.varyings.end(),
                                            [&](const auto& value)
                                            {
                                                return value.usage == MOJOSHADER_USAGE_TEXCOORD &&
                                                       value.usageIndex == index;
                                            });
            if (found == result.varyings.end())
                throw std::runtime_error("Missing conditional TEXCOORD output");
            return found->value;
        };
        Check(varying(0u) == std::array<float, 4>{10.0f, 20.0f, 110.0f, 120.0f},
              "nested vertex IF/IFC/ELSE selection differs");
        constexpr std::array<float, 6> expected = {1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f};
        for (std::uint8_t index = 0u; index < expected.size(); ++index)
        {
            Check(varying(static_cast<std::uint8_t>(index + 1u))[0] == expected[index],
                  "vertex SETP comparison control " + std::to_string(index + 1u) + " differs");
        }
    }

    void CheckPixelConditionalSemantics()
    {
        constexpr std::uint32_t constant = 2u;
        constexpr std::uint32_t colorOutput = 8u;
        constexpr std::uint32_t booleanConstant = 14u;
        constexpr std::uint32_t predicate = 19u;
        const auto registerBits = [](std::uint32_t type)
        {
            return ((type & 0x7u) << 28u) | ((type >> 3u) << 11u);
        };
        const auto destination = [&](std::uint32_t type, std::uint32_t number,
                                     std::uint32_t mask = 0xFu)
        {
            return 0x80000000u | registerBits(type) | number | (mask << 16u);
        };
        const auto source = [&](std::uint32_t type, std::uint32_t number,
                                std::uint32_t swizzle = 0xE4u)
        {
            return 0x80000000u | registerBits(type) | number | (swizzle << 16u);
        };
        SoftwareShaderProgramEXT program;
        program.stage = SoftwareShaderStageEXT::Pixel;
        program.majorVersion = 3;
        const auto add = [&](std::uint16_t opcode, std::initializer_list<std::uint32_t> tokens,
                             std::uint8_t controls = 0u)
        {
            SoftwareShaderInstructionEXT instruction;
            instruction.opcode = opcode;
            instruction.controls = controls;
            instruction.tokens.assign(tokens);
            program.instructions.push_back(std::move(instruction));
        };
        add(47u, {47u, destination(booleanConstant, 0u), 1u});
        add(47u, {47u, destination(booleanConstant, 1u), 0u});
        add(94u, {94u, destination(predicate, 0u, 0xFu), source(constant, 0u),
                  source(constant, 1u)}, 2u);
        add(40u, {40u, source(booleanConstant, 0u, 0x00u)});
        add(40u, {40u, source(predicate, 0u, 0xAAu)});
        add(1u, {1u, destination(colorOutput, 0u), source(constant, 2u)});
        add(42u, {42u});
        add(1u, {1u, destination(colorOutput, 0u), source(constant, 3u)});
        add(43u, {43u});
        add(42u, {42u});
        add(1u, {1u, destination(colorOutput, 0u), source(constant, 3u)});
        add(43u, {43u});
        add(40u, {40u, source(booleanConstant, 1u, 0x00u)});
        add(1u, {1u, destination(colorOutput, 0u), source(constant, 3u)});
        add(43u, {43u});
        add(41u, {41u, source(constant, 0u, 0xFFu), source(constant, 1u, 0xFFu)}, 6u);
        add(1u, {1u, destination(colorOutput, 0u, 0x8u), source(constant, 4u)});
        add(43u, {43u});
        add(1u, {1u, destination(colorOutput, 1u), source(constant, 5u)});
        add(1u, {1u, destination(colorOutput, 2u), source(constant, 5u)});
        for (std::uint8_t control = 1u; control <= 6u; ++control)
        {
            const std::uint32_t output = 1u + (control - 1u) / 4u;
            const std::uint32_t mask = 1u << ((control - 1u) % 4u);
            add(41u, {41u, source(constant, 6u, 0x00u),
                      source(constant, 7u, 0x00u)}, control);
            add(1u, {1u, destination(colorOutput, output, mask), source(constant, 8u)});
            add(43u, {43u});
        }

        std::array<float, 256u * 4u> floats{};
        const auto setConstant = [&](std::size_t index, std::array<float, 4> value)
        {
            std::copy(value.begin(), value.end(),
                      floats.begin() + static_cast<std::ptrdiff_t>(index * 4u));
        };
        setConstant(0u, {1.0f, 2.0f, 3.0f, 4.0f});
        setConstant(1u, {1.0f, 0.0f, 3.0f, 5.0f});
        setConstant(2u, {0.1f, 0.2f, 0.3f, 0.4f});
        setConstant(3u, {0.5f, 0.6f, 0.7f, 0.8f});
        setConstant(4u, {0.0f, 0.0f, 0.0f, 0.9f});
        setConstant(5u, {0.0f, 0.0f, 0.0f, 0.0f});
        setConstant(6u, {2.0f, 0.0f, 0.0f, 0.0f});
        setConstant(7u, {1.0f, 0.0f, 0.0f, 0.0f});
        setConstant(8u, {1.0f, 1.0f, 1.0f, 1.0f});
        const std::array<int, 16u * 4u> integers{};
        const std::array<unsigned char, 16> booleans{};
        const auto result = ExecuteSoftwarePixelShaderEXT(
            program, floats, integers, booleans, {});
        Check(result.colorWriteMask == 0x7u &&
                  result.colors[0] == std::array<float, 4>{0.1f, 0.2f, 0.3f, 0.9f},
              "nested pixel SETP/IF/IFC/ELSE or local DEFB result differs");
        Check(result.colors[1] == std::array<float, 4>{1.0f, 0.0f, 1.0f, 0.0f} &&
                  result.colors[2] == std::array<float, 4>{1.0f, 0.0f, 0.0f, 0.0f},
              "pixel IFC comparison controls differ");
    }

    void CheckVertexLoopSemantics()
    {
        constexpr std::uint32_t constant = 2u;
        constexpr std::uint32_t outputRegister = 6u;
        constexpr std::uint32_t integerConstant = 7u;
        constexpr std::uint32_t loop = 15u;
        constexpr std::uint32_t predicate = 19u;
        const auto registerBits = [](std::uint32_t type)
        {
            return ((type & 0x7u) << 28u) | ((type >> 3u) << 11u);
        };
        const auto destination = [&](std::uint32_t type, std::uint32_t number,
                                     std::uint32_t mask = 0xFu)
        {
            return 0x80000000u | registerBits(type) | number | (mask << 16u);
        };
        const auto source = [&](std::uint32_t type, std::uint32_t number,
                                std::uint32_t swizzle = 0xE4u)
        {
            return 0x80000000u | registerBits(type) | number | (swizzle << 16u);
        };
        SoftwareShaderProgramEXT program;
        program.stage = SoftwareShaderStageEXT::Vertex;
        program.majorVersion = 3;
        program.outputSemantics = {
            {MOJOSHADER_USAGE_POSITION, 0u, 0u, outputRegister},
            {MOJOSHADER_USAGE_TEXCOORD, 0u, 1u, outputRegister},
            {MOJOSHADER_USAGE_TEXCOORD, 1u, 2u, outputRegister},
        };
        const auto add = [&](std::uint16_t opcode, std::initializer_list<std::uint32_t> tokens,
                             std::uint8_t controls = 0u)
        {
            SoftwareShaderInstructionEXT instruction;
            instruction.opcode = opcode;
            instruction.controls = controls;
            instruction.tokens.assign(tokens);
            program.instructions.push_back(std::move(instruction));
        };
        add(48u, {48u, destination(integerConstant, 0u), 3u, 10u, 2u, 0u});
        add(48u, {48u, destination(integerConstant, 1u), 5u, 0u, 0u, 0u});
        add(48u, {48u, destination(integerConstant, 2u), 2u, 1u, 1u, 0u});
        add(1u, {1u, destination(outputRegister, 0u), source(constant, 0u)});
        add(1u, {1u, destination(outputRegister, 1u), source(constant, 1u)});
        add(1u, {1u, destination(outputRegister, 2u), source(constant, 1u)});
        add(27u, {27u, source(loop, 0u, 0x00u), source(integerConstant, 0u)});
        add(2u, {2u, destination(outputRegister, 1u, 0x1u),
                 source(outputRegister, 1u), source(constant, 2u)});
        add(2u, {2u, destination(outputRegister, 1u, 0x2u),
                 source(outputRegister, 1u), source(loop, 0u, 0x00u)});
        add(27u, {27u, source(loop, 0u, 0x00u), source(integerConstant, 2u)});
        add(2u, {2u, destination(outputRegister, 2u, 0x4u),
                 source(outputRegister, 2u), source(loop, 0u, 0x00u)});
        add(29u, {29u});
        add(2u, {2u, destination(outputRegister, 2u, 0x8u),
                 source(outputRegister, 2u), source(loop, 0u, 0x00u)});
        add(29u, {29u});
        add(38u, {38u, source(integerConstant, 1u, 0x00u)});
        add(2u, {2u, destination(outputRegister, 1u, 0x4u),
                 source(outputRegister, 1u), source(constant, 2u)});
        add(45u, {45u, source(outputRegister, 1u, 0xAAu),
                  source(constant, 3u, 0x00u)}, 3u);
        add(39u, {39u});
        add(38u, {38u, source(integerConstant, 1u, 0x00u)});
        add(2u, {2u, destination(outputRegister, 1u, 0x8u),
                 source(outputRegister, 1u), source(constant, 2u)});
        add(39u, {39u});
        add(38u, {38u, source(integerConstant, 1u, 0x00u)});
        add(2u, {2u, destination(outputRegister, 2u, 0x1u),
                 source(outputRegister, 2u), source(constant, 2u)});
        add(40u, {40u, source(constant, 6u, 0x00u)});
        add(44u, {44u});
        add(43u, {43u});
        add(2u, {2u, destination(outputRegister, 2u, 0x1u),
                 source(outputRegister, 2u), source(constant, 4u)});
        add(39u, {39u});
        add(38u, {38u, source(integerConstant, 1u, 0x00u)});
        add(2u, {2u, destination(outputRegister, 2u, 0x2u),
                 source(outputRegister, 2u), source(constant, 2u)});
        add(94u, {94u, destination(predicate, 0u, 0x1u),
                  source(outputRegister, 2u, 0x55u), source(constant, 5u, 0x00u)}, 3u);
        add(96u, {96u, source(predicate, 0u, 0x00u)});
        add(39u, {39u});
        add(40u, {40u, source(constant, 7u, 0x00u)});
        add(38u, {38u, source(integerConstant, 16u, 0x00u)});
        add(2u, {2u, destination(outputRegister, 2u, 0x1u),
                 source(outputRegister, 2u), source(constant, 4u)});
        add(39u, {39u});
        add(43u, {43u});

        std::array<float, 256u * 4u> floats{};
        const auto setConstant = [&](std::size_t index, std::array<float, 4> value)
        {
            std::copy(value.begin(), value.end(),
                      floats.begin() + static_cast<std::ptrdiff_t>(index * 4u));
        };
        setConstant(0u, {0.0f, 0.0f, 0.0f, 1.0f});
        setConstant(1u, {0.0f, 0.0f, 0.0f, 0.0f});
        setConstant(2u, {1.0f, 1.0f, 1.0f, 1.0f});
        setConstant(3u, {3.0f, 0.0f, 0.0f, 0.0f});
        setConstant(4u, {10.0f, 10.0f, 10.0f, 10.0f});
        setConstant(5u, {2.0f, 0.0f, 0.0f, 0.0f});
        setConstant(6u, {1.0f, 0.0f, 0.0f, 0.0f});
        setConstant(7u, {0.0f, 0.0f, 0.0f, 0.0f});
        const std::array<int, 16u * 4u> integers{};
        const std::array<unsigned char, 16> booleans{};
        const auto result = ExecuteSoftwareVertexShaderEXT(
            program, floats, integers, booleans, {});
        const auto varying = [&](std::uint8_t index) -> const std::array<float, 4>&
        {
            const auto found = std::find_if(result.varyings.begin(), result.varyings.end(),
                                            [&](const auto& value)
                                            {
                                                return value.usage == MOJOSHADER_USAGE_TEXCOORD &&
                                                       value.usageIndex == index;
                                            });
            if (found == result.varyings.end())
                throw std::runtime_error("Missing loop TEXCOORD output");
            return found->value;
        };
        Check(varying(0u) == std::array<float, 4>{3.0f, 36.0f, 3.0f, 5.0f},
              "vertex LOOP/REP/BREAKC result differs");
        Check(varying(1u) == std::array<float, 4>{1.0f, 2.0f, 9.0f, 36.0f},
              "vertex BREAK/BREAKP or nested aL restoration differs");

        SoftwareShaderProgramEXT hostile = program;
        hostile.instructions.clear();
        const auto addHostile = [&](std::uint16_t opcode,
                                    std::initializer_list<std::uint32_t> tokens)
        {
            SoftwareShaderInstructionEXT instruction;
            instruction.opcode = opcode;
            instruction.tokens.assign(tokens);
            hostile.instructions.push_back(std::move(instruction));
        };
        addHostile(48u, {48u, destination(integerConstant, 0u), 256u, 0u, 1u, 0u});
        addHostile(38u, {38u, source(integerConstant, 0u, 0x00u)});
        addHostile(39u, {39u});
        bool bounded = false;
        try
        {
            static_cast<void>(ExecuteSoftwareVertexShaderEXT(
                hostile, floats, integers, booleans, {}));
        }
        catch (const std::runtime_error& error)
        {
            bounded = std::string(error.what()).find("iteration count") != std::string::npos;
        }
        Check(bounded, "vertex REP accepted an iteration count above the D3D limit");
    }

    void CheckPixelLoopSemantics()
    {
        constexpr std::uint32_t temporary = 0u;
        constexpr std::uint32_t constant = 2u;
        constexpr std::uint32_t integerConstant = 7u;
        constexpr std::uint32_t colorOutput = 8u;
        constexpr std::uint32_t loop = 15u;
        constexpr std::uint32_t predicate = 19u;
        const auto registerBits = [](std::uint32_t type)
        {
            return ((type & 0x7u) << 28u) | ((type >> 3u) << 11u);
        };
        const auto destination = [&](std::uint32_t type, std::uint32_t number,
                                     std::uint32_t mask = 0xFu)
        {
            return 0x80000000u | registerBits(type) | number | (mask << 16u);
        };
        const auto source = [&](std::uint32_t type, std::uint32_t number,
                                std::uint32_t swizzle = 0xE4u)
        {
            return 0x80000000u | registerBits(type) | number | (swizzle << 16u);
        };
        SoftwareShaderProgramEXT program;
        program.stage = SoftwareShaderStageEXT::Pixel;
        program.majorVersion = 3;
        const auto add = [&](std::uint16_t opcode, std::initializer_list<std::uint32_t> tokens,
                             std::uint8_t controls = 0u)
        {
            SoftwareShaderInstructionEXT instruction;
            instruction.opcode = opcode;
            instruction.controls = controls;
            instruction.tokens.assign(tokens);
            program.instructions.push_back(std::move(instruction));
        };
        add(48u, {48u, destination(integerConstant, 0u), 3u, 2u, 2u, 0u});
        add(48u, {48u, destination(integerConstant, 1u), 5u, 0u, 0u, 0u});
        add(1u, {1u, destination(temporary, 0u), source(constant, 0u)});
        add(27u, {27u, source(loop, 0u, 0x00u), source(integerConstant, 0u)});
        add(2u, {2u, destination(temporary, 0u, 0x1u),
                 source(temporary, 0u), source(constant, 1u)});
        add(2u, {2u, destination(temporary, 0u, 0x2u),
                 source(temporary, 0u), source(loop, 0u, 0x00u)});
        add(29u, {29u});
        add(38u, {38u, source(integerConstant, 1u, 0x00u)});
        add(2u, {2u, destination(temporary, 0u, 0x4u),
                 source(temporary, 0u), source(constant, 1u)});
        add(45u, {45u, source(temporary, 0u, 0xAAu),
                  source(constant, 2u, 0x00u)}, 3u);
        add(39u, {39u});
        add(38u, {38u, source(integerConstant, 1u, 0x00u)});
        add(2u, {2u, destination(temporary, 0u, 0x8u),
                 source(temporary, 0u), source(constant, 1u)});
        add(94u, {94u, destination(predicate, 0u, 0x1u),
                  source(temporary, 0u, 0xFFu), source(constant, 3u, 0x00u)}, 3u);
        add(40u, {40u, source(constant, 4u, 0x00u)});
        add(96u, {96u, source(predicate, 0u, 0x00u)});
        add(43u, {43u});
        add(39u, {39u});
        add(1u, {1u, destination(colorOutput, 0u), source(temporary, 0u)});

        std::array<float, 256u * 4u> floats{};
        const auto setConstant = [&](std::size_t index, std::array<float, 4> value)
        {
            std::copy(value.begin(), value.end(),
                      floats.begin() + static_cast<std::ptrdiff_t>(index * 4u));
        };
        setConstant(0u, {0.0f, 0.0f, 0.0f, 0.0f});
        setConstant(1u, {1.0f, 1.0f, 1.0f, 1.0f});
        setConstant(2u, {3.0f, 0.0f, 0.0f, 0.0f});
        setConstant(3u, {2.0f, 0.0f, 0.0f, 0.0f});
        setConstant(4u, {1.0f, 0.0f, 0.0f, 0.0f});
        const std::array<int, 16u * 4u> integers{};
        const std::array<unsigned char, 16> booleans{};
        const auto result = ExecuteSoftwarePixelShaderEXT(
            program, floats, integers, booleans, {});
        Check(result.colorWriteMask == 1u &&
                  result.colors[0] == std::array<float, 4>{3.0f, 12.0f, 3.0f, 2.0f},
              "pixel LOOP/REP/BREAKC/BREAKP result differs");
    }

    void CheckVertexSubroutineSemantics()
    {
        constexpr std::uint32_t constant = 2u;
        constexpr std::uint32_t outputRegister = 6u;
        constexpr std::uint32_t integerConstant = 7u;
        constexpr std::uint32_t booleanConstant = 14u;
        constexpr std::uint32_t loop = 15u;
        constexpr std::uint32_t label = 18u;
        constexpr std::uint32_t predicate = 19u;
        const auto registerBits = [](std::uint32_t type)
        {
            return ((type & 0x7u) << 28u) | ((type >> 3u) << 11u);
        };
        const auto destination = [&](std::uint32_t type, std::uint32_t number,
                                     std::uint32_t mask = 0xFu)
        {
            return 0x80000000u | registerBits(type) | number | (mask << 16u);
        };
        const auto source = [&](std::uint32_t type, std::uint32_t number,
                                std::uint32_t swizzle = 0xE4u)
        {
            return 0x80000000u | registerBits(type) | number | (swizzle << 16u);
        };
        SoftwareShaderProgramEXT program;
        program.stage = SoftwareShaderStageEXT::Vertex;
        program.majorVersion = 3;
        program.outputSemantics = {
            {MOJOSHADER_USAGE_POSITION, 0u, 0u, outputRegister},
            {MOJOSHADER_USAGE_TEXCOORD, 0u, 1u, outputRegister},
        };
        const auto add = [&](std::uint16_t opcode, std::initializer_list<std::uint32_t> tokens,
                             std::uint8_t controls = 0u)
        {
            SoftwareShaderInstructionEXT instruction;
            instruction.opcode = opcode;
            instruction.controls = controls;
            instruction.tokens.assign(tokens);
            program.instructions.push_back(std::move(instruction));
        };

        add(1u, {1u, destination(outputRegister, 0u), source(constant, 0u)});
        add(1u, {1u, destination(outputRegister, 1u), source(constant, 1u)});
        add(25u, {25u, source(label, 0u)});
        add(26u, {26u, source(label, 2u), source(booleanConstant, 0u, 0x00u)});
        add(26u, {26u, source(label, 2u), source(booleanConstant, 1u, 0x00u)});
        add(94u, {94u, destination(predicate, 0u, 0x1u), source(constant, 5u, 0x00u),
                  source(constant, 1u, 0x00u)}, 1u);
        add(40u, {40u, source(booleanConstant, 0u, 0x00u)});
        add(26u, {26u, source(label, 2u), source(predicate, 0u, 0x00u)});
        add(43u, {43u});
        add(27u, {27u, source(loop, 0u, 0x00u), source(integerConstant, 0u)});
        add(25u, {25u, source(label, 3u)});
        add(29u, {29u});
        add(28u, {28u});
        add(30u, {30u, source(label, 0u)});
        add(2u, {2u, destination(outputRegister, 1u, 0x1u),
                 source(outputRegister, 1u), source(constant, 2u)});
        add(25u, {25u, source(label, 1u)});
        add(28u, {28u});
        add(30u, {30u, source(label, 1u)});
        add(2u, {2u, destination(outputRegister, 1u, 0x2u),
                 source(outputRegister, 1u), source(constant, 3u)});
        add(28u, {28u});
        add(30u, {30u, source(label, 2u)});
        add(2u, {2u, destination(outputRegister, 1u, 0x4u),
                 source(outputRegister, 1u), source(constant, 4u)});
        add(28u, {28u});
        add(30u, {30u, source(label, 3u)});
        add(2u, {2u, destination(outputRegister, 1u, 0x8u),
                 source(outputRegister, 1u), source(loop, 0u, 0x00u)});
        add(28u, {28u});
        add(30u, {30u, source(label, 4u)});
        add(2u, {2u, destination(outputRegister, 1u),
                 source(outputRegister, 1u), source(constant, 6u)});
        add(28u, {28u});

        std::array<float, 256u * 4u> floats{};
        const auto setConstant = [&](std::size_t index, std::array<float, 4> value)
        {
            std::copy(value.begin(), value.end(),
                      floats.begin() + static_cast<std::ptrdiff_t>(index * 4u));
        };
        setConstant(0u, {0.0f, 0.0f, 0.0f, 1.0f});
        setConstant(1u, {0.0f, 0.0f, 0.0f, 0.0f});
        setConstant(2u, {1.0f, 0.0f, 0.0f, 0.0f});
        setConstant(3u, {0.0f, 2.0f, 0.0f, 0.0f});
        setConstant(4u, {0.0f, 0.0f, 3.0f, 0.0f});
        setConstant(5u, {1.0f, 0.0f, 0.0f, 0.0f});
        setConstant(6u, {100.0f, 100.0f, 100.0f, 100.0f});
        std::array<int, 16u * 4u> integers{};
        integers[0] = 2;
        integers[1] = 3;
        integers[2] = 4;
        std::array<unsigned char, 16> booleans{};
        booleans[0] = 1u;
        const auto result = ExecuteSoftwareVertexShaderEXT(
            program, floats, integers, booleans, {});
        const auto varying = std::find_if(result.varyings.begin(), result.varyings.end(),
                                          [](const auto& value)
                                          {
                                              return value.usage == MOJOSHADER_USAGE_TEXCOORD &&
                                                     value.usageIndex == 0u;
                                          });
        Check(varying != result.varyings.end() &&
                  varying->value == std::array<float, 4>{1.0f, 2.0f, 6.0f, 10.0f},
              "vertex CALL/CALLNZ/LABEL/RET, nested call or inherited aL result differs");
    }

    void CheckPixelSubroutineSemantics()
    {
        constexpr std::uint32_t temporary = 0u;
        constexpr std::uint32_t constant = 2u;
        constexpr std::uint32_t integerConstant = 7u;
        constexpr std::uint32_t colorOutput = 8u;
        constexpr std::uint32_t booleanConstant = 14u;
        constexpr std::uint32_t loop = 15u;
        constexpr std::uint32_t label = 18u;
        constexpr std::uint32_t predicate = 19u;
        const auto registerBits = [](std::uint32_t type)
        {
            return ((type & 0x7u) << 28u) | ((type >> 3u) << 11u);
        };
        const auto destination = [&](std::uint32_t type, std::uint32_t number,
                                     std::uint32_t mask = 0xFu)
        {
            return 0x80000000u | registerBits(type) | number | (mask << 16u);
        };
        const auto source = [&](std::uint32_t type, std::uint32_t number,
                                std::uint32_t swizzle = 0xE4u)
        {
            return 0x80000000u | registerBits(type) | number | (swizzle << 16u);
        };
        SoftwareShaderProgramEXT program;
        program.stage = SoftwareShaderStageEXT::Pixel;
        program.majorVersion = 3;
        const auto add = [&](std::uint16_t opcode, std::initializer_list<std::uint32_t> tokens,
                             std::uint8_t controls = 0u)
        {
            SoftwareShaderInstructionEXT instruction;
            instruction.opcode = opcode;
            instruction.controls = controls;
            instruction.tokens.assign(tokens);
            program.instructions.push_back(std::move(instruction));
        };

        add(1u, {1u, destination(temporary, 0u), source(constant, 0u)});
        add(25u, {25u, source(label, 0u)});
        add(26u, {26u, source(label, 2u), source(booleanConstant, 0u, 0x00u)});
        add(26u, {26u, source(label, 2u), source(booleanConstant, 1u, 0x00u)});
        add(94u, {94u, destination(predicate, 0u, 0x1u), source(constant, 5u, 0x00u),
                  source(constant, 0u, 0x00u)}, 1u);
        add(40u, {40u, source(booleanConstant, 0u, 0x00u)});
        add(26u, {26u, source(label, 2u), source(predicate, 0u, 0x00u)});
        add(43u, {43u});
        add(27u, {27u, source(loop, 0u, 0x00u), source(integerConstant, 0u)});
        add(25u, {25u, source(label, 3u)});
        add(29u, {29u});
        add(1u, {1u, destination(colorOutput, 0u), source(temporary, 0u)});
        add(28u, {28u});
        add(30u, {30u, source(label, 0u)});
        add(2u, {2u, destination(temporary, 0u, 0x1u),
                 source(temporary, 0u), source(constant, 2u)});
        add(25u, {25u, source(label, 1u)});
        add(28u, {28u});
        add(30u, {30u, source(label, 1u)});
        add(2u, {2u, destination(temporary, 0u, 0x2u),
                 source(temporary, 0u), source(constant, 3u)});
        add(28u, {28u});
        add(30u, {30u, source(label, 2u)});
        add(2u, {2u, destination(temporary, 0u, 0x4u),
                 source(temporary, 0u), source(constant, 4u)});
        add(28u, {28u});
        add(30u, {30u, source(label, 3u)});
        add(2u, {2u, destination(temporary, 0u, 0x8u),
                 source(temporary, 0u), source(loop, 0u, 0x00u)});
        add(28u, {28u});

        std::array<float, 256u * 4u> floats{};
        const auto setConstant = [&](std::size_t index, std::array<float, 4> value)
        {
            std::copy(value.begin(), value.end(),
                      floats.begin() + static_cast<std::ptrdiff_t>(index * 4u));
        };
        setConstant(0u, {0.0f, 0.0f, 0.0f, 0.0f});
        setConstant(2u, {1.0f, 0.0f, 0.0f, 0.0f});
        setConstant(3u, {0.0f, 2.0f, 0.0f, 0.0f});
        setConstant(4u, {0.0f, 0.0f, 3.0f, 0.0f});
        setConstant(5u, {1.0f, 0.0f, 0.0f, 0.0f});
        std::array<int, 16u * 4u> integers{};
        integers[0] = 2;
        integers[1] = 3;
        integers[2] = 4;
        std::array<unsigned char, 16> booleans{};
        booleans[0] = 1u;
        const auto result = ExecuteSoftwarePixelShaderEXT(
            program, floats, integers, booleans, {});
        Check(result.colorWriteMask == 1u &&
                  result.colors[0] == std::array<float, 4>{1.0f, 2.0f, 6.0f, 10.0f},
              "pixel CALL/CALLNZ/LABEL/RET, nested call or inherited aL result differs");
    }

    void CheckSubroutineValidation()
    {
        constexpr std::uint32_t label = 18u;
        const auto registerBits = [](std::uint32_t type)
        {
            return ((type & 0x7u) << 28u) | ((type >> 3u) << 11u);
        };
        const auto source = [&](std::uint32_t type, std::uint32_t number)
        {
            return 0x80000000u | registerBits(type) | number | (0xE4u << 16u);
        };
        const auto append = [](SoftwareShaderProgramEXT& program, std::uint16_t opcode,
                               std::initializer_list<std::uint32_t> tokens)
        {
            SoftwareShaderInstructionEXT instruction;
            instruction.opcode = opcode;
            instruction.tokens.assign(tokens);
            program.instructions.push_back(std::move(instruction));
        };
        const std::array<float, 256u * 4u> floats{};
        const std::array<int, 16u * 4u> integers{};
        const std::array<unsigned char, 16> booleans{};

        for (const SoftwareShaderStageEXT stage :
             {SoftwareShaderStageEXT::Vertex, SoftwareShaderStageEXT::Pixel})
        {
            const std::string stageName =
                stage == SoftwareShaderStageEXT::Vertex ? "vertex" : "pixel";
            const auto expectRejected = [&](SoftwareShaderProgramEXT program,
                                            const std::string& needle,
                                            const std::string& scenario)
            {
                bool rejected = false;
                try
                {
                    if (stage == SoftwareShaderStageEXT::Vertex)
                        static_cast<void>(ExecuteSoftwareVertexShaderEXT(
                            program, floats, integers, booleans, {}));
                    else
                        static_cast<void>(ExecuteSoftwarePixelShaderEXT(
                            program, floats, integers, booleans, {}));
                }
                catch (const std::runtime_error& error)
                {
                    rejected = std::string(error.what()).find(needle) != std::string::npos;
                }
                Check(rejected, stageName + " " + scenario +
                                    " did not report the bounded subroutine error");
            };
            const auto makeProgram = [&](std::uint8_t major = 3u)
            {
                SoftwareShaderProgramEXT program;
                program.stage = stage;
                program.majorVersion = major;
                return program;
            };

            auto missing = makeProgram();
            append(missing, 25u, {25u, source(label, 7u)});
            append(missing, 28u, {28u});
            expectRejected(std::move(missing), "undefined label", "missing-label CALL");

            auto backward = makeProgram();
            append(backward, 25u, {25u, source(label, 0u)});
            append(backward, 28u, {28u});
            append(backward, 30u, {30u, source(label, 0u)});
            append(backward, 25u, {25u, source(label, 0u)});
            append(backward, 28u, {28u});
            expectRejected(std::move(backward), "only forward", "backward CALL");

            auto duplicate = makeProgram();
            append(duplicate, 28u, {28u});
            append(duplicate, 30u, {30u, source(label, 0u)});
            append(duplicate, 28u, {28u});
            append(duplicate, 30u, {30u, source(label, 0u)});
            append(duplicate, 28u, {28u});
            expectRejected(std::move(duplicate), "duplicate LABEL", "duplicate label");

            auto deep = makeProgram();
            append(deep, 25u, {25u, source(label, 0u)});
            append(deep, 28u, {28u});
            for (std::uint32_t index = 0u; index < 5u; ++index)
            {
                append(deep, 30u, {30u, source(label, index)});
                if (index < 4u)
                    append(deep, 25u, {25u, source(label, index + 1u)});
                append(deep, 28u, {28u});
            }
            expectRejected(std::move(deep), "call nesting", "five-deep Shader Model 3 CALL");

            auto shaderModel2 = makeProgram(2u);
            append(shaderModel2, 25u, {25u, source(label, 0u)});
            append(shaderModel2, 28u, {28u});
            append(shaderModel2, 30u, {30u, source(label, 0u)});
            append(shaderModel2, 25u, {25u, source(label, 1u)});
            append(shaderModel2, 28u, {28u});
            append(shaderModel2, 30u, {30u, source(label, 1u)});
            append(shaderModel2, 28u, {28u});
            expectRejected(std::move(shaderModel2), "call nesting",
                           "nested Shader Model 2.0 CALL");

            auto missingReturn = makeProgram();
            append(missingReturn, 25u, {25u, source(label, 0u)});
            append(missingReturn, 28u, {28u});
            append(missingReturn, 30u, {30u, source(label, 0u)});
            append(missingReturn, 0u, {0u});
            expectRejected(std::move(missingReturn), "without RET",
                           "unterminated subroutine");
        }
    }

    void CheckParsedSubroutineEffect(SoftwareRenderer& renderer)
    {
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.pixelShaderUsesSubroutine = true;
        const auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
        auto runtime = renderer.CreateCompiledEffect(bytes.data(), bytes.size());
        const float tint[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        runtime->SetParameterValue(FindParameter(*runtime, "Tint"), tint, sizeof(tint));
        runtime->SetTechnique(0u);
        CompiledEffectPassStateChanges changes;
        runtime->ApplyPass(1u, {}, changes);
        auto* software = dynamic_cast<SoftwareCompiledEffect*>(runtime.get());
        Check(software != nullptr, "parsed subroutine Effect has the wrong backend type");
        if (software == nullptr)
            return;
        const auto result = software->ExecutePixelEXT({});
        Check(result.colorWriteMask == 1u &&
                  result.colors[0] == std::array<float, 4>{0.25f, 0.25f, 0.25f, 1.0f},
              "parsed pixel CALL/CALLNZ/LABEL/RET program produced the wrong result");
    }

    void CheckParsedSignedLogEffect(SoftwareRenderer& renderer)
    {
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.pixelShaderUsesSignedLog = true;
        const auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
        auto runtime = renderer.CreateCompiledEffect(bytes.data(), bytes.size());
        const float tint[4] = {-2.0f, 0.0f, 0.0f, 1.0f};
        runtime->SetParameterValue(FindParameter(*runtime, "Tint"), tint, sizeof(tint));
        runtime->SetTechnique(0u);
        CompiledEffectPassStateChanges changes;
        runtime->ApplyPass(1u, {}, changes);
        auto* software = dynamic_cast<SoftwareCompiledEffect*>(runtime.get());
        Check(software != nullptr, "parsed signed-LOG Effect has the wrong backend type");
        if (software == nullptr)
            return;
        const auto result = software->ExecutePixelEXT({});
        Check(result.colorWriteMask == 1u &&
                  result.colors[0] == std::array<float, 4>{1.0f, 1.0f, 0.0f, 1.0f},
              "parsed pixel LOG did not ignore sign or keep its zero result finite");
    }

    void CheckParsedNrmWriteMaskEffect(SoftwareRenderer& renderer)
    {
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.pixelShaderUsesNrmWriteMask = true;
        const auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
        auto runtime = renderer.CreateCompiledEffect(bytes.data(), bytes.size());
        const float tint[4] = {3.0f, 4.0f, 12.0f, 26.0f};
        runtime->SetParameterValue(FindParameter(*runtime, "Tint"), tint, sizeof(tint));
        runtime->SetTechnique(0u);
        CompiledEffectPassStateChanges changes;
        runtime->ApplyPass(1u, {}, changes);
        auto* software = dynamic_cast<SoftwareCompiledEffect*>(runtime.get());
        Check(software != nullptr, "parsed NRM write-mask Effect has the wrong backend type");
        if (software == nullptr)
            return;
        const auto result = software->ExecutePixelEXT({});
        Check(result.colorWriteMask == 1u &&
                  result.colors[0] == std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f},
              "parsed pixel NRM derived its length from the destination mask");
    }

    void CheckParsedShaderModel11VertexEffects(SoftwareRenderer& renderer)
    {
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.vertexShaderUsesShaderModel11Input = true;
        auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
        auto runtime = renderer.CreateCompiledEffect(bytes.data(), bytes.size());
        const Matrix identity = Matrix::getIdentityProperty();
        runtime->SetParameterValue(FindParameter(*runtime, "Transform"), &identity,
                                   sizeof(identity));
        runtime->SetTechnique(0u);
        CompiledEffectPassStateChanges changes;
        runtime->ApplyPass(1u, {}, changes);
        auto* software = dynamic_cast<SoftwareCompiledEffect*>(runtime.get());
        Check(software != nullptr, "parsed vs_1_1 input Effect has the wrong backend type");
        if (software == nullptr)
            return;
        const SoftwareShaderSemanticValueEXT input = {
            MOJOSHADER_USAGE_POSITION, 0u, {0.25f, -0.5f, 0.75f, 1.0f},
        };
        auto result = software->ExecuteVertexEXT(std::span(&input, 1u));
        Check(result.position == input.value,
              "parsed vs_1_1 vertex program did not bind implicit v0 as POSITION0");

        options.vertexShaderUsesShaderModel11Input = false;
        options.vertexShaderUsesShaderModel11ExtendedInputs = true;
        bytes = CNA::TestSupport::BuildSyntheticEffect(options);
        runtime = renderer.CreateCompiledEffect(bytes.data(), bytes.size());
        runtime->SetParameterValue(FindParameter(*runtime, "Transform"), &identity,
                                   sizeof(identity));
        runtime->SetTechnique(0u);
        runtime->ApplyPass(1u, {}, changes);
        software = dynamic_cast<SoftwareCompiledEffect*>(runtime.get());
        Check(software != nullptr, "parsed extended vs_1_1 input Effect has the wrong backend type");
        if (software == nullptr)
            return;
        const SoftwareShaderProgramEXT* extendedProgram = software->GetVertexProgramEXT();
        Check(extendedProgram != nullptr && extendedProgram->inputSemantics.size() == 3u,
              "parsed extended vs_1_1 program did not expose exactly three fixed inputs");
        const auto hasInput = [extendedProgram](MOJOSHADER_usage usage, std::uint8_t usageIndex,
                                                std::uint16_t registerNumber)
        {
            return extendedProgram != nullptr &&
                   std::find_if(extendedProgram->inputSemantics.begin(),
                                extendedProgram->inputSemantics.end(),
                                [=](const auto& semantic)
                                {
                                    return semantic.usage == usage &&
                                           semantic.usageIndex == usageIndex &&
                                           semantic.registerNumber == registerNumber;
                                }) != extendedProgram->inputSemantics.end();
        };
        Check(hasInput(MOJOSHADER_USAGE_POSITION, 0u, 0u) &&
                  hasInput(MOJOSHADER_USAGE_COLOR, 0u, 5u) &&
                  hasInput(MOJOSHADER_USAGE_TEXCOORD, 7u, 14u),
              "parsed vs_1_1 fixed input table does not contain v0/v5/v14");
        const std::array<SoftwareShaderSemanticValueEXT, 3> extendedInputs = {{
            input,
            {MOJOSHADER_USAGE_COLOR, 0u, {1.0f, 1.0f, 1.0f, 1.0f}},
            {MOJOSHADER_USAGE_TEXCOORD, 7u, {1.0f, 1.0f, 1.0f, 1.0f}},
        }};
        result = software->ExecuteVertexEXT(extendedInputs);
        Check(result.position == input.value,
              "parsed vs_1_1 vertex program did not bind v5/v14 as COLOR0/TEXCOORD7");

        options.vertexShaderUsesShaderModel11ExtendedInputs = false;
        options.vertexShaderUsesLegacyExpp = true;
        bytes = CNA::TestSupport::BuildSyntheticEffect(options);
        runtime = renderer.CreateCompiledEffect(bytes.data(), bytes.size());
        runtime->SetParameterValue(FindParameter(*runtime, "Transform"), &identity,
                                   sizeof(identity));
        runtime->SetTechnique(0u);
        runtime->ApplyPass(1u, {}, changes);
        software = dynamic_cast<SoftwareCompiledEffect*>(runtime.get());
        Check(software != nullptr, "parsed legacy-EXPP Effect has the wrong backend type");
        if (software == nullptr)
            return;
        result = software->ExecuteVertexEXT(std::span(&input, 1u));
        Check(result.position == input.value,
              "parsed vs_1_1 EXPP used the Shader Model 2 replicated result");
    }

    void CheckCompiledRelativeTextureCoordinate()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            PresentationParameters());
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = true;
        options.pixelShaderUsesRelativeTextureCoordinate = true;
        options.samplerStates = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
        };
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));

        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        effect->getParametersProperty()["Tint"]->SetValue(Vector4(0.5f, 0.5f, 0.0f, 0.0f));
        Texture2D texture(device, 8, 8, true, SurfaceFormat::Color);
        const std::vector<Color> base(64, Color::Green);
        const Rectangle baseRectangle(0, 0, 8, 8);
        texture.SetData(0, &baseRectangle, base.data(), 0, static_cast<int>(base.size()));
        for (int level = 1; level < texture.getLevelCountProperty(); ++level)
        {
            const int extent = std::max(1, 8 >> level);
            const std::vector<Color> mip(
                static_cast<std::size_t>(extent * extent), Color::Blue);
            const Rectangle whole(0, 0, extent, extent);
            texture.SetData(level, &whole, mip.data(), 0, static_cast<int>(mip.size()));
        }
        effect->getParametersProperty()["FxTexture"]->SetValue(&texture);

        struct Vertex { float x, y, z, u, v; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const Vertex quad[6] = {
            {-1,  1, 0, 0, 0}, {-1, -1, 0, 0, 8}, { 1, -1, 0, 8, 8},
            {-1,  1, 0, 0, 0}, { 1, -1, 0, 8, 8}, { 1,  1, 0, 8, 0},
        };
        RenderTarget2D target(device, 4, 4);
        device.SetRenderTarget(&target);
        device.Clear(Color::Magenta);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                  static_cast<const void*>(quad), 0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color centre;
        const Rectangle probe(2, 2, 1, 1);
        target.GetData(0, &probe, &centre, 0, 1);
        Check(centre == Color::Green,
              "compiled relative c0[aL] texture coordinate did not sample the uniform base mip");
    }

    void CheckCompiledDependentTemporaryTextureCoordinate()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            PresentationParameters());
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = true;
        options.pixelShaderUsesDependentTemporaryTextureCoordinate = true;
        options.samplerStates = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
        };
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        Texture2D texture(device, 4, 4, true, SurfaceFormat::Color);
        const std::vector<Color> base(16, Color::Red);
        const Rectangle baseRectangle(0, 0, 4, 4);
        texture.SetData(0, &baseRectangle, base.data(), 0, static_cast<int>(base.size()));
        for (int level = 1; level < texture.getLevelCountProperty(); ++level)
        {
            const int extent = std::max(1, 4 >> level);
            const std::vector<Color> mip(
                static_cast<std::size_t>(extent * extent), Color::Blue);
            const Rectangle whole(0, 0, extent, extent);
            texture.SetData(level, &whole, mip.data(), 0, static_cast<int>(mip.size()));
        }
        effect->getParametersProperty()["FxTexture"]->SetValue(&texture);
        struct Vertex { float x, y, z, u, v; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const Vertex quad[6] = {
            {-1,  1, 0, 0, 0},       {-1, -1, 0, 0, 0.25f},
            { 1, -1, 0, 0.25f, 0.25f}, {-1,  1, 0, 0, 0},
            { 1, -1, 0, 0.25f, 0.25f}, { 1,  1, 0, 0.25f, 0},
        };
        RenderTarget2D target(device, 4, 4);
        device.SetRenderTarget(&target);
        device.Clear(Color::Magenta);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                  static_cast<const void*>(quad), 0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color centre;
        const Rectangle probe(2, 2, 1, 1);
        target.GetData(0, &probe, &centre, 0, 1);
        Check(centre == Color::Blue,
              "compiled dependent temporary coordinate did not select the minified mip");
    }

    void CheckCompiledDependentTemporaryCubeCoordinate()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            PresentationParameters());
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = true;
        options.samplerKind = CNA::TestSupport::SyntheticSamplerKind::SamplerCube;
        options.pixelShaderUsesDependentTemporaryTextureCoordinate = true;
        options.samplerStates = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
        };
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        TextureCube cube(device, 8, true, SurfaceFormat::Color);
        for (int face = 0; face < 6; ++face)
        {
            const std::vector<Color> base(64, Color::Red);
            cube.SetData(static_cast<CubeMapFace>(face), base.data(),
                         static_cast<int>(base.size()));
            for (int level = 1; level < cube.getLevelCountProperty(); ++level)
            {
                const int extent = std::max(1, 8 >> level);
                const std::vector<Color> mip(
                    static_cast<std::size_t>(extent * extent), Color::Blue);
                cube.SetData(static_cast<CubeMapFace>(face), level, nullptr,
                             mip.data(), 0, static_cast<int>(mip.size()));
            }
        }
        effect->getParametersProperty()["FxTexture"]->SetValue(&cube);
        struct Vertex { float x, y, z, u, v, w; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const Vertex vertices[6] = {
            {-1,  1, 0, -.125f, -.125f, 1},
            {-1, -1, 0, -.125f,  .125f, 1},
            { 1, -1, 0,  .125f,  .125f, 1},
            {-1,  1, 0, -.125f, -.125f, 1},
            { 1, -1, 0,  .125f,  .125f, 1},
            { 1,  1, 0,  .125f, -.125f, 1},
        };
        RenderTarget2D target(device, 4, 4);
        device.SetRenderTarget(&target);
        device.Clear(Color::Magenta);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                  static_cast<const void*>(vertices), 0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color centre;
        const Rectangle probe(2, 2, 1, 1);
        target.GetData(0, &probe, &centre, 0, 1);
        Check(centre == Color::Blue,
              "compiled dependent temporary cube coordinate did not select the minified mip");
    }

    void CheckCompiledDependentTemporaryVolumeCoordinate()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            PresentationParameters());
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = true;
        options.samplerKind = CNA::TestSupport::SyntheticSamplerKind::Sampler3D;
        options.pixelShaderUsesDependentTemporaryTextureCoordinate = true;
        options.samplerStates = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
            {Fx::SampAddressW, Fx::AddressClamp},
        };
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        Texture3D volume(device, 8, 8, 8, true, SurfaceFormat::Color);
        const std::vector<Color> base(512, Color::Red);
        volume.SetData(base.data(), static_cast<int>(base.size()));
        for (int level = 1; level < volume.getLevelCountProperty(); ++level)
        {
            const int extent = std::max(1, 8 >> level);
            const std::vector<Color> mip(
                static_cast<std::size_t>(extent * extent * extent), Color::Blue);
            volume.SetData(level, 0, 0, extent, extent, 0, extent,
                           mip.data(), 0, static_cast<int>(mip.size()));
        }
        effect->getParametersProperty()["FxTexture"]->SetValue(&volume);
        struct Vertex { float x, y, z, u, v, w; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const Vertex vertices[6] = {
            {-1,  1, 0, 0, 0, .5f},       {-1, -1, 0, 0, .25f, .5f},
            { 1, -1, 0, .25f, .25f, .5f}, {-1,  1, 0, 0, 0, .5f},
            { 1, -1, 0, .25f, .25f, .5f}, { 1,  1, 0, .25f, 0, .5f},
        };
        RenderTarget2D target(device, 4, 4);
        device.SetRenderTarget(&target);
        device.Clear(Color::Magenta);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                  static_cast<const void*>(vertices), 0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color centre;
        const Rectangle probe(2, 2, 1, 1);
        target.GetData(0, &probe, &centre, 0, 1);
        Check(centre == Color::Blue,
              "compiled dependent temporary volume coordinate did not select the minified mip");
    }

    void CheckCompiledDerivativeRasterization()
    {
        GraphicsDevice device;
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.pixelShaderUsesDerivatives = true;
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        effect->getParametersProperty()["Tint"]->SetValue(Vector4::One);
        struct PositionUv
        {
            float x, y, z;
            float u, v;
        };
        const VertexDeclaration declaration(static_cast<int>(sizeof(PositionUv)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const PositionUv quad[6] = {
            {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
            {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
            {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
            { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
            { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
        };
        RenderTarget2D target(device, 4, 4);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                  static_cast<const void*>(quad), 0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::array<Color, 16> pixels{};
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        const auto expect = [&](int x, int y, int red, int green)
        {
            const Color& pixel = pixels[static_cast<std::size_t>(y * 4 + x)];
            Check(std::abs(static_cast<int>(pixel.getRProperty()) - red) <= 2 &&
                      std::abs(static_cast<int>(pixel.getGProperty()) - green) <= 2 &&
                      pixel.getBProperty() == 0 && pixel.getAProperty() == 255,
                  "compiled DSX/DSY raster result differs at " +
                      std::to_string(x) + "," + std::to_string(y) + " (actual " +
                      std::to_string(pixel.getRProperty()) + "," +
                      std::to_string(pixel.getGProperty()) + "," +
                      std::to_string(pixel.getBProperty()) + "," +
                      std::to_string(pixel.getAProperty()) + ")");
        };
        expect(0, 0, 16, 16);
        expect(1, 1, 16, 16);
        expect(2, 0, 80, 16);
        expect(0, 2, 16, 80);
        expect(3, 3, 80, 80);
    }

    void CheckCompiledRasterInputs()
    {
        GraphicsDevice device;
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.pixelShaderUsesRasterInputs = true;
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        effect->getParametersProperty()["Tint"]->SetValue(Vector4::Zero);
        struct Position
        {
            float x, y, z;
        };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Position)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        const Position front[6] = {
            {-1.0f,  1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f},
            { 1.0f,  1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f},
        };
        const Position back[6] = {
            {-1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
            { 1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
        };
        RenderTarget2D target(device, 4, 4);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        const auto drawAndRead = [&](const Position* vertices)
        {
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(vertices), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::array<Color, 16> pixels{};
            target.GetData(pixels.data(), static_cast<int>(pixels.size()));
            return pixels;
        };
        const auto frontPixels = drawAndRead(front);
        const auto backPixels = drawAndRead(back);
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                const Color& frontPixel =
                    frontPixels[static_cast<std::size_t>(y * 4 + x)];
                const Color& backPixel =
                    backPixels[static_cast<std::size_t>(y * 4 + x)];
                const int expectedX = static_cast<int>(std::lround(x * 0.25f * 255.0f));
                const int expectedY = static_cast<int>(std::lround(y * 0.25f * 255.0f));
                Check(std::abs(static_cast<int>(frontPixel.getRProperty()) - expectedX) <= 1 &&
                          std::abs(static_cast<int>(frontPixel.getGProperty()) - expectedY) <= 1 &&
                          frontPixel.getBProperty() == 255 && frontPixel.getAProperty() == 255,
                      "compiled vPos/vFace front result differs at " +
                          std::to_string(x) + "," + std::to_string(y));
                Check(std::abs(static_cast<int>(backPixel.getRProperty()) - expectedX) <= 1 &&
                          std::abs(static_cast<int>(backPixel.getGProperty()) - expectedY) <= 1 &&
                          backPixel.getBProperty() == 0 && backPixel.getAProperty() == 255,
                      "compiled vPos/vFace back result differs at " +
                          std::to_string(x) + "," + std::to_string(y));
            }
        }
    }

    void CheckCompiledInstructionPredication()
    {
        GraphicsDevice device;
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.shadersUsePredication = true;
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());

        struct Position
        {
            float x, y, z;
        };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Position)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        const Position quad[6] = {
            {-1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
            {-1.0f,  1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
        };
        RenderTarget2D target(device, 4, 4);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                  static_cast<const void*>(quad), 0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color centre;
        const Rectangle centreRectangle(2, 2, 1, 1);
        target.GetData(0, &centreRectangle, &centre, 0, 1);
        Check(std::abs(static_cast<int>(centre.getRProperty()) - 223) <= 2 &&
                  std::abs(static_cast<int>(centre.getGProperty()) - 159) <= 2 &&
                  std::abs(static_cast<int>(centre.getBProperty()) - 159) <= 2 &&
                  std::abs(static_cast<int>(centre.getAProperty()) - 32) <= 2,
              "compiled SM3 instruction predication produced the wrong RGBA result");
    }

    void CheckCompiledPredicatedTexkill()
    {
        GraphicsDevice device;
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.pixelShaderUsesPredicatedTexkill = true;
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());

        struct Position { float x, y, z; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Position)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        const Position quad[6] = {
            {-1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
            {-1.0f,  1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
        };
        RenderTarget2D target(device, 4, 4);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        const auto drawAndRead = [&](const Vector4& tint)
        {
            effect->getParametersProperty()["Tint"]->SetValue(tint);
            device.SetRenderTarget(&target);
            device.Clear(Color::Magenta);
            effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(quad), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color centre;
            const Rectangle centreRectangle(2, 2, 1, 1);
            target.GetData(0, &centreRectangle, &centre, 0, 1);
            return centre;
        };

        Check(drawAndRead(Vector4::Zero) == Color::Lime,
              "a false predicate did not suppress compiled TEXKILL");
        Check(drawAndRead(Vector4::One) == Color::Magenta,
              "a true predicate did not execute compiled TEXKILL");
    }

    void CheckCompiledProjectiveSourceModifiers()
    {
        GraphicsDevice device;
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = false;
        options.pixelShaderUsesProjectiveModifiers = true;
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        effect->getParametersProperty()["Tint"]->SetValue(Vector4(0.0f, 0.0f, 0.25f, 1.0f));

        struct Vertex { float x, y, z, u, v, q, w; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 0),
        });
        RenderTarget2D target(device, 4, 4);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        const auto drawAndRead = [&](Effect& selectedEffect, float divisorZ, float divisorW)
        {
            const Vertex quad[6] = {
                {-1,  1, 0, .25f, .5f, divisorZ, divisorW},
                {-1, -1, 0, .25f, .5f, divisorZ, divisorW},
                { 1, -1, 0, .25f, .5f, divisorZ, divisorW},
                {-1,  1, 0, .25f, .5f, divisorZ, divisorW},
                { 1, -1, 0, .25f, .5f, divisorZ, divisorW},
                { 1,  1, 0, .25f, .5f, divisorZ, divisorW},
            };
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            selectedEffect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(quad), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color centre;
            const Rectangle centreRectangle(2, 2, 1, 1);
            target.GetData(0, &centreRectangle, &centre, 0, 1);
            return centre;
        };
        const Color projected = drawAndRead(*effect, 0.5f, 0.25f);
        Check(std::abs(static_cast<int>(projected.getRProperty()) - 128) <= 1 &&
                  projected.getGProperty() == 255 &&
                  std::abs(static_cast<int>(projected.getBProperty()) - 64) <= 1 &&
                  projected.getAProperty() == 255,
              "compiled ps_1_4 _dz/_dw projection produced the wrong color");
        const Color zeroDivisors = drawAndRead(*effect, 0.0f, 0.0f);
        Check(zeroDivisors.getRProperty() == 255 && zeroDivisors.getGProperty() == 255 &&
                  std::abs(static_cast<int>(zeroDivisors.getBProperty()) - 64) <= 1 &&
                  zeroDivisors.getAProperty() == 255,
              "compiled ps_1_4 projective zero divisors did not produce one");

        options.pixelShaderUsesProjectiveModifiers = false;
        options.pixelShaderUsesProjectiveSwizzleModifiers = true;
        auto swizzled = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        swizzled->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        const Color swizzledProjection = drawAndRead(*swizzled, 0.5f, 0.25f);
        Check(swizzledProjection.getRProperty() == 255 &&
                  std::abs(static_cast<int>(swizzledProjection.getGProperty()) - 128) <= 1 &&
                  swizzledProjection.getBProperty() == 255 &&
                  swizzledProjection.getAProperty() == 255,
              "compiled ps_1_4 projection ran before the coordinate source swizzle");
    }

    void CheckCompiledSamplerResultSwizzle()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        GraphicsDevice device;
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = true;
        options.pixelShaderSamplesTexture = true;
        options.pixelShaderSwizzlesSampleResult = true;
        options.samplerStates = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
        };
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        effect->getParametersProperty()["Tint"]->SetValue(Vector4::One);

        Texture2D texture(device, 1, 1);
        const Color sourcePixel(32, 64, 128, 255);
        texture.SetData(&sourcePixel, 1);
        effect->getParametersProperty()["FxTexture"]->SetValue(&texture);

        struct Vertex { float x, y, z, u, v; };
        const Vertex quad[6] = {
            {-1,  1, 0, .5f, .5f}, {-1, -1, 0, .5f, .5f},
            { 1, -1, 0, .5f, .5f}, {-1,  1, 0, .5f, .5f},
            { 1, -1, 0, .5f, .5f}, { 1,  1, 0, .5f, .5f},
        };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        RenderTarget2D target(device, 4, 4);
        device.SetRenderTarget(&target);
        device.Clear(Color::Magenta);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                  static_cast<const void*>(quad), 0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color centre;
        const Rectangle probe(2, 2, 1, 1);
        target.GetData(0, &probe, &centre, 0, 1);
        Check(centre == Color(128, 64, 32, 255),
              "compiled SM3 sampler source swizzle did not reorder the sampled result");
    }

    void CheckCompiledShaderModel14TextureLoad()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        GraphicsDevice device;
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = true;
        options.samplerRegister = 1;
        options.pixelShaderUsesShaderModel14TextureLoad = true;
        options.samplerStates = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
        };
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());

        Texture2D texture(device, 4, 1);
        const Color texels[4] = {Color::Red, Color::Green, Color::Blue, Color::White};
        texture.SetData(texels, 4);
        effect->getParametersProperty()["FxTexture"]->SetValue(&texture);

        struct Vertex { float x, y, z, u, v, q, w; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        RenderTarget2D target(device, 4, 4);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        const auto drawAndRead = [&](float divisorW)
        {
            const Vertex quad[6] = {
                {-1,  1, 0, .2f, .5f, .5f, divisorW},
                {-1, -1, 0, .2f, .5f, .5f, divisorW},
                { 1, -1, 0, .2f, .5f, .5f, divisorW},
                {-1,  1, 0, .2f, .5f, .5f, divisorW},
                { 1, -1, 0, .2f, .5f, .5f, divisorW},
                { 1,  1, 0, .2f, .5f, .5f, divisorW},
            };
            device.SetRenderTarget(&target);
            device.Clear(Color::Magenta);
            effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(quad), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color centre;
            const Rectangle centreRectangle(2, 2, 1, 1);
            target.GetData(0, &centreRectangle, &centre, 0, 1);
            return centre;
        };

        const Color projected = drawAndRead(0.5f);
        const Color zero = drawAndRead(0.0f);
        Check(projected == Color::Green,
              "compiled ps_1_4 TEXLD did not sample stage r1 from projected TEXCOORD0");
        Check(zero == Color::White,
              "compiled ps_1_4 TEXLD _dw did not map a zero divisor to x/y=1");

        Texture2D mipTexture(device, 8, 8, true, SurfaceFormat::Color);
        const std::vector<Color> base(64, Color::Red);
        const Rectangle baseRectangle(0, 0, 8, 8);
        mipTexture.SetData(0, &baseRectangle, base.data(), 0, static_cast<int>(base.size()));
        for (int level = 1; level < mipTexture.getLevelCountProperty(); ++level)
        {
            const int extent = std::max(1, 8 >> level);
            const std::vector<Color> mip(
                static_cast<std::size_t>(extent * extent), Color::Blue);
            const Rectangle whole(0, 0, extent, extent);
            mipTexture.SetData(level, &whole, mip.data(), 0, static_cast<int>(mip.size()));
        }
        effect->getParametersProperty()["FxTexture"]->SetValue(&mipTexture);
        const Vertex projectedQuad[6] = {
            {-1,  1, 0, .25f, .25f, .5f, 1.0f},
            {-1, -1, 0, .25f, .25f, .5f, 1.0f},
            { 1, -1, 0, .25f, .25f, .5f, .125f},
            {-1,  1, 0, .25f, .25f, .5f, 1.0f},
            { 1, -1, 0, .25f, .25f, .5f, .125f},
            { 1,  1, 0, .25f, .25f, .5f, .125f},
        };
        device.SetRenderTarget(&target);
        device.Clear(Color::Magenta);
        effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                  static_cast<const void*>(projectedQuad), 0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color projectedMip;
        const Rectangle centreRectangle(2, 2, 1, 1);
        target.GetData(0, &centreRectangle, &projectedMip, 0, 1);
        Check(projectedMip == Color::Blue,
              "compiled ps_1_4 TEXLD LOD ignored projected-coordinate derivatives");
    }

    void CheckCompiledShaderModel14Phase()
    {
        GraphicsDevice device;
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = false;
        options.pixelShaderUsesShaderModel14Phase = true;
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        effect->getParametersProperty()["Tint"]->SetValue(Vector4(0.0f, 0.0f, 0.25f, 1.0f));

        struct Vertex { float x, y, z, u, v, q, w; };
        const Vertex quad[6] = {
            {-1,  1, 0, .25f, .5f, 0, 1}, {-1, -1, 0, .25f, .5f, 0, 1},
            { 1, -1, 0, .25f, .5f, 0, 1}, {-1,  1, 0, .25f, .5f, 0, 1},
            { 1, -1, 0, .25f, .5f, 0, 1}, { 1,  1, 0, .25f, .5f, 0, 1},
        };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        RenderTarget2D target(device, 4, 4);
        device.SetRenderTarget(&target);
        device.Clear(Color::Magenta);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                  static_cast<const void*>(quad), 0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color centre;
        const Rectangle centreRectangle(2, 2, 1, 1);
        target.GetData(0, &centreRectangle, &centre, 0, 1);
        Check(centre == Color(64, 128, 64, 255),
              "compiled ps_1_4 PHASE did not preserve temporary RGB into phase 2");
    }

    void CheckCompiledShaderModel14PhaseValidation(SoftwareRenderer& renderer)
    {
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.pixelShaderDuplicatesShaderModel14Phase = true;
        const auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
        bool rejected = false;
        try
        {
            static_cast<void>(renderer.CreateCompiledEffect(bytes.data(), bytes.size()));
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        Check(rejected, "compiled ps_1_4 parser accepted a duplicate PHASE marker");
    }

    void CheckCompiledUninitializedTemporaryValidation(SoftwareRenderer& renderer)
    {
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = false;
        options.pixelShaderReadsUninitializedDestination = true;
        const auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
        bool rejected = false;
        try
        {
            static_cast<void>(renderer.CreateCompiledEffect(bytes.data(), bytes.size()));
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        Check(rejected,
              "compiled Effect parser accepted a temporary self-read before initialization");
    }

    void CheckCompiledTexkillTemporaryValidation(SoftwareRenderer& renderer)
    {
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = false;
        options.pixelShaderTexkillReadsPartialTemporary = true;
        const auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
        bool rejected = false;
        try
        {
            static_cast<void>(renderer.CreateCompiledEffect(bytes.data(), bytes.size()));
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        Check(rejected,
              "compiled Effect parser accepted TEXKILL with undefined temporary components");

        options.pixelShaderTexkillReadsPartialTemporary = false;
        options.pixelShaderTexkillReadsSplitTemporary = true;
        const auto splitBytes = CNA::TestSupport::BuildSyntheticEffect(options);
        bool accepted = true;
        try
        {
            static_cast<void>(renderer.CreateCompiledEffect(splitBytes.data(), splitBytes.size()));
        }
        catch (const std::runtime_error&)
        {
            accepted = false;
        }
        Check(accepted,
              "compiled Effect parser rejected TEXKILL after complete split temporary writes");
    }

    void CheckCompiledSgnValidation(SoftwareRenderer& renderer)
    {
        using CNA::TestSupport::SyntheticSgnScratchOperands;
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.vertexShaderSgnScratchOperands =
            SyntheticSgnScratchOperands::ValidUninitialized;
        const auto validBytes = CNA::TestSupport::BuildSyntheticEffect(options);
        bool accepted = true;
        try
        {
            static_cast<void>(renderer.CreateCompiledEffect(validBytes.data(), validBytes.size()));
        }
        catch (const std::runtime_error&)
        {
            accepted = false;
        }
        Check(accepted, "compiled Effect parser rejected valid uninitialized SGN scratch registers");

        const auto expectRejected = [&renderer, &options](const char* message)
        {
            const auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
            bool rejected = false;
            try
            {
                static_cast<void>(renderer.CreateCompiledEffect(bytes.data(), bytes.size()));
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            Check(rejected, message);
        };
        options.vertexShaderSgnScratchOperands = SyntheticSgnScratchOperands::Aliased;
        expectRejected("compiled Effect parser accepted aliased SGN scratch registers");
        options.vertexShaderSgnScratchOperands = SyntheticSgnScratchOperands::NonTemporary;
        expectRejected("compiled Effect parser accepted non-temporary SGN scratch registers");
        options.vertexShaderSgnScratchOperands = SyntheticSgnScratchOperands::None;
        options.pixelShaderUsesInvalidSgn = true;
        expectRejected("compiled Effect parser accepted pixel-shader SGN");
    }

    void CheckCompiledPartialPrecisionOpcodeValidation(SoftwareRenderer& renderer)
    {
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        const auto expectRejected = [&renderer, &options](const char* message)
        {
            const auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
            bool rejected = false;
            try
            {
                static_cast<void>(renderer.CreateCompiledEffect(bytes.data(), bytes.size()));
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            Check(rejected, message);
        };

        options.pixelShaderUsesInvalidExpp = true;
        expectRejected("compiled Effect parser accepted pixel-shader EXPP");
        options.pixelShaderUsesInvalidExpp = false;
        options.pixelShaderUsesInvalidLogp = true;
        expectRejected("compiled Effect parser accepted pixel-shader LOGP");
        options.pixelShaderUsesInvalidLogp = false;
        options.vertexShaderUsesInvalidExppSwizzle = true;
        expectRejected("compiled Effect parser accepted EXPP without a replicate source swizzle");
    }

    void CheckCompiledVertexOnlyAndExpValidation(SoftwareRenderer& renderer)
    {
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        const auto expectRejected = [&renderer, &options](const char* message)
        {
            const auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
            bool rejected = false;
            try
            {
                static_cast<void>(renderer.CreateCompiledEffect(bytes.data(), bytes.size()));
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            Check(rejected, message);
        };

        options.pixelShaderUsesInvalidLit = true;
        expectRejected("compiled Effect parser accepted pixel-shader LIT");
        options.pixelShaderUsesInvalidLit = false;
        options.pixelShaderUsesInvalidSlt = true;
        expectRejected("compiled Effect parser accepted pixel-shader SLT");
        options.pixelShaderUsesInvalidSlt = false;
        options.pixelShaderUsesInvalidSge = true;
        expectRejected("compiled Effect parser accepted pixel-shader SGE");
        options.pixelShaderUsesInvalidSge = false;
        options.pixelShaderUsesInvalidExpSwizzle = true;
        expectRejected("compiled Effect parser accepted pixel EXP without replicate swizzle");
        options.pixelShaderUsesInvalidExpSwizzle = false;
        options.vertexShaderUsesInvalidExpSwizzle = true;
        expectRejected("compiled Effect parser accepted vertex EXP without replicate swizzle");
    }

    void CheckCompiledPixelShaderModel1OpcodeValidation(SoftwareRenderer& renderer)
    {
        using Opcode = CNA::TestSupport::SyntheticInvalidPixelShaderModel1Opcode;
        constexpr std::array opcodes{
            Opcode::Rcp, Opcode::Rsq, Opcode::Min, Opcode::Max, Opcode::Exp,
            Opcode::Log, Opcode::Frc, Opcode::Pow, Opcode::Crs, Opcode::Abs,
            Opcode::Nrm, Opcode::Dsx, Opcode::Dsy,
        };
        for (const Opcode opcode : opcodes)
        {
            CNA::TestSupport::SyntheticEffectOptions options;
            options.includeDrawableProgram = true;
            options.pixelShaderModel1InvalidOpcode = opcode;
            const auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
            bool rejected = false;
            try
            {
                static_cast<void>(renderer.CreateCompiledEffect(bytes.data(), bytes.size()));
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            Check(rejected, "compiled Effect parser accepted a Shader Model 2+ opcode in ps_1_4");
        }
    }

    void CheckCompiledVertexShaderModel1OpcodeValidation(SoftwareRenderer& renderer)
    {
        using Opcode = CNA::TestSupport::SyntheticInvalidVertexShaderModel1Opcode;
        constexpr std::array opcodes{
            Opcode::Abs, Opcode::Crs, Opcode::Nrm, Opcode::Pow, Opcode::SinCos,
            Opcode::Sgn, Opcode::Mova, Opcode::Defb, Opcode::Defi,
        };
        for (const Opcode opcode : opcodes)
        {
            CNA::TestSupport::SyntheticEffectOptions options;
            options.includeDrawableProgram = true;
            options.vertexShaderModel1InvalidOpcode = opcode;
            const auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
            bool rejected = false;
            try
            {
                static_cast<void>(renderer.CreateCompiledEffect(bytes.data(), bytes.size()));
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            Check(rejected, "compiled Effect parser accepted a later-profile opcode in vs_1_1");
        }
    }

    void CheckCompiledPixelShaderModel20OpcodeValidation(SoftwareRenderer& renderer)
    {
        using Opcode = CNA::TestSupport::SyntheticInvalidPixelShaderModel20Opcode;
        constexpr std::array opcodes{
            Opcode::Defb, Opcode::Defi, Opcode::Rep, Opcode::If,
            Opcode::Dsx, Opcode::Dsy, Opcode::Setp,
        };
        for (const Opcode opcode : opcodes)
        {
            CNA::TestSupport::SyntheticEffectOptions options;
            options.includeDrawableProgram = true;
            options.pixelShaderModel20InvalidOpcode = opcode;
            const auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
            bool rejected = false;
            try
            {
                static_cast<void>(renderer.CreateCompiledEffect(bytes.data(), bytes.size()));
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            Check(rejected, "compiled Effect parser accepted a later-profile opcode in ps_2_0");
        }
    }

    void CheckCompiledLegacyTextureMatrix()
    {
        GraphicsDevice device;
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = false;
        options.pixelShaderUsesLegacyTextureMatrix = true;
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());

        struct Vertex { float x, y, z; };
        const Vertex vertices[6] = {
            {-1,  1, 0}, {-1, -1, 0}, { 1, -1, 0},
            {-1,  1, 0}, { 1, -1, 0}, { 1,  1, 0},
        };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        RenderTarget2D target(device, 4, 4);
        device.SetRenderTarget(&target);
        device.Clear(Color::Magenta);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                  static_cast<const void*>(vertices), 0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color centre;
        const Rectangle centreRectangle(2, 2, 1, 1);
        target.GetData(0, &centreRectangle, &centre, 0, 1);
        Check(centre == Color(64, 128, 64, 255),
              "compiled ps_1_2 TEXM3X3 did not produce the exact matrix product");
    }

    void CheckCompiledLegacyTextureMatrix2()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        GraphicsDevice device;
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = true;
        options.samplerRegister = 2;
        options.pixelShaderUsesLegacyTextureMatrix2 = true;
        options.samplerStates = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
        };
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());

        Texture2D texture(device, 8, 8, /*mipMap=*/true, SurfaceFormat::Color);
        std::vector<Color> base(64, Color::Red);
        for (int y = 4; y < 8; ++y)
            for (int x = 0; x < 4; ++x)
                base[static_cast<std::size_t>(y * 8 + x)] = Color::Green;
        const Rectangle wholeBase(0, 0, 8, 8);
        texture.SetData(0, &wholeBase, base.data(), 0, static_cast<int>(base.size()));
        for (int level = 1; level < texture.getLevelCountProperty(); ++level)
        {
            const int extent = std::max(1, 8 >> level);
            std::vector<Color> mip(static_cast<std::size_t>(extent * extent), Color::Blue);
            const Rectangle whole(0, 0, extent, extent);
            texture.SetData(level, &whole, mip.data(), 0, static_cast<int>(mip.size()));
        }
        effect->getParametersProperty()["FxTexture"]->SetValue(&texture);

        struct Vertex { float x, y, z, u, v, w; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const auto render = [&](bool minify)
        {
            const float left = minify ? 0.0f : 1.0f;
            const float right = minify ? 8.0f : 1.0f;
            const Vertex vertices[6] = {
                {-1,  1, 0, 0, 0, left}, {-1, -1, 0, 0, 0, left},
                { 1, -1, 0, 0, 0, right}, {-1,  1, 0, 0, 0, left},
                { 1, -1, 0, 0, 0, right}, { 1,  1, 0, 0, 0, right},
            };
            RenderTarget2D target(device, 4, 4);
            device.SetRenderTarget(&target);
            device.Clear(Color::Magenta);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(vertices), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color centre;
            const Rectangle centreRectangle(2, 2, 1, 1);
            target.GetData(0, &centreRectangle, &centre, 0, 1);
            return centre;
        };
        Check(render(/*minify=*/false) == Color::Green,
              "compiled ps_1_2 TEXM3X2 did not sample the matrix product");
        Check(render(/*minify=*/true) == Color::Blue,
              "compiled ps_1_2 TEXM3X2 did not derive implicit LOD from the matrix product");
    }

    void CheckCompiledLegacyTextureMatrix3Sample()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            PresentationParameters());
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = true;
        options.samplerRegister = 3;
        options.samplerKind = CNA::TestSupport::SyntheticSamplerKind::Sampler3D;
        options.pixelShaderUsesLegacyTextureMatrix3Sample = true;
        options.samplerStates = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
            {Fx::SampAddressW, Fx::AddressClamp},
        };
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());

        Texture3D texture(device, 8, 8, 8, /*mipMap=*/true, SurfaceFormat::Color);
        std::vector<Color> base(512, Color::Red);
        for (int z = 4; z < 8; ++z)
            for (int y = 4; y < 8; ++y)
                for (int x = 0; x < 4; ++x)
                    base[static_cast<std::size_t>(z * 64 + y * 8 + x)] = Color::Green;
        texture.SetData(base.data(), static_cast<int>(base.size()));
        for (int level = 1; level < texture.getLevelCountProperty(); ++level)
        {
            const int extent = std::max(1, 8 >> level);
            std::vector<Color> mip(
                static_cast<std::size_t>(extent * extent * extent), Color::Blue);
            texture.SetData(level, 0, 0, extent, extent, 0, extent,
                            mip.data(), 0, static_cast<int>(mip.size()));
        }
        effect->getParametersProperty()["FxTexture"]->SetValue(&texture);

        struct Vertex { float x, y, z, u, v, w; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const auto render = [&](Effect& selectedEffect, bool minify)
        {
            const float left = minify ? 0.0f : 0.125f;
            const float right = minify ? 0.5f : 0.125f;
            const Vertex vertices[6] = {
                {-1,  1, 0, 0, 0, left}, {-1, -1, 0, 0, 0, left},
                { 1, -1, 0, 0, 0, right}, {-1,  1, 0, 0, 0, left},
                { 1, -1, 0, 0, 0, right}, { 1,  1, 0, 0, 0, right},
            };
            RenderTarget2D target(device, 4, 4);
            device.SetRenderTarget(&target);
            device.Clear(Color::Magenta);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            selectedEffect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(vertices), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color centre;
            const Rectangle centreRectangle(2, 2, 1, 1);
            target.GetData(0, &centreRectangle, &centre, 0, 1);
            return centre;
        };
        Check(render(*effect, /*minify=*/false) == Color::Green,
              "compiled ps_1_2 TEXM3X3TEX did not sample the matrix product");
        Check(render(*effect, /*minify=*/true) == Color::Blue,
              "compiled ps_1_2 TEXM3X3TEX did not derive implicit LOD from the matrix product");

        options.samplerKind = CNA::TestSupport::SyntheticSamplerKind::SamplerCube;
        auto cubeEffect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        cubeEffect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        TextureCube cube(device, 2, /*mipMap=*/false, SurfaceFormat::Color);
        for (int face = 0; face < 6; ++face)
        {
            const Color color = face == 2 ? Color::Yellow : Color::Red;
            const Color texels[4] = {color, color, color, color};
            cube.SetData(static_cast<CubeMapFace>(face), texels, 4);
        }
        cubeEffect->getParametersProperty()["FxTexture"]->SetValue(&cube);
        Check(render(*cubeEffect, /*minify=*/false) == Color::Yellow,
              "compiled ps_1_2 TEXM3X3TEX did not sample its cube direction");
    }

    void CheckCompiledLegacyTextureMatrix3Specular()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            PresentationParameters());
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = true;
        options.samplerRegister = 3;
        options.samplerKind = CNA::TestSupport::SyntheticSamplerKind::SamplerCube;
        options.pixelShaderUsesLegacyTextureMatrix3Specular = true;
        options.samplerStates = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
            {Fx::SampAddressW, Fx::AddressClamp},
        };
        auto constantEye = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        constantEye->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());

        options.pixelShaderUsesLegacyTextureMatrix3Specular = false;
        options.pixelShaderUsesLegacyTextureMatrix3VertexSpecular = true;
        auto varyingEye = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticEffect(options));
        varyingEye->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());

        constexpr int cubeSize = 256;
        TextureCube cube(device, cubeSize, /*mipMap=*/true, SurfaceFormat::Color);
        const Color faceColors[6] = {
            Color::Red, Color::Green, Color::Blue, Color::Cyan, Color::Magenta, Color::Yellow};
        for (int face = 0; face < 6; ++face)
        {
            std::vector<Color> texels(
                static_cast<std::size_t>(cubeSize * cubeSize), faceColors[face]);
            cube.SetData(static_cast<CubeMapFace>(face), texels.data(),
                         static_cast<int>(texels.size()));
            for (int level = 1; level < cube.getLevelCountProperty(); ++level)
            {
                const int extent = std::max(1, cubeSize >> level);
                std::vector<Color> mip(static_cast<std::size_t>(extent * extent), Color::Black);
                cube.SetData(static_cast<CubeMapFace>(face), level, nullptr,
                             mip.data(), 0, static_cast<int>(mip.size()));
            }
        }
        constantEye->getParametersProperty()["FxTexture"]->SetValue(&cube);
        varyingEye->getParametersProperty()["FxTexture"]->SetValue(&cube);

        struct Vertex { float x, y, z, nx, ny, nz, eyeZ; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const auto render = [&](Effect& effect, bool minifyEye)
        {
            const float leftEye = minifyEye ? .4f : 1.0f;
            const float rightEye = minifyEye ? 2.0f : 1.0f;
            const Vertex vertices[6] = {
                {-1,  1, 0, .1f, 1, .1f, leftEye},
                {-1, -1, 0, .1f, 1, .1f, leftEye},
                { 1, -1, 0, .1f, 1, .1f, rightEye},
                {-1,  1, 0, .1f, 1, .1f, leftEye},
                { 1, -1, 0, .1f, 1, .1f, rightEye},
                { 1,  1, 0, .1f, 1, .1f, rightEye},
            };
            RenderTarget2D target(device, 4, 4);
            device.SetRenderTarget(&target);
            device.Clear(Color::White);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(vertices), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color centre;
            const Rectangle centreRectangle(2, 2, 1, 1);
            target.GetData(0, &centreRectangle, &centre, 0, 1);
            return centre;
        };

        Check(render(*constantEye, /*minifyEye=*/false) == Color::Green,
              "compiled ps_1_2 TEXM3X3SPEC did not reflect the constant eye ray to -X");
        Check(render(*varyingEye, /*minifyEye=*/false) == Color::Yellow,
              "compiled ps_1_2 TEXM3X3VSPEC did not read the varying eye ray from row w");
        Check(render(*varyingEye, /*minifyEye=*/true) == Color::Black,
              "compiled ps_1_2 TEXM3X3VSPEC did not derive LOD from the varying eye ray");
    }

    void CheckCompiledLegacyDepthOutputs()
    {
        using CNA::TestSupport::SyntheticLegacyDepthOutput;
        GraphicsDevice device;
        const auto render = [&](SyntheticLegacyDepthOutput instruction, bool zeroDivisor)
        {
            CNA::TestSupport::SyntheticEffectOptions options;
            options.includeDrawableProgram = true;
            options.pixelShaderLegacyDepthOutput = instruction;
            options.pixelShaderLegacyDepthZeroDivisor = zeroDivisor;
            auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
                device, CNA::TestSupport::BuildSyntheticEffect(options));
            effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
            effect->getParametersProperty()["Tint"]->SetValue(
                Vector4(0.0f, 1.0f, 0.0f, 1.0f));

            struct Vertex { float x, y, z, u, v, w; };
            const Vertex vertices[6] = {
                {-1,  1, .75f, 0, 0, 1}, {-1, -1, .75f, 0, 0, 1},
                { 1, -1, .75f, 0, 0, 1}, {-1,  1, .75f, 0, 0, 1},
                { 1, -1, .75f, 0, 0, 1}, { 1,  1, .75f, 0, 0, 1},
            };
            const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
                VertexElement(0, VertexElementFormat::Vector3,
                              VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3,
                              VertexElementUsage::TextureCoordinate, 0),
            });
            RenderTarget2D target(
                device, 4, 4, false, SurfaceFormat::Color, DepthFormat::Depth24);
            device.SetRenderTarget(&target);
            device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                         Color::Red, .5f, 0);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::Default);
            device.setBlendStateProperty(BlendState::Opaque);
            effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(vertices), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color centre;
            const Rectangle centreRectangle(2, 2, 1, 1);
            target.GetData(0, &centreRectangle, &centre, 0, 1);
            return centre;
        };

        for (const SyntheticLegacyDepthOutput instruction : {
                 SyntheticLegacyDepthOutput::TextureMatrix2,
                 SyntheticLegacyDepthOutput::Register})
        {
            Check(render(instruction, /*zeroDivisor=*/false) == Color::Lime,
                  "compiled legacy pixel depth did not replace raster depth");
            Check(render(instruction, /*zeroDivisor=*/true) == Color::Red,
                  "compiled legacy pixel depth did not map zero divisor to one");
        }
    }

    void CheckCompiledLegacyTextureRemap()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        using CNA::TestSupport::SyntheticLegacyTextureRemap;

        GraphicsDevice device;
        Texture2D texture(device, 8, 8, /*mipMap=*/true, SurfaceFormat::Color);
        std::vector<Color> base(64, Color::Yellow);
        for (int y = 0; y < 4; ++y)
            for (int x = 4; x < 8; ++x)
                base[static_cast<std::size_t>(y * 8 + x)] = Color::Red;
        for (int y = 4; y < 8; ++y)
            for (int x = 0; x < 4; ++x)
                base[static_cast<std::size_t>(y * 8 + x)] = Color::Green;
        const Rectangle wholeBase(0, 0, 8, 8);
        texture.SetData(0, &wholeBase, base.data(), 0, static_cast<int>(base.size()));
        for (int level = 1; level < texture.getLevelCountProperty(); ++level)
        {
            const int extent = std::max(1, 8 >> level);
            std::vector<Color> mip(static_cast<std::size_t>(extent * extent), Color::Blue);
            const Rectangle whole(0, 0, extent, extent);
            texture.SetData(level, &whole, mip.data(), 0, static_cast<int>(mip.size()));
        }

        struct Vertex { float x, y, z, r, g, b, a; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const auto render = [&](SyntheticLegacyTextureRemap remap)
        {
            CNA::TestSupport::SyntheticEffectOptions options;
            options.includeDrawableProgram = true;
            options.includeSampler = true;
            options.samplerRegister = 1;
            options.pixelShaderLegacyTextureRemap = remap;
            options.samplerStates = {
                {Fx::SampMagFilter, Fx::FilterPoint},
                {Fx::SampMinFilter, Fx::FilterPoint},
                {Fx::SampMipFilter, Fx::FilterPoint},
                {Fx::SampAddressU, Fx::AddressClamp},
                {Fx::SampAddressV, Fx::AddressClamp},
            };
            auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
                device, CNA::TestSupport::BuildSyntheticEffect(options));
            effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
            effect->getParametersProperty()["FxTexture"]->SetValue(&texture);

            const bool alphaRed = remap == SyntheticLegacyTextureRemap::AlphaRed;
            const auto makeVertex = [alphaRed](float x, float y, float ignored)
            {
                return alphaRed ? Vertex{x, y, 0, .25f, ignored, .75f, .75f}
                                : Vertex{x, y, 0, ignored, .25f, .75f, .75f};
            };
            const Vertex quad[6] = {
                makeVertex(-1,  1, 0), makeVertex(-1, -1, 32),
                makeVertex( 1, -1, 32), makeVertex(-1,  1, 0),
                makeVertex( 1, -1, 32), makeVertex( 1,  1, 0),
            };
            RenderTarget2D target(device, 4, 4);
            device.SetRenderTarget(&target);
            device.Clear(Color::Magenta);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(quad), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color centre;
            const Rectangle centreRectangle(2, 2, 1, 1);
            target.GetData(0, &centreRectangle, &centre, 0, 1);
            return centre;
        };

        Check(render(SyntheticLegacyTextureRemap::AlphaRed) == Color::Red,
              "compiled ps_1_2 TEXREG2AR did not sample AR with AR-derived LOD");
        Check(render(SyntheticLegacyTextureRemap::GreenBlue) == Color::Green,
              "compiled ps_1_2 TEXREG2GB did not sample GB with GB-derived LOD");
    }

    void CheckCompiledLegacyDependentTextures()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        using Operation = CNA::TestSupport::SyntheticLegacyDependentTexture;
        using CNA::TestSupport::SyntheticSamplerKind;

        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            PresentationParameters());
        const auto makeEffect = [&](Operation operation, SyntheticSamplerKind samplerKind,
                                    bool samplesTexture)
        {
            CNA::TestSupport::SyntheticEffectOptions options;
            options.includeDrawableProgram = true;
            options.includeSampler = samplesTexture;
            options.samplerRegister = 1;
            options.samplerKind = samplerKind;
            options.pixelShaderLegacyDependentTexture = operation;
            if (samplesTexture)
            {
                options.samplerStates = {
                    {Fx::SampMagFilter, Fx::FilterPoint},
                    {Fx::SampMinFilter, Fx::FilterPoint},
                    {Fx::SampMipFilter, Fx::FilterPoint},
                    {Fx::SampAddressU, Fx::AddressClamp},
                    {Fx::SampAddressV, Fx::AddressClamp},
                    {Fx::SampAddressW, Fx::AddressClamp},
                };
            }
            auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
                device, CNA::TestSupport::BuildSyntheticEffect(options));
            effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
            return effect;
        };

        struct Vertex { float x, y, z, u, v, w; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const auto render = [&](Effect& effect, const Vector3& left, const Vector3& right)
        {
            const Vertex vertices[6] = {
                {-1,  1, 0, left.X, left.Y, left.Z},
                {-1, -1, 0, left.X, left.Y, left.Z},
                { 1, -1, 0, right.X, right.Y, right.Z},
                {-1,  1, 0, left.X, left.Y, left.Z},
                { 1, -1, 0, right.X, right.Y, right.Z},
                { 1,  1, 0, right.X, right.Y, right.Z},
            };
            RenderTarget2D target(device, 4, 4);
            device.SetRenderTarget(&target);
            device.Clear(Color::Magenta);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(vertices), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color centre;
            const Rectangle centreRectangle(2, 2, 1, 1);
            target.GetData(0, &centreRectangle, &centre, 0, 1);
            return centre;
        };

        Texture3D volume(device, 8, 8, 8, /*mipMap=*/true, SurfaceFormat::Color);
        std::vector<Color> volumeBase(512, Color::Red);
        for (int z = 4; z < 8; ++z)
            for (int y = 4; y < 8; ++y)
                for (int x = 0; x < 4; ++x)
                    volumeBase[static_cast<std::size_t>(z * 64 + y * 8 + x)] = Color::Green;
        volume.SetData(volumeBase.data(), static_cast<int>(volumeBase.size()));
        for (int level = 1; level < volume.getLevelCountProperty(); ++level)
        {
            const int extent = std::max(1, 8 >> level);
            std::vector<Color> mip(
                static_cast<std::size_t>(extent * extent * extent), Color::Blue);
            volume.SetData(level, 0, 0, extent, extent, 0, extent,
                           mip.data(), 0, static_cast<int>(mip.size()));
        }
        auto registerRgb = makeEffect(Operation::RegisterRgb, SyntheticSamplerKind::Sampler3D,
                                      /*samplesTexture=*/true);
        registerRgb->getParametersProperty()["FxTexture"]->SetValue(&volume);
        Check(render(*registerRgb, Vector3(.625f, .875f, .875f),
                                   Vector3(.625f, .875f, .875f)) == Color::Green,
              "compiled ps_1_2 TEXREG2RGB did not use all three volume coordinates");
        Check(render(*registerRgb, Vector3(.625f, .875f, .5f),
                                   Vector3(.625f, .875f, 4.5f)) == Color::Blue,
              "compiled ps_1_2 TEXREG2RGB did not derive LOD from _bx2 source RGB");

        Texture2D texture(device, 8, 8, /*mipMap=*/true, SurfaceFormat::Color);
        std::vector<Color> base(64, Color::Red);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 4; ++x)
                base[static_cast<std::size_t>(y * 8 + x)] = Color::Green;
        const Rectangle wholeBase(0, 0, 8, 8);
        texture.SetData(0, &wholeBase, base.data(), 0, static_cast<int>(base.size()));
        for (int level = 1; level < texture.getLevelCountProperty(); ++level)
        {
            const int extent = std::max(1, 8 >> level);
            std::vector<Color> mip(static_cast<std::size_t>(extent * extent), Color::Blue);
            const Rectangle whole(0, 0, extent, extent);
            texture.SetData(level, &whole, mip.data(), 0, static_cast<int>(mip.size()));
        }
        auto dotSample = makeEffect(Operation::DotSample, SyntheticSamplerKind::Sampler2D,
                                    /*samplesTexture=*/true);
        dotSample->getParametersProperty()["FxTexture"]->SetValue(&texture);
        Check(render(*dotSample, Vector3(.25f, 1.0f, 1.0f),
                                 Vector3(.25f, 1.0f, 1.0f)) == Color::Green,
              "compiled ps_1_2 TEXDP3TEX did not sample its dot-product coordinate");
        Check(render(*dotSample, Vector3(0.0f, 1.0f, 1.0f),
                                 Vector3(8.0f, 1.0f, 1.0f)) == Color::Blue,
              "compiled ps_1_2 TEXDP3TEX did not derive LOD from its dot product");

        auto dot = makeEffect(Operation::Dot, SyntheticSamplerKind::Sampler2D,
                              /*samplesTexture=*/false);
        Check(render(*dot, Vector3(.5f, 1.0f, 1.0f),
                           Vector3(.5f, 1.0f, 1.0f)) == Color(128, 128, 128, 128),
              "compiled ps_1_2 TEXDP3 did not replicate its dot product to RGBA");
    }

    void CheckCompiledLegacyBumpEnvironment()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        using Operation = CNA::TestSupport::SyntheticLegacyBumpEnvironment;

        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            PresentationParameters());
        Texture2D texture(device, 8, 8, true, SurfaceFormat::Color);
        std::vector<Color> texels(64, Color::Red);
        for (int y = 0; y < 4; ++y)
            for (int x = 4; x < 8; ++x)
                texels[static_cast<std::size_t>(y * 8 + x)] = Color::Green;
        for (int y = 4; y < 8; ++y)
            for (int x = 0; x < 4; ++x)
                texels[static_cast<std::size_t>(y * 8 + x)] = Color::Blue;
        for (int y = 4; y < 8; ++y)
            for (int x = 4; x < 8; ++x)
                texels[static_cast<std::size_t>(y * 8 + x)] = Color::Yellow;
        texture.SetData(texels.data(), static_cast<int>(texels.size()));
        for (int level = 1; level < texture.getLevelCountProperty(); ++level)
        {
            const int extent = std::max(1, 8 >> level);
            std::vector<Color> mip(static_cast<std::size_t>(extent * extent), Color::Blue);
            const Rectangle whole(0, 0, extent, extent);
            texture.SetData(level, &whole, mip.data(), 0, static_cast<int>(mip.size()));
        }

        struct Vertex { float x, y, z, u, v, w; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const auto render = [&](Operation operation,
                                const std::vector<CNA::TestSupport::SyntheticRenderState>& states,
                                float rightU = .75f)
        {
            const Vertex quad[6] = {
                {-1,  1, 0, .75f, .25f, .75f}, {-1, -1, 0, .75f, .25f, .75f},
                { 1, -1, 0, rightU, .25f, .75f}, {-1,  1, 0, .75f, .25f, .75f},
                { 1, -1, 0, rightU, .25f, .75f}, { 1,  1, 0, rightU, .25f, .75f},
            };
            CNA::TestSupport::SyntheticEffectOptions options;
            options.includeDrawableProgram = true;
            options.includeSampler = operation != Operation::Arithmetic;
            options.samplerRegister = 1;
            options.pixelShaderLegacyBumpEnvironment = operation;
            options.renderStates = states;
            if (options.includeSampler)
            {
                options.samplerStates = {
                    {Fx::SampMagFilter, Fx::FilterPoint},
                    {Fx::SampMinFilter, Fx::FilterPoint},
                    {Fx::SampMipFilter, Fx::FilterNone},
                    {Fx::SampAddressU, Fx::AddressClamp},
                    {Fx::SampAddressV, Fx::AddressClamp},
                };
            }
            auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
                device, CNA::TestSupport::BuildSyntheticEffect(options));
            effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
            if (options.includeSampler)
                effect->getParametersProperty()["FxTexture"]->SetValue(&texture);

            RenderTarget2D target(device, 4, 4);
            device.SetRenderTarget(&target);
            device.Clear(Color::Magenta);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(quad), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color centre;
            const Rectangle probe(2, 2, 1, 1);
            target.GetData(0, &probe, &centre, 0, 1);
            return centre;
        };
        const auto textureStates = [](float m00, float m01, float m10, float m11)
        {
            return std::vector<CNA::TestSupport::SyntheticRenderState>{
                {Fx::RsBumpEnvMat00, CNA::TestSupport::FloatBits(m00), true, 1},
                {Fx::RsBumpEnvMat01, CNA::TestSupport::FloatBits(m01), true, 1},
                {Fx::RsBumpEnvMat10, CNA::TestSupport::FloatBits(m10), true, 1},
                {Fx::RsBumpEnvMat11, CNA::TestSupport::FloatBits(m11), true, 1},
            };
        };

        Check(render(Operation::Texture, textureStates(1, 0, 0, 0)) == Color::Green,
              "compiled TEXBEM ignored BUMPENVMAT00");
        Check(render(Operation::Texture, textureStates(0, 0, -1, 0)) == Color::Green,
              "compiled TEXBEM ignored BUMPENVMAT10");
        Check(render(Operation::Texture, textureStates(0, 1, 0, 0)) == Color::Blue,
              "compiled TEXBEM ignored BUMPENVMAT01");
        Check(render(Operation::Texture, textureStates(0, 0, 0, -1)) == Color::Blue,
              "compiled TEXBEM ignored BUMPENVMAT11");
        Check(render(Operation::Texture, textureStates(0, 0, 0, 0), 8.75f) == Color::Red,
              "compiled TEXBEM LOD used source derivatives removed by a zero matrix");
        Check(render(Operation::Texture, textureStates(1, 0, 0, 0), 8.75f) == Color::Blue,
              "compiled TEXBEM LOD ignored matrix-transformed source derivatives");
        Check(render(Operation::Texture, {}) == Color::Green,
              "compiled TEXBEM lost device bump state across effects");

        auto luminanceStates = textureStates(0, 0, 0, 0);
        luminanceStates.push_back(
            {Fx::RsBumpEnvLScale, CNA::TestSupport::FloatBits(.5f), true, 1});
        luminanceStates.push_back(
            {Fx::RsBumpEnvLOffset, CNA::TestSupport::FloatBits(.25f), true, 1});
        const Color luminance = render(Operation::TextureLuminance, luminanceStates);
        Check(std::abs(static_cast<int>(luminance.getRProperty()) - 128) <= 1 &&
                  luminance.getGProperty() == 0 && luminance.getBProperty() == 0 &&
                  std::abs(static_cast<int>(luminance.getAProperty()) - 128) <= 1,
              "compiled TEXBEML ignored luminance scale/offset");

        const std::vector<CNA::TestSupport::SyntheticRenderState> arithmeticStates = {
            {Fx::RsBumpEnvMat00, CNA::TestSupport::FloatBits(.4f), true, 0},
            {Fx::RsBumpEnvMat01, CNA::TestSupport::FloatBits(.8f), true, 0},
            {Fx::RsBumpEnvMat10, CNA::TestSupport::FloatBits(.2f), true, 0},
            {Fx::RsBumpEnvMat11, CNA::TestSupport::FloatBits(.6f), true, 0},
        };
        const Color arithmetic = render(Operation::Arithmetic, arithmeticStates);
        Check(std::abs(static_cast<int>(arithmetic.getRProperty()) - 89) <= 1 &&
                  std::abs(static_cast<int>(arithmetic.getGProperty()) - 191) <= 1 &&
                  std::abs(static_cast<int>(arithmetic.getBProperty()) - 77) <= 1 &&
                  std::abs(static_cast<int>(arithmetic.getAProperty()) - 102) <= 1,
              "compiled ps_1_4 BEM ignored the destination-stage matrix");
    }

    void CheckCompiledVertexSamplerRasterization()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        constexpr int slot = 2;
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            PresentationParameters());
        const std::vector<CNA::TestSupport::SyntheticSamplerState> states = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
            {Fx::SampAddressW, Fx::AddressMirror},
            {Fx::SampMaxAnisotropy, 7},
            {Fx::SampMaxMipLevel, 1},
            {Fx::SampMipMapLodBias, CNA::TestSupport::FloatBits(0.0f), true},
        };
        auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticVertexSamplingEffect(
                        states, slot, CNA::TestSupport::SyntheticSamplerKind::Sampler2D,
                        /*swizzlesSampleResult=*/true));
        effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        effect->getParametersProperty()["Tint"]->SetValue(Vector4(1, 0, 0, 1));

        const Vector4 outside(0, 3, 3, 1);
        std::array<Vector4, 16> outsideBase{};
        outsideBase.fill(outside);
        const std::array<Vector4, 4> outsideMip1 = {outside, outside, outside, outside};
        Texture2D passTexture(device, 4, 4, true, SurfaceFormat::Vector4);
        passTexture.SetData(outsideBase.data(), static_cast<int>(outsideBase.size()));
        passTexture.SetData(1, nullptr, outsideMip1.data(), 0,
                            static_cast<int>(outsideMip1.size()));
        passTexture.SetData(2, nullptr, &outside, 0, 1);

        Texture2D positions(device, 4, 4, true, SurfaceFormat::Vector4);
        std::array<Vector4, 16> base = outsideBase;
        base[0] = Vector4(0, 1, -1, 1);
        base[12] = Vector4(0, -1, -1, 1);
        base[15] = Vector4(0, -1, 1, 1);
        base[3] = Vector4(0, 1, 1, 1);
        positions.SetData(base.data(), static_cast<int>(base.size()));
        positions.SetData(1, nullptr, outsideMip1.data(), 0,
                          static_cast<int>(outsideMip1.size()));
        positions.SetData(2, nullptr, &outside, 0, 1);

        Texture2D pixelSentinel(device, 1, 1);
        const Color blue = Color::Blue;
        pixelSentinel.SetData(&blue, 1);
        device.getTexturesProperty()(slot, &pixelSentinel);
        effect->getParametersProperty()["FxTexture"]->SetValue(&passTexture);
        effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        Check(device.getTexturesProperty()[slot] == &pixelSentinel,
              "vertex sampler aliased the pixel texture collection");
        Check(device.getVertexTexturesProperty()[slot] == &passTexture,
              "vertex sampler assignment did not reach VertexTextures");
        const SamplerState& applied = device.getVertexSamplerStatesProperty()[slot];
        Check(applied.getFilterProperty() == TextureFilter::Point &&
                  applied.getAddressWProperty() == TextureAddressMode::Mirror &&
                  applied.getMaxAnisotropyProperty() == 7 &&
                  applied.getMaxMipLevelProperty() == 1 &&
                  applied.getMipMapLevelOfDetailBiasProperty() == 0.0f,
              "vertex sampler assignment lost a public SamplerState property");

        struct Vertex
        {
            float x, y, z;
            float u, v, q, lod;
        };
        const Vertex quad[6] = {
            {0, 0, 0, .125f, .125f, 0, 0}, {0, 0, 0, .125f, .875f, 0, 0},
            {0, 0, 0, .875f, .875f, 0, 0}, {0, 0, 0, .125f, .125f, 0, 0},
            {0, 0, 0, .875f, .875f, 0, 0}, {0, 0, 0, .875f, .125f, 0, 0},
        };
        const VertexDeclaration declaration(sizeof(Vertex), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        RenderTarget2D target(device, 8, 8);
        const Rectangle centre(4, 4, 1, 1);
        const Color background(9, 19, 29, 255);
        const auto draw = [&]()
        {
            device.SetRenderTarget(&target);
            device.Clear(background);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(quad), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color result = Color::Transparent;
            target.GetData(0, &centre, &result, 0, 1);
            return result;
        };

        device.getVertexTexturesProperty()(slot, &positions);
        device.getVertexSamplerStatesProperty()[slot] = SamplerState::LinearClamp;
        device.SetRenderTarget(&target);
        device.Clear(background);
        bool rejectedFilteredVertexTexture = false;
        try
        {
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(quad), 0, 2, declaration);
        }
        catch (const System::NotSupportedException&)
        {
            rejectedFilteredVertexTexture = true;
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(rejectedFilteredVertexTexture,
              "Vector4 vertex texture filtering used the pixel sampler collection");

        device.getVertexSamplerStatesProperty()[slot] = SamplerState::PointClamp;
        const Color first = draw();
        GpuDrawParams inspectedParams;
        effect->FillGpuDrawParams(inspectedParams);
        const auto* inspectedRuntime =
            dynamic_cast<const SoftwareCompiledEffect*>(inspectedParams.compiledEffectRuntime);
        const auto lastPosition = inspectedRuntime != nullptr
            ? inspectedRuntime->GetLastVertexResultEXT().position
            : std::array<float, 4>{};
        Check(first.getRProperty() >= 252,
              "vertex TEXLDL ignored application texture/sampler overrides (pixel " +
                  std::to_string(first.getRProperty()) + "," +
                  std::to_string(first.getGProperty()) + "," +
                  std::to_string(first.getBProperty()) + "; last position " +
                  std::to_string(lastPosition[0]) + "," +
                  std::to_string(lastPosition[1]) + "," +
                  std::to_string(lastPosition[2]) + "," +
                  std::to_string(lastPosition[3]) + ")");
        SamplerState biasOne = SamplerState::PointClamp;
        biasOne.setMipMapLevelOfDetailBiasProperty(1.0f);
        device.getVertexSamplerStatesProperty()[slot] = biasOne;
        Check(draw() == background, "vertex TEXLDL ignored LOD-bias transition");
        SamplerState mipOne = SamplerState::PointClamp;
        mipOne.setMaxMipLevelProperty(1);
        device.getVertexSamplerStatesProperty()[slot] = mipOne;
        Check(draw() == background, "vertex TEXLDL ignored MaxMipLevel transition");
        device.getVertexSamplerStatesProperty()[slot] = SamplerState::PointClamp;
        Check(draw().getRProperty() >= 252,
              "vertex sampler did not recover without pass reapplication");
        device.getVertexTexturesProperty()(slot, nullptr);
        Check(draw() == background, "vertex texture null transition did not unbind");
        device.getVertexTexturesProperty()(slot, &positions);
        positions.Dispose();
        Check(device.getVertexTexturesProperty()[slot] == nullptr,
              "disposing a vertex texture did not clear its public binding");
        Check(draw() == background,
              "disposing a bound vertex texture did not remove the native binding");
    }

    void CheckCompiledVertexSamplerDimensions()
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        using CNA::TestSupport::SyntheticSamplerKind;
        constexpr int slot = 1;
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            PresentationParameters());
        const auto states = [](std::uint32_t addressW)
        {
            return std::vector<CNA::TestSupport::SyntheticSamplerState>{
                {Fx::SampMagFilter, Fx::FilterPoint},
                {Fx::SampMinFilter, Fx::FilterPoint},
                {Fx::SampMipFilter, Fx::FilterPoint},
                {Fx::SampAddressU, Fx::AddressClamp},
                {Fx::SampAddressV, Fx::AddressClamp},
                {Fx::SampAddressW, addressW},
                {Fx::SampMaxMipLevel, 0},
                {Fx::SampMipMapLodBias, CNA::TestSupport::FloatBits(0.0f), true},
            };
        };
        struct Vertex
        {
            float x, y, z;
            float u, v, q, lod;
        };
        const VertexDeclaration declaration(sizeof(Vertex), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        RenderTarget2D target(device, 8, 8);
        const Rectangle centre(4, 4, 1, 1);
        const Color background(9, 19, 29, 255);
        const auto draw = [&](const Vertex (&vertices)[6])
        {
            device.SetRenderTarget(&target);
            device.Clear(background);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(vertices), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color result = Color::Transparent;
            target.GetData(0, &centre, &result, 0, 1);
            return result;
        };

        const Vector4 outside(3, 3, 0, 1);
        std::array<Vector4, 16> positions{};
        positions.fill(outside);
        positions[0] = Vector4(-1, 1, 0, 1);
        positions[12] = Vector4(-1, -1, 0, 1);
        positions[15] = Vector4(1, -1, 0, 1);
        positions[3] = Vector4(1, 1, 0, 1);

        {
            auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
                device, CNA::TestSupport::BuildSyntheticVertexSamplingEffect(
                            states(Fx::AddressClamp), slot, SyntheticSamplerKind::SamplerCube));
            effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
            effect->getParametersProperty()["Tint"]->SetValue(Vector4(0, 1, 0, 1));
            TextureCube cube(device, 4, false, SurfaceFormat::Vector4);
            std::array<Vector4, 16> face{};
            face.fill(outside);
            for (int cubeFace = 0; cubeFace < 6; ++cubeFace)
                cube.SetData(static_cast<CubeMapFace>(cubeFace), face.data(),
                             static_cast<int>(face.size()));
            cube.SetData(CubeMapFace::PositiveZ, positions.data(),
                         static_cast<int>(positions.size()));
            effect->getParametersProperty()["FxTexture"]->SetValue(&cube);
            effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            const Vertex quad[6] = {
                {0, 0, 0, -.75f, .75f, 1, 0}, {0, 0, 0, -.75f, -.75f, 1, 0},
                {0, 0, 0, .75f, -.75f, 1, 0}, {0, 0, 0, -.75f, .75f, 1, 0},
                {0, 0, 0, .75f, -.75f, 1, 0}, {0, 0, 0, .75f, .75f, 1, 0},
            };
            Check(draw(quad).getGProperty() >= 252,
                  "compiled vertex samplerCUBE did not produce clip positions");
        }

        {
            auto effect = CNA::TestSupport::CompiledEffectTestAccess::Create(
                device, CNA::TestSupport::BuildSyntheticVertexSamplingEffect(
                            states(Fx::AddressClamp), slot, SyntheticSamplerKind::Sampler3D));
            effect->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
            effect->getParametersProperty()["Tint"]->SetValue(Vector4(0, 0, 1, 1));
            Texture3D volume(device, 4, 4, 2, false, SurfaceFormat::Vector4);
            std::array<Vector4, 32> texels{};
            texels.fill(outside);
            std::copy(positions.begin(), positions.end(), texels.begin());
            volume.SetData(texels.data(), static_cast<int>(texels.size()));
            effect->getParametersProperty()["FxTexture"]->SetValue(&volume);
            effect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            const Vertex quad[6] = {
                {0, 0, 0, .125f, .125f, 1.125f, 0},
                {0, 0, 0, .125f, .875f, 1.125f, 0},
                {0, 0, 0, .875f, .875f, 1.125f, 0},
                {0, 0, 0, .125f, .125f, 1.125f, 0},
                {0, 0, 0, .875f, .875f, 1.125f, 0},
                {0, 0, 0, .875f, .125f, 1.125f, 0},
            };
            Check(draw(quad) == background,
                  "vertex sampler3D did not begin with AddressW.Clamp");
            SamplerState wrap = SamplerState::PointClamp;
            wrap.setAddressWProperty(TextureAddressMode::Wrap);
            device.getVertexSamplerStatesProperty()[slot] = wrap;
            Check(draw(quad).getBProperty() >= 252,
                  "vertex sampler3D ignored AddressW.Wrap");
            device.getVertexSamplerStatesProperty()[slot] = SamplerState::PointClamp;
            Check(draw(quad) == background,
                  "vertex sampler3D ignored AddressW.Clamp transition");
        }
    }

    void CheckCompiledSamplerRasterization(SoftwareRenderer& renderer)
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        using CNA::TestSupport::SyntheticSamplerKind;

        GraphicsDevice textureDevice(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            PresentationParameters());
        struct SamplingVertex
        {
            float position[4];
            float coordinate[4];
        };
        const VertexDeclaration declaration(
            static_cast<int>(sizeof(SamplingVertex)),
            {VertexElement(0, VertexElementFormat::Vector4, VertexElementUsage::Position, 0),
             VertexElement(16, VertexElementFormat::Vector4,
                           VertexElementUsage::TextureCoordinate, 0)});
        auto vertexBuffer = renderer.CreateVertexBuffer(3);
        vertexBuffer->SetVertexDeclaration(declaration);
        const Matrix identity = Matrix::getIdentityProperty();
        renderer.ApplyRasterizerState(0, 0, false);
        renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, BlendWriteState{});

        const std::vector<CNA::TestSupport::SyntheticSamplerState> samplerStates = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
            {Fx::SampAddressW, Fx::AddressClamp},
            {Fx::SampMaxMipLevel, 0},
            {Fx::SampMipMapLodBias, CNA::TestSupport::FloatBits(0.0f), true},
        };
        const auto createRuntime = [&](SyntheticSamplerKind kind, std::uint32_t slot = 0)
        {
            const auto bytes = CNA::TestSupport::BuildSyntheticSamplingEffect(
                samplerStates, slot, kind);
            auto runtime = renderer.CreateCompiledEffect(bytes.data(), bytes.size());
            const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            runtime->SetParameterValue(FindParameter(*runtime, "Tint"), white, sizeof(white));
            runtime->SetTechnique(0);
            CompiledEffectPassStateChanges changes;
            runtime->ApplyPass(1, {}, changes);
            auto* software = dynamic_cast<SoftwareCompiledEffect*>(runtime.get());
            Check(software != nullptr && software->GetPixelProgramEXT() != nullptr,
                  "sampling fixture did not select a Software pixel program");
            if (software != nullptr && software->GetPixelProgramEXT() != nullptr)
            {
                const auto& samplers = software->GetPixelProgramEXT()->samplers;
                Check(samplers.size() == 1u && samplers[0].registerNumber == slot,
                      "pixel sampler declaration lost its register");
                const SoftwareShaderSamplerTypeEXT expected =
                    kind == SyntheticSamplerKind::SamplerCube
                        ? SoftwareShaderSamplerTypeEXT::Cube
                        : kind == SyntheticSamplerKind::Sampler3D
                              ? SoftwareShaderSamplerTypeEXT::Volume
                              : SoftwareShaderSamplerTypeEXT::Texture2D;
                Check(samplers.size() == 1u && samplers[0].type == expected,
                      "pixel sampler declaration lost its dimension");
            }
            return runtime;
        };
        const auto draw = [&](ICompiledEffectRuntime& runtime,
                              const std::array<float, 4>& coordinate)
        {
            const SamplingVertex vertices[3] = {
                {{-0.75f, 0.75f, 0.5f, 1.0f},
                 {coordinate[0], coordinate[1], coordinate[2], coordinate[3]}},
                {{-0.75f, -0.75f, 0.5f, 1.0f},
                 {coordinate[0], coordinate[1], coordinate[2], coordinate[3]}},
                {{0.75f, 0.0f, 0.5f, 1.0f},
                 {coordinate[0], coordinate[1], coordinate[2], coordinate[3]}},
            };
            vertexBuffer->SetData(vertices, 3, sizeof(SamplingVertex));
            GpuDrawParams params;
            params.compiledEffectRuntime = &runtime;
            params.compiledDeviceTextures = &textureDevice.getTexturesProperty();
            params.compiledDeviceSamplerStates = &textureDevice.getSamplerStatesProperty();
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            renderer.DrawPrimitivesEx(*vertexBuffer, identity, identity, identity,
                                      PrimitiveType::TriangleList, 1, params);
        };
        const auto expectBackbuffer = [&](const std::array<std::uint8_t, 4>& color,
                                          const std::string& label)
        {
            Check(CountBackbufferColor(renderer, color) > 0, label);
        };
        const auto applySampler = [&](std::uint32_t slot, int filter, int addressU,
                                      int addressV, int addressW, int maxMip, float bias)
        {
            renderer.ApplySamplerState(static_cast<int>(slot), filter, addressU, addressV, 4);
            renderer.ApplySamplerMipState(static_cast<int>(slot), maxMip, bias);
            renderer.ApplySamplerAddressW(static_cast<int>(slot), addressW);
        };

        auto flatRuntime = createRuntime(SyntheticSamplerKind::Sampler2D, 2);
        Texture2D red(textureDevice, 1, 1);
        Texture2D blue(textureDevice, 1, 1);
        const Microsoft::Xna::Framework::Color redPixel[1] = {
            Microsoft::Xna::Framework::Color(255, 0, 0, 255)};
        const Microsoft::Xna::Framework::Color bluePixel[1] = {
            Microsoft::Xna::Framework::Color(0, 0, 255, 255)};
        red.SetData(redPixel, 1);
        blue.SetData(bluePixel, 1);
        applySampler(2, static_cast<int>(TextureFilter::Point),
                     static_cast<int>(TextureAddressMode::Clamp),
                     static_cast<int>(TextureAddressMode::Clamp),
                     static_cast<int>(TextureAddressMode::Clamp), 0, 0.0f);
        textureDevice.getTexturesProperty()(2, &red);
        draw(*flatRuntime, {0.5f, 0.5f, 0.0f, 1.0f});
        expectBackbuffer({255u, 0u, 0u, 255u},
                         "compiled sampler did not read the effect-selected texture slot");
        textureDevice.getTexturesProperty()(2, &blue);
        draw(*flatRuntime, {0.5f, 0.5f, 0.0f, 1.0f});
        expectBackbuffer({0u, 0u, 255u, 255u},
                         "compiled sampler did not observe texture replacement");
        textureDevice.getTexturesProperty()(2, nullptr);
        draw(*flatRuntime, {0.5f, 0.5f, 0.0f, 1.0f});
        expectBackbuffer({0u, 0u, 0u, 255u},
                         "unbound compiled sampler did not return D3D black");

        auto addressRuntime = createRuntime(SyntheticSamplerKind::Sampler2D);
        Texture2D columns(textureDevice, 2, 1);
        const Microsoft::Xna::Framework::Color columnPixels[2] = {
            Microsoft::Xna::Framework::Color(255, 0, 0, 255),
            Microsoft::Xna::Framework::Color(0, 0, 255, 255)};
        columns.SetData(columnPixels, 2);
        textureDevice.getTexturesProperty()(0, &columns);
        applySampler(0, static_cast<int>(TextureFilter::Point),
                     static_cast<int>(TextureAddressMode::Wrap),
                     static_cast<int>(TextureAddressMode::Clamp),
                     static_cast<int>(TextureAddressMode::Clamp), 0, 0.0f);
        draw(*addressRuntime, {1.75f, 0.5f, 0.0f, 1.0f});
        expectBackbuffer({0u, 0u, 255u, 255u},
                         "compiled sampler did not apply AddressU.Wrap");
        applySampler(0, static_cast<int>(TextureFilter::Point),
                     static_cast<int>(TextureAddressMode::Mirror),
                     static_cast<int>(TextureAddressMode::Clamp),
                     static_cast<int>(TextureAddressMode::Clamp), 0, 0.0f);
        draw(*addressRuntime, {1.75f, 0.5f, 0.0f, 1.0f});
        expectBackbuffer({255u, 0u, 0u, 255u},
                         "compiled sampler did not apply AddressU.Mirror");
        applySampler(0, static_cast<int>(TextureFilter::Linear),
                     static_cast<int>(TextureAddressMode::Clamp),
                     static_cast<int>(TextureAddressMode::Clamp),
                     static_cast<int>(TextureAddressMode::Clamp), 0, 0.0f);
        draw(*addressRuntime, {0.375f, 0.5f, 0.0f, 1.0f});
        expectBackbuffer({191u, 0u, 64u, 255u},
                         "compiled sampler did not apply linear filtering");

        applySampler(0, static_cast<int>(TextureFilter::Point),
                     static_cast<int>(TextureAddressMode::Clamp),
                     static_cast<int>(TextureAddressMode::Clamp),
                     static_cast<int>(TextureAddressMode::Clamp), 0, 0.0f);
        const SamplingVertex lineVertices[2] = {
            {{-0.75f, 0.0f, 0.5f, 1.0f}, {0.25f, 0.5f, 0.0f, 1.0f}},
            {{0.75f, 0.0f, 0.5f, 1.0f}, {0.75f, 0.5f, 0.0f, 1.0f}},
        };
        auto lineBuffer = renderer.CreateVertexBuffer(2);
        lineBuffer->SetVertexDeclaration(declaration);
        lineBuffer->SetData(lineVertices, 2, sizeof(SamplingVertex));
        GpuDrawParams lineParams;
        lineParams.compiledEffectRuntime = addressRuntime.get();
        lineParams.compiledDeviceTextures = &textureDevice.getTexturesProperty();
        lineParams.compiledDeviceSamplerStates = &textureDevice.getSamplerStatesProperty();
        renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
        renderer.DrawPrimitivesEx(*lineBuffer, identity, identity, identity,
                                  PrimitiveType::LineList, 1, lineParams);
        Check(CountBackbufferColor(renderer, {255u, 0u, 0u, 255u}) > 0 &&
                  CountBackbufferColor(renderer, {0u, 0u, 255u, 255u}) > 0,
              "compiled line did not perspective-interpolate and sample its TEXCOORD varying");

        Texture2D mipped(textureDevice, 2, 2, true, SurfaceFormat::Color);
        const Microsoft::Xna::Framework::Color level0[4] = {
            redPixel[0], redPixel[0], redPixel[0], redPixel[0]};
        const Microsoft::Xna::Framework::Color level1[1] = {
            Microsoft::Xna::Framework::Color(0, 255, 0, 255)};
        const Microsoft::Xna::Framework::Rectangle whole0(0, 0, 2, 2);
        const Microsoft::Xna::Framework::Rectangle whole1(0, 0, 1, 1);
        mipped.SetData(0, &whole0, level0, 0, 4);
        mipped.SetData(1, &whole1, level1, 0, 1);
        textureDevice.getTexturesProperty()(0, &mipped);
        applySampler(0, static_cast<int>(TextureFilter::Point), 1, 1, 1, 1, 0.0f);
        draw(*addressRuntime, {0.5f, 0.5f, 0.0f, 1.0f});
        expectBackbuffer({0u, 255u, 0u, 255u},
                         "compiled sampler did not apply MaxMipLevel");
        applySampler(0, static_cast<int>(TextureFilter::Point), 1, 1, 1, 0, 1.0f);
        draw(*addressRuntime, {0.5f, 0.5f, 0.0f, 1.0f});
        expectBackbuffer({0u, 255u, 0u, 255u},
                         "compiled sampler did not apply mip LOD bias");

        auto cubeRuntime = createRuntime(SyntheticSamplerKind::SamplerCube);
        Microsoft::Xna::Framework::Graphics::TextureCube cube(
            textureDevice, 1, false, SurfaceFormat::Color);
        for (int face = 0; face < 6; ++face)
        {
            const Microsoft::Xna::Framework::Color pixel[1] = {
                face == 0 ? Microsoft::Xna::Framework::Color(255, 255, 0, 255)
                          : Microsoft::Xna::Framework::Color(0, 0, 255, 255)};
            cube.SetData(static_cast<Microsoft::Xna::Framework::Graphics::CubeMapFace>(face),
                         pixel, 1);
        }
        textureDevice.getTexturesProperty()(0, &cube);
        applySampler(0, static_cast<int>(TextureFilter::Point), 1, 1, 1, 0, 0.0f);
        draw(*cubeRuntime, {1.0f, 0.0f, 0.0f, 1.0f});
        expectBackbuffer({255u, 255u, 0u, 255u},
                         "compiled sampler did not select the cube +X face");

        RenderTargetCube renderedCube(
            textureDevice, 4, false, SurfaceFormat::Color, DepthFormat::None);
        Texture2D cubePaint(textureDevice, 1, 1);
        const Microsoft::Xna::Framework::Color whitePixel[1] = {
            Microsoft::Xna::Framework::Color::White};
        cubePaint.SetData(whitePixel, 1);
        textureDevice.SetRenderTarget(&renderedCube, CubeMapFace::PositiveX);
        textureDevice.Clear(Microsoft::Xna::Framework::Color::Black);
        {
            SpriteBatch batch(textureDevice);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            batch.Draw(cubePaint, Rectangle(0, 0, 2, 4), Color::Red);
            batch.Draw(cubePaint, Rectangle(2, 0, 2, 4), Color::Blue);
            batch.End();
        }
        textureDevice.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        textureDevice.getTexturesProperty()(0, &renderedCube);
        draw(*cubeRuntime, {1.0f, 0.0f, 0.5f, 1.0f});
        expectBackbuffer({255u, 0u, 0u, 255u},
                         "compiled cube sampling inverted the rendered +X face's left half");
        draw(*cubeRuntime, {1.0f, 0.0f, -0.5f, 1.0f});
        expectBackbuffer({0u, 0u, 255u, 255u},
                         "compiled cube sampling inverted the rendered +X face's right half");

        auto volumeRuntime = createRuntime(SyntheticSamplerKind::Sampler3D);
        Texture3D volume(textureDevice, 1, 1, 2, false, SurfaceFormat::Color);
        const Microsoft::Xna::Framework::Color volumePixels[2] = {
            Microsoft::Xna::Framework::Color(255, 0, 0, 255),
            Microsoft::Xna::Framework::Color(0, 0, 255, 255)};
        volume.SetData(volumePixels, 2);
        textureDevice.getTexturesProperty()(0, &volume);
        applySampler(0, static_cast<int>(TextureFilter::Point), 1, 1,
                     static_cast<int>(TextureAddressMode::Wrap), 0, 0.0f);
        draw(*volumeRuntime, {0.5f, 0.5f, 1.75f, 1.0f});
        expectBackbuffer({0u, 0u, 255u, 255u},
                         "compiled volume sampler did not apply AddressW.Wrap");
        applySampler(0, static_cast<int>(TextureFilter::Point), 1, 1,
                     static_cast<int>(TextureAddressMode::Mirror), 0, 0.0f);
        draw(*volumeRuntime, {0.5f, 0.5f, 1.75f, 1.0f});
        expectBackbuffer({255u, 0u, 0u, 255u},
                         "compiled volume sampler did not apply AddressW.Mirror");
        applySampler(0, static_cast<int>(TextureFilter::Point), 1, 1,
                     static_cast<int>(TextureAddressMode::Clamp), 0, 0.0f);
        draw(*volumeRuntime, {0.5f, 0.5f, 1.75f, 1.0f});
        expectBackbuffer({0u, 0u, 255u, 255u},
                         "compiled volume sampler did not apply AddressW.Clamp");
        applySampler(0, static_cast<int>(TextureFilter::Linear), 1, 1, 1, 0, 0.0f);
        draw(*volumeRuntime, {0.5f, 0.5f, 0.5f, 1.0f});
        expectBackbuffer({128u, 0u, 128u, 255u},
                         "compiled volume sampler did not apply trilinear voxel filtering");

        textureDevice.getTexturesProperty()(0, &columns);
        bool rejectedDimension = false;
        try
        {
            draw(*cubeRuntime, {1.0f, 0.0f, 0.0f, 1.0f});
        }
        catch (const std::runtime_error&)
        {
            rejectedDimension = true;
        }
        Check(rejectedDimension,
              "compiled sampler accepted a Texture2D for a samplerCUBE declaration");
    }

    void CheckCompiledMrtRasterization(SoftwareRenderer& renderer)
    {
        constexpr int size = 8;
        std::array<std::unique_ptr<IRenderTargetRenderer>, 4> targets;
        std::array<SoftwareRenderTargetRenderer*, 4> softwareTargets{};
        std::vector<RenderTargetBindingDescriptor> bindings;
        for (std::size_t slot = 0; slot < targets.size(); ++slot)
        {
            targets[slot] = renderer.CreateRenderTarget2DEXT(
                size, size, 0, false, false, 4, static_cast<int>(SurfaceFormat::Color));
            softwareTargets[slot] =
                dynamic_cast<SoftwareRenderTargetRenderer*>(targets[slot].get());
            Check(softwareTargets[slot] != nullptr,
                  "compiled MRT target did not use Software storage");
            bindings.push_back(RenderTargetBindingDescriptor::ForRenderTarget2D(
                targets[slot].get(), 0, size, size, 4));
        }
        renderer.SetRenderTargets(bindings.data(), static_cast<int>(bindings.size()));
        renderer.ApplyRasterizerState(static_cast<int>(CullMode::None), 0, false);
        renderer.ApplyRasterizerMultiSampleState(true);
        renderer.ApplyDepthStencilState(
            false, false, 0, false, 0, 0, 0, 0, 0xFF, 0xFF, 0,
            false, 0, 0, 0, 0);

        const auto bytes = CNA::TestSupport::BuildSyntheticMrtEffect();
        auto runtime = renderer.CreateCompiledEffect(bytes.data(), bytes.size());
        const float tint[4] = {0.8f, 0.4f, 0.2f, 0.5f};
        const Matrix identity = Matrix::getIdentityProperty();
        runtime->SetParameterValue(FindParameter(*runtime, "Tint"), tint, sizeof(tint));
        const float matrix[16] = {
            identity.M11, identity.M21, identity.M31, identity.M41,
            identity.M12, identity.M22, identity.M32, identity.M42,
            identity.M13, identity.M23, identity.M33, identity.M43,
            identity.M14, identity.M24, identity.M34, identity.M44,
        };
        runtime->SetParameterValue(
            FindParameter(*runtime, "Transform"), matrix, sizeof(matrix));
        runtime->SetTechnique(0);
        CompiledEffectPassStateChanges changes;
        runtime->ApplyPass(1, {}, changes);

        struct Position
        {
            float value[4];
        };
        const Position vertices[3] = {
            {{-0.75f, 0.75f, 0.5f, 1.0f}},
            {{-0.75f, -0.75f, 0.5f, 1.0f}},
            {{0.75f, 0.0f, 0.5f, 1.0f}},
        };
        const VertexDeclaration declaration(
            sizeof(Position),
            {VertexElement(0, VertexElementFormat::Vector4,
                           VertexElementUsage::Position, 0)});
        auto vertexBuffer = renderer.CreateVertexBuffer(3);
        vertexBuffer->SetVertexDeclaration(declaration);
        vertexBuffer->SetData(vertices, 3, sizeof(Position));
        GpuDrawParams params;
        params.compiledEffectRuntime = runtime.get();

        BlendWriteState masks;
        masks.colorWriteChannels[0] = 15;
        masks.colorWriteChannels[1] = 1;
        masks.colorWriteChannels[2] = 2;
        masks.colorWriteChannels[3] = 4;
        renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, masks);
        renderer.ClearColorAndDepth(0.1f, 0.3f, 0.6f, 1.0f, 1.0f);
        renderer.DrawPrimitivesEx(*vertexBuffer, identity, identity, identity,
                                  PrimitiveType::TriangleList, 1, params);

        const std::array<std::array<float, 4>, 4> expected = {{
            {0.8f, 0.4f, 0.2f, 0.5f},
            {0.4f, 0.3f, 0.6f, 1.0f},
            {0.1f, 0.8f, 0.6f, 1.0f},
            {0.1f, 0.3f, 0.8f, 1.0f},
        }};
        const std::size_t pixelIndex = 4u * size + 3u;
        for (std::size_t slot = 0; slot < softwareTargets.size(); ++slot)
        {
            Check(softwareTargets[slot]->Framebuffer().HasMultiSampleColor(),
                  "compiled MRT target did not allocate four-sample storage");
            for (int sample = 0; sample < 4; ++sample)
            {
                const auto actual =
                    softwareTargets[slot]->Framebuffer().ReadColor(pixelIndex, sample);
                for (std::size_t component = 0; component < 4; ++component)
                    Check(std::abs(actual[component] - expected[slot][component]) < 0.002f,
                          "compiled MRT COLOR output, mask or sample routing differs");
            }
        }

        masks.colorWriteChannels[0] = 15;
        masks.colorWriteChannels[1] = 15;
        masks.colorWriteChannels[2] = 15;
        masks.colorWriteChannels[3] = 15;
        renderer.ApplyBlendState(4, 0, 5, 1, 0, 0, masks);
        renderer.ClearColorAndDepth(0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
        renderer.DrawPrimitivesEx(*vertexBuffer, identity, identity, identity,
                                  PrimitiveType::TriangleList, 1, params);
        const auto blended = softwareTargets[0]->Framebuffer().ReadColor(pixelIndex, 0);
        Check(std::abs(blended[0] - 0.4f) < 0.002f &&
                  std::abs(blended[1] - 0.2f) < 0.002f &&
                  std::abs(blended[2] - 0.6f) < 0.002f &&
                  std::abs(blended[3] - 0.5f) < 0.002f,
              "compiled MRT output bypassed the active blend equation");

        renderer.SetRenderTargets(nullptr, 0);
        renderer.ApplyDepthStencilState(
            true, true, 3, false, 0, 0, 0, 0, 0xFF, 0xFF, 0,
            false, 0, 0, 0, 0);
    }

    void CheckCompiledLineAndWireframeRasterization(SoftwareRenderer& renderer)
    {
        const auto bytes = CNA::TestSupport::BuildSyntheticDrawableEffect();
        auto runtime = renderer.CreateCompiledEffect(bytes.data(), bytes.size());
        const float tint[4] = {1.0f, 128.0f / 255.0f, 0.0f, 1.0f};
        runtime->SetParameterValue(FindParameter(*runtime, "Tint"), tint, sizeof(tint));
        const Matrix identity = Matrix::getIdentityProperty();
        const float matrix[16] = {
            identity.M11, identity.M21, identity.M31, identity.M41,
            identity.M12, identity.M22, identity.M32, identity.M42,
            identity.M13, identity.M23, identity.M33, identity.M43,
            identity.M14, identity.M24, identity.M34, identity.M44,
        };
        runtime->SetParameterValue(
            FindParameter(*runtime, "Transform"), matrix, sizeof(matrix));
        runtime->SetTechnique(0);
        CompiledEffectPassStateChanges changes;
        runtime->ApplyPass(1, {}, changes);

        struct CompiledPosition
        {
            float value[4];
        };
        const CompiledPosition compiledVertices[3] = {
            {{-0.75f, 0.75f, 0.5f, 1.0f}},
            {{-0.75f, -0.75f, 0.5f, 1.0f}},
            {{0.75f, 0.0f, 0.5f, 1.0f}},
        };
        const VertexDeclaration compiledDeclaration(
            sizeof(CompiledPosition),
            {VertexElement(0, VertexElementFormat::Vector4,
                           VertexElementUsage::Position, 0)});
        auto compiledBuffer = renderer.CreateVertexBuffer(3);
        compiledBuffer->SetVertexDeclaration(compiledDeclaration);
        compiledBuffer->SetData(compiledVertices, 3, sizeof(CompiledPosition));

        struct StockPositionColor
        {
            float position[3];
            std::uint8_t color[4];
        };
        const StockPositionColor stockVertices[3] = {
            {{-0.75f, 0.75f, 0.5f}, {255u, 128u, 0u, 255u}},
            {{-0.75f, -0.75f, 0.5f}, {255u, 128u, 0u, 255u}},
            {{0.75f, 0.0f, 0.5f}, {255u, 128u, 0u, 255u}},
        };
        auto stockBuffer = renderer.CreateVertexBuffer(3);
        stockBuffer->SetData(stockVertices, 3, sizeof(StockPositionColor));

        GpuDrawParams params;
        params.compiledEffectRuntime = runtime.get();
        GpuDrawParams stockParams;
        stockParams.vertexColorEnabled = true;
        const auto clear = [&]
        {
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
        };
        const auto requireExactStockMatch = [&](PrimitiveType primitive, int primitiveCount,
                                                 const std::string& label)
        {
            clear();
            renderer.DrawPrimitivesEx(*compiledBuffer, identity, identity, identity,
                                      primitive, primitiveCount, params);
            const auto compiled = ReadBackbufferPixels(renderer);
            clear();
            renderer.DrawPrimitivesEx(*stockBuffer, identity, identity, identity,
                                      primitive, primitiveCount, stockParams);
            const auto stock = ReadBackbufferPixels(renderer);
            Check(compiled == stock, label + " compiled coverage/output differs from stock");
            Check(CountBackbufferColor(renderer, {255u, 128u, 0u, 255u}) > 0,
                  label + " produced no visible pixels");
        };

        renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, BlendWriteState{});
        renderer.ApplyDepthStencilState(
            true, true, 3, false, 0, 0, 0, 0, 0xFF, 0xFF, 0,
            false, 0, 0, 0, 0);
        renderer.ApplyRasterizerState(static_cast<int>(CullMode::None), 0, false);
        requireExactStockMatch(PrimitiveType::LineList, 1, "LineList");
        requireExactStockMatch(PrimitiveType::LineStrip, 2, "LineStrip");

        const std::uint16_t indices[3] = {2u, 1u, 0u};
        auto indexBuffer = renderer.CreateIndexBuffer16(3);
        indexBuffer->SetData16(indices, 3);
        clear();
        renderer.DrawIndexedPrimitivesEx(
            *compiledBuffer, *indexBuffer, identity, identity, identity,
            PrimitiveType::LineStrip, 2, params);
        const auto compiledIndexed = ReadBackbufferPixels(renderer);
        clear();
        renderer.DrawIndexedPrimitivesEx(
            *stockBuffer, *indexBuffer, identity, identity, identity,
            PrimitiveType::LineStrip, 2, stockParams);
        Check(compiledIndexed == ReadBackbufferPixels(renderer),
              "indexed compiled LineStrip coverage/output differs from stock");
        Check(CountBackbufferColor(renderer, {255u, 128u, 0u, 255u}) > 0,
              "indexed compiled LineStrip produced no visible pixels");

        renderer.ApplyRasterizerState(static_cast<int>(CullMode::None), 1, false);
        requireExactStockMatch(PrimitiveType::TriangleList, 1, "wireframe TriangleList");

        renderer.ApplyRasterizerState(
            static_cast<int>(CullMode::None), 1, false, 0.02f, 0.0f);
        renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 0.51f);
        renderer.DrawPrimitivesEx(*compiledBuffer, identity, identity, identity,
                                  PrimitiveType::TriangleList, 1, params);
        Check(CountBackbufferColor(renderer, {255u, 128u, 0u, 255u}) == 0,
              "compiled wireframe bypassed positive triangle depth bias");
        renderer.ApplyRasterizerState(
            static_cast<int>(CullMode::None), 1, false, -0.02f, 0.0f);
        renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 0.49f);
        renderer.DrawPrimitivesEx(*compiledBuffer, identity, identity, identity,
                                  PrimitiveType::TriangleList, 1, params);
        Check(CountBackbufferColor(renderer, {255u, 128u, 0u, 255u}) > 0,
              "compiled wireframe did not apply negative triangle depth bias");

        auto msaaTarget = renderer.CreateRenderTarget2DEXT(
            16, 16, 0, false, false, 4, static_cast<int>(SurfaceFormat::Color));
        auto* softwareMsaaTarget =
            dynamic_cast<SoftwareRenderTargetRenderer*>(msaaTarget.get());
        Check(softwareMsaaTarget != nullptr,
              "compiled line MSAA target did not use Software storage");
        const RenderTargetBindingDescriptor msaaBinding =
            RenderTargetBindingDescriptor::ForRenderTarget2D(
                msaaTarget.get(), 0, 16, 16, 4);
        renderer.SetRenderTargets(&msaaBinding, 1);
        renderer.ApplyRasterizerState(static_cast<int>(CullMode::None), 0, false);
        renderer.ApplyRasterizerMultiSampleState(true);
        clear();
        renderer.DrawPrimitivesEx(*compiledBuffer, identity, identity, identity,
                                  PrimitiveType::LineList, 1, params);
        bool foundPartialCoverage = false;
        for (std::size_t pixel = 0; pixel < 16u * 16u; ++pixel)
        {
            int coveredSamples = 0;
            for (int sample = 0; sample < 4; ++sample)
            {
                const auto value = softwareMsaaTarget->Framebuffer().ReadColor(pixel, sample);
                if (value[0] > 0.99f && value[1] > 0.49f && value[2] < 0.01f)
                    ++coveredSamples;
            }
            foundPartialCoverage = foundPartialCoverage ||
                (coveredSamples > 0 && coveredSamples < 4);
        }
        Check(foundPartialCoverage,
              "compiled LineList did not preserve independent 4x MSAA coverage");

        renderer.ApplyRasterizerMultiSampleState(false);
        clear();
        renderer.DrawPrimitivesEx(*compiledBuffer, identity, identity, identity,
                                  PrimitiveType::LineList, 1, params);
        bool foundReplicatedCoverage = false;
        bool foundPartialWhenDisabled = false;
        for (std::size_t pixel = 0; pixel < 16u * 16u; ++pixel)
        {
            int coveredSamples = 0;
            for (int sample = 0; sample < 4; ++sample)
            {
                const auto value = softwareMsaaTarget->Framebuffer().ReadColor(pixel, sample);
                if (value[0] > 0.99f && value[1] > 0.49f && value[2] < 0.01f)
                    ++coveredSamples;
            }
            foundReplicatedCoverage = foundReplicatedCoverage || coveredSamples == 4;
            foundPartialWhenDisabled = foundPartialWhenDisabled ||
                (coveredSamples > 0 && coveredSamples < 4);
        }
        Check(foundReplicatedCoverage && !foundPartialWhenDisabled,
              "compiled LineList did not replicate disabled-MSAA coverage to all samples");

        renderer.SetRenderTargets(nullptr, 0);
        renderer.ApplyRasterizerMultiSampleState(true);
        renderer.ApplyRasterizerState(static_cast<int>(CullMode::None), 0, false);

        CNA::TestSupport::SyntheticEffectOptions derivativeOptions;
        derivativeOptions.includeDrawableProgram = true;
        derivativeOptions.pixelShaderUsesDerivatives = true;
        const auto derivativeBytes =
            CNA::TestSupport::BuildSyntheticEffect(derivativeOptions);
        auto derivativeRuntime =
            renderer.CreateCompiledEffect(derivativeBytes.data(), derivativeBytes.size());
        derivativeRuntime->SetParameterValue(
            FindParameter(*derivativeRuntime, "Transform"), matrix, sizeof(matrix));
        const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        derivativeRuntime->SetParameterValue(
            FindParameter(*derivativeRuntime, "Tint"), white, sizeof(white));
        derivativeRuntime->SetTechnique(0);
        derivativeRuntime->ApplyPass(1, {}, changes);

        struct DerivativeVertex
        {
            float position[4];
            float coordinate[2];
        };
        const VertexDeclaration derivativeDeclaration(
            sizeof(DerivativeVertex),
            {VertexElement(0, VertexElementFormat::Vector4,
                           VertexElementUsage::Position, 0),
             VertexElement(16, VertexElementFormat::Vector2,
                           VertexElementUsage::TextureCoordinate, 0)});
        auto derivativeBuffer = renderer.CreateVertexBuffer(3);
        derivativeBuffer->SetVertexDeclaration(derivativeDeclaration);
        GpuDrawParams derivativeParams;
        derivativeParams.compiledEffectRuntime = derivativeRuntime.get();
        const auto expectDerivativePixel = [&](int x, int y, int red, int green,
                                               const std::string& label)
        {
            const auto pixels = ReadBackbufferPixels(renderer);
            const std::size_t offset = static_cast<std::size_t>(y * 16 + x) * 4u;
            Check(std::abs(static_cast<int>(pixels[offset]) - red) <= 1 &&
                      std::abs(static_cast<int>(pixels[offset + 1u]) - green) <= 1 &&
                      pixels[offset + 2u] == 0u && pixels[offset + 3u] == 255u,
                  label + " derivative differs at " + std::to_string(x) + "," +
                      std::to_string(y) + " (actual " + std::to_string(pixels[offset]) + "," +
                      std::to_string(pixels[offset + 1u]) + "," +
                      std::to_string(pixels[offset + 2u]) + "," +
                      std::to_string(pixels[offset + 3u]) + ")");
        };

        const DerivativeVertex horizontal[2] = {
            {{-0.75f, 0.0f, 0.5f, 1.0f}, {0.0f, 0.0f}},
            {{ 0.75f, 0.0f, 0.5f, 1.0f}, {1.0f, 0.0f}},
        };
        derivativeBuffer->SetData(horizontal, 2, sizeof(DerivativeVertex));
        clear();
        renderer.DrawPrimitivesEx(*derivativeBuffer, identity, identity, identity,
                                  PrimitiveType::LineList, 1, derivativeParams);
        expectDerivativePixel(4, 8, 9, 0, "horizontal compiled LineList");
        expectDerivativePixel(12, 8, 37, 0, "horizontal nonlinear compiled LineList");

        const DerivativeVertex vertical[2] = {
            {{0.0f,  0.75f, 0.5f, 1.0f}, {0.0f, 0.0f}},
            {{0.0f, -0.75f, 0.5f, 1.0f}, {0.0f, 1.0f}},
        };
        derivativeBuffer->SetData(vertical, 2, sizeof(DerivativeVertex));
        clear();
        renderer.DrawPrimitivesEx(*derivativeBuffer, identity, identity, identity,
                                  PrimitiveType::LineList, 1, derivativeParams);
        expectDerivativePixel(8, 4, 0, 9, "vertical compiled LineList");
        expectDerivativePixel(8, 12, 0, 37, "vertical nonlinear compiled LineList");

        const DerivativeVertex wireframe[3] = {
            {{-0.75f,  0.75f, 0.5f, 1.0f}, {0.0f, 0.0f}},
            {{-0.75f, -0.75f, 0.5f, 1.0f}, {0.0f, 1.0f}},
            {{ 0.75f,  0.0f,  0.5f, 1.0f}, {1.0f, 0.5f}},
        };
        derivativeBuffer->SetData(wireframe, 3, sizeof(DerivativeVertex));
        renderer.ApplyRasterizerState(static_cast<int>(CullMode::None), 1, false);
        clear();
        renderer.DrawPrimitivesEx(*derivativeBuffer, identity, identity, identity,
                                  PrimitiveType::TriangleList, 1, derivativeParams);
        expectDerivativePixel(2, 4, 0, 9, "compiled wireframe TriangleList");
        renderer.ApplyRasterizerState(static_cast<int>(CullMode::None), 0, false);
    }

    void CheckCompiledSpriteBatchRouting()
    {
        constexpr int size = 8;
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            PresentationParameters());
        const auto setProjection = [](Effect& effect)
        {
            effect.getParametersProperty()["Transform"]->SetValue(
                Matrix::CreateOrthographicOffCenter(
                    0.0f, static_cast<float>(size), static_cast<float>(size),
                    0.0f, -1.0f, 1.0f));
        };
        const Rectangle centre(size / 2, size / 2, 1, 1);
        const auto read = [&](RenderTarget2D& target, const Rectangle& area)
        {
            Color pixel = Color::Transparent;
            target.GetData(0, &area, &pixel, 0, 1);
            return pixel;
        };

        Texture2D white(device, 1, 1);
        const Color whitePixel[1] = {Color::White};
        white.SetData(whitePixel, 1);

        auto multiPass = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticDrawableEffect());
        setProjection(*multiPass);
        multiPass->getParametersProperty()["Tint"]->SetValue(
            Vector4(1.0f, 0.0f, 0.0f, 0.5f));
        RenderTarget2D multiPassTarget(device, size, size);
        device.SetRenderTarget(&multiPassTarget);
        device.Clear(Color::Black);
        {
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied,
                        nullptr, nullptr, nullptr, multiPass.get());
            batch.Draw(white, Rectangle(0, 0, size, size), Color::White);
            batch.Draw(white, Rectangle(0, 0, size, size), Color::White);
            batch.End();
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const Color multiPassPixel = read(multiPassTarget, centre);
        Check(std::abs(static_cast<int>(multiPassPixel.getRProperty()) - 192) <= 4 &&
                  multiPassPixel.getGProperty() <= 3,
              "Software SpriteBatch did not execute compiled passes in pass-major order");

        namespace Fx = CNA::TestSupport::EffectFormat;
        auto sampling = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticSamplingEffect({
                {Fx::SampMagFilter, Fx::FilterPoint},
                {Fx::SampMinFilter, Fx::FilterPoint},
                {Fx::SampMipFilter, Fx::FilterPoint},
                {Fx::SampAddressU, Fx::AddressClamp},
                {Fx::SampAddressV, Fx::AddressClamp},
            }));
        setProjection(*sampling);
        sampling->getParametersProperty()["Tint"]->SetValue(Vector4::One);
        Texture2D effectTexture(device, 1, 1);
        const Color redPixel[1] = {Color::Red};
        effectTexture.SetData(redPixel, 1);
        sampling->getParametersProperty()["FxTexture"]->SetValue(&effectTexture);

        Texture2D spriteTexture(device, 2, 1);
        const Color spritePixels[2] = {Color::Green, Color::Blue};
        spriteTexture.SetData(spritePixels, 2);
        RenderTarget2D textureTarget(device, size, size);
        device.SetRenderTarget(&textureTarget);
        device.Clear(Color::Black);
        {
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                        &SamplerState::PointClamp, nullptr, nullptr, sampling.get());
            batch.Draw(spriteTexture, Rectangle(0, 0, size, size), Color::White);
            batch.End();
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        // Stay away from the quad's shared diagonal so this assertion isolates sampler-slot
        // replacement from multisample edge ownership; x=6 also deterministically selects texel 1.
        const Color sampledSprite = read(textureTarget, Rectangle(6, 2, 1, 1));
        Check(sampledSprite.getRProperty() <= 3 && sampledSprite.getBProperty() >= 252,
              "compiled SpriteBatch did not override effect sampler zero with its source texture "
              "(actual RGBA=" + std::to_string(sampledSprite.getRProperty()) + "," +
                  std::to_string(sampledSprite.getGProperty()) + "," +
                  std::to_string(sampledSprite.getBProperty()) + "," +
                  std::to_string(sampledSprite.getAProperty()) + ")");

        auto pixelOnly = CNA::TestSupport::CompiledEffectTestAccess::Create(
            device, CNA::TestSupport::BuildSyntheticPixelOnlySamplingEffect({
                {Fx::SampMagFilter, Fx::FilterPoint},
                {Fx::SampMinFilter, Fx::FilterPoint},
                {Fx::SampMipFilter, Fx::FilterPoint},
                {Fx::SampAddressU, Fx::AddressClamp},
                {Fx::SampAddressV, Fx::AddressClamp},
            }));
        pixelOnly->getParametersProperty()["Tint"]->SetValue(
            Vector4(0.0f, 1.0f, 1.0f, 1.0f));
        pixelOnly->getParametersProperty()["FxTexture"]->SetValue(&effectTexture);
        RenderTarget2D inheritedVertexTarget(device, size, size);
        device.SetRenderTarget(&inheritedVertexTarget);
        device.Clear(Color::Black);
        {
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                        &SamplerState::PointClamp, nullptr, nullptr, pixelOnly.get(),
                        Matrix::CreateTranslation(4.0f, 0.0f, 0.0f));
            batch.Draw(white, Rectangle(0, 0, 4, size), Color::White);
            batch.End();
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const Color untranslated = read(inheritedVertexTarget, Rectangle(1, 2, 1, 1));
        const Color inherited = read(inheritedVertexTarget, Rectangle(5, 2, 1, 1));
        Check(untranslated == Color::Black && inherited.getRProperty() <= 3 &&
                  inherited.getGProperty() >= 252 && inherited.getBProperty() >= 252,
              "compiled SpriteBatch did not inherit SpriteEffect's transformed vertex stage "
              "for a pixel-only custom Effect");

        RenderTarget2D source(device, size, size);
        device.SetRenderTarget(&source);
        device.Clear(Color::Black);
        {
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            batch.Draw(white, Rectangle(0, 0, size, size / 2), Color::Red);
            batch.Draw(white, Rectangle(0, size / 2, size, size / 2), Color::Blue);
            batch.End();
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        RenderTarget2D destination(device, size, size);
        device.SetRenderTarget(&destination);
        device.Clear(Color::Black);
        {
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                        &SamplerState::PointClamp, nullptr, nullptr, sampling.get());
            batch.Draw(source, Rectangle(0, 0, size, size), Color::White);
            batch.End();
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const Color top = read(destination, Rectangle(size / 2, 1, 1, 1));
        const Color bottom = read(destination, Rectangle(size / 2, size - 2, 1, 1));
        Check(top.getRProperty() >= 252 && top.getBProperty() <= 3 &&
                  bottom.getBProperty() >= 252 && bottom.getRProperty() <= 3,
              "compiled SpriteBatch inverted or leaked a RenderTarget2D source transition");
    }

} // namespace

int main()
{
    try
    {
        SoftwareRenderer renderer(16, 16);
        CheckVertexInstructionSemantics();
        CheckPixelInstructionSemantics();
        CheckPixelDerivativeSemantics();
        CheckVertexConditionalSemantics();
        CheckPixelConditionalSemantics();
        CheckVertexLoopSemantics();
        CheckPixelLoopSemantics();
        CheckVertexSubroutineSemantics();
        CheckPixelSubroutineSemantics();
        CheckSubroutineValidation();
        CheckParsedSubroutineEffect(renderer);
        CheckParsedSignedLogEffect(renderer);
        CheckParsedNrmWriteMaskEffect(renderer);
        CheckParsedShaderModel11VertexEffects(renderer);
        CheckCompiledRelativeTextureCoordinate();
        CheckCompiledDependentTemporaryTextureCoordinate();
        CheckCompiledDependentTemporaryCubeCoordinate();
        CheckCompiledDependentTemporaryVolumeCoordinate();
        CheckCompiledDerivativeRasterization();
        CheckCompiledRasterInputs();
        CheckCompiledInstructionPredication();
        CheckCompiledPredicatedTexkill();
        CheckCompiledProjectiveSourceModifiers();
        CheckCompiledSamplerResultSwizzle();
        CheckCompiledShaderModel14TextureLoad();
        CheckCompiledShaderModel14Phase();
        CheckCompiledShaderModel14PhaseValidation(renderer);
        CheckCompiledUninitializedTemporaryValidation(renderer);
        CheckCompiledTexkillTemporaryValidation(renderer);
        CheckCompiledSgnValidation(renderer);
        CheckCompiledPartialPrecisionOpcodeValidation(renderer);
        CheckCompiledVertexOnlyAndExpValidation(renderer);
        CheckCompiledPixelShaderModel1OpcodeValidation(renderer);
        CheckCompiledVertexShaderModel1OpcodeValidation(renderer);
        CheckCompiledPixelShaderModel20OpcodeValidation(renderer);
        CheckCompiledLegacyTextureMatrix();
        CheckCompiledLegacyTextureMatrix2();
        CheckCompiledLegacyTextureMatrix3Sample();
        CheckCompiledLegacyTextureMatrix3Specular();
        CheckCompiledLegacyDepthOutputs();
        CheckCompiledLegacyTextureRemap();
        CheckCompiledLegacyDependentTextures();
        CheckCompiledLegacyBumpEnvironment();
        CheckCompiledVertexSamplerRasterization();
        CheckCompiledVertexSamplerDimensions();
        CheckCompiledSamplerRasterization(renderer);
        CheckCompiledMrtRasterization(renderer);
        CheckCompiledLineAndWireframeRasterization(renderer);
        CheckCompiledSpriteBatchRouting();
        Check(!renderer.SupportsCompiledEffects(),
              "incomplete SOFTWARE-164/165 path must not advertise compiled effects");

        const std::string stockDirectory = CNA_SOFTWARE_STOCK_EFFECT_DIRECTORY;
        int textureFreePixelPrograms = 0;
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
                ExerciseEveryVertexProgram(*runtime, name);
                textureFreePixelPrograms +=
                    ExerciseTextureFreePixelPrograms(*runtime, name);
                ExerciseEveryPixelProgram(*runtime, name);
            }
        }
        Check(textureFreePixelPrograms > 0,
              "stock effects exposed no texture-free pixel program to execute");

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
        ExerciseEveryVertexProgram(*authentic, "authentic XNA 4 effect");
        static_cast<void>(ExerciseTextureFreePixelPrograms(
            *authentic, "authentic XNA 4 effect"));
        ExerciseEveryPixelProgram(*authentic, "authentic XNA 4 effect");

        const auto syntheticBytes = CNA::TestSupport::BuildSyntheticDrawableEffect(
            /*readsSecondStream=*/true);
        auto synthetic =
            renderer.CreateCompiledEffect(syntheticBytes.data(), syntheticBytes.size());
        const float streamMix[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        synthetic->SetParameterValue(FindParameter(*synthetic, "StreamMix"), streamMix,
                                     sizeof(streamMix));
        const float syntheticTint[4] = {0.25f, 0.5f, 0.75f, 1.0f};
        synthetic->SetParameterValue(FindParameter(*synthetic, "Tint"), syntheticTint,
                                     sizeof(syntheticTint));
        constexpr std::array<std::uint8_t, 4> expectedTint{64u, 128u, 191u, 255u};
        synthetic->SetTechnique(0);
        CompiledEffectPassStateChanges syntheticChanges;
        synthetic->ApplyPass(1, {}, syntheticChanges);
        renderer.ApplyRasterizerState(0, 0, false);
        auto* softwareSynthetic = dynamic_cast<SoftwareCompiledEffect*>(synthetic.get());
        Check(softwareSynthetic != nullptr, "synthetic runtime has the wrong backend type");
        if (softwareSynthetic != nullptr)
        {
            const SoftwareShaderSemanticValueEXT syntheticInputs[] = {
                {MOJOSHADER_USAGE_POSITION, 0u, {0.25f, -0.5f, 0.75f, 1.0f}},
                {MOJOSHADER_USAGE_TEXCOORD, 0u, {0.5f, 0.25f, -0.25f, 0.0f}},
            };
            const auto syntheticVertex = softwareSynthetic->ExecuteVertexEXT(syntheticInputs);
            Check(syntheticVertex.position ==
                      std::array<float, 4>{0.75f, -0.25f, 0.5f, 1.0f},
                  "synthetic MAD/M4X4 vertex execution differs");

            struct PositionUv
            {
                float position[4];
                float uv[4];
            };
            const VertexDeclaration interleavedDeclaration(
                static_cast<int>(sizeof(PositionUv)),
                {VertexElement(0, VertexElementFormat::Vector4, VertexElementUsage::Position, 0),
                 VertexElement(16, VertexElementFormat::Vector4,
                               VertexElementUsage::TextureCoordinate, 0)});
            const Matrix identity = Matrix::getIdentityProperty();

            const PositionUv stagedVertices[] = {
                {{9.0f, 9.0f, 9.0f, 1.0f}, {9.0f, 9.0f, 9.0f, 0.0f}},
                {{-2.0f, 0.75f, 0.5f, 1.0f}, {0.25f, 0.0f, 0.0f, 0.0f}},
                {{-0.5f, -0.75f, 0.5f, 1.0f}, {0.25f, 0.0f, 0.0f, 0.0f}},
                {{0.5f, 0.5f, 0.5f, 1.0f}, {0.25f, 0.0f, 0.0f, 0.0f}},
            };
            auto staged = renderer.CreateVertexBuffer(4);
            staged->SetVertexDeclaration(interleavedDeclaration);
            staged->SetData(stagedVertices, 4, sizeof(PositionUv));
            GpuDrawParams stagedParams;
            stagedParams.compiledEffectRuntime = synthetic.get();
            stagedParams.vertexStart = 1;
            const std::size_t beforeStaged = softwareSynthetic->GetVertexExecutionCountEXT();
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            renderer.DrawPrimitivesEx(*staged, identity, identity, identity,
                                      PrimitiveType::TriangleList, 1, stagedParams);
            Check(softwareSynthetic->GetVertexExecutionCountEXT() == beforeStaged + 3u,
                  "non-indexed vertexStart route did not execute exactly three vertices");
            Check(softwareSynthetic->GetLastVertexResultEXT().position ==
                      std::array<float, 4>{0.75f, 0.5f, 0.5f, 1.0f},
                  "non-indexed vertexStart route fetched the wrong final record");
            Check(CountBackbufferColor(renderer, expectedTint) > 0,
                  "compiled non-indexed route did not write pixel-shader COLOR0");

            const PositionUv indexedVertices[] = {
                {{9.0f, 9.0f, 9.0f, 1.0f}, {9.0f, 9.0f, 9.0f, 0.0f}},
                {{9.0f, 9.0f, 9.0f, 1.0f}, {9.0f, 9.0f, 9.0f, 0.0f}},
                {{-0.75f, 0.75f, 0.5f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
                {{-0.75f, -0.75f, 0.5f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
                {{0.5f, 0.0f, 0.5f, 1.0f}, {0.125f, 0.0f, 0.0f, 0.0f}},
            };
            const std::uint16_t indexedElements[] = {7, 7, 7, 0, 1, 2};
            auto indexedVertexBuffer = renderer.CreateVertexBuffer(5);
            indexedVertexBuffer->SetVertexDeclaration(interleavedDeclaration);
            indexedVertexBuffer->SetData(indexedVertices, 5, sizeof(PositionUv));
            auto indexBuffer = renderer.CreateIndexBuffer16(6);
            indexBuffer->SetData16(indexedElements, 6);
            GpuDrawParams indexedParams;
            indexedParams.compiledEffectRuntime = synthetic.get();
            indexedParams.startIndex = 3;
            indexedParams.baseVertex = 2;
            const std::size_t beforeIndexed = softwareSynthetic->GetVertexExecutionCountEXT();
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            renderer.DrawIndexedPrimitivesEx(
                *indexedVertexBuffer, *indexBuffer, identity, identity, identity,
                PrimitiveType::TriangleList, 1, indexedParams);
            Check(softwareSynthetic->GetVertexExecutionCountEXT() == beforeIndexed + 3u,
                  "indexed base/start route did not execute exactly three vertices");
            Check(softwareSynthetic->GetLastVertexResultEXT().position ==
                      std::array<float, 4>{0.625f, 0.0f, 0.5f, 1.0f},
                  "indexed base/start route fetched the wrong final record");
            Check(CountBackbufferColor(renderer, expectedTint) > 0,
                  "compiled indexed route did not write pixel-shader COLOR0");

            struct PositionOnly
            {
                float value[4];
            };
            const VertexDeclaration positionDeclaration(
                16, {VertexElement(0, VertexElementFormat::Vector4,
                                   VertexElementUsage::Position, 0)});
            const VertexDeclaration offsetDeclaration(
                16, {VertexElement(0, VertexElementFormat::Vector4,
                                   VertexElementUsage::TextureCoordinate, 0)});
            PositionOnly positions[5]{};
            PositionOnly offsets[6]{};
            positions[2] = {{-0.75f, 0.75f, 0.5f, 1.0f}};
            positions[3] = {{-0.75f, -0.75f, 0.5f, 1.0f}};
            positions[4] = {{0.25f, 0.0f, 0.5f, 1.0f}};
            offsets[3] = {{0.5f, 0.0f, 0.0f, 0.0f}};
            offsets[4] = {{0.5f, 0.0f, 0.0f, 0.0f}};
            offsets[5] = {{0.5f, 0.0f, 0.0f, 0.0f}};
            auto positionsBuffer = renderer.CreateVertexBuffer(5);
            positionsBuffer->SetVertexDeclaration(positionDeclaration);
            positionsBuffer->SetData(positions, 5, sizeof(PositionOnly));
            auto offsetsBuffer = renderer.CreateVertexBuffer(6);
            offsetsBuffer->SetVertexDeclaration(offsetDeclaration);
            offsetsBuffer->SetData(offsets, 6, sizeof(PositionOnly));
            GpuDrawParams multiStreamParams;
            multiStreamParams.compiledEffectRuntime = synthetic.get();
            multiStreamParams.vertexStart = 1;
            multiStreamParams.vertexStreamCount = 2;
            multiStreamParams.combinedVertexStride = 32;
            multiStreamParams.vertexStreams[0] =
                GpuVertexStreamBinding{0, positionsBuffer.get(), 16, 0, 1, 0, 5, true};
            multiStreamParams.vertexStreams[1] =
                GpuVertexStreamBinding{1, offsetsBuffer.get(), 16, 16, 2, 0, 6, true};
            const std::size_t beforeMulti = softwareSynthetic->GetVertexExecutionCountEXT();
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            renderer.DrawPrimitivesEx(*positionsBuffer, identity, identity, identity,
                                      PrimitiveType::TriangleList, 1, multiStreamParams);
            Check(softwareSynthetic->GetVertexExecutionCountEXT() == beforeMulti + 3u,
                  "multi-stream route did not execute exactly three vertices");
            Check(softwareSynthetic->GetLastVertexResultEXT().position ==
                      std::array<float, 4>{0.75f, 0.0f, 0.5f, 1.0f},
                  "multi-stream route did not combine its independent offsets");
            Check(CountBackbufferColor(renderer, expectedTint) > 0,
                  "compiled multi-stream route did not write pixel-shader COLOR0");

            const PositionOnly instancePositions[] = {
                {{-0.75f, 0.75f, 0.5f, 1.0f}},
                {{-0.75f, -0.75f, 0.5f, 1.0f}},
                {{-0.25f, 0.0f, 0.5f, 1.0f}},
            };
            const PositionOnly instances[] = {
                {{0.0f, 0.0f, 0.0f, 0.0f}},
                {{1.0f, 0.0f, 0.0f, 0.0f}},
            };
            const std::uint16_t triangleIndices[] = {0, 1, 2};
            auto instancePositionBuffer = renderer.CreateVertexBuffer(3);
            instancePositionBuffer->SetVertexDeclaration(positionDeclaration);
            instancePositionBuffer->SetData(instancePositions, 3, sizeof(PositionOnly));
            auto instanceBuffer = renderer.CreateVertexBuffer(2);
            instanceBuffer->SetVertexDeclaration(offsetDeclaration);
            instanceBuffer->SetData(instances, 2, sizeof(PositionOnly));
            auto instanceIndexBuffer = renderer.CreateIndexBuffer16(3);
            instanceIndexBuffer->SetData16(triangleIndices, 3);
            GpuDrawParams instanceParams;
            instanceParams.compiledEffectRuntime = synthetic.get();
            instanceParams.vertexStreamCount = 2;
            instanceParams.combinedVertexStride = 16;
            instanceParams.vertexStreams[0] = GpuVertexStreamBinding{
                0, instancePositionBuffer.get(), 16, 0, 0, 0, 3, true};
            instanceParams.vertexStreams[1] =
                GpuVertexStreamBinding{1, instanceBuffer.get(), 16, 16, 0, 1, 2, true};
            const std::size_t beforeInstances = softwareSynthetic->GetVertexExecutionCountEXT();
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            renderer.DrawInstancedPrimitivesEx(
                *instancePositionBuffer, *instanceIndexBuffer, identity, identity, identity,
                PrimitiveType::TriangleList, 1, 2, instanceParams);
            Check(softwareSynthetic->GetVertexExecutionCountEXT() == beforeInstances + 6u,
                  "instanced route did not execute three vertices for each instance");
            Check(softwareSynthetic->GetLastVertexResultEXT().position ==
                      std::array<float, 4>{0.75f, 0.0f, 0.5f, 1.0f},
                  "instanced route did not advance the per-instance semantic");
            Check(CountBackbufferColor(renderer, expectedTint) > 0,
                  "compiled instanced route did not write pixel-shader COLOR0");

            const float blendedTint[4] = {1.0f, 0.0f, 0.0f, 0.5f};
            synthetic->SetParameterValue(FindParameter(*synthetic, "Tint"), blendedTint,
                                         sizeof(blendedTint));
            synthetic->ApplyPass(1, {}, syntheticChanges);
            renderer.ApplyBlendState(4, 0, 5, 1, 0, 0, BlendWriteState{});
            renderer.ClearColorAndDepth(0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
            renderer.DrawPrimitivesEx(*staged, identity, identity, identity,
                                      PrimitiveType::TriangleList, 1, stagedParams);
            Check(CountBackbufferColor(renderer, {128u, 0u, 128u, 128u}) > 0,
                  "compiled COLOR0 bypassed the active independent blend equation");

            synthetic->SetParameterValue(FindParameter(*synthetic, "Tint"), syntheticTint,
                                         sizeof(syntheticTint));
            synthetic->ApplyPass(1, {}, syntheticChanges);
            BlendWriteState greenOnly;
            greenOnly.colorWriteChannels[0] = 2;
            renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, greenOnly);
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            renderer.DrawPrimitivesEx(*staged, identity, identity, identity,
                                      PrimitiveType::TriangleList, 1, stagedParams);
            Check(CountBackbufferColor(renderer, {0u, 128u, 0u, 255u}) > 0,
                  "compiled COLOR0 bypassed ColorWriteChannels0");

            renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, BlendWriteState{});
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 0.4f);
            renderer.DrawPrimitivesEx(*staged, identity, identity, identity,
                                      PrimitiveType::TriangleList, 1, stagedParams);
            Check(CountBackbufferColor(renderer, expectedTint) == 0,
                  "compiled COLOR0 ignored a failing depth comparison");
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 0.6f);
            renderer.DrawPrimitivesEx(*staged, identity, identity, identity,
                                      PrimitiveType::TriangleList, 1, stagedParams);
            Check(CountBackbufferColor(renderer, expectedTint) > 0,
                  "compiled COLOR0 did not reach a passing depth fragment");
        }

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
            const SoftwareShaderSemanticValueEXT inputs[] = {
                {MOJOSHADER_USAGE_POSITION, 0u, {0.25f, -0.5f, 0.75f, 1.0f}},
                {MOJOSHADER_USAGE_TEXCOORD, 0u, {0.125f, 0.875f, 0.0f, 1.0f}},
            };
            const auto vertex = softwareRuntime->ExecuteVertexEXT(inputs);
            Check(vertex.position == inputs[0].value,
                  "identity conformance vertex shader changed POSITION0");
            const auto texCoord = std::find_if(
                vertex.varyings.begin(), vertex.varyings.end(), [](const auto& value)
                {
                    return value.usage == MOJOSHADER_USAGE_TEXCOORD &&
                           value.usageIndex == 0u;
                });
            Check(texCoord != vertex.varyings.end(),
                  "conformance vertex shader did not publish TEXCOORD0");
            if (texCoord != vertex.varyings.end())
            {
                Check(texCoord->value[0] == inputs[1].value[0] &&
                          texCoord->value[1] == inputs[1].value[1],
                      "conformance vertex shader changed TEXCOORD0");
            }
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
