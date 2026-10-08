// SPDX-License-Identifier: MS-PL
//
// plans/plan_apple_m4.md AM4-144. See MetalCompiledEffect.hpp for the route: MojoShader's portable
// SPIR-V profile, linked per draw against the bound vertex declarations, translated to MSL by
// SPIRV-Cross in process. The effect bookkeeping is the shape the Vulkan and WebGPU backends share.

#if defined(CNA_METAL_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/Metal/MetalCompiledEffect.hpp"

#include "CNA/Internal/Renderers/Metal/MetalRenderer.hpp"
#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/InvalidCastException.hpp"
#include "System/NotSupportedException.hpp"

#include "spirv_msl.hpp"

#include <TargetConditionals.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace CNA::Internal::Renderers::Metal
{
    namespace
    {
        /// Same ceiling the shared translation applies to reflected tables.
        constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;
        /// The largest effect binary CNA will parse, matching every other compiled-effect backend.
        constexpr std::size_t kMaximumCompiledEffectBytes = 64u * 1024u * 1024u;

        // ------------------------------------------------------------------------------------
        // The nine-function MOJOSHADER_effectShaderContext against MOJOSHADER_parse() with the
        // SPIR-V profile -- the Vulkan and WebGPU backends' shape, which follows
        // mojoshader_sdlgpu.c's bookkeeping.
        // ------------------------------------------------------------------------------------

        void* MOJOSHADERCALL BackendCompileShader(
            const void* ctxVoid, const char* mainfn, const unsigned char* tokenbuf,
            const unsigned int bufsize, const MOJOSHADER_swizzle* swiz,
            const unsigned int swizcount, const MOJOSHADER_samplerMap* smap,
            const unsigned int smapcount)
        {
            auto* ctx = static_cast<MetalMojoShaderContextEXT*>(const_cast<void*>(ctxVoid));
            // MOJOSHADER_PROFILE_SPIRV, not GLSPIRV: only the former emits the DescriptorSet and
            // Binding decorations the resource bindings below are keyed on.
            const MOJOSHADER_parseData* parsed = MOJOSHADER_parse(
                MOJOSHADER_PROFILE_SPIRV, mainfn, tokenbuf, bufsize, swiz, swizcount, smap,
                smapcount, nullptr, nullptr, nullptr);
            if (parsed == nullptr)
            {
                ctx->lastError = "MojoShader returned no parse result for a shader object.";
                return nullptr;
            }
            if (parsed->error_count > 0)
            {
                ctx->lastError = (parsed->errors != nullptr && parsed->errors[0].error != nullptr)
                                     ? parsed->errors[0].error
                                     : "<null>";
                MOJOSHADER_freeParseData(parsed);
                return nullptr;
            }
            auto* shader = new MetalCompiledShaderEXT{};
            shader->parseData = parsed;
            return shader;
        }

        void MOJOSHADERCALL BackendShaderAddRef(void* shaderVoid)
        {
            if (shaderVoid != nullptr)
                static_cast<MetalCompiledShaderEXT*>(shaderVoid)->refcount++;
        }

        void MOJOSHADERCALL BackendDeleteShader(const void* ctxVoid, void* shaderVoid)
        {
            if (shaderVoid == nullptr) return;
            auto* shader = static_cast<MetalCompiledShaderEXT*>(shaderVoid);
            if (--shader->refcount > 0) return;
            auto* ctx = static_cast<MetalMojoShaderContextEXT*>(const_cast<void*>(ctxVoid));
            if (ctx != nullptr)
            {
                if (ctx->boundVertex == shader) ctx->boundVertex = nullptr;
                if (ctx->boundPixel == shader) ctx->boundPixel = nullptr;
            }
            if (shader->parseData != nullptr) MOJOSHADER_freeParseData(shader->parseData);
            delete shader;
        }

        MOJOSHADER_parseData* MOJOSHADERCALL BackendGetParseData(void* shaderVoid)
        {
            return shaderVoid != nullptr
                       ? const_cast<MOJOSHADER_parseData*>(
                             static_cast<MetalCompiledShaderEXT*>(shaderVoid)->parseData)
                       : nullptr;
        }

        void MOJOSHADERCALL BackendBindShaders(const void* ctxVoid, void* vshader, void* pshader)
        {
            auto* ctx = static_cast<MetalMojoShaderContextEXT*>(const_cast<void*>(ctxVoid));
            ctx->boundVertex = static_cast<MetalCompiledShaderEXT*>(vshader);
            ctx->boundPixel = static_cast<MetalCompiledShaderEXT*>(pshader);
        }

        void MOJOSHADERCALL BackendGetBoundShaders(const void* ctxVoid, void** vshader,
                                                  void** pshader)
        {
            const auto* ctx = static_cast<const MetalMojoShaderContextEXT*>(ctxVoid);
            if (vshader != nullptr) *vshader = ctx->boundVertex;
            if (pshader != nullptr) *pshader = ctx->boundPixel;
        }

        void MOJOSHADERCALL BackendMapUniformBufferMemory(
            const void* ctxVoid, float** vsf, int** vsi, unsigned char** vsb, float** psf,
            int** psi, unsigned char** psb)
        {
            auto* ctx = static_cast<MetalMojoShaderContextEXT*>(const_cast<void*>(ctxVoid));
            *vsf = ctx->vsRegF.data();
            *vsi = ctx->vsRegI.data();
            *vsb = ctx->vsRegB.data();
            *psf = ctx->psRegF.data();
            *psi = ctx->psRegI.data();
            *psb = ctx->psRegB.data();
        }

        void MOJOSHADERCALL BackendUnmapUniformBufferMemory(const void*) {}

        const char* MOJOSHADERCALL BackendGetError(const void* ctxVoid)
        {
            return static_cast<const MetalMojoShaderContextEXT*>(ctxVoid)->lastError.c_str();
        }

        [[nodiscard]] MOJOSHADER_effectShaderContext MakeBackend(MetalMojoShaderContextEXT* ctx)
        {
            MOJOSHADER_effectShaderContext backend{};
            backend.shaderContext = ctx;
            backend.compileShader = BackendCompileShader;
            backend.shaderAddRef = BackendShaderAddRef;
            backend.deleteShader = BackendDeleteShader;
            backend.getParseData = BackendGetParseData;
            backend.bindShaders = BackendBindShaders;
            backend.getBoundShaders = BackendGetBoundShaders;
            backend.mapUniformBufferMemory = BackendMapUniformBufferMemory;
            backend.unmapUniformBufferMemory = BackendUnmapUniformBufferMemory;
            backend.getError = BackendGetError;
            return backend;
        }

        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        /// XNA vertex semantics to MojoShader's own usage enumeration.
        [[nodiscard]] MOJOSHADER_usage ToMojoShaderUsage(VertexElementUsage usage)
        {
            switch (usage)
            {
                case VertexElementUsage::Position:          return MOJOSHADER_USAGE_POSITION;
                case VertexElementUsage::Color:             return MOJOSHADER_USAGE_COLOR;
                case VertexElementUsage::TextureCoordinate: return MOJOSHADER_USAGE_TEXCOORD;
                case VertexElementUsage::Normal:            return MOJOSHADER_USAGE_NORMAL;
                case VertexElementUsage::Binormal:          return MOJOSHADER_USAGE_BINORMAL;
                case VertexElementUsage::Tangent:           return MOJOSHADER_USAGE_TANGENT;
                case VertexElementUsage::BlendIndices:      return MOJOSHADER_USAGE_BLENDINDICES;
                case VertexElementUsage::BlendWeight:       return MOJOSHADER_USAGE_BLENDWEIGHT;
                case VertexElementUsage::Depth:             return MOJOSHADER_USAGE_DEPTH;
                case VertexElementUsage::Fog:               return MOJOSHADER_USAGE_FOG;
                case VertexElementUsage::PointSize:         return MOJOSHADER_USAGE_POINTSIZE;
                case VertexElementUsage::Sample:            return MOJOSHADER_USAGE_SAMPLE;
                case VertexElementUsage::TessellateFactor:  return MOJOSHADER_USAGE_TESSFACTOR;
            }
            throw std::invalid_argument(
                "CNA Metal: unrecognized VertexElementUsage ordinal " +
                std::to_string(static_cast<int>(usage)));
        }

        /// XNA vertex formats to MojoShader's own, so linking patches the shader's input types.
        [[nodiscard]] MOJOSHADER_vertexElementFormat ToMojoShaderVertexElementFormat(
            VertexElementFormat format)
        {
            switch (format)
            {
                case VertexElementFormat::Single:  return MOJOSHADER_VERTEXELEMENTFORMAT_SINGLE;
                case VertexElementFormat::Vector2: return MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR2;
                case VertexElementFormat::Vector3: return MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR3;
                case VertexElementFormat::Vector4: return MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4;
                case VertexElementFormat::Color:   return MOJOSHADER_VERTEXELEMENTFORMAT_COLOR;
                case VertexElementFormat::Byte4:   return MOJOSHADER_VERTEXELEMENTFORMAT_BYTE4;
                case VertexElementFormat::Short2:  return MOJOSHADER_VERTEXELEMENTFORMAT_SHORT2;
                case VertexElementFormat::Short4:  return MOJOSHADER_VERTEXELEMENTFORMAT_SHORT4;
                case VertexElementFormat::NormalizedShort2:
                    return MOJOSHADER_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2;
                case VertexElementFormat::NormalizedShort4:
                    return MOJOSHADER_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4;
                case VertexElementFormat::HalfVector2:
                    return MOJOSHADER_VERTEXELEMENTFORMAT_HALFVECTOR2;
                case VertexElementFormat::HalfVector4:
                    return MOJOSHADER_VERTEXELEMENTFORMAT_HALFVECTOR4;
            }
            throw std::invalid_argument(
                "CNA Metal: unrecognized VertexElementFormat ordinal " +
                std::to_string(static_cast<int>(format)));
        }

        /// Packs one shader's constant registers into MojoShader's SPIR-V uniform block
        /// (mojoshader_profile_spirv.c): the float4 registers, then two float4s for each sampler a
        /// ps_1_x TEXBEM/TEXBEML reads -- its BUMPENVMAT00/01/10/11 and its luminance scale and
        /// offset -- then the int4 registers, then one 16-byte slot per bool register. MojoShader
        /// declares every uniform before any sampler, so the bump pairs follow the floats in
        /// sampler order. Its own adapters leave those pairs zero, which samples TEXBEM unperturbed.
        void PackUniforms(const MOJOSHADER_parseData* parseData, const float* regF, const int* regI,
                          const unsigned char* regB, std::vector<std::uint8_t>& out,
                          const std::array<CompiledEffectLegacyBumpMapEnvState, 16>* bumpEnvironment = nullptr)
        {
            out.clear();
            if (parseData == nullptr) return;
            std::size_t floats = 0, ints = 0, bools = 0, bumpPairs = 0;
            for (int i = 0; i < parseData->uniform_count; ++i)
            {
                const MOJOSHADER_uniform& uniform = parseData->uniforms[i];
                const std::size_t span = uniform.array_count ? static_cast<std::size_t>(uniform.array_count) : 1u;
                if (uniform.type == MOJOSHADER_UNIFORM_FLOAT) floats += span;
                else if (uniform.type == MOJOSHADER_UNIFORM_INT) ints += span;
                else if (uniform.type == MOJOSHADER_UNIFORM_BOOL) bools += span;
            }
            for (int i = 0; i < parseData->sampler_count; ++i)
                if (parseData->samplers[i].texbem != 0) ++bumpPairs;
            const std::size_t total = (floats + 2u * bumpPairs + ints + bools) * 16u;
            if (total == 0) return;
            out.assign(total, 0u);
            std::size_t floatSlot = 0;
            std::size_t intSlot = floats + 2u * bumpPairs;
            std::size_t boolSlot = intSlot + ints;
            for (int i = 0; i < parseData->uniform_count; ++i)
            {
                const MOJOSHADER_uniform& uniform = parseData->uniforms[i];
                const int span = uniform.array_count ? uniform.array_count : 1;
                const int index = uniform.index;
                if (uniform.type == MOJOSHADER_UNIFORM_FLOAT)
                {
                    if (index >= 0 &&
                        static_cast<std::size_t>(4 * index) + span * 4u <=
                            MetalMojoShaderContextEXT::kMaxFloat4Registers * 4u)
                    {
                        std::memcpy(out.data() + floatSlot * 16u, &regF[4 * index],
                                    static_cast<std::size_t>(span) * 16u);
                    }
                    floatSlot += static_cast<std::size_t>(span);
                }
                else if (uniform.type == MOJOSHADER_UNIFORM_INT)
                {
                    if (index >= 0 &&
                        static_cast<std::size_t>(4 * index) + span * 4u <=
                            MetalMojoShaderContextEXT::kMaxInt4Registers * 4u)
                    {
                        std::memcpy(out.data() + intSlot * 16u, &regI[4 * index],
                                    static_cast<std::size_t>(span) * 16u);
                    }
                    intSlot += static_cast<std::size_t>(span);
                }
                else if (uniform.type == MOJOSHADER_UNIFORM_BOOL)
                {
                    // A bool occupies only the low four bytes of its own 16-byte slot.
                    for (int j = 0; j < span; ++j)
                    {
                        if (index >= 0 &&
                            static_cast<std::size_t>(index + j) < MetalMojoShaderContextEXT::kMaxBoolRegisters)
                        {
                            const std::int32_t value = regB[index + j] != 0 ? 1 : 0;
                            std::memcpy(out.data() + (boolSlot + static_cast<std::size_t>(j)) * 16u,
                                        &value, sizeof(value));
                        }
                    }
                    boolSlot += static_cast<std::size_t>(span);
                }
            }
            std::size_t pair = 0;
            for (int i = 0; i < parseData->sampler_count; ++i)
            {
                const MOJOSHADER_sampler& sampler = parseData->samplers[i];
                if (sampler.texbem == 0) continue;
                float* slot = reinterpret_cast<float*>(out.data() + (floats + 2u * pair) * 16u);
                if (bumpEnvironment != nullptr && sampler.index >= 0 && sampler.index < 16)
                {
                    const auto& state = (*bumpEnvironment)[static_cast<std::size_t>(sampler.index)];
                    slot[0] = state.matrix[0]; slot[1] = state.matrix[1];
                    slot[2] = state.matrix[2]; slot[3] = state.matrix[3];
                    slot[4] = state.luminanceScale; slot[5] = state.luminanceOffset;
                }
                ++pair;
            }
        }

        /// Direct3D 9's vPos is the pixel's integer position; Metal's [[position]] is its centre.
        /// MojoShader's SPIR-V profile copies FragCoord straight into vPos (its GLSL profile has CNA's
        /// raster-input patch), so the entry point's FragCoord is rebased by half a pixel. False
        /// when the translation has a shape this does not recognise.
        [[nodiscard]] bool RebaseFragCoordToPixelCorner(std::string& msl)
        {
            static const std::string kParameter = "float4 gl_FragCoord [[position]]";
            std::size_t positions = 0;
            for (std::size_t at = msl.find("[[position]]"); at != std::string::npos;
                 at = msl.find("[[position]]", at + 1))
                ++positions;
            if (positions == 0) return true;
            const std::size_t at = msl.find(kParameter);
            if (positions != 1 || at == std::string::npos) return false;
            msl.replace(at, kParameter.size(), "float4 cnaFragCoordCentre [[position]]");
            const std::size_t body = msl.find("\n{\n", at);
            if (body == std::string::npos) return false;
            msl.insert(body + 3,
                       "    float4 gl_FragCoord = float4(cnaFragCoordCentre.xy - float2(0.5), "
                       "cnaFragCoordCentre.zw);\n");
            return true;
        }

        /// FNV-1a over the linked SPIR-V, so identical bodies share one translation and library.
        [[nodiscard]] std::uint64_t HashWords(const std::uint32_t* words, std::size_t count,
                                              std::uint64_t seed = 1469598103934665603ull)
        {
            std::uint64_t hash = seed;
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(words);
            for (std::size_t i = 0; i < count * sizeof(std::uint32_t); ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
            return hash;
        }

        /// MojoShader's SPIR-V descriptor sets (mojoshader_profile_spirv.c).
        constexpr std::uint32_t kVertexSamplerSet = 0u;
        constexpr std::uint32_t kVertexUniformSet = 1u;
        constexpr std::uint32_t kPixelSamplerSet = 2u;
        constexpr std::uint32_t kPixelUniformSet = 3u;

        /// The process-wide translation cache: the MSL depends only on the linked words, the
        /// stage and the platform, and SPIRV-Cross is far too slow to run per draw.
        struct TranslationCache
        {
            std::mutex mutex;
            std::unordered_map<std::uint64_t, MetalSpirvToMslResultEXT> entries;
        };

        TranslationCache& Translations()
        {
            static TranslationCache cache;
            return cache;
        }

        [[nodiscard]] bool TargetIsIos()
        {
#if TARGET_OS_IPHONE
            return true;
#else
            return false;
#endif
        }
    }

    MetalSpirvToMslResultEXT TranslateMetalCompiledStageEXT(
        const std::uint32_t* words, std::size_t wordCount, MetalCompiledStageKind stageKind,
        bool iosPlatform)
    {
        MetalSpirvToMslResultEXT result;
        if (words == nullptr || wordCount < 5u || words[0] != 0x07230203u)
        {
            result.error = "the stage is not a SPIR-V module";
            return result;
        }
        const bool vertex = stageKind == MetalCompiledStageKind::Vertex;
        try
        {
            spirv_cross::CompilerMSL compiler(words, wordCount);
            auto options = compiler.get_msl_options();
            options.platform = iosPlatform ? spirv_cross::CompilerMSL::Options::iOS
                                           : spirv_cross::CompilerMSL::Options::macOS;
            options.set_msl_version(2, 1);
            compiler.set_msl_options(options);

            const spv::ExecutionModel model =
                vertex ? spv::ExecutionModelVertex : spv::ExecutionModelFragment;
            const spirv_cross::ShaderResources resources = compiler.get_shader_resources();

            for (const auto& resource : resources.uniform_buffers)
            {
                spirv_cross::MSLResourceBinding binding;
                binding.stage = model;
                binding.desc_set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                binding.msl_buffer = vertex ? kMetalCompiledVertexUniformBuffer
                                            : kMetalCompiledPixelUniformBuffer;
                if (binding.desc_set != (vertex ? kVertexUniformSet : kPixelUniformSet))
                {
                    result.error = "a uniform block lies outside MojoShader's stage set";
                    return result;
                }
                compiler.add_msl_resource_binding(binding);
                result.hasUniforms = true;
            }
            for (const auto& resource : resources.sampled_images)
            {
                const std::uint32_t set =
                    compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                const std::uint32_t slot = compiler.get_decoration(resource.id, spv::DecorationBinding);
                if (vertex || set == kVertexSamplerSet)
                {
                    result.samplesInVertexStage = true;
                    continue;
                }
                if (set != kPixelSamplerSet || slot >= 16u)
                {
                    result.error = "a sampler lies outside MojoShader's pixel sampler set";
                    return result;
                }
                const auto& type = compiler.get_type(resource.type_id);
                MetalCompiledSamplerBindingEXT sampler;
                sampler.slot = slot;
                switch (type.image.dim)
                {
                    case spv::DimCube: sampler.kind = MetalCompiledTextureKind::TextureCube; break;
                    case spv::Dim3D:   sampler.kind = MetalCompiledTextureKind::Texture3D; break;
                    default:           sampler.kind = MetalCompiledTextureKind::Texture2D; break;
                }
                result.samplers.push_back(sampler);
                spirv_cross::MSLResourceBinding binding;
                binding.stage = model;
                binding.desc_set = set;
                binding.binding = slot;
                binding.msl_texture = slot;
                binding.msl_sampler = slot;
                compiler.add_msl_resource_binding(binding);
            }
            if (result.samplesInVertexStage)
                return result;
            if (!vertex)
            {
                for (const auto& output : resources.stage_outputs)
                {
                    const std::uint32_t location = compiler.get_decoration(output.id, spv::DecorationLocation);
                    if (location < 8u) result.colorOutputMask |= 1u << location;
                }
            }

            result.stage.msl = compiler.compile();
            if (!vertex && !RebaseFragCoordToPixelCorner(result.stage.msl))
            {
                result = MetalSpirvToMslResultEXT{};
                result.error = "the pixel shader's [[position]] input has an unexpected MSL shape";
                return result;
            }
            const auto entryPoints = compiler.get_entry_points_and_stages();
            if (entryPoints.empty())
            {
                result.error = "the module declares no entry point";
                return result;
            }
            result.stage.entryPoint =
                compiler.get_cleansed_entry_point_name(entryPoints[0].name, entryPoints[0].execution_model);
            result.stage.hash = HashWords(words, wordCount, vertex ? 0x5643u : 0x5046u);
            std::sort(result.samplers.begin(), result.samplers.end(),
                      [](const MetalCompiledSamplerBindingEXT& a, const MetalCompiledSamplerBindingEXT& b)
                      { return a.slot < b.slot; });
        }
        catch (const std::exception& e)
        {
            result = MetalSpirvToMslResultEXT{};
            result.error = std::string("SPIRV-Cross: ") + e.what();
        }
        return result;
    }

    MetalCompiledEffect::MetalCompiledEffect(MetalRenderer& renderer,
                                             const std::uint8_t* effectCode,
                                             std::size_t effectCodeLength)
        : renderer_(renderer)
    {
        BuildDescriptionAndBackend(effectCode, effectCodeLength);
    }

    MetalCompiledEffect::MetalCompiledEffect(MetalRenderer& renderer,
                                             const MetalCompiledEffect& cloneSource)
        : renderer_(renderer)
        , context_(cloneSource.context_)
        , techniqueIndex_(cloneSource.techniqueIndex_)
    {
        // MOJOSHADER_cloneEffect gives the copy its OWN parameter storage while sharing the
        // immutable compiled shader objects -- which is exactly XNA's Clone() contract.
        effectData_ = MOJOSHADER_cloneEffect(cloneSource.effectData_);
        try
        {
            MojoShaderEffect::ValidateNativeEffect(effectData_, "clone");
            description_ = MojoShaderEffect::BuildDescription(effectData_);
            samplerTextureParameters_ =
                MojoShaderEffect::BuildSamplerTextureParameterMap(effectData_);
            textures_ = cloneSource.textures_;
            boundSamplers_ = cloneSource.boundSamplers_;
            boundVertexSamplers_ = cloneSource.boundVertexSamplers_;
            boundSamplerTextures_ = cloneSource.boundSamplerTextures_;
            boundVertexSamplerTextures_ = cloneSource.boundVertexSamplerTextures_;
            samplerAssigned_ = cloneSource.samplerAssigned_;
            vertexSamplerAssigned_ = cloneSource.vertexSamplerAssigned_;
            SetTechnique(techniqueIndex_);
        }
        catch (...)
        {
            // A rejected parse may be one of MojoShader's static sentinels rather than an
            // allocation, and deleting one of those walks static storage as if it owned a heap.
            if (MojoShaderEffect::CanSafelyDeleteNativeEffect(effectData_))
                MOJOSHADER_deleteEffect(effectData_);
            effectData_ = nullptr;
            throw;
        }
    }

    void MetalCompiledEffect::BuildDescriptionAndBackend(const std::uint8_t* effectCode,
                                                         std::size_t effectCodeLength)
    {
        if (effectCode == nullptr || effectCodeLength == 0)
            throw std::invalid_argument("Metal compiled effect: bytecode must not be empty.");
        if (effectCodeLength > kMaximumCompiledEffectBytes)
            throw std::invalid_argument(
                "Metal compiled effect: bytecode exceeds CNA's 64 MiB safety limit.");

        context_ = renderer_.GetMojoShaderContextEXT();
        if (context_ == nullptr)
            throw std::runtime_error(
                "Metal compiled effect: this renderer has no MojoShader backend context.");

        MOJOSHADER_effectShaderContext backend = MakeBackend(context_);
        effectData_ = MOJOSHADER_compileEffect(effectCode,
                                               static_cast<unsigned int>(effectCodeLength),
                                               nullptr, 0, nullptr, 0, &backend);
        MojoShaderEffect::ValidateNativeEffect(effectData_, "Metal compiled effect");
        description_ = MojoShaderEffect::BuildDescription(effectData_);
        samplerTextureParameters_ = MojoShaderEffect::BuildSamplerTextureParameterMap(effectData_);
        // Runtime indices refer to MojoShader's complete parameter table. Public XNA reflection
        // omits sampler and shader-object parameters, so its compacted count cannot size native
        // indexed storage when one of those private entries precedes a texture.
        textures_.assign(static_cast<std::size_t>(effectData_->param_count), nullptr);
        boundSamplerTextures_.fill(nullptr);
        boundVertexSamplerTextures_.fill(nullptr);
    }

    MetalCompiledEffect::~MetalCompiledEffect()
    {
        if (effectData_ == nullptr) return;
        if (passActive_)
        {
            MOJOSHADER_effectEndPass(effectData_);
            MOJOSHADER_effectEnd(effectData_);
            passActive_ = false;
        }
        if (MojoShaderEffect::CanSafelyDeleteNativeEffect(effectData_))
            MOJOSHADER_deleteEffect(effectData_);
        effectData_ = nullptr;
    }

    std::unique_ptr<ICompiledEffectRuntime> MetalCompiledEffect::Clone() const
    {
        return std::unique_ptr<ICompiledEffectRuntime>(new MetalCompiledEffect(renderer_, *this));
    }

    const CompiledEffectDescription& MetalCompiledEffect::GetDescription() const
    {
        return description_;
    }

    void MetalCompiledEffect::SetTechnique(std::uint32_t techniqueIndex)
    {
        if (effectData_ == nullptr ||
            techniqueIndex >= static_cast<std::uint32_t>(effectData_->technique_count))
        {
            throw std::out_of_range("Metal compiled effect: technique index is out of range.");
        }
        techniqueIndex_ = techniqueIndex;
        MOJOSHADER_effectSetTechnique(effectData_, &effectData_->techniques[techniqueIndex]);
    }

    void MetalCompiledEffect::SetParameterValue(std::uint32_t runtimeIndex, const void* data,
                                                std::size_t dataBytes)
    {
        if (effectData_ == nullptr ||
            runtimeIndex >= static_cast<std::uint32_t>(effectData_->param_count))
        {
            throw std::out_of_range("Metal compiled effect: parameter index is out of range.");
        }
        MOJOSHADER_effectParam& parameter = effectData_->params[runtimeIndex];
        if (data == nullptr)
            throw std::invalid_argument("Metal compiled effect: parameter data is null.");
        if (dataBytes > 0)
        {
            MOJOSHADER_effectSetRawValueHandle(&parameter, data, 0,
                                               static_cast<unsigned int>(dataBytes));
        }
    }

    void MetalCompiledEffect::SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture)
    {
        if (runtimeIndex >= textures_.size())
        {
            throw std::out_of_range(
                "Metal compiled effect: texture parameter index is out of range.");
        }
        const auto parameterType = static_cast<std::underlying_type_t<MOJOSHADER_symbolType>>(
            effectData_->params[runtimeIndex].value.type.parameter_type);
        if (parameterType < MOJOSHADER_SYMTYPE_TEXTURE ||
            parameterType > MOJOSHADER_SYMTYPE_TEXTURECUBE)
        {
            throw std::invalid_argument("Metal compiled effect: parameter is not a texture.");
        }
        if (texture != nullptr && !renderer_.OwnsSampleableTextureEXT(texture))
        {
            throw std::invalid_argument(
                "Metal compiled effect: texture was not created by the active Metal renderer.");
        }
        // plans/plan_fx.md FX-110: the assigned texture's dimension has to match the one the effect
        // declared, as on every other backend -- a mismatched kind binds an unrelated texture
        // type and samples something the game never asked for.
        if (texture != nullptr)
        {
            using namespace Microsoft::Xna::Framework::Graphics;
            const bool isCube = dynamic_cast<TextureCube*>(texture) != nullptr;
            const bool isVolume = !isCube && dynamic_cast<Texture3D*>(texture) != nullptr;
            const auto declared = static_cast<MOJOSHADER_symbolType>(parameterType);
            const bool declaredCube = declared == MOJOSHADER_SYMTYPE_TEXTURECUBE;
            const bool declaredVolume = declared == MOJOSHADER_SYMTYPE_TEXTURE3D;
            const bool declaredAny = declared == MOJOSHADER_SYMTYPE_TEXTURE;
            if (!declaredAny && (isCube != declaredCube || isVolume != declaredVolume))
            {
                const auto* declaredName = declaredCube ? "TextureCube"
                                         : declaredVolume ? "Texture3D" : "Texture2D";
                const auto* assignedName = isCube ? "TextureCube"
                                         : isVolume ? "Texture3D" : "Texture2D";
                throw System::InvalidCastException(
                    std::string("Metal compiled effect: parameter declares ") + declaredName +
                    " but a " + assignedName + " was assigned; the dimensions must match.");
            }
        }
        textures_[runtimeIndex] = texture;
    }

    void MetalCompiledEffect::ApplyPass(std::uint32_t passIndex,
                                        const CompiledEffectDeviceState& deviceState,
                                        CompiledEffectPassStateChanges& changes)
    {
        if (effectData_ == nullptr)
            throw std::runtime_error("Metal compiled effect: the native effect is gone.");
        const MOJOSHADER_effectTechnique& technique = effectData_->techniques[techniqueIndex_];
        if (passIndex >= technique.pass_count)
            throw std::out_of_range("Metal compiled effect: pass index is out of range.");

        // plans/plan_fx.md FX-101: `stateChanges_` is deliberately NOT cleared between applications.
        // MojoShader writes it only in effectBeginPass, so a repeated application of the same pass
        // must keep the pointers the previous one left.
        unsigned int passCount = 0;
        if (passActive_)
        {
            MOJOSHADER_effectEndPass(effectData_);
            MOJOSHADER_effectEnd(effectData_);
            passActive_ = false;
        }
        MOJOSHADER_effectBegin(effectData_, &passCount, 0, &stateChanges_);
        MOJOSHADER_effectBeginPass(effectData_, passIndex);
        passActive_ = true;

        if (stateChanges_.render_state_change_count > kMaximumReflectedItems ||
            (stateChanges_.render_state_change_count > 0 &&
             stateChanges_.render_state_changes == nullptr) ||
            stateChanges_.sampler_state_change_count > kMaximumReflectedItems ||
            (stateChanges_.sampler_state_change_count > 0 &&
             stateChanges_.sampler_state_changes == nullptr) ||
            stateChanges_.vertex_sampler_state_change_count > kMaximumReflectedItems ||
            (stateChanges_.vertex_sampler_state_change_count > 0 &&
             stateChanges_.vertex_sampler_state_changes == nullptr))
        {
            throw std::runtime_error(
                "Metal compiled effect: native pass state changes exceed the safety limit.");
        }

        MojoShaderEffect::TranslateRenderStates(stateChanges_, deviceState, changes);
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.sampler_state_changes, stateChanges_.sampler_state_change_count,
            /*vertexStage=*/false, boundSamplers_.size(), samplerTextureParameters_, textures_,
            deviceState, changes);
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.vertex_sampler_state_changes,
            stateChanges_.vertex_sampler_state_change_count,
            /*vertexStage=*/true, boundVertexSamplers_.size(), samplerTextureParameters_, textures_,
            deviceState, changes);
        MojoShaderEffect::TranslateLegacySamplerAssignments(
            effectData_, stateChanges_, boundSamplers_.size(), samplerTextureParameters_, textures_,
            deviceState, changes);

        // The texture stages' bump-environment state is device state: merge what this pass
        // assigned into the renderer-wide context, as EasyGL keeps it on its renderer.
        for (const auto& change : changes.legacyBumpMapEnvs)
        {
            if (change.slot >= context_->bumpEnvironment.size()) continue;
            auto& state = context_->bumpEnvironment[change.slot];
            for (std::size_t component = 0; component < state.matrix.size(); ++component)
                if ((change.assignedMask & (1u << component)) != 0u)
                    state.matrix[component] = change.state.matrix[component];
            if ((change.assignedMask & (1u << 4u)) != 0u)
                state.luminanceScale = change.state.luminanceScale;
            if ((change.assignedMask & (1u << 5u)) != 0u)
                state.luminanceOffset = change.state.luminanceOffset;
        }

        // Persist the per-slot bindings, so a later pass that reassigns nothing keeps them --
        // real XNA behaviour.
        for (const CompiledEffectSamplerChange& change : changes.samplers)
        {
            if (change.slot >= boundSamplers_.size()) continue;
            if (change.vertexStage)
            {
                boundVertexSamplers_[change.slot] = change.sampler;
                boundVertexSamplerTextures_[change.slot] = change.texture;
                vertexSamplerAssigned_[change.slot] = true;
            }
            else
            {
                boundSamplers_[change.slot] = change.sampler;
                boundSamplerTextures_[change.slot] = change.texture;
                samplerAssigned_[change.slot] = true;
            }
        }
    }

    void MetalCompiledEffect::GetBoundShadersEXT(MetalCompiledShaderEXT*& vertex,
                                                 MetalCompiledShaderEXT*& pixel) const
    {
        vertex = context_ != nullptr ? context_->boundVertex : nullptr;
        pixel = context_ != nullptr ? context_->boundPixel : nullptr;
    }

    void MetalCompiledEffect::CaptureUniformSnapshotEXT(std::vector<std::uint8_t>& vertexBytes,
                                                        std::vector<std::uint8_t>& pixelBytes) const
    {
        vertexBytes.clear();
        pixelBytes.clear();
        if (context_ == nullptr) return;
        if (context_->boundVertex != nullptr)
        {
            PackUniforms(context_->boundVertex->parseData, context_->vsRegF.data(),
                         context_->vsRegI.data(), context_->vsRegB.data(), vertexBytes);
        }
        if (context_->boundPixel != nullptr)
        {
            PackUniforms(context_->boundPixel->parseData, context_->psRegF.data(),
                         context_->psRegI.data(), context_->psRegB.data(), pixelBytes,
                         &context_->bumpEnvironment);
        }
    }

    void MetalCompiledEffect::GetBoundSamplerEXT(
        std::uint32_t slot, bool vertexStage, Texture*& texture,
        Microsoft::Xna::Framework::Graphics::SamplerState& sampler, bool* samplerAssigned) const
    {
        texture = nullptr;
        if (slot >= boundSamplers_.size())
        {
            if (samplerAssigned != nullptr) *samplerAssigned = false;
            return;
        }
        if (vertexStage)
        {
            texture = boundVertexSamplerTextures_[slot];
            sampler = boundVertexSamplers_[slot];
            if (samplerAssigned != nullptr) *samplerAssigned = vertexSamplerAssigned_[slot];
        }
        else
        {
            texture = boundSamplerTextures_[slot];
            sampler = boundSamplers_[slot];
            if (samplerAssigned != nullptr) *samplerAssigned = samplerAssigned_[slot];
        }
    }

    MetalCompiledEffect::LinkedPassEXT MetalCompiledEffect::LinkAndGetShadersEXT(
        const std::vector<CompiledVertexStreamEXT>& streams) const
    {
        MetalCompiledShaderEXT* vertex = nullptr;
        MetalCompiledShaderEXT* pixel = nullptr;
        GetBoundShadersEXT(vertex, pixel);
        if (vertex == nullptr || pixel == nullptr || vertex->parseData == nullptr ||
            pixel->parseData == nullptr)
        {
            throw std::runtime_error(
                "CNA Metal: the applied compiled-effect pass bound no shader pair.");
        }

        const MOJOSHADER_parseData* vertexData = vertex->parseData;
        const MOJOSHADER_parseData* pixelData = pixel->parseData;

        LinkedPassEXT linked;
        // One MOJOSHADER_vertexAttribute per shader input, resolved from the caller's own
        // declarations. A shader input no declaration supplies fails loudly: binding nothing there
        // would read undefined vertex data.
        std::vector<MOJOSHADER_vertexAttribute> mojoAttributes;
        mojoAttributes.reserve(static_cast<std::size_t>(std::max(vertexData->attribute_count, 0)));
        for (int i = 0; i < vertexData->attribute_count; ++i)
        {
            const MOJOSHADER_attribute& shaderInput = vertexData->attributes[i];
            const Microsoft::Xna::Framework::Graphics::VertexElement* match = nullptr;
            std::size_t matchStream = 0;
            for (std::size_t streamIndex = 0;
                 streamIndex < streams.size() && match == nullptr; ++streamIndex)
            {
                if (streams[streamIndex].elements == nullptr) continue;
                for (const auto& element : *streams[streamIndex].elements)
                {
                    if (ToMojoShaderUsage(element.getVertexElementUsageProperty()) ==
                            shaderInput.usage &&
                        element.getUsageIndexProperty() == shaderInput.index)
                    {
                        match = &element;
                        matchStream = streamIndex;
                        break;
                    }
                }
            }
            if (match == nullptr)
            {
                const char* name = shaderInput.name != nullptr ? shaderInput.name : "<unnamed>";
                throw System::NotSupportedException(
                    "CNA Metal: this compiled effect's vertex shader requires attribute '" +
                    std::string(name) + "' (usage " +
                    std::to_string(static_cast<int>(shaderInput.usage)) + ", index " +
                    std::to_string(shaderInput.index) +
                    "), but no vertex declaration supplied to this draw has an element with that "
                    "usage and usage index.");
            }
            MOJOSHADER_vertexAttribute attribute{};
            attribute.usage = shaderInput.usage;
            attribute.usageIndex = shaderInput.index;
            attribute.vertexElementFormat =
                ToMojoShaderVertexElementFormat(match->getVertexElementFormatProperty());
            mojoAttributes.push_back(attribute);

            // MojoShader assigns SPIR-V input locations in the vertex shader's own declaration
            // order, which is the order this loop walks (spikes/webgpu-spirv-spike).
            MetalCompiledAttributeEXT out;
            out.location = static_cast<std::uint32_t>(i);
            out.streamIndex = static_cast<std::uint32_t>(matchStream);
            out.offset = static_cast<std::uint32_t>(match->getOffsetProperty());
            out.format = match->getVertexElementFormatProperty();
            linked.attributes.push_back(out);
        }

        // The explicit link step: patches the vertex shader's input types to the real vertex
        // format, links vertex outputs to pixel inputs, and returns the internal patch table's
        // size, which must be subtracted from output_len before the SPIR-V is used.
        const int patchTableSize = MOJOSHADER_linkSPIRVShaders(
            vertexData, pixelData, mojoAttributes.data(),
            static_cast<int>(mojoAttributes.size()));
        if (patchTableSize <= 0)
            throw std::runtime_error("CNA Metal: MOJOSHADER_linkSPIRVShaders failed.");

        const bool ios = TargetIsIos();
        const auto finishStage = [&](const MOJOSHADER_parseData* data, MetalCompiledStageKind kind)
            -> const MetalSpirvToMslResultEXT& {
            if (data->output == nullptr ||
                static_cast<std::size_t>(data->output_len) <=
                    static_cast<std::size_t>(patchTableSize))
            {
                throw std::runtime_error("CNA Metal: a shader produced no usable SPIR-V.");
            }
            const std::size_t wordCount =
                (static_cast<std::size_t>(data->output_len) -
                 static_cast<std::size_t>(patchTableSize)) / sizeof(std::uint32_t);
            const auto* words = reinterpret_cast<const std::uint32_t*>(data->output);
            // Linking PATCHES the SPIR-V in place, so the words mean this draw's vertex format
            // right now; the translation is therefore asked for by content.
            const std::uint64_t key =
                HashWords(words, wordCount, kind == MetalCompiledStageKind::Vertex ? 0x5643u : 0x5046u) ^
                (ios ? 0x105ull : 0ull);
            TranslationCache& cache = Translations();
            std::lock_guard<std::mutex> lock(cache.mutex);
            if (const auto it = cache.entries.find(key); it != cache.entries.end())
                return it->second;
            MetalSpirvToMslResultEXT translated =
                TranslateMetalCompiledStageEXT(words, wordCount, kind, ios);
            if (!translated.error.empty())
            {
                throw std::runtime_error(
                    "CNA Metal: a compiled effect's SPIR-V has no MSL translation: " +
                    translated.error);
            }
            return cache.entries.emplace(key, std::move(translated)).first->second;
        };

        const MetalSpirvToMslResultEXT& vertexStage =
            finishStage(vertexData, MetalCompiledStageKind::Vertex);
        // plans/plan_fx.md FX-109: no CNA renderer routes GraphicsDevice.VertexTextures to a
        // backend. Refuse by name rather than draw with an unbound texture.
        if (vertexStage.samplesInVertexStage)
        {
            throw System::NotSupportedException(
                "CNA Metal: this compiled effect's VERTEX shader samples a texture, which no CNA "
                "renderer routes today (plans/plan_fx.md FX-109).");
        }
        const MetalSpirvToMslResultEXT& pixelStage =
            finishStage(pixelData, MetalCompiledStageKind::Pixel);
        linked.vertex = &vertexStage.stage;
        linked.pixel = &pixelStage.stage;
        linked.pixelSamplers = pixelStage.samplers;
        linked.vertexHasUniforms = vertexStage.hasUniforms;
        linked.pixelHasUniforms = pixelStage.hasUniforms;
        linked.pixelColorOutputs = pixelStage.colorOutputMask != 0 ? pixelStage.colorOutputMask : 1u;

        std::uint64_t key = linked.vertex->hash * 31ull ^ linked.pixel->hash;
        for (const auto& stream : streams)
        {
            key = key * 1099511628211ull ^ stream.stride;
            key = key * 1099511628211ull ^ (stream.perInstance ? 0x9E37u : 0x1234u);
        }
        for (const auto& attribute : linked.attributes)
        {
            key = key * 1099511628211ull ^ attribute.location;
            key = key * 1099511628211ull ^ attribute.streamIndex;
            key = key * 1099511628211ull ^ attribute.offset;
            key = key * 1099511628211ull ^ static_cast<std::uint64_t>(attribute.format);
        }
        linked.pipelineKey = key == 0 ? 1 : key;
        return linked;
    }
}

#endif  // CNA_METAL_COMPILED_EFFECTS
