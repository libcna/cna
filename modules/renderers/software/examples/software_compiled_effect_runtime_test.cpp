// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareCompiledEffect.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
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
using CNA::Internal::Renderers::Software::ExecuteSoftwareVertexShaderEXT;
using Microsoft::Xna::Framework::Graphics::Blend;
using Microsoft::Xna::Framework::Graphics::BlendState;
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
        add(15, {15u, destination(textureOutput, 5), source(temporary, 0)});
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
        constexpr float normalizedLength = 9.433981132056603f;
        Check(std::abs(varying(3)[0] - 3.0f / normalizedLength) < 0.00001f &&
                  std::abs(varying(3)[1] - 4.0f / normalizedLength) < 0.00001f &&
                  varying(3)[2] == 0.0f &&
                  std::abs(varying(3)[3] - 8.0f / normalizedLength) < 0.00001f,
              "NRM write-mask dimensionality differs");
        Check(varying(4) == std::array<float, 4>{22.0f, 23.0f, 24.0f, 25.0f},
              "MOVA/relative constant result differs");
        for (std::size_t component = 0; component < 4; ++component)
            Check(std::abs(varying(5)[component] - floats[15u * 4u + component]) < 0.00001f,
                  "EXP/LOG round trip differs");
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
        checkArithmetic(80u, 0xFu,
                        {source(constant, 32), source(constant, 33), source(constant, 34)},
                        {5.0f, 2.0f, 7.0f, 4.0f}, "pixel Shader Model 1 CND", 1u);

        constexpr std::uint32_t samplerRegisterType = 10u;
        constexpr std::uint32_t predicateRegisterType = 19u;
        SoftwareShaderProgramEXT textureProgram;
        textureProgram.stage = SoftwareShaderStageEXT::Pixel;
        textureProgram.majorVersion = 2;
        textureProgram.minorVersion = 0;
        textureProgram.inputSemantics = {
            {MOJOSHADER_USAGE_TEXCOORD, 0u, 0u, 3u},
        };
        textureProgram.samplers.push_back(
            SoftwareShaderSamplerEXT{3u, SoftwareShaderSamplerTypeEXT::Volume});
        SoftwareShaderInstructionEXT textureLookup;
        textureLookup.opcode = 66u;
        textureLookup.controls = 2u;
        textureLookup.tokens = {
            66u, destination(temporary, 0), source(texture, 0),
            source(samplerRegisterType, 3)};
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
        Check(sampled.colorWriteMask == 1u && sampled.colors[0] == recordingSampler.result,
              "TEX result did not reach COLOR0");

        textureProgram.instructions[0].opcode = 95u;
        textureProgram.instructions[0].controls = 0u;
        textureProgram.instructions[0].tokens[0] = 95u;
        static_cast<void>(ExecuteSoftwarePixelShaderEXT(
            textureProgram, floats, integers, booleans, inputs, &recordingSampler));
        Check(recordingSampler.lastRequest.lodMode == SoftwareTextureLodModeEXT::Explicit &&
                  recordingSampler.lastRequest.lod == inputs[1].value[3],
              "TEXLDL did not publish its explicit level");

        textureProgram.instructions[0].opcode = 93u;
        textureProgram.instructions[0].tokens = {
            93u, destination(temporary, 0), source(texture, 0),
            source(samplerRegisterType, 3), source(input, 0), source(constant, 0)};
        static_cast<void>(ExecuteSoftwarePixelShaderEXT(
            textureProgram, floats, integers, booleans, inputs, &recordingSampler));
        Check(recordingSampler.lastRequest.lodMode == SoftwareTextureLodModeEXT::Gradients &&
                  recordingSampler.lastRequest.gradientX == inputs[0].value &&
                  recordingSampler.lastRequest.gradientY ==
                      std::array<float, 4>{2.0f, -1.0f, 0.5f, 1.0f},
              "TEXLDD did not publish both explicit gradients");

        const int samplesBeforeSetp = recordingSampler.sampleCount;
        textureProgram.instructions[0].opcode = 94u;
        textureProgram.instructions[0].tokens = {
            94u, destination(predicateRegisterType, 0),
            source(constant, 0), source(constant, 1)};
        bool setpRejectedAsPending = false;
        try
        {
            static_cast<void>(ExecuteSoftwarePixelShaderEXT(
                textureProgram, floats, integers, booleans, inputs, &recordingSampler));
        }
        catch (const std::runtime_error& error)
        {
            setpRejectedAsPending =
                std::string(error.what()).find("unsupported opcode 94") != std::string::npos;
        }
        Check(setpRejectedAsPending && recordingSampler.sampleCount == samplesBeforeSetp,
              "SETP opcode 94 was still misrouted through TEXLDD sampling");

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
