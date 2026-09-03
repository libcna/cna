// SPDX-License-Identifier: MS-PL
//
// plans/plan_fx.md FX-065. See VulkanCompiledEffect.hpp for why this backend is CNA's own rather than a
// MojoShader-provided adapter.

#if defined(CNA_VULKAN_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/Vulkan/VulkanCompiledEffect.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
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

namespace CNA::Internal::Renderers::Vulkan
{
    namespace
    {
        /// Same ceiling the shared translation applies to reflected tables.
        constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;
        /// The largest effect binary CNA will parse, matching every other compiled-effect backend.
        constexpr std::size_t kMaximumCompiledEffectBytes = 64u * 1024u * 1024u;

        // ------------------------------------------------------------------------------------
        // The nine-function MOJOSHADER_effectShaderContext, implemented directly against
        // MOJOSHADER_parse() with the portable SPIR-V profile. MojoShader has no Vulkan adapter,
        // so every one of these is CNA's own; the shape follows mojoshader_sdlgpu.c's bookkeeping
        // (ref-counted shaders, a bound pair, flat register files) so that the runtime above can
        // stay recognisably the same as SdlGpuCompiledEffect's.
        // ------------------------------------------------------------------------------------

        void* MOJOSHADERCALL BackendCompileShader(
            const void* ctxVoid, const char* mainfn, const unsigned char* tokenbuf,
            const unsigned int bufsize, const MOJOSHADER_swizzle* swiz,
            const unsigned int swizcount, const MOJOSHADER_samplerMap* smap,
            const unsigned int smapcount)
        {
            auto* ctx = static_cast<VulkanMojoShaderContextEXT*>(const_cast<void*>(ctxVoid));
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
            auto* shader = new VulkanCompiledShaderEXT{};
            shader->parseData = parsed;
            return shader;
        }

        void MOJOSHADERCALL BackendShaderAddRef(void* shaderVoid)
        {
            if (shaderVoid != nullptr)
                static_cast<VulkanCompiledShaderEXT*>(shaderVoid)->refcount++;
        }

        void MOJOSHADERCALL BackendDeleteShader(const void* ctxVoid, void* shaderVoid)
        {
            if (shaderVoid == nullptr) return;
            auto* shader = static_cast<VulkanCompiledShaderEXT*>(shaderVoid);
            if (--shader->refcount > 0) return;
            auto* ctx = static_cast<VulkanMojoShaderContextEXT*>(const_cast<void*>(ctxVoid));
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
                             static_cast<VulkanCompiledShaderEXT*>(shaderVoid)->parseData)
                       : nullptr;
        }

        void MOJOSHADERCALL BackendBindShaders(const void* ctxVoid, void* vshader, void* pshader)
        {
            auto* ctx = static_cast<VulkanMojoShaderContextEXT*>(const_cast<void*>(ctxVoid));
            ctx->boundVertex = static_cast<VulkanCompiledShaderEXT*>(vshader);
            ctx->boundPixel = static_cast<VulkanCompiledShaderEXT*>(pshader);
        }

        void MOJOSHADERCALL BackendGetBoundShaders(const void* ctxVoid, void** vshader,
                                                  void** pshader)
        {
            const auto* ctx = static_cast<const VulkanMojoShaderContextEXT*>(ctxVoid);
            if (vshader != nullptr) *vshader = ctx->boundVertex;
            if (pshader != nullptr) *pshader = ctx->boundPixel;
        }

        void MOJOSHADERCALL BackendMapUniformBufferMemory(
            const void* ctxVoid, float** vsf, int** vsi, unsigned char** vsb, float** psf,
            int** psi, unsigned char** psb)
        {
            auto* ctx = static_cast<VulkanMojoShaderContextEXT*>(const_cast<void*>(ctxVoid));
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
            return static_cast<const VulkanMojoShaderContextEXT*>(ctxVoid)->lastError.c_str();
        }

        [[nodiscard]] MOJOSHADER_effectShaderContext MakeBackend(VulkanMojoShaderContextEXT* ctx)
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

