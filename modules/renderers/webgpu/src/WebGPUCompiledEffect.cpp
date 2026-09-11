// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-167..171. See WebGPUCompiledEffect.hpp for why this backend is CNA's
// own rather than a MojoShader-provided adapter, and for the combined-image-sampler split that is
// the one translation step between MojoShader's SPIR-V and a WebGPU shader module.

#if defined(CNA_WEBGPU_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/WebGPU/WebGPUCompiledEffect.hpp"

#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
#include "CNA/Internal/Renderers/MojoShader/SpirvCombinedSamplerSplit.hpp"
#include "CNA/Internal/Renderers/MojoShader/SpirvSamplerLodBias.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/InvalidCastException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace CNA::Internal::Renderers::WebGPU
{
    namespace
    {
        /// Same ceiling the shared translation applies to reflected tables.
        constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;
        /// The largest effect binary CNA will parse, matching every other compiled-effect backend.
        constexpr std::size_t kMaximumCompiledEffectBytes = 64u * 1024u * 1024u;

        // ------------------------------------------------------------------------------------
        // The nine-function MOJOSHADER_effectShaderContext, implemented directly against
        // MOJOSHADER_parse() with the portable SPIR-V profile. MojoShader has no WebGPU adapter,
        // so every one of these is CNA's own; the shape follows the Vulkan backend's, which in
        // turn follows mojoshader_sdlgpu.c's bookkeeping.
        // ------------------------------------------------------------------------------------

        void* MOJOSHADERCALL BackendCompileShader(
            const void* ctxVoid, const char* mainfn, const unsigned char* tokenbuf,
            const unsigned int bufsize, const MOJOSHADER_swizzle* swiz,
            const unsigned int swizcount, const MOJOSHADER_samplerMap* smap,
            const unsigned int smapcount)
        {
            auto* ctx = static_cast<WebGPUMojoShaderContextEXT*>(const_cast<void*>(ctxVoid));
            // MOJOSHADER_PROFILE_SPIRV, not GLSPIRV: only the former emits real DescriptorSet and
            // Binding decorations. See spikes/webgpu-spirv-spike/README.md.
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
            auto* shader = new WebGPUCompiledShaderEXT{};
            shader->parseData = parsed;
            return shader;
        }

        void MOJOSHADERCALL BackendShaderAddRef(void* shaderVoid)
        {
            if (shaderVoid != nullptr)
                static_cast<WebGPUCompiledShaderEXT*>(shaderVoid)->refcount++;
        }

        void MOJOSHADERCALL BackendDeleteShader(const void* ctxVoid, void* shaderVoid)
        {
            if (shaderVoid == nullptr) return;
            auto* shader = static_cast<WebGPUCompiledShaderEXT*>(shaderVoid);
            if (--shader->refcount > 0) return;
            auto* ctx = static_cast<WebGPUMojoShaderContextEXT*>(const_cast<void*>(ctxVoid));
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
                             static_cast<WebGPUCompiledShaderEXT*>(shaderVoid)->parseData)
                       : nullptr;
        }

        void MOJOSHADERCALL BackendBindShaders(const void* ctxVoid, void* vshader, void* pshader)
        {
            auto* ctx = static_cast<WebGPUMojoShaderContextEXT*>(const_cast<void*>(ctxVoid));
            ctx->boundVertex = static_cast<WebGPUCompiledShaderEXT*>(vshader);
            ctx->boundPixel = static_cast<WebGPUCompiledShaderEXT*>(pshader);
        }

        void MOJOSHADERCALL BackendGetBoundShaders(const void* ctxVoid, void** vshader,
                                                  void** pshader)
        {
            const auto* ctx = static_cast<const WebGPUMojoShaderContextEXT*>(ctxVoid);
            if (vshader != nullptr) *vshader = ctx->boundVertex;
            if (pshader != nullptr) *pshader = ctx->boundPixel;
        }

        void MOJOSHADERCALL BackendMapUniformBufferMemory(
            const void* ctxVoid, float** vsf, int** vsi, unsigned char** vsb, float** psf,
            int** psi, unsigned char** psb)
        {
            auto* ctx = static_cast<WebGPUMojoShaderContextEXT*>(const_cast<void*>(ctxVoid));
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
            return static_cast<const WebGPUMojoShaderContextEXT*>(ctxVoid)->lastError.c_str();
        }

        [[nodiscard]] MOJOSHADER_effectShaderContext MakeBackend(WebGPUMojoShaderContextEXT* ctx)
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
                "CNA WebGPU: unrecognized VertexElementUsage ordinal " +
                std::to_string(static_cast<int>(usage)));
        }

        /// XNA vertex formats to MojoShader's own, so linking can patch the shader's input types.
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
                "CNA WebGPU: unrecognized VertexElementFormat ordinal " +
                std::to_string(static_cast<int>(format)));
        }

        /// XNA vertex formats to the WebGPU attribute formats the pipeline declares.
        [[nodiscard]] WGPUVertexFormat ToWGPUVertexFormat(VertexElementFormat format)
        {
            switch (format)
            {
                case VertexElementFormat::Single:  return WGPUVertexFormat_Float32;
                case VertexElementFormat::Vector2: return WGPUVertexFormat_Float32x2;
                case VertexElementFormat::Vector3: return WGPUVertexFormat_Float32x3;
                case VertexElementFormat::Vector4: return WGPUVertexFormat_Float32x4;
                // XNA's Color is BGRA in memory and the shader wants RGBA; WebGPU has no
                // BGRA8 vertex format, so the swizzle is applied by the shader's own input
                // patching (MOJOSHADER_VERTEXELEMENTFORMAT_COLOR tells the linker to do it).
                case VertexElementFormat::Color:   return WGPUVertexFormat_Unorm8x4;
                case VertexElementFormat::Byte4:   return WGPUVertexFormat_Uint8x4;
                case VertexElementFormat::Short2:  return WGPUVertexFormat_Sint16x2;
                case VertexElementFormat::Short4:  return WGPUVertexFormat_Sint16x4;
                case VertexElementFormat::NormalizedShort2: return WGPUVertexFormat_Snorm16x2;
                case VertexElementFormat::NormalizedShort4: return WGPUVertexFormat_Snorm16x4;
                case VertexElementFormat::HalfVector2: return WGPUVertexFormat_Float16x2;
                case VertexElementFormat::HalfVector4: return WGPUVertexFormat_Float16x4;
            }
            throw std::invalid_argument(
                "CNA WebGPU: unrecognized VertexElementFormat ordinal " +
                std::to_string(static_cast<int>(format)));
        }

        /// The SPIR-V `Dim` the split reported, as the view dimension a bind group must supply.
        [[nodiscard]] WGPUTextureViewDimension ToViewDimension(std::uint32_t spirvDim, bool arrayed)
        {
            switch (spirvDim)
            {
                case 0: return WGPUTextureViewDimension_1D;
                case 1: return arrayed ? WGPUTextureViewDimension_2DArray
                                       : WGPUTextureViewDimension_2D;
                case 2: return WGPUTextureViewDimension_3D;
                case 3: return arrayed ? WGPUTextureViewDimension_CubeArray
                                       : WGPUTextureViewDimension_Cube;
                default: return WGPUTextureViewDimension_2D;
            }
        }

        /// Packs one shader's declared uniforms into MojoShader's own SPIR-V uniform-block layout.
        void PackUniforms(const MOJOSHADER_parseData* parseData, const float* regF, const int* regI,
                          const unsigned char* regB, std::vector<std::uint8_t>& out)
        {
            out.clear();
            if (parseData == nullptr || parseData->uniform_count <= 0) return;
            std::size_t total = 0;
            for (int i = 0; i < parseData->uniform_count; ++i)
            {
                const int span = parseData->uniforms[i].array_count
                                     ? parseData->uniforms[i].array_count : 1;
                total += static_cast<std::size_t>(span) * 16u;
            }
            if (total == 0) return;
            out.assign(total, 0u);
            std::size_t offset = 0;
            for (int i = 0; i < parseData->uniform_count; ++i)
            {
                const MOJOSHADER_uniform& uniform = parseData->uniforms[i];
                const int span = uniform.array_count ? uniform.array_count : 1;
                const int index = uniform.index;
                std::uint8_t* dst = out.data() + offset;
                const std::size_t bytes = static_cast<std::size_t>(span) * 16u;
                if (uniform.type == MOJOSHADER_UNIFORM_FLOAT)
                {
                    if (index >= 0 &&
                        static_cast<std::size_t>(4 * index) + span * 4u <=
                            WebGPUMojoShaderContextEXT::kMaxFloat4Registers * 4u)
                    {
                        std::memcpy(dst, &regF[4 * index], bytes);
                    }
                }
                else if (uniform.type == MOJOSHADER_UNIFORM_INT)
                {
                    if (index >= 0 &&
                        static_cast<std::size_t>(4 * index) + span * 4u <=
                            WebGPUMojoShaderContextEXT::kMaxInt4Registers * 4u)
                    {
                        std::memcpy(dst, &regI[4 * index], bytes);
                    }
                }
                else if (uniform.type == MOJOSHADER_UNIFORM_BOOL)
                {
                    // A bool occupies only the low four bytes of its own 16-byte slot, exactly as
                    // MojoShader's other adapters pack it.
                    for (int j = 0; j < span; ++j)
                    {
                        if (index < 0 ||
                            static_cast<std::size_t>(index + j) >=
                                WebGPUMojoShaderContextEXT::kMaxBoolRegisters)
                        {
                            continue;
                        }
                        const std::int32_t value = regB[index + j] != 0 ? 1 : 0;
                        std::memcpy(dst + static_cast<std::size_t>(j) * 16u, &value, sizeof(value));
                    }
                }
                offset += bytes;
            }
        }

        /// FNV-1a over the finished SPIR-V, so identical bodies share one shader module.
        [[nodiscard]] std::uint64_t HashWords(const std::uint32_t* words, std::size_t count)
        {
            std::uint64_t hash = 1469598103934665603ull;
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(words);
            for (std::size_t i = 0; i < count * sizeof(std::uint32_t); ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
            return hash;
        }
    }

    WebGPUCompiledEffect::WebGPUCompiledEffect(WebGPURenderer& renderer,
                                               const std::uint8_t* effectCode,
                                               std::size_t effectCodeLength)
        : renderer_(renderer)
    {
        BuildDescriptionAndBackend(effectCode, effectCodeLength);
    }

    WebGPUCompiledEffect::WebGPUCompiledEffect(WebGPURenderer& renderer,
                                               const WebGPUCompiledEffect& cloneSource)
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

    void WebGPUCompiledEffect::BuildDescriptionAndBackend(const std::uint8_t* effectCode,
                                                          std::size_t effectCodeLength)
    {
        if (effectCode == nullptr || effectCodeLength == 0)
            throw std::invalid_argument("WebGPU compiled effect: bytecode must not be empty.");
        if (effectCodeLength > kMaximumCompiledEffectBytes)
            throw std::invalid_argument(
                "WebGPU compiled effect: bytecode exceeds CNA's 64 MiB safety limit.");

        context_ = renderer_.GetMojoShaderContextEXT();
        if (context_ == nullptr)
            throw std::runtime_error(
                "WebGPU compiled effect: this renderer has no MojoShader backend context.");

        MOJOSHADER_effectShaderContext backend = MakeBackend(context_);
        effectData_ = MOJOSHADER_compileEffect(effectCode,
                                               static_cast<unsigned int>(effectCodeLength),
                                               nullptr, 0, nullptr, 0, &backend);
        MojoShaderEffect::ValidateNativeEffect(effectData_, "WebGPU compiled effect");
        description_ = MojoShaderEffect::BuildDescription(effectData_);
        samplerTextureParameters_ = MojoShaderEffect::BuildSamplerTextureParameterMap(effectData_);
        textures_.assign(description_.parameters.size(), nullptr);
        boundSamplerTextures_.fill(nullptr);
        boundVertexSamplerTextures_.fill(nullptr);
    }

    WebGPUCompiledEffect::~WebGPUCompiledEffect()
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

    std::unique_ptr<ICompiledEffectRuntime> WebGPUCompiledEffect::Clone() const
    {
        return std::unique_ptr<ICompiledEffectRuntime>(
            new WebGPUCompiledEffect(renderer_, *this));
    }

    const CompiledEffectDescription& WebGPUCompiledEffect::GetDescription() const
    {
        return description_;
    }

    void WebGPUCompiledEffect::SetTechnique(std::uint32_t techniqueIndex)
    {
        if (effectData_ == nullptr ||
            techniqueIndex >= static_cast<std::uint32_t>(effectData_->technique_count))
        {
            throw std::out_of_range("WebGPU compiled effect: technique index is out of range.");
        }
        techniqueIndex_ = techniqueIndex;
        MOJOSHADER_effectSetTechnique(effectData_, &effectData_->techniques[techniqueIndex]);
    }

    void WebGPUCompiledEffect::SetParameterValue(std::uint32_t runtimeIndex, const void* data,
                                                 std::size_t dataBytes)
    {
        if (effectData_ == nullptr ||
            runtimeIndex >= static_cast<std::uint32_t>(effectData_->param_count))
        {
            throw std::out_of_range("WebGPU compiled effect: parameter index is out of range.");
        }
        MOJOSHADER_effectParam& parameter = effectData_->params[runtimeIndex];
        if (data == nullptr)
            throw std::invalid_argument("WebGPU compiled effect: parameter data is null.");
        if (dataBytes > 0)
        {
            MOJOSHADER_effectSetRawValueHandle(&parameter, data, 0,
                                               static_cast<unsigned int>(dataBytes));
        }
    }

    void WebGPUCompiledEffect::SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture)
    {
        if (runtimeIndex >= textures_.size())
        {
            throw std::out_of_range(
                "WebGPU compiled effect: texture parameter index is out of range.");
        }
        const auto parameterType = static_cast<std::underlying_type_t<MOJOSHADER_symbolType>>(
            effectData_->params[runtimeIndex].value.type.parameter_type);
        if (parameterType < MOJOSHADER_SYMTYPE_TEXTURE ||
            parameterType > MOJOSHADER_SYMTYPE_TEXTURECUBE)
        {
            throw std::invalid_argument("WebGPU compiled effect: parameter is not a texture.");
        }
        if (texture != nullptr && !renderer_.OwnsSampleableTextureEXT(texture))
        {
            throw std::invalid_argument(
                "WebGPU compiled effect: texture was not created by the active WebGPU renderer.");
        }
        // plans/plan_fx.md FX-110: the assigned texture's dimension has to match the one the effect
        // declared, for the same reason it does on every other backend -- a mismatched kind binds
        // an unrelated view and samples something the game never asked for.
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
                    std::string("WebGPU compiled effect: parameter declares ") + declaredName +
                    " but a " + assignedName + " was assigned; the dimensions must match.");
            }
        }
        textures_[runtimeIndex] = texture;
    }

    void WebGPUCompiledEffect::ApplyPass(std::uint32_t passIndex,
                                         const CompiledEffectDeviceState& deviceState,
                                         CompiledEffectPassStateChanges& changes)
    {
        if (effectData_ == nullptr)
            throw std::runtime_error("WebGPU compiled effect: the native effect is gone.");
        const MOJOSHADER_effectTechnique& technique = effectData_->techniques[techniqueIndex_];
        if (passIndex >= technique.pass_count)
            throw std::out_of_range("WebGPU compiled effect: pass index is out of range.");

        // plans/plan_fx.md FX-101: `stateChanges_` is deliberately NOT cleared between applications.
        // MojoShader writes it only in effectBeginPass, so a repeated application of the same pass
        // must keep the pointers the previous one left -- clearing them makes a repeat apply
        // publish no render state, no sampler state and no texture binding at all.
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
                "WebGPU compiled effect: native pass state changes exceed the safety limit.");
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

        // Persist the per-slot bindings, so a later pass that reassigns nothing keeps them --
        // real XNA behaviour, and what a deferred draw reads back at record time.
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

    void WebGPUCompiledEffect::GetBoundShadersEXT(WebGPUCompiledShaderEXT*& vertex,
                                                  WebGPUCompiledShaderEXT*& pixel) const
    {
        vertex = context_ != nullptr ? context_->boundVertex : nullptr;
        pixel = context_ != nullptr ? context_->boundPixel : nullptr;
    }

    void WebGPUCompiledEffect::CaptureUniformSnapshotEXT(std::vector<std::uint8_t>& vertexBytes,
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
                         context_->psRegI.data(), context_->psRegB.data(), pixelBytes);
        }
    }

    void WebGPUCompiledEffect::GetBoundSamplerEXT(
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

    WebGPUCompiledEffect::LinkedPassEXT WebGPUCompiledEffect::LinkAndGetShadersEXT(
        const std::vector<CompiledVertexStreamEXT>& streams) const
    {
        WebGPUCompiledShaderEXT* vertex = nullptr;
        WebGPUCompiledShaderEXT* pixel = nullptr;
        GetBoundShadersEXT(vertex, pixel);
        if (vertex == nullptr || pixel == nullptr || vertex->parseData == nullptr ||
            pixel->parseData == nullptr)
        {
            throw std::runtime_error(
                "CNA WebGPU: the applied compiled-effect pass bound no shader pair.");
        }

        const MOJOSHADER_parseData* vertexData = vertex->parseData;
        const MOJOSHADER_parseData* pixelData = pixel->parseData;

        LinkedPassEXT linked;
        linked.streams.resize(streams.size());
        for (std::size_t streamIndex = 0; streamIndex < streams.size(); ++streamIndex)
        {
            linked.streams[streamIndex].arrayStride = streams[streamIndex].stride;
            linked.streams[streamIndex].perInstance = streams[streamIndex].perInstance;
        }

        // One MOJOSHADER_vertexAttribute per shader input, resolved from the caller's own
        // declaration. A shader input the declaration does not supply must fail loudly: binding
        // nothing there would sample undefined vertex data.
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
                    "CNA WebGPU: this compiled effect's vertex shader requires attribute '" +
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

            WGPUVertexAttribute description{};
            // MojoShader assigns SPIR-V input locations in the vertex shader's own declaration
            // order, which is the order this loop walks -- confirmed by dumping the emitted
            // OpDecorate Location decorations in spikes/webgpu-spirv-spike.
            description.shaderLocation = static_cast<std::uint32_t>(i);
            description.format = ToWGPUVertexFormat(match->getVertexElementFormatProperty());
            description.offset = static_cast<std::uint64_t>(match->getOffsetProperty());
            linked.streams[matchStream].attributes.push_back(description);
        }

        // The explicit link step: patches the vertex shader's input types to the real vertex
        // format, links vertex outputs to pixel inputs, and returns the internal patch table's
        // size, which must be subtracted from output_len before the SPIR-V is used.
        const int patchTableSize = MOJOSHADER_linkSPIRVShaders(
            vertexData, pixelData, mojoAttributes.data(),
            static_cast<int>(mojoAttributes.size()));
        if (patchTableSize <= 0)
            throw std::runtime_error("CNA WebGPU: MOJOSHADER_linkSPIRVShaders failed.");

        const auto finishStage = [&](const MOJOSHADER_parseData* data,
                                     std::vector<WebGPUCompiledSamplerBindingEXT>& samplersOut,
                                     std::uint32_t expectedSet, bool* lodBiasOut) {
            if (data->output == nullptr ||
                static_cast<std::size_t>(data->output_len) <=
                    static_cast<std::size_t>(patchTableSize))
            {
                throw std::runtime_error("CNA WebGPU: a shader produced no usable SPIR-V.");
            }
            const std::size_t wordCount =
                (static_cast<std::size_t>(data->output_len) -
                 static_cast<std::size_t>(patchTableSize)) / sizeof(std::uint32_t);
            const auto* words = reinterpret_cast<const std::uint32_t*>(data->output);
            // WEBGPU-166: WGSL has no combined image sampler, so this is where MojoShader's
            // output stops being Vulkan-shaped and becomes WebGPU-shaped.
            MojoShaderEffect::SpirvSplitResult split =
                MojoShaderEffect::SplitCombinedImageSamplers(words, wordCount);
            if (!split.error.empty())
            {
                throw std::runtime_error(
                    "CNA WebGPU: could not rewrite a compiled effect's SPIR-V: " + split.error);
            }
            for (const auto& binding : split.samplers)
            {
                if (binding.set != expectedSet) continue;
                WebGPUCompiledSamplerBindingEXT out{};
                out.slot = binding.originalBinding;
                out.textureBinding = binding.textureBinding;
                out.samplerBinding = binding.samplerBinding;
                out.viewDimension = ToViewDimension(binding.dim, binding.arrayed);
                samplersOut.push_back(out);
            }
            // WEBGPU-208: SamplerState.MipMapLevelOfDetailBias is XNA sampler state that no
            // WebGPU sampler descriptor can carry, so it has to reach the shader. This is the ONE
            // place it is injected, before the native and browser routes diverge, so the two
            // cannot end up with different XNA semantics. It is a no-op for a stage that samples
            // nothing, which is why the vertex stage passes a null flag.
            std::vector<std::uint32_t> stageWords = std::move(split.words);
            if (lodBiasOut != nullptr)
            {
                MojoShaderEffect::SpirvLodBiasResult biased = MojoShaderEffect::InjectSamplerLodBias(
                    stageWords.data(), stageWords.size(), expectedSet,
                    kWebGPUCompiledEffectLodBiasBinding);
                if (!biased.error.empty())
                {
                    throw std::runtime_error(
                        "CNA WebGPU: could not give a compiled effect's sampling its LOD bias: " +
                        biased.error);
                }
                *lodBiasOut = biased.changed;
                stageWords = std::move(biased.words);
            }
            // Linking PATCHES the SPIR-V in place, so the bytes only mean this draw's vertex
            // format right now. wgpuDeviceCreateShaderModule copies them, so a module made here
            // stays correct forever -- which is why it is asked for by content and owned by the
            // renderer rather than held on the shader and replaced at the next link.
            return renderer_.GetOrCreateCompiledEffectShaderModuleEXT(
                stageWords.data(), stageWords.size(),
                HashWords(stageWords.data(), stageWords.size()));
        };

        linked.vertexModule = finishStage(vertexData, linked.vertexSamplers,
                                          /*expectedSet=*/0u, /*lodBiasOut=*/nullptr);
        linked.pixelModule = finishStage(pixelData, linked.pixelSamplers, /*expectedSet=*/2u,
                                         &linked.pixelHasLodBias);
        linked.vertexEntryPoint = vertexData->mainfn != nullptr ? vertexData->mainfn : "main";
        linked.pixelEntryPoint = pixelData->mainfn != nullptr ? pixelData->mainfn : "main";
        linked.vertexHasUniforms = vertexData->uniform_count > 0;
        linked.pixelHasUniforms = pixelData->uniform_count > 0;

        // plans/plan_fx.md FX-109: no CNA renderer routes GraphicsDevice.VertexTextures to a
        // backend, and this one has no vertex-stage sampler binding either. Refuse by name rather
        // than draw with an unbound group.
        if (!linked.vertexSamplers.empty())
        {
            throw System::NotSupportedException(
                "CNA WebGPU: this compiled effect's VERTEX shader samples a texture, which no CNA "
                "renderer routes today (plans/plan_fx.md FX-109).");
        }

        std::uint64_t key = HashWords(
            reinterpret_cast<const std::uint32_t*>(&linked.vertexModule), 2);
        key ^= HashWords(reinterpret_cast<const std::uint32_t*>(&linked.pixelModule), 2) * 31ull;
        for (const auto& stream : linked.streams)
        {
            key = key * 1099511628211ull ^ stream.arrayStride;
            key = key * 1099511628211ull ^ (stream.perInstance ? 0x9E37u : 0x1234u);
            for (const auto& attribute : stream.attributes)
            {
                key = key * 1099511628211ull ^ static_cast<std::uint64_t>(attribute.format);
                key = key * 1099511628211ull ^ attribute.offset;
                key = key * 1099511628211ull ^ attribute.shaderLocation;
            }
        }
        linked.pipelineKey = key;
        return linked;
    }

    std::unique_ptr<ICompiledEffectRuntime> WebGPURenderer::CreateCompiledEffect(
        const std::uint8_t* effectCode, std::size_t effectCodeBytes)
    {
        return std::make_unique<WebGPUCompiledEffect>(*this, effectCode, effectCodeBytes);
    }

    bool WebGPURenderer::OwnsSampleableTextureEXT(
        Microsoft::Xna::Framework::Graphics::Texture* texture) const
    {
        if (texture == nullptr) return false;
        using namespace Microsoft::Xna::Framework::Graphics;
        if (auto* textureCube = dynamic_cast<TextureCube*>(texture))
        {
            return dynamic_cast<const IWebGPUCubeSamplable*>(&textureCube->GetRenderer()) !=
                   nullptr;
        }
        if (auto* texture3D = dynamic_cast<Texture3D*>(texture))
        {
            return dynamic_cast<const WebGPUTexture3DRenderer*>(&texture3D->GetRenderer()) !=
                   nullptr;
        }
        if (auto* texture2D = dynamic_cast<Texture2D*>(texture))
        {
            return dynamic_cast<const IWebGPUSamplable*>(&texture2D->GetRenderer()) != nullptr;
        }
        return false;
    }
}

#endif  // CNA_WEBGPU_COMPILED_EFFECTS
