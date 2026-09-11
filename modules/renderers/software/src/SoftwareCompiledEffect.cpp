// SPDX-License-Identifier: MS-PL

#if defined(CNA_SOFTWARE_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/Software/SoftwareCompiledEffect.hpp"

#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace CNA::Internal::Renderers::Software
{
    namespace
    {
        constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;
        constexpr std::size_t kMaximumShaderBytes = 64u * 1024u * 1024u;
        constexpr std::size_t kFloatRegisterCount = 8192u * 4u;
        constexpr std::size_t kIntegerRegisterCount = 2047u * 4u;
        constexpr std::size_t kBooleanRegisterCount = 2047u;

        std::uint32_t ReadUInt32LittleEndian(const std::uint8_t* bytes)
        {
            return static_cast<std::uint32_t>(bytes[0]) |
                   (static_cast<std::uint32_t>(bytes[1]) << 8u) |
                   (static_cast<std::uint32_t>(bytes[2]) << 16u) |
                   (static_cast<std::uint32_t>(bytes[3]) << 24u);
        }

        std::uint8_t ShaderRegisterType(std::uint32_t token)
        {
            return static_cast<std::uint8_t>(((token >> 28u) & 0x7u) |
                                             ((token >> 8u) & 0x18u));
        }

        std::pair<MOJOSHADER_usage, std::uint8_t> ShaderModelOneVertexInputSemantic(
            int registerNumber)
        {
            switch (registerNumber)
            {
            case 0: return {MOJOSHADER_USAGE_POSITION, 0u};
            case 1: return {MOJOSHADER_USAGE_BLENDWEIGHT, 0u};
            case 2: return {MOJOSHADER_USAGE_BLENDINDICES, 0u};
            case 3: return {MOJOSHADER_USAGE_NORMAL, 0u};
            case 4: return {MOJOSHADER_USAGE_POINTSIZE, 0u};
            case 5: return {MOJOSHADER_USAGE_COLOR, 0u};
            case 6: return {MOJOSHADER_USAGE_COLOR, 1u};
            case 7: return {MOJOSHADER_USAGE_TEXCOORD, 0u};
            case 8: return {MOJOSHADER_USAGE_TEXCOORD, 1u};
            case 9: return {MOJOSHADER_USAGE_TEXCOORD, 2u};
            case 10: return {MOJOSHADER_USAGE_TEXCOORD, 3u};
            case 11: return {MOJOSHADER_USAGE_TEXCOORD, 4u};
            case 12: return {MOJOSHADER_USAGE_TEXCOORD, 5u};
            case 13: return {MOJOSHADER_USAGE_TEXCOORD, 6u};
            case 14: return {MOJOSHADER_USAGE_TEXCOORD, 7u};
            case 15: return {MOJOSHADER_USAGE_POSITION, 1u};
            case 16: return {MOJOSHADER_USAGE_NORMAL, 1u};
            default:
                throw std::runtime_error(
                    "Software compiled effect: Shader Model 1 vertex input register is out of "
                    "range.");
            }
        }

        int ShaderModelOneVertexInputRegister(const MOJOSHADER_attribute& input)
        {
            constexpr std::string_view prefix = "vs_v";
            if (input.name == nullptr)
                throw std::runtime_error(
                    "Software compiled effect: Shader Model 1 vertex input has no parser name.");
            const std::string_view name(input.name);
            if (!name.starts_with(prefix) || name.size() == prefix.size())
                throw std::runtime_error(
                    "Software compiled effect: Shader Model 1 vertex input parser name is "
                    "invalid.");
            int result = 0;
            for (const char digit : name.substr(prefix.size()))
            {
                if (digit < '0' || digit > '9')
                    throw std::runtime_error(
                        "Software compiled effect: Shader Model 1 vertex input parser name is "
                        "invalid.");
                result = result * 10 + (digit - '0');
            }
            return result;
        }

        std::size_t ShaderModelOneOperandCount(std::uint16_t opcode, SoftwareShaderStageEXT stage,
                                               std::uint8_t minorVersion)
        {
            switch (opcode)
            {
            case 0:
                return 0; // nop
            case 1:
                return 2; // mov
            case 2:
                return 3; // add
            case 3:
                return 3; // sub
            case 4:
                return 4; // mad
            case 5:
                return 3; // mul
            case 6:
                return 2; // rcp
            case 7:
                return 2; // rsq
            case 8:
                return 3; // dp3
            case 9:
                return 3; // dp4
            case 10:
                return 3; // min
            case 11:
                return 3; // max
            case 12:
                return 3; // slt
            case 13:
                return 3; // sge
            case 14:
                return 2; // exp
            case 15:
                return 2; // log
            case 16:
                return 2; // lit
            case 17:
                return 3; // dst
            case 18:
                return 4; // lrp
            case 19:
                return 2; // frc
            case 20:
                return 3; // m4x4
            case 21:
                return 3; // m4x3
            case 22:
                return 3; // m3x4
            case 23:
                return 3; // m3x3
            case 24:
                return 3; // m3x2
            case 31:
                return 2; // dcl declaration token, destination
            case 46:
                return 2; // mova
            case 64:      // texcrd: ps_1_4 adds an explicit source
                return stage == SoftwareShaderStageEXT::Pixel && minorVersion == 4 ? 2u : 1u;
            case 65:
                return 1; // texkill
            case 66:      // texld: ps_1_4 adds an explicit source
                return stage == SoftwareShaderStageEXT::Pixel && minorVersion == 4 ? 2u : 1u;
            case 67:
                return 2; // texbem
            case 68:
                return 2; // texbeml
            case 69:
                return 2; // texreg2ar
            case 70:
                return 2; // texreg2gb
            case 71:
                return 2; // texm3x2pad
            case 72:
                return 2; // texm3x2tex
            case 73:
                return 2; // texm3x3pad
            case 74:
                return 2; // texm3x3tex
            case 76:
                return 3; // texm3x3spec
            case 77:
                return 2; // texm3x3vspec
            case 78:
                return 2; // expp
            case 79:
                return 2; // logp
            case 80:
                return 4; // cnd
            case 81:
                return 5; // def
            case 82:
                return 2; // texreg2rgb
            case 83:
                return 2; // texdp3tex
            case 84:
                return 2; // texm3x2depth
            case 85:
                return 2; // texdp3
            case 86:
                return 2; // texm3x3
            case 87:
                return 1; // texdepth
            case 88:
                return 4; // cmp
            case 89:
                return 3; // bem
            case 90:
                return 4; // dp2add
            default:
                throw std::runtime_error(
                    "Software compiled effect: unsupported Shader Model 1 opcode " +
                    std::to_string(opcode) + ".");
            }
        }

        SoftwareShaderProgramEXT DecodeProgram(const std::uint8_t* bytecode, std::size_t byteCount,
                                               const MOJOSHADER_parseData& parseData)
        {
            if (bytecode == nullptr || byteCount < 8u || byteCount > kMaximumShaderBytes ||
                (byteCount & 3u) != 0u)
            {
                throw std::runtime_error(
                    "Software compiled effect: shader token stream has an invalid size.");
            }

            SoftwareShaderProgramEXT result;
            const std::size_t tokenCount = byteCount / 4u;
            result.tokens.reserve(tokenCount);
            for (std::size_t i = 0; i < tokenCount; ++i)
                result.tokens.push_back(ReadUInt32LittleEndian(bytecode + i * 4u));

            const std::uint32_t version = result.tokens.front();
            const std::uint16_t stageToken = static_cast<std::uint16_t>(version >> 16u);
            if (stageToken == 0xFFFEu)
                result.stage = SoftwareShaderStageEXT::Vertex;
            else if (stageToken == 0xFFFFu)
                result.stage = SoftwareShaderStageEXT::Pixel;
            else
                throw std::runtime_error("Software compiled effect: shader token stream "
                                         "has no D3D9 stage marker.");

            result.majorVersion = static_cast<std::uint8_t>((version >> 8u) & 0xFFu);
            result.minorVersion = static_cast<std::uint8_t>(version & 0xFFu);
            if (result.majorVersion < 1u || result.majorVersion > 3u ||
                parseData.major_ver != result.majorVersion ||
                parseData.minor_ver != result.minorVersion ||
                (parseData.shader_type == MOJOSHADER_TYPE_VERTEX) !=
                    (result.stage == SoftwareShaderStageEXT::Vertex))
            {
                throw std::runtime_error("Software compiled effect: parsed shader metadata "
                                         "disagrees with its token "
                                         "stream.");
            }

            bool foundEnd = false;
            for (std::size_t offset = 1; offset < result.tokens.size();)
            {
                const std::uint32_t instructionToken = result.tokens[offset];
                const std::uint16_t opcode = static_cast<std::uint16_t>(instructionToken & 0xFFFFu);
                if (opcode == 0xFFFFu)
                {
                    if (instructionToken != 0x0000FFFFu || offset + 1u != result.tokens.size())
                    {
                        throw std::runtime_error("Software compiled effect: shader END token "
                                                 "is malformed or not final.");
                    }
                    foundEnd = true;
                    break;
                }

                std::size_t operandCount = 0;
                if (opcode == 0xFFFEu)
                {
                    operandCount = static_cast<std::size_t>((instructionToken >> 16u) & 0x7FFFu);
                }
                else if (opcode == 0xFFFDu)
                {
                    if (result.majorVersion != 1u ||
                        result.stage != SoftwareShaderStageEXT::Pixel || result.minorVersion != 4u)
                    {
                        throw std::runtime_error(
                            "Software compiled effect: PHASE token appears outside ps_1_4.");
                    }
                }
                else if (result.majorVersion >= 2u)
                {
                    operandCount = static_cast<std::size_t>((instructionToken >> 24u) & 0x0Fu);
                }
                else
                {
                    operandCount =
                        ShaderModelOneOperandCount(opcode, result.stage, result.minorVersion);
                }

                const std::size_t instructionSize = operandCount + 1u;
                if (instructionSize > result.tokens.size() - offset)
                {
                    throw std::runtime_error("Software compiled effect: shader instruction "
                                             "exceeds its token stream.");
                }

                SoftwareShaderInstructionEXT instruction;
                instruction.opcode = opcode;
                instruction.controls = static_cast<std::uint8_t>((instructionToken >> 16u) & 0xFFu);
                instruction.coissue = (instructionToken & 0x40000000u) != 0u;
                instruction.predicated = (instructionToken & 0x10000000u) != 0u;
                instruction.tokenOffset = offset;
                instruction.tokens.assign(
                    result.tokens.begin() + static_cast<std::ptrdiff_t>(offset),
                    result.tokens.begin() + static_cast<std::ptrdiff_t>(offset + instructionSize));
                if (opcode == 31u && instruction.tokens.size() == 3u)
                {
                    const std::uint32_t declarationToken = instruction.tokens[1];
                    const std::uint32_t registerToken = instruction.tokens[2];
                    const std::uint8_t registerType = ShaderRegisterType(registerToken);
                    const std::uint16_t registerNumber =
                        static_cast<std::uint16_t>(registerToken & 0x7FFu);
                    SoftwareShaderSemanticEXT semantic;
                    semantic.usage = static_cast<MOJOSHADER_usage>(declarationToken & 0xFu);
                    semantic.usageIndex =
                        static_cast<std::uint8_t>((declarationToken >> 16u) & 0xFu);
                    semantic.registerNumber = registerNumber;
                    semantic.registerType = registerType;
                    if (registerType == 10u)
                    {
                        const auto samplerType = static_cast<SoftwareShaderSamplerTypeEXT>(
                            (declarationToken >> 27u) & 0xFu);
                        if (samplerType != SoftwareShaderSamplerTypeEXT::Texture2D &&
                            samplerType != SoftwareShaderSamplerTypeEXT::Cube &&
                            samplerType != SoftwareShaderSamplerTypeEXT::Volume)
                        {
                            throw std::runtime_error(
                                "Software compiled effect: shader sampler declaration has an "
                                "unsupported texture dimension.");
                        }
                        result.samplers.push_back(SoftwareShaderSamplerEXT{
                            static_cast<std::uint8_t>(registerNumber), samplerType});
                    }
                    else if (result.stage == SoftwareShaderStageEXT::Vertex)
                    {
                        if (registerType == 1u)
                            result.inputSemantics.push_back(semantic);
                        else if (registerType == 6u && result.majorVersion >= 3u)
                            result.outputSemantics.push_back(semantic);
                    }
                    else if (registerType == 1u)
                    {
                        if (result.majorVersion < 3u)
                        {
                            semantic.usage = MOJOSHADER_USAGE_COLOR;
                            semantic.usageIndex = static_cast<std::uint8_t>(registerNumber);
                        }
                        result.inputSemantics.push_back(semantic);
                    }
                    else if (registerType == 3u)
                    {
                        semantic.usage = MOJOSHADER_USAGE_TEXCOORD;
                        semantic.usageIndex = static_cast<std::uint8_t>(registerNumber);
                        result.inputSemantics.push_back(semantic);
                    }
                    else if (registerType == 17u)
                    {
                        result.inputSemantics.push_back(semantic);
                    }
                }
                result.instructions.push_back(std::move(instruction));
                offset += instructionSize;
            }

            if (!foundEnd)
                throw std::runtime_error("Software compiled effect: shader has no END token.");
            if (result.stage == SoftwareShaderStageEXT::Vertex && result.majorVersion == 1u)
            {
                for (int attribute = 0; attribute < parseData.attribute_count; ++attribute)
                {
                    const MOJOSHADER_attribute& input = parseData.attributes[attribute];
                    const int registerNumber = ShaderModelOneVertexInputRegister(input);
                    const auto [usage, usageIndex] =
                        ShaderModelOneVertexInputSemantic(registerNumber);
                    result.inputSemantics.push_back(
                        {usage, usageIndex,
                         static_cast<std::uint16_t>(registerNumber), 1u});
                }
            }
            if (result.stage == SoftwareShaderStageEXT::Pixel)
            {
                for (unsigned int symbolIndex = 0; symbolIndex < parseData.symbol_count;
                     ++symbolIndex)
                {
                    const MOJOSHADER_symbol& symbol = parseData.symbols[symbolIndex];
                    if (symbol.register_set != MOJOSHADER_SYMREGSET_SAMPLER)
                        continue;
                    SoftwareShaderSamplerTypeEXT type = SoftwareShaderSamplerTypeEXT::Unknown;
                    if (symbol.info.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER2D)
                        type = SoftwareShaderSamplerTypeEXT::Texture2D;
                    else if (symbol.info.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER3D)
                        type = SoftwareShaderSamplerTypeEXT::Volume;
                    else if (symbol.info.parameter_type == MOJOSHADER_SYMTYPE_SAMPLERCUBE)
                        type = SoftwareShaderSamplerTypeEXT::Cube;
                    if (type == SoftwareShaderSamplerTypeEXT::Unknown ||
                        symbol.register_index >= 16u)
                        continue;
                    const auto declaration = std::find_if(
                        result.samplers.begin(), result.samplers.end(),
                        [&symbol](const SoftwareShaderSamplerEXT& candidate)
                        {
                            return candidate.registerNumber == symbol.register_index;
                        });
                    if (declaration != result.samplers.end())
                        declaration->type = type;
                    else
                        result.samplers.push_back(SoftwareShaderSamplerEXT{
                            static_cast<std::uint8_t>(symbol.register_index), type});
                }
            }
            return result;
        }
    } // namespace

    struct SoftwareCompiledEffect::Shader
    {
        const MOJOSHADER_parseData* parseData = nullptr;
        SoftwareShaderProgramEXT program;
        int referenceCount = 1;

        ~Shader()
        {
            MOJOSHADER_freeParseData(parseData);
        }
    };

    struct SoftwareCompiledEffect::ParserContext
    {
        std::array<float, kFloatRegisterCount> vertexFloat{};
        std::array<int, kIntegerRegisterCount> vertexInteger{};
        std::array<unsigned char, kBooleanRegisterCount> vertexBoolean{};
        std::array<float, kFloatRegisterCount> pixelFloat{};
        std::array<int, kIntegerRegisterCount> pixelInteger{};
        std::array<unsigned char, kBooleanRegisterCount> pixelBoolean{};
        Shader* boundVertex = nullptr;
        Shader* boundPixel = nullptr;
        std::string error;

        static void* CompileShader(const void* opaque, const char* mainFunction,
                                   const unsigned char* tokenBuffer, unsigned int bufferSize,
                                   const MOJOSHADER_swizzle* swizzles, unsigned int swizzleCount,
                                   const MOJOSHADER_samplerMap* samplerMap,
                                   unsigned int samplerMapCount)
        {
            auto& context = *const_cast<ParserContext*>(static_cast<const ParserContext*>(opaque));
            try
            {
                // The pinned FNA3D MojoShader build disables its D3D/bytecode emitters.
                // GLSL 1.20 is available on every build used by EasyGL and still performs
                // the complete D3D9 token validation/reflection pass; Software retains
                // the original token buffer and deliberately discards the generated GLSL.
                const MOJOSHADER_parseData* parsed = MOJOSHADER_parse(
                    MOJOSHADER_PROFILE_GLSL120, mainFunction, tokenBuffer, bufferSize, swizzles,
                    swizzleCount, samplerMap, samplerMapCount, nullptr, nullptr, nullptr);
                if (parsed == nullptr)
                    throw std::runtime_error("MojoShader returned no shader parse result.");
                if (parsed->error_count > 0)
                {
                    const std::string message =
                        parsed->errors != nullptr && parsed->errors[0].error != nullptr
                            ? parsed->errors[0].error
                            : "MojoShader rejected the shader token stream.";
                    MOJOSHADER_freeParseData(parsed);
                    throw std::runtime_error(message);
                }

                std::unique_ptr<Shader> shader = std::make_unique<Shader>();
                shader->parseData = parsed;
                try
                {
                    shader->program = DecodeProgram(tokenBuffer, bufferSize, *parsed);
                }
                catch (...)
                {
                    shader->parseData = nullptr;
                    MOJOSHADER_freeParseData(parsed);
                    throw;
                }
                return shader.release();
            }
            catch (const std::exception& exception)
            {
                context.error = exception.what();
                return nullptr;
            }
            catch (...)
            {
                context.error = "Unknown Software shader parser failure.";
                return nullptr;
            }
        }

        static void AddShaderReference(void* shader)
        {
            if (shader != nullptr)
                ++static_cast<Shader*>(shader)->referenceCount;
        }

        static void DeleteShader(const void*, void* shader)
        {
            Shader* typed = static_cast<Shader*>(shader);
            if (typed != nullptr && --typed->referenceCount == 0)
                delete typed;
        }

        static MOJOSHADER_parseData* GetParseData(void* shader)
        {
            Shader* typed = static_cast<Shader*>(shader);
            return typed != nullptr ? const_cast<MOJOSHADER_parseData*>(typed->parseData) : nullptr;
        }

        static void BindShaders(const void* opaque, void* vertex, void* pixel)
        {
            auto& context = *const_cast<ParserContext*>(static_cast<const ParserContext*>(opaque));
            context.boundVertex = static_cast<Shader*>(vertex);
            context.boundPixel = static_cast<Shader*>(pixel);
        }

        static void GetBoundShaders(const void* opaque, void** vertex, void** pixel)
        {
            const auto& context = *static_cast<const ParserContext*>(opaque);
            if (vertex != nullptr)
                *vertex = context.boundVertex;
            if (pixel != nullptr)
                *pixel = context.boundPixel;
        }

        static void MapUniformMemory(const void* opaque, float** vertexFloat, int** vertexInteger,
                                     unsigned char** vertexBoolean, float** pixelFloat,
                                     int** pixelInteger, unsigned char** pixelBoolean)
        {
            auto& context = *const_cast<ParserContext*>(static_cast<const ParserContext*>(opaque));
            if (vertexFloat != nullptr)
                *vertexFloat = context.vertexFloat.data();
            if (vertexInteger != nullptr)
                *vertexInteger = context.vertexInteger.data();
            if (vertexBoolean != nullptr)
                *vertexBoolean = context.vertexBoolean.data();
            if (pixelFloat != nullptr)
                *pixelFloat = context.pixelFloat.data();
            if (pixelInteger != nullptr)
                *pixelInteger = context.pixelInteger.data();
            if (pixelBoolean != nullptr)
                *pixelBoolean = context.pixelBoolean.data();
        }

        static void UnmapUniformMemory(const void*) {}

        static const char* GetError(const void* opaque)
        {
            const auto& context = *static_cast<const ParserContext*>(opaque);
            return context.error.c_str();
        }

        MOJOSHADER_effectShaderContext MakeBackend()
        {
            MOJOSHADER_effectShaderContext backend{};
            backend.compileShader = CompileShader;
            backend.shaderAddRef = AddShaderReference;
            backend.deleteShader = DeleteShader;
            backend.getParseData = GetParseData;
            backend.bindShaders = BindShaders;
            backend.getBoundShaders = GetBoundShaders;
            backend.mapUniformBufferMemory = MapUniformMemory;
            backend.unmapUniformBufferMemory = UnmapUniformMemory;
            backend.getError = GetError;
            backend.shaderContext = this;
            return backend;
        }
    };

    SoftwareCompiledEffect::SoftwareCompiledEffect(
        const std::uint8_t* effectCode, std::size_t effectCodeLength,
        std::shared_ptr<std::array<CompiledEffectLegacyBumpMapEnvState, 16>> legacyBumpMapEnvs)
        : parserContext_(std::make_unique<ParserContext>()),
          legacyBumpMapEnvs_(legacyBumpMapEnvs != nullptr
              ? std::move(legacyBumpMapEnvs)
              : std::make_shared<
                    std::array<CompiledEffectLegacyBumpMapEnvState, 16>>())
    {
        if (effectCode == nullptr || effectCodeLength == 0u ||
            effectCodeLength > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument(
                "Software compiled effect: invalid Effect Framework bytecode buffer.");
        }
        effectCode_ = std::make_shared<const std::vector<std::uint8_t>>(
            effectCode, effectCode + effectCodeLength);
        CreateEffect();
        textures_.resize(static_cast<std::size_t>(effectData_->param_count), nullptr);
        parameterValues_.resize(static_cast<std::size_t>(effectData_->param_count));
    }

    SoftwareCompiledEffect::SoftwareCompiledEffect(const SoftwareCompiledEffect& cloneSource, int)
        : parserContext_(std::make_unique<ParserContext>()), effectCode_(cloneSource.effectCode_),
          parameterValues_(cloneSource.parameterValues_), textures_(cloneSource.textures_),
          legacyBumpMapEnvs_(cloneSource.legacyBumpMapEnvs_),
          techniqueIndex_(cloneSource.techniqueIndex_)
    {
        CreateEffect();
        for (std::size_t index = 0; index < parameterValues_.size(); ++index)
        {
            const auto& value = parameterValues_[index];
            if (!value.empty())
            {
                MOJOSHADER_effectSetRawValueHandle(&effectData_->params[index], value.data(), 0,
                                                   static_cast<unsigned int>(value.size()));
            }
        }
        SetTechnique(techniqueIndex_);
    }

    SoftwareCompiledEffect::~SoftwareCompiledEffect()
    {
        CloseActivePass();
        if (MojoShaderEffect::CanSafelyDeleteNativeEffect(effectData_))
            MOJOSHADER_deleteEffect(effectData_);
        effectData_ = nullptr;
    }

    void SoftwareCompiledEffect::CreateEffect()
    {
        parserContext_->error.clear();
        MOJOSHADER_effectShaderContext backend = parserContext_->MakeBackend();
        effectData_ = MOJOSHADER_compileEffect(effectCode_->data(),
                                               static_cast<unsigned int>(effectCode_->size()),
                                               nullptr, 0, nullptr, 0, &backend);
        try
        {
            MojoShaderEffect::ValidateNativeEffect(effectData_, "Software create");
            description_ = MojoShaderEffect::BuildDescription(effectData_);
            samplerTextureParameters_ =
                MojoShaderEffect::BuildSamplerTextureParameterMap(effectData_);
            MOJOSHADER_effectSetTechnique(effectData_, &effectData_->techniques[0]);
        }
        catch (...)
        {
            if (MojoShaderEffect::CanSafelyDeleteNativeEffect(effectData_))
                MOJOSHADER_deleteEffect(effectData_);
            effectData_ = nullptr;
            throw;
        }
    }

    void SoftwareCompiledEffect::CloseActivePass() noexcept
    {
        if (!passActive_ || effectData_ == nullptr)
            return;
        MOJOSHADER_effectEndPass(effectData_);
        MOJOSHADER_effectEnd(effectData_);
        passActive_ = false;
    }

    std::unique_ptr<ICompiledEffectRuntime> SoftwareCompiledEffect::Clone() const
    {
        return std::unique_ptr<ICompiledEffectRuntime>(new SoftwareCompiledEffect(*this, 0));
    }

    const CompiledEffectDescription& SoftwareCompiledEffect::GetDescription() const
    {
        return description_;
    }

    void SoftwareCompiledEffect::SetTechnique(std::uint32_t techniqueIndex)
    {
        if (techniqueIndex >= static_cast<std::uint32_t>(effectData_->technique_count))
        {
            throw std::out_of_range("Software compiled effect: technique index is out of range.");
        }
        CloseActivePass();
        techniqueIndex_ = techniqueIndex;
        MOJOSHADER_effectSetTechnique(effectData_, &effectData_->techniques[techniqueIndex]);
    }

    void SoftwareCompiledEffect::SetParameterValue(std::uint32_t runtimeIndex, const void* data,
                                                   std::size_t dataBytes)
    {
        if (runtimeIndex >= static_cast<std::uint32_t>(effectData_->param_count))
            throw std::out_of_range("Software compiled effect: parameter index is out of range.");
        const MOJOSHADER_effectParam& parameter = effectData_->params[runtimeIndex];
        const std::size_t capacity = static_cast<std::size_t>(parameter.value.value_count) * 4u;
        if (dataBytes > capacity)
            throw std::invalid_argument("Software compiled effect: parameter value is too large.");
        if (dataBytes > 0u && data == nullptr)
            throw std::invalid_argument("Software compiled effect: parameter data is null.");
        if (dataBytes == 0u)
            return;

        MOJOSHADER_effectSetRawValueHandle(&effectData_->params[runtimeIndex], data, 0,
                                           static_cast<unsigned int>(dataBytes));
        auto& saved = parameterValues_[runtimeIndex];
        saved.assign(static_cast<const std::uint8_t*>(data),
                     static_cast<const std::uint8_t*>(data) + dataBytes);
    }

    void SoftwareCompiledEffect::SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture)
    {
        if (runtimeIndex >= textures_.size())
            throw std::out_of_range("Software compiled effect: texture index is out of range.");
        const auto parameterType = static_cast<std::underlying_type_t<MOJOSHADER_symbolType>>(
            effectData_->params[runtimeIndex].value.type.parameter_type);
        if (parameterType < MOJOSHADER_SYMTYPE_TEXTURE ||
            parameterType > MOJOSHADER_SYMTYPE_TEXTURECUBE)
        {
            throw std::invalid_argument("Software compiled effect: parameter is not a texture.");
        }
        textures_[runtimeIndex] = texture;
    }

    void SoftwareCompiledEffect::ApplyPass(std::uint32_t passIndex,
                                           const CompiledEffectDeviceState& deviceState,
                                           CompiledEffectPassStateChanges& changes)
    {
        const MOJOSHADER_effectTechnique& technique = effectData_->techniques[techniqueIndex_];
        if (passIndex >= technique.pass_count)
            throw std::out_of_range("Software compiled effect: pass index is out of range.");

        CloseActivePass();
        parserContext_->boundVertex = nullptr;
        parserContext_->boundPixel = nullptr;
        std::memset(&stateChanges_, 0, sizeof(stateChanges_));
        changes = {};

        unsigned int passCount = 0;
        MOJOSHADER_effectBegin(effectData_, &passCount, 0, &stateChanges_);
        MOJOSHADER_effectBeginPass(effectData_, passIndex);
        passActive_ = true;

        if (stateChanges_.render_state_change_count > kMaximumReflectedItems ||
            (stateChanges_.render_state_change_count > 0u &&
             stateChanges_.render_state_changes == nullptr) ||
            stateChanges_.sampler_state_change_count > kMaximumReflectedItems ||
            (stateChanges_.sampler_state_change_count > 0u &&
             stateChanges_.sampler_state_changes == nullptr) ||
            stateChanges_.vertex_sampler_state_change_count > kMaximumReflectedItems ||
            (stateChanges_.vertex_sampler_state_change_count > 0u &&
             stateChanges_.vertex_sampler_state_changes == nullptr))
        {
            throw std::runtime_error("Software compiled effect: pass state changes "
                                     "exceed the safety limit.");
        }

        MojoShaderEffect::TranslateRenderStates(stateChanges_, deviceState, changes);
        constexpr std::size_t maxSlots = static_cast<std::size_t>(
            Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers);
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.sampler_state_changes, stateChanges_.sampler_state_change_count, false,
            maxSlots, samplerTextureParameters_, textures_, deviceState, changes);
        MojoShaderEffect::TranslateSamplers(stateChanges_.vertex_sampler_state_changes,
                                            stateChanges_.vertex_sampler_state_change_count, true,
                                            maxSlots, samplerTextureParameters_, textures_,
                                            deviceState, changes);
        MojoShaderEffect::TranslateLegacySamplerAssignments(effectData_, stateChanges_, maxSlots,
                                                            samplerTextureParameters_, textures_,
                                                            deviceState, changes);
        for (const auto& change : changes.legacyBumpMapEnvs)
        {
            auto& state = (*legacyBumpMapEnvs_)[change.slot];
            for (std::size_t component = 0; component < state.matrix.size(); ++component)
                if ((change.assignedMask & (1u << component)) != 0u)
                    state.matrix[component] = change.state.matrix[component];
            if ((change.assignedMask & (1u << 4u)) != 0u)
                state.luminanceScale = change.state.luminanceScale;
            if ((change.assignedMask & (1u << 5u)) != 0u)
                state.luminanceOffset = change.state.luminanceOffset;
        }
    }

    const SoftwareShaderProgramEXT* SoftwareCompiledEffect::GetVertexProgramEXT() const noexcept
    {
        return parserContext_->boundVertex != nullptr ? &parserContext_->boundVertex->program
                                                      : nullptr;
    }

    const SoftwareShaderProgramEXT* SoftwareCompiledEffect::GetPixelProgramEXT() const noexcept
    {
        return parserContext_->boundPixel != nullptr ? &parserContext_->boundPixel->program
                                                     : nullptr;
    }

    std::span<const float>
    SoftwareCompiledEffect::GetFloatRegistersEXT(SoftwareShaderStageEXT stage) const noexcept
    {
        const auto& registers = stage == SoftwareShaderStageEXT::Vertex
                                    ? parserContext_->vertexFloat
                                    : parserContext_->pixelFloat;
        return registers;
    }

    std::span<const int>
    SoftwareCompiledEffect::GetIntegerRegistersEXT(SoftwareShaderStageEXT stage) const noexcept
    {
        const auto& registers = stage == SoftwareShaderStageEXT::Vertex
                                    ? parserContext_->vertexInteger
                                    : parserContext_->pixelInteger;
        return registers;
    }

    std::span<const unsigned char>
    SoftwareCompiledEffect::GetBooleanRegistersEXT(SoftwareShaderStageEXT stage) const noexcept
    {
        const auto& registers = stage == SoftwareShaderStageEXT::Vertex
                                    ? parserContext_->vertexBoolean
                                    : parserContext_->pixelBoolean;
        return registers;
    }

    SoftwareVertexShaderResultEXT SoftwareCompiledEffect::ExecuteVertexEXT(
        std::span<const SoftwareShaderSemanticValueEXT> inputs,
        const ISoftwarePixelSamplerEXT* sampler) const
    {
        const SoftwareShaderProgramEXT* program = GetVertexProgramEXT();
        if (program == nullptr)
            throw std::runtime_error("Software compiled effect: no vertex program is selected.");
        SoftwareVertexShaderResultEXT result = ExecuteSoftwareVertexShaderEXT(
            *program, GetFloatRegistersEXT(SoftwareShaderStageEXT::Vertex),
            GetIntegerRegistersEXT(SoftwareShaderStageEXT::Vertex),
            GetBooleanRegistersEXT(SoftwareShaderStageEXT::Vertex), inputs, sampler);
        lastVertexResult_ = result;
        ++vertexExecutionCount_;
        return result;
    }

    SoftwarePixelShaderResultEXT SoftwareCompiledEffect::ExecutePixelEXT(
        std::span<const SoftwareShaderSemanticValueEXT> inputs,
        const ISoftwarePixelSamplerEXT* sampler,
        const SoftwarePixelShaderBuiltinsEXT* builtins) const
    {
        const SoftwareShaderProgramEXT* program = GetPixelProgramEXT();
        if (program == nullptr)
            throw std::runtime_error("Software compiled effect: no pixel program is selected.");
        return ExecuteSoftwarePixelShaderEXT(
            *program, GetFloatRegistersEXT(SoftwareShaderStageEXT::Pixel),
            GetIntegerRegistersEXT(SoftwareShaderStageEXT::Pixel),
            GetBooleanRegistersEXT(SoftwareShaderStageEXT::Pixel), inputs, sampler, builtins,
            *legacyBumpMapEnvs_);
    }

    std::array<SoftwarePixelShaderResultEXT, 4> SoftwareCompiledEffect::ExecutePixelQuadEXT(
        const std::array<std::span<const SoftwareShaderSemanticValueEXT>, 4>& inputs,
        const ISoftwarePixelSamplerEXT* sampler,
        const std::array<SoftwarePixelShaderBuiltinsEXT, 4>* builtins) const
    {
        const SoftwareShaderProgramEXT* program = GetPixelProgramEXT();
        if (program == nullptr)
            throw std::runtime_error("Software compiled effect: no pixel program is selected.");
        return ExecuteSoftwarePixelShaderQuadEXT(
            *program, GetFloatRegistersEXT(SoftwareShaderStageEXT::Pixel),
            GetIntegerRegistersEXT(SoftwareShaderStageEXT::Pixel),
            GetBooleanRegistersEXT(SoftwareShaderStageEXT::Pixel), inputs, sampler, builtins,
            *legacyBumpMapEnvs_);
    }

    std::size_t SoftwareCompiledEffect::GetVertexExecutionCountEXT() const noexcept
    {
        return vertexExecutionCount_;
    }

    const SoftwareVertexShaderResultEXT&
    SoftwareCompiledEffect::GetLastVertexResultEXT() const noexcept
    {
        return lastVertexResult_;
    }
} // namespace CNA::Internal::Renderers::Software

#endif // CNA_SOFTWARE_COMPILED_EFFECTS