        /// XNA vertex semantics to MojoShader's own usage enumeration. Same table the SDL_GPU
        /// backend carries; duplicated rather than shared because the two renderers must not gain a
        /// build dependency on each other for one switch.
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
                "CNA Vulkan: unrecognized VertexElementUsage ordinal " +
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
                "CNA Vulkan: unrecognized VertexElementFormat ordinal " +
                std::to_string(static_cast<int>(format)));
        }

        /// XNA vertex formats to the Vulkan attribute formats the pipeline declares.
        [[nodiscard]] VkFormat ToVkVertexFormat(VertexElementFormat format)
        {
            switch (format)
            {
                case VertexElementFormat::Single:  return VK_FORMAT_R32_SFLOAT;
                case VertexElementFormat::Vector2: return VK_FORMAT_R32G32_SFLOAT;
                case VertexElementFormat::Vector3: return VK_FORMAT_R32G32B32_SFLOAT;
                case VertexElementFormat::Vector4: return VK_FORMAT_R32G32B32A32_SFLOAT;
                // XNA's Color is BGRA in memory; the shader wants RGBA, and the swizzle is what
                // VK_FORMAT_B8G8R8A8_UNORM expresses without a shader-side fixup.
                case VertexElementFormat::Color:   return VK_FORMAT_B8G8R8A8_UNORM;
                case VertexElementFormat::Byte4:   return VK_FORMAT_R8G8B8A8_UINT;
                case VertexElementFormat::Short2:  return VK_FORMAT_R16G16_SINT;
                case VertexElementFormat::Short4:  return VK_FORMAT_R16G16B16A16_SINT;
                case VertexElementFormat::NormalizedShort2: return VK_FORMAT_R16G16_SNORM;
                case VertexElementFormat::NormalizedShort4: return VK_FORMAT_R16G16B16A16_SNORM;
                case VertexElementFormat::HalfVector2: return VK_FORMAT_R16G16_SFLOAT;
                case VertexElementFormat::HalfVector4: return VK_FORMAT_R16G16B16A16_SFLOAT;
            }
            throw std::invalid_argument(
                "CNA Vulkan: unrecognized VertexElementFormat ordinal " +
                std::to_string(static_cast<int>(format)));
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
                            VulkanMojoShaderContextEXT::kMaxFloat4Registers * 4u)
                    {
                        std::memcpy(dst, &regF[4 * index], bytes);
                    }
                }
                else if (uniform.type == MOJOSHADER_UNIFORM_INT)
                {
                    if (index >= 0 &&
                        static_cast<std::size_t>(4 * index) + span * 4u <=
                            VulkanMojoShaderContextEXT::kMaxInt4Registers * 4u)
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
                        if (index + j < 0 ||
                            index + j >= VulkanMojoShaderContextEXT::kMaxBoolRegisters)
                        {
                            break;
                        }
                        const std::uint32_t value = regB[index + j] != 0 ? 1u : 0u;
                        std::memcpy(dst + static_cast<std::size_t>(j) * 16u, &value, sizeof(value));
                    }
                }
                offset += bytes;
            }
        }
    }

    VulkanCompiledEffect::VulkanCompiledEffect(VulkanRenderer& renderer,
                                               const std::uint8_t* effectCode,
                                               std::size_t effectCodeLength)
        : renderer_(renderer)
    {
        BuildDescriptionAndBackend(effectCode, effectCodeLength);
    }

    VulkanCompiledEffect::VulkanCompiledEffect(VulkanRenderer& renderer,
                                               const VulkanCompiledEffect& cloneSource)
        : renderer_(renderer)
        , context_(cloneSource.context_)
        , techniqueIndex_(cloneSource.techniqueIndex_)
    {
        // MOJOSHADER_cloneEffect gives the copy its OWN parameter storage while sharing the
        // immutable compiled shader objects -- which is exactly XNA's Clone() contract, and the
        // same call the SDL_GPU backend makes.
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

    void VulkanCompiledEffect::BuildDescriptionAndBackend(const std::uint8_t* effectCode,
                                                          std::size_t effectCodeLength)
    {
        if (effectCode == nullptr || effectCodeLength == 0)
            throw std::invalid_argument("Vulkan compiled effect: bytecode must not be empty.");
        if (effectCodeLength > kMaximumCompiledEffectBytes)
            throw std::invalid_argument(
                "Vulkan compiled effect: bytecode exceeds CNA's 64 MiB safety limit.");

        context_ = renderer_.GetMojoShaderContextEXT();
        if (context_ == nullptr)
            throw std::runtime_error(
                "Vulkan compiled effect: this renderer has no MojoShader backend context.");

        MOJOSHADER_effectShaderContext backend = MakeBackend(context_);
        effectData_ = MOJOSHADER_compileEffect(effectCode,
                                               static_cast<unsigned int>(effectCodeLength),
                                               nullptr, 0, nullptr, 0, &backend);
        MojoShaderEffect::ValidateNativeEffect(effectData_, "Vulkan compiled effect");
        description_ = MojoShaderEffect::BuildDescription(effectData_);
        samplerTextureParameters_ = MojoShaderEffect::BuildSamplerTextureParameterMap(effectData_);
        textures_.assign(description_.parameters.size(), nullptr);
        boundSamplerTextures_.fill(nullptr);
        boundVertexSamplerTextures_.fill(nullptr);
    }

    VulkanCompiledEffect::~VulkanCompiledEffect()
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

    std::unique_ptr<ICompiledEffectRuntime> VulkanCompiledEffect::Clone() const
    {
        return std::unique_ptr<ICompiledEffectRuntime>(
            new VulkanCompiledEffect(renderer_, *this));
    }

    const CompiledEffectDescription& VulkanCompiledEffect::GetDescription() const
    {
        return description_;
    }

    void VulkanCompiledEffect::SetTechnique(std::uint32_t techniqueIndex)
    {
        if (effectData_ == nullptr ||
            techniqueIndex >= static_cast<std::uint32_t>(effectData_->technique_count))
        {
            throw std::out_of_range("Vulkan compiled effect: technique index is out of range.");
        }
        techniqueIndex_ = techniqueIndex;
        MOJOSHADER_effectSetTechnique(effectData_, &effectData_->techniques[techniqueIndex]);
    }

    void VulkanCompiledEffect::SetParameterValue(std::uint32_t runtimeIndex, const void* data,
                                                 std::size_t dataBytes)
    {
        if (effectData_ == nullptr ||
            runtimeIndex >= static_cast<std::uint32_t>(effectData_->param_count))
        {
            throw std::out_of_range("Vulkan compiled effect: parameter index is out of range.");
        }
        MOJOSHADER_effectParam& parameter = effectData_->params[runtimeIndex];
        if (data == nullptr)
            throw std::invalid_argument("Vulkan compiled effect: parameter data is null.");
        if (dataBytes > 0)
        {
            MOJOSHADER_effectSetRawValueHandle(&parameter, data, 0,
                                               static_cast<unsigned int>(dataBytes));
        }
    }

    void VulkanCompiledEffect::SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture)
    {
        if (runtimeIndex >= textures_.size())
        {
            throw std::out_of_range(
                "Vulkan compiled effect: texture parameter index is out of range.");
        }
        const auto parameterType = static_cast<std::underlying_type_t<MOJOSHADER_symbolType>>(
            effectData_->params[runtimeIndex].value.type.parameter_type);
        if (parameterType < MOJOSHADER_SYMTYPE_TEXTURE ||
            parameterType > MOJOSHADER_SYMTYPE_TEXTURECUBE)
        {
            throw std::invalid_argument("Vulkan compiled effect: parameter is not a texture.");
        }
        if (texture != nullptr && !renderer_.OwnsSampleableTextureEXT(texture))
        {
            throw std::invalid_argument(
                "Vulkan compiled effect: texture was not created by the active Vulkan renderer.");
        }
        // plans/plan_fx.md FX-110: the assigned texture's dimension has to match the one the effect
        // declared, for the same reason it does on every other backend -- a mismatched kind binds
        // an unrelated image view and samples something the game never asked for.
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
                    std::string("Vulkan compiled effect: parameter declares ") + declaredName +
                    " but a " + assignedName + " was assigned; the dimensions must match.");
            }
        }
        textures_[runtimeIndex] = texture;
    }

    void VulkanCompiledEffect::ApplyPass(std::uint32_t passIndex,
                                         const CompiledEffectDeviceState& deviceState,
                                         CompiledEffectPassStateChanges& changes)
    {
        if (effectData_ == nullptr)
            throw std::runtime_error("Vulkan compiled effect: the native effect is gone.");
        const MOJOSHADER_effectTechnique& technique = effectData_->techniques[techniqueIndex_];
        if (passIndex >= technique.pass_count)
            throw std::out_of_range("Vulkan compiled effect: pass index is out of range.");

        // plans/plan_fx.md FX-101: `stateChanges_` is deliberately NOT cleared between applications.
        // MojoShader writes it only in effectBeginPass, so a repeated application of the same pass
        // must keep the pointers the previous one left -- clearing them makes a repeat apply
        // publish no render state, no sampler state and no texture binding at all. FNA relies on
        // the same persistence; see Fna3dCompiledEffect::ApplyPass for the full account.
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
                "Vulkan compiled effect: native pass state changes exceed the safety limit.");
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

    void VulkanCompiledEffect::GetBoundShadersEXT(VulkanCompiledShaderEXT*& vertex,
                                                  VulkanCompiledShaderEXT*& pixel) const
    {
        vertex = context_ != nullptr ? context_->boundVertex : nullptr;
        pixel = context_ != nullptr ? context_->boundPixel : nullptr;
    }

    void VulkanCompiledEffect::CaptureUniformSnapshotEXT(std::vector<std::uint8_t>& vertexBytes,
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

    void VulkanCompiledEffect::GetBoundSamplerEXT(
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

    VulkanCompiledEffect::LinkedPassEXT VulkanCompiledEffect::LinkAndGetShadersEXT(
        const std::vector<CompiledVertexStreamEXT>& streams) const
    {
        VulkanCompiledShaderEXT* vertex = nullptr;
        VulkanCompiledShaderEXT* pixel = nullptr;
        GetBoundShadersEXT(vertex, pixel);
        if (vertex == nullptr || pixel == nullptr || vertex->parseData == nullptr ||
            pixel->parseData == nullptr)
        {
            throw std::runtime_error(
                "CNA Vulkan: the applied compiled-effect pass bound no shader pair.");
        }

        const MOJOSHADER_parseData* vertexData = vertex->parseData;
        const MOJOSHADER_parseData* pixelData = pixel->parseData;

        // One MOJOSHADER_vertexAttribute per shader input, resolved from the caller's own
        // declaration. A shader input the declaration does not supply must fail loudly: binding
        // nothing there would sample undefined vertex data, which is the silent-corruption shape
        // every other backend refuses too.
        LinkedPassEXT linked;
        // plans/plan_fx.md FX-112: one binding per supplied stream, in the caller's order, so binding
        // index and stream index are the same number everywhere below and at record time.
        linked.vertexBindings.reserve(streams.size());
        for (std::size_t streamIndex = 0; streamIndex < streams.size(); ++streamIndex)
        {
            VkVertexInputBindingDescription binding{};
            binding.binding = static_cast<std::uint32_t>(streamIndex);
            binding.stride = streams[streamIndex].stride;
            binding.inputRate = streams[streamIndex].perInstance
                                    ? VK_VERTEX_INPUT_RATE_INSTANCE
                                    : VK_VERTEX_INPUT_RATE_VERTEX;
            linked.vertexBindings.push_back(binding);
        }

        std::vector<MOJOSHADER_vertexAttribute> mojoAttributes;
        mojoAttributes.reserve(static_cast<std::size_t>(std::max(vertexData->attribute_count, 0)));
        linked.vertexAttributes.reserve(mojoAttributes.capacity());
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
                    "CNA Vulkan: this compiled effect's vertex shader requires attribute '" +
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

            VkVertexInputAttributeDescription description{};
            // MojoShader assigns SPIR-V input locations in the vertex shader's own declaration
            // order, which is the order this loop walks -- the same correspondence FX-064's probe
            // confirmed by dumping the emitted OpDecorate Location decorations.
            description.location = static_cast<std::uint32_t>(i);
            description.binding = static_cast<std::uint32_t>(matchStream);
            description.format = ToVkVertexFormat(match->getVertexElementFormatProperty());
            description.offset = static_cast<std::uint32_t>(match->getOffsetProperty());
            linked.vertexAttributes.push_back(description);
        }

        // The explicit link step: patches the vertex shader's input types to the real vertex
        // format, links vertex outputs to pixel inputs, and returns the internal patch table's
        // size, which must be subtracted from output_len before the SPIR-V reaches Vulkan.
        const int patchTableSize = MOJOSHADER_linkSPIRVShaders(
            vertexData, pixelData, mojoAttributes.data(),
            static_cast<int>(mojoAttributes.size()));
        if (patchTableSize <= 0)
            throw std::runtime_error("CNA Vulkan: MOJOSHADER_linkSPIRVShaders failed.");

        const auto makeModule = [&](VulkanCompiledShaderEXT& shader) {
            const auto* data = shader.parseData;
            if (data->output == nullptr ||
                static_cast<std::size_t>(data->output_len) <= static_cast<std::size_t>(patchTableSize))
            {
                throw std::runtime_error("CNA Vulkan: a shader produced no usable SPIR-V.");
            }
            // Linking PATCHES the SPIR-V in place, so the bytes only mean this draw's vertex format
            // right now. vkCreateShaderModule copies them, so a module made here stays correct
            // forever -- which is why it is asked for by content and owned by the renderer rather
            // than held on the shader and replaced at the next link. See
            // VulkanRenderer::GetOrCreateCompiledEffectShaderModuleEXT for why that matters to a
            // draw recorded at Present().
            const std::size_t codeBytes = static_cast<std::size_t>(data->output_len) -
                                          static_cast<std::size_t>(patchTableSize);
            return renderer_.GetOrCreateCompiledEffectShaderModuleEXT(data->output, codeBytes);
        };
        linked.vertexModule = makeModule(*vertex);
        linked.pixelModule = makeModule(*pixel);
        linked.vertexEntryPoint = vertexData->mainfn != nullptr ? vertexData->mainfn : "main";
        linked.pixelEntryPoint = pixelData->mainfn != nullptr ? pixelData->mainfn : "main";

        if (vertexData->sampler_count > 0)
        {
            // plans/plan_fx.md FX-109: renderer-wide, not compiled-Effect specific. No CNA renderer
            // implements vertex-stage texture sampling through any route.
            throw System::NotSupportedException(
                "CNA Vulkan: this compiled effect's vertex shader samples a texture. Vertex-stage "
                "texture sampling is not implemented in this renderer at all, by any draw route.");
        }
        for (int i = 0; i < pixelData->sampler_count; ++i)
            linked.pixelSamplers.push_back(pixelData->samplers[i]);

        const auto declaresUniforms = [](const MOJOSHADER_parseData* data) {
            return data != nullptr && data->uniform_count > 0;
        };
        linked.vertexHasUniforms = declaresUniforms(vertexData);
        linked.pixelHasUniforms = declaresUniforms(pixelData);
        // The pipeline's identity: the shader pair AND the vertex input layout it is used with.
        // The modules alone are not enough. They are content-addressed, so two draws whose patched
        // SPIR-V happens to come out identical share one pair of handles while still binding
        // different attribute formats and offsets -- and a pipeline built for the first would then
        // read the second's vertices from the wrong bytes. The renderer's own state key covers the
        // stride, but not what sits where inside it.
        std::uint64_t key = 1469598103934665603ull;
        const auto mix = [&key](std::uint64_t value) {
            key ^= value + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2);
        };
        mix(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(linked.vertexModule)));
        mix(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(linked.pixelModule)));
        for (const VkVertexInputAttributeDescription& attribute : linked.vertexAttributes)
        {
            mix(attribute.location);
            mix(attribute.binding);
            mix(static_cast<std::uint64_t>(attribute.format));
            mix(attribute.offset);
        }
        for (const VkVertexInputBindingDescription& binding : linked.vertexBindings)
        {
            mix(binding.binding);
            mix(binding.stride);
            mix(static_cast<std::uint64_t>(binding.inputRate));
        }
        linked.pipelineKey = key;
        return linked;
    }

    // ---- VulkanRenderer hooks (plans/plan_fx.md FX-065) -------------------------------------------
    //
    // Defined here rather than in VulkanRenderer.cpp so the whole compiled-effect surface lives in
    // one guarded translation unit; nothing else in the renderer changes when the option is off.

    VulkanMojoShaderContextEXT* VulkanRenderer::GetMojoShaderContextEXT()
    {
        if (mojoShaderContext_ == nullptr)
        {
            mojoShaderContext_ = std::make_unique<VulkanMojoShaderContextEXT>();
            mojoShaderContext_->device = device_;
        }
        return mojoShaderContext_.get();
    }

    std::unique_ptr<ICompiledEffectRuntime> VulkanRenderer::CreateCompiledEffect(
        const std::uint8_t* effectCode, std::size_t effectCodeBytes)
    {
        return std::make_unique<VulkanCompiledEffect>(*this, effectCode, effectCodeBytes);
    }

    bool VulkanRenderer::OwnsSampleableTextureEXT(
        Microsoft::Xna::Framework::Graphics::Texture* texture) const
    {
        if (texture == nullptr) return false;
        using namespace Microsoft::Xna::Framework::Graphics;
        if (auto* textureCube = dynamic_cast<TextureCube*>(texture))
        {
            return dynamic_cast<const IVulkanCubeSamplable*>(&textureCube->GetRenderer()) != nullptr;
        }
        if (auto* texture3D = dynamic_cast<Texture3D*>(texture))
        {
            return dynamic_cast<const IVulkanVolumeSamplable*>(&texture3D->GetRenderer()) !=
                   nullptr;
        }
        if (auto* texture2D = dynamic_cast<Texture2D*>(texture))
        {
            return dynamic_cast<const IVulkanSamplable*>(&texture2D->GetRenderer()) != nullptr;
        }
        return false;
    }
}

#endif  // CNA_VULKAN_COMPILED_EFFECTS
