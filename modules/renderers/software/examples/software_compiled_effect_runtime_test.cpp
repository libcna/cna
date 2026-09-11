// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareCompiledEffect.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/NotSupportedException.hpp"

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
using CNA::Internal::Renderers::GpuDrawParams;
using CNA::Internal::Renderers::GpuVertexStreamBinding;
using CNA::Internal::Renderers::ICompiledEffectRuntime;
using CNA::Internal::Renderers::Software::SoftwareCompiledEffect;
using CNA::Internal::Renderers::Software::SoftwareRenderer;
using CNA::Internal::Renderers::Software::SoftwareShaderInstructionEXT;
using CNA::Internal::Renderers::Software::SoftwareShaderSemanticValueEXT;
using CNA::Internal::Renderers::Software::SoftwareShaderProgramEXT;
using CNA::Internal::Renderers::Software::SoftwareShaderStageEXT;
using CNA::Internal::Renderers::Software::ExecuteSoftwareVertexShaderEXT;
using Microsoft::Xna::Framework::Graphics::Blend;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::CullMode;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
using Microsoft::Xna::Framework::Graphics::TextureFilter;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;

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

    template<typename Draw>
    void ExpectPixelPhaseRefusal(Draw&& draw, const std::string& label)
    {
        try
        {
            draw();
            Check(false, label + " silently drew without a pixel interpreter");
        }
        catch (const System::NotSupportedException& exception)
        {
            Check(std::string(exception.what()).find("SOFTWARE-164") != std::string::npos,
                  label + " returned the wrong incomplete-phase error");
        }
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
        Check(varying(7) == std::array<float, 4>{-2.0f, 1.0f, -0.5f, -3.0f},
              "source negate result differs");
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

} // namespace

int main()
{
    try
    {
        SoftwareRenderer renderer(16, 16);
        CheckVertexInstructionSemantics();
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
                ExerciseEveryVertexProgram(*runtime, name);
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
        ExerciseEveryVertexProgram(*authentic, "authentic XNA 4 effect");

        const auto syntheticBytes = CNA::TestSupport::BuildSyntheticDrawableEffect(
            /*readsSecondStream=*/true);
        auto synthetic =
            renderer.CreateCompiledEffect(syntheticBytes.data(), syntheticBytes.size());
        const float streamMix[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        synthetic->SetParameterValue(FindParameter(*synthetic, "StreamMix"), streamMix,
                                     sizeof(streamMix));
        synthetic->SetTechnique(0);
        CompiledEffectPassStateChanges syntheticChanges;
        synthetic->ApplyPass(1, {}, syntheticChanges);
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
            ExpectPixelPhaseRefusal(
                [&]
                {
                    renderer.DrawPrimitivesEx(*staged, identity, identity, identity,
                                              PrimitiveType::TriangleList, 1, stagedParams);
                },
                "compiled staged/user non-indexed route");
            Check(softwareSynthetic->GetVertexExecutionCountEXT() == beforeStaged + 3u,
                  "non-indexed vertexStart route did not execute exactly three vertices");
            Check(softwareSynthetic->GetLastVertexResultEXT().position ==
                      std::array<float, 4>{0.75f, 0.5f, 0.5f, 1.0f},
                  "non-indexed vertexStart route fetched the wrong final record");

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
            ExpectPixelPhaseRefusal(
                [&]
                {
                    renderer.DrawIndexedPrimitivesEx(
                        *indexedVertexBuffer, *indexBuffer, identity, identity, identity,
                        PrimitiveType::TriangleList, 1, indexedParams);
                },
                "compiled indexed base/start route");
            Check(softwareSynthetic->GetVertexExecutionCountEXT() == beforeIndexed + 3u,
                  "indexed base/start route did not execute exactly three vertices");
            Check(softwareSynthetic->GetLastVertexResultEXT().position ==
                      std::array<float, 4>{0.625f, 0.0f, 0.5f, 1.0f},
                  "indexed base/start route fetched the wrong final record");

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
            ExpectPixelPhaseRefusal(
                [&]
                {
                    renderer.DrawPrimitivesEx(*positionsBuffer, identity, identity, identity,
                                              PrimitiveType::TriangleList, 1,
                                              multiStreamParams);
                },
                "compiled multi-stream offset route");
            Check(softwareSynthetic->GetVertexExecutionCountEXT() == beforeMulti + 3u,
                  "multi-stream route did not execute exactly three vertices");
            Check(softwareSynthetic->GetLastVertexResultEXT().position ==
                      std::array<float, 4>{0.75f, 0.0f, 0.5f, 1.0f},
                  "multi-stream route did not combine its independent offsets");

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
            ExpectPixelPhaseRefusal(
                [&]
                {
                    renderer.DrawInstancedPrimitivesEx(
                        *instancePositionBuffer, *instanceIndexBuffer, identity, identity, identity,
                        PrimitiveType::TriangleList, 1, 2, instanceParams);
                },
                "compiled instanced divisor route");
            Check(softwareSynthetic->GetVertexExecutionCountEXT() == beforeInstances + 6u,
                  "instanced route did not execute three vertices for each instance");
            Check(softwareSynthetic->GetLastVertexResultEXT().position ==
                      std::array<float, 4>{0.75f, 0.0f, 0.5f, 1.0f},
                  "instanced route did not advance the per-instance semantic");
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
