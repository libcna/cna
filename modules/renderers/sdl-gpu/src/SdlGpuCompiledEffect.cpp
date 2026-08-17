// SPDX-License-Identifier: MS-PL
//
// plan_fx.md FX-061: built only when CNA_SDL_GPU_COMPILED_EFFECTS is on, because MojoShader is a
// fetched dependency this renderer does not otherwise need. The whole translation unit is guarded
// rather than excluded from the source glob, so the renderer's source list stays the plain
// directory contents every other renderer family uses.
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuCompiledEffect.hpp"

#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuCompiledEffectVertexLayout.hpp"
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace CNA::Internal::Renderers::SdlGpu
{
    namespace
    {
        /// Same ceiling the shared translation applies to reflected tables.
        constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;

        /// Resolves a public texture to the SDL_GPU resource behind it, or null if it is not one.
        const SdlGpuTextureRenderer* AsSdlGpuTexture(Texture* texture)
        {
            if (texture == nullptr) return nullptr;
            using namespace Microsoft::Xna::Framework::Graphics;
            if (auto* texture2D = dynamic_cast<Texture2D*>(texture))
                return dynamic_cast<const SdlGpuTextureRenderer*>(&texture2D->GetRenderer());
            if (auto* texture3D = dynamic_cast<Texture3D*>(texture))
                return dynamic_cast<const SdlGpuTextureRenderer*>(&texture3D->GetRenderer());
            if (auto* textureCube = dynamic_cast<TextureCube*>(texture))
                return dynamic_cast<const SdlGpuTextureRenderer*>(&textureCube->GetRenderer());
            return nullptr;
        }

        /// Wires MojoShader's own SDL_GPU adapter as the backend the effect parser compiles with.
        MOJOSHADER_effectShaderContext MakeBackend(MOJOSHADER_sdlContext* context)
        {
            MOJOSHADER_effectShaderContext backend{};
            backend.shaderContext = context;
            backend.compileShader = (MOJOSHADER_compileShaderFunc) MOJOSHADER_sdlCompileShader;
            backend.shaderAddRef = (MOJOSHADER_shaderAddRefFunc) MOJOSHADER_sdlShaderAddRef;
            backend.deleteShader = (MOJOSHADER_deleteShaderFunc) MOJOSHADER_sdlDeleteShader;
            backend.getParseData =
                (MOJOSHADER_getParseDataFunc) MOJOSHADER_sdlGetShaderParseData;
            backend.bindShaders = (MOJOSHADER_bindShadersFunc) MOJOSHADER_sdlBindShaders;
            backend.getBoundShaders =
                (MOJOSHADER_getBoundShadersFunc) MOJOSHADER_sdlGetBoundShaderData;
            backend.mapUniformBufferMemory =
                (MOJOSHADER_mapUniformBufferMemoryFunc) MOJOSHADER_sdlMapUniformBufferMemory;
            backend.unmapUniformBufferMemory =
                (MOJOSHADER_unmapUniformBufferMemoryFunc) MOJOSHADER_sdlUnmapUniformBufferMemory;
            backend.getError = (MOJOSHADER_getErrorFunc) MOJOSHADER_sdlGetError;
            return backend;
        }
    }

    SdlGpuCompiledEffect::SdlGpuCompiledEffect(SdlGpuRenderer& renderer,
                                               const std::uint8_t* effectCode,
                                               std::size_t effectCodeLength)
        : renderer_(renderer)
        , context_(renderer.GetMojoShaderContextEXT())
    {
        if (effectCode == nullptr || effectCodeLength == 0 ||
            effectCodeLength > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument("SDL_GPU compiled effect: invalid bytecode buffer.");
        }
        if (context_ == nullptr)
        {
            throw std::runtime_error(
                "SDL_GPU compiled effect: MojoShader has no context for this device.");
        }

        MOJOSHADER_effectShaderContext backend = MakeBackend(context_);
        effectData_ = MOJOSHADER_compileEffect(effectCode,
                                               static_cast<unsigned int>(effectCodeLength),
                                               nullptr, 0, nullptr, 0, &backend);
        try
        {
            MojoShaderEffect::ValidateNativeEffect(effectData_, "create");
            description_ = MojoShaderEffect::BuildDescription(effectData_);
            samplerTextureParameters_ =
                MojoShaderEffect::BuildSamplerTextureParameterMap(effectData_);
            textures_.resize(static_cast<std::size_t>(effectData_->param_count), nullptr);
            SetTechnique(0);
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

    SdlGpuCompiledEffect::SdlGpuCompiledEffect(SdlGpuRenderer& renderer,
                                               const SdlGpuCompiledEffect& cloneSource)
        : renderer_(renderer)
        , context_(cloneSource.context_)
        , techniqueIndex_(cloneSource.techniqueIndex_)
    {
        effectData_ = MOJOSHADER_cloneEffect(cloneSource.effectData_);
        try
        {
            MojoShaderEffect::ValidateNativeEffect(effectData_, "clone");
            description_ = MojoShaderEffect::BuildDescription(effectData_);
            samplerTextureParameters_ =
                MojoShaderEffect::BuildSamplerTextureParameterMap(effectData_);
            textures_ = cloneSource.textures_;
            boundTextures_ = cloneSource.boundTextures_;
            boundVertexTextures_ = cloneSource.boundVertexTextures_;
            boundSamplers_ = cloneSource.boundSamplers_;
            boundVertexSamplers_ = cloneSource.boundVertexSamplers_;
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

    SdlGpuCompiledEffect::~SdlGpuCompiledEffect()
    {
        if (effectData_ != nullptr)
        {
            if (passActive_) MOJOSHADER_effectEndPass(effectData_);
            if (MojoShaderEffect::CanSafelyDeleteNativeEffect(effectData_))
                MOJOSHADER_deleteEffect(effectData_);
            effectData_ = nullptr;
        }
    }

    std::unique_ptr<ICompiledEffectRuntime> SdlGpuCompiledEffect::Clone() const
    {
        return std::unique_ptr<ICompiledEffectRuntime>(
            new SdlGpuCompiledEffect(renderer_, *this));
    }

    const CompiledEffectDescription& SdlGpuCompiledEffect::GetDescription() const
    {
        return description_;
    }

    void SdlGpuCompiledEffect::SetTechnique(std::uint32_t techniqueIndex)
    {
        if (techniqueIndex >= static_cast<std::uint32_t>(effectData_->technique_count))
        {
            throw std::out_of_range("SDL_GPU compiled effect: technique index is out of range.");
        }
        techniqueIndex_ = techniqueIndex;
        MOJOSHADER_effectSetTechnique(effectData_, &effectData_->techniques[techniqueIndex]);
    }

    void SdlGpuCompiledEffect::SetParameterValue(std::uint32_t runtimeIndex, const void* data,
                                                 std::size_t dataBytes)
    {
        if (runtimeIndex >= static_cast<std::uint32_t>(effectData_->param_count))
            throw std::out_of_range("SDL_GPU compiled effect: parameter index is out of range.");
        MOJOSHADER_effectParam& parameter = effectData_->params[runtimeIndex];
        const std::size_t capacity = static_cast<std::size_t>(parameter.value.value_count) * 4;
        if (dataBytes > capacity)
            throw std::invalid_argument("SDL_GPU compiled effect: parameter value is too large.");
        if (dataBytes > 0 && data == nullptr)
            throw std::invalid_argument("SDL_GPU compiled effect: parameter data is null.");
        if (dataBytes > 0)
        {
            MOJOSHADER_effectSetRawValueHandle(&parameter, data, 0,
                                               static_cast<unsigned int>(dataBytes));
        }
    }

    void SdlGpuCompiledEffect::SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture)
    {
        if (runtimeIndex >= textures_.size())
        {
            throw std::out_of_range(
                "SDL_GPU compiled effect: texture parameter index is out of range.");
        }
        const auto parameterType = static_cast<std::underlying_type_t<MOJOSHADER_symbolType>>(
            effectData_->params[runtimeIndex].value.type.parameter_type);
        if (parameterType < MOJOSHADER_SYMTYPE_TEXTURE ||
            parameterType > MOJOSHADER_SYMTYPE_TEXTURECUBE)
        {
            throw std::invalid_argument("SDL_GPU compiled effect: parameter is not a texture.");
        }
        if (texture != nullptr && AsSdlGpuTexture(texture) == nullptr)
        {
            throw std::invalid_argument(
                "SDL_GPU compiled effect: texture was not created by the active SDL_GPU renderer.");
        }
        textures_[runtimeIndex] = texture;
    }

    void SdlGpuCompiledEffect::ApplyPass(std::uint32_t passIndex,
                                         const CompiledEffectDeviceState& deviceState,
                                         CompiledEffectPassStateChanges& changes)
    {
        const MOJOSHADER_effectTechnique& technique = effectData_->techniques[techniqueIndex_];
        if (passIndex >= technique.pass_count)
            throw std::out_of_range("SDL_GPU compiled effect: pass index is out of range.");

        // MojoShader's effect runtime is a begin/pass/end state machine rather than FNA3D's single
        // apply call, so a pass left open by a previous apply has to be closed first. Reopening
        // per apply is what keeps each pass's reported state changes its own.
        if (passActive_)
        {
            MOJOSHADER_effectEndPass(effectData_);
            MOJOSHADER_effectEnd(effectData_);
            passActive_ = false;
        }

        std::memset(&stateChanges_, 0, sizeof(stateChanges_));
        unsigned int passCount = 0;
        MOJOSHADER_effectBegin(effectData_, &passCount, /*saveShaderState=*/0, &stateChanges_);
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
                "SDL_GPU compiled effect: native pass state changes exceed the safety limit.");
        }

        MojoShaderEffect::TranslateRenderStates(stateChanges_, deviceState, changes);

        using Microsoft::Xna::Framework::Graphics::SamplerStateCollection;
        constexpr std::size_t maxSlots =
            static_cast<std::size_t>(SamplerStateCollection::MaxSamplers);
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.sampler_state_changes, stateChanges_.sampler_state_change_count,
            /*vertexStage=*/false, maxSlots, samplerTextureParameters_, textures_,
            deviceState, changes);
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.vertex_sampler_state_changes,
            stateChanges_.vertex_sampler_state_change_count,
            /*vertexStage=*/true, maxSlots, samplerTextureParameters_, textures_,
            deviceState, changes);

        // FX-071: fold this pass's assignments into the persistent per-slot state a draw route
        // reads through GetBoundSamplerEXT. Matches real XNA behavior -- a slot this pass does not
        // touch keeps whatever an earlier pass assigned, so only entries this pass actually
        // reported (samplerChanged/textureChanged) are written here.
        for (const auto& sampler : changes.samplers)
        {
            if (sampler.slot >= maxSlots) continue;
            auto& textureSlot = sampler.vertexStage ? boundVertexTextures_ : boundTextures_;
            auto& samplerSlot = sampler.vertexStage ? boundVertexSamplers_ : boundSamplers_;
            if (sampler.textureChanged) textureSlot[sampler.slot] = sampler.texture;
            if (sampler.samplerChanged)
            {
                samplerSlot[sampler.slot] = sampler.sampler;
                (sampler.vertexStage ? vertexSamplerAssigned_ : samplerAssigned_)[sampler.slot] =
                    true;
            }
        }

        // Native sampler/texture binding does not happen here, unlike the FNA3D backend. This
        // renderer binds textures and samplers as part of building a draw's pipeline, through
        // GetBoundSamplerEXT above.
    }

    void SdlGpuCompiledEffect::GetBoundSamplerEXT(
        std::uint32_t slot, bool vertexStage, Texture*& texture,
        Microsoft::Xna::Framework::Graphics::SamplerState& sampler,
        bool* samplerAssigned) const
    {
        using Microsoft::Xna::Framework::Graphics::SamplerStateCollection;
        if (samplerAssigned != nullptr) *samplerAssigned = false;
        if (slot >= static_cast<std::uint32_t>(SamplerStateCollection::MaxSamplers))
        {
            texture = nullptr;
            return;
        }
        texture = vertexStage ? boundVertexTextures_[slot] : boundTextures_[slot];
        sampler = vertexStage ? boundVertexSamplers_[slot] : boundSamplers_[slot];
        if (samplerAssigned != nullptr)
        {
            *samplerAssigned =
                vertexStage ? vertexSamplerAssigned_[slot] : samplerAssigned_[slot];
        }
    }

    std::vector<SDL_GPUVertexAttribute> SdlGpuCompiledEffect::LinkAndGetShadersEXT(
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaredElements,
        SDL_GPUShader*& vertexShader, SDL_GPUShader*& pixelShader) const
    {
        vertexShader = nullptr;
        pixelShader = nullptr;
        if (context_ == nullptr)
            throw std::runtime_error("SDL_GPU compiled effect: no MojoShader context.");

        MOJOSHADER_sdlShaderData* vertex = nullptr;
        MOJOSHADER_sdlShaderData* pixel = nullptr;
        MOJOSHADER_sdlGetBoundShaderData(context_, &vertex, &pixel);
        if (vertex == nullptr || pixel == nullptr)
        {
            throw std::runtime_error(
                "SDL_GPU compiled effect: the applied pass bound no shader pair.");
        }

        const MOJOSHADER_parseData* vertexParseData = MOJOSHADER_sdlGetShaderParseData(vertex);
        if (vertexParseData == nullptr)
        {
            throw std::runtime_error(
                "SDL_GPU compiled effect: the applied vertex shader has no reflection.");
        }

        std::vector<SDL_GPUVertexAttribute> sdlAttributes =
            BuildCompiledEffectVertexAttributes(*vertexParseData, declaredElements, /*bufferSlot=*/0);
        std::vector<MOJOSHADER_vertexAttribute> mojoAttributes =
            BuildMojoShaderVertexAttributes(*vertexParseData, declaredElements);

        // MOJOSHADER_sdlLinkProgram reads ctx->bound_vshader_data/bound_pshader_data -- exactly
        // what MOJOSHADER_sdlGetBoundShaderData just returned, so this links the pair this pass
        // just applied, not some other effect's. It also sets ctx->bound_program itself, so a
        // separate MOJOSHADER_sdlBindProgram call would be redundant.
        MOJOSHADER_sdlProgram* program = MOJOSHADER_sdlLinkProgram(
            context_, mojoAttributes.empty() ? nullptr : mojoAttributes.data(),
            static_cast<int>(mojoAttributes.size()));
        if (program == nullptr)
        {
            throw std::runtime_error(
                "SDL_GPU compiled effect: failed to link the applied pass's shaders.");
        }

        MOJOSHADER_sdlGetShaders(context_, &vertexShader, &pixelShader);
        if (vertexShader == nullptr || pixelShader == nullptr)
        {
            throw std::runtime_error(
                "SDL_GPU compiled effect: the linked program has no native shader modules.");
        }

        return sdlAttributes;
    }

    void SdlGpuCompiledEffect::GetBoundShadersEXT(MOJOSHADER_sdlShaderData*& vertex,
                                                  MOJOSHADER_sdlShaderData*& pixel) const
    {
        vertex = nullptr;
        pixel = nullptr;
        if (context_ != nullptr) MOJOSHADER_sdlGetBoundShaderData(context_, &vertex, &pixel);
    }

    namespace
    {
        /// Mirrors mojoshader_sdlgpu.c's own `update_uniform_buffer`, sourced from the register
        /// files `MOJOSHADER_sdlMapUniformBufferMemory` exposes rather than that private function.
        void PackUniformBuffer(const MOJOSHADER_sdlShaderData* shader,
                               const float* regF, const int* regI, const unsigned char* regB,
                               std::vector<std::uint8_t>& out)
        {
            out.clear();
            if (shader == nullptr) return;
            const MOJOSHADER_parseData* parseData = MOJOSHADER_sdlGetShaderParseData(
                const_cast<MOJOSHADER_sdlShaderData*>(shader));
            if (parseData == nullptr || parseData->uniform_count <= 0 || parseData->uniforms == nullptr)
                return;

            std::size_t offset = 0;
            for (int i = 0; i < parseData->uniform_count; ++i)
            {
                const int size = parseData->uniforms[i].array_count > 0
                                     ? parseData->uniforms[i].array_count
                                     : 1;
                offset += static_cast<std::size_t>(size) * 16u;
            }
            out.assign(offset, 0u);

            offset = 0;
            for (int i = 0; i < parseData->uniform_count; ++i)
            {
                const MOJOSHADER_uniform& uniform = parseData->uniforms[i];
                const int index = uniform.index;
                const int size = uniform.array_count > 0 ? uniform.array_count : 1;
                switch (uniform.type)
                {
                    case MOJOSHADER_UNIFORM_FLOAT:
                        std::memcpy(out.data() + offset, &regF[4 * index],
                                   static_cast<std::size_t>(size) * 16u);
                        break;
                    case MOJOSHADER_UNIFORM_INT:
                        std::memcpy(out.data() + offset, &regI[4 * index],
                                   static_cast<std::size_t>(size) * 16u);
                        break;
                    case MOJOSHADER_UNIFORM_BOOL:
                        for (int j = 0; j < size; ++j)
                        {
                            std::uint32_t bit = regB[index + j];
                            std::memcpy(out.data() + offset + static_cast<std::size_t>(j) * 16u,
                                       &bit, sizeof(bit));
                        }
                        break;
                    default:
                        break;
                }
                offset += static_cast<std::size_t>(size) * 16u;
            }
        }
    }

    void SdlGpuCompiledEffect::CaptureUniformSnapshotEXT(std::vector<std::uint8_t>& vertexBytes,
                                                         std::vector<std::uint8_t>& pixelBytes) const
    {
        vertexBytes.clear();
        pixelBytes.clear();
        if (context_ == nullptr) return;

        MOJOSHADER_sdlShaderData* vertex = nullptr;
        MOJOSHADER_sdlShaderData* pixel = nullptr;
        MOJOSHADER_sdlGetBoundShaderData(context_, &vertex, &pixel);
        if (vertex == nullptr && pixel == nullptr) return;

        float* vsf = nullptr; int* vsi = nullptr; unsigned char* vsb = nullptr;
        float* psf = nullptr; int* psi = nullptr; unsigned char* psb = nullptr;
        MOJOSHADER_sdlMapUniformBufferMemory(context_, &vsf, &vsi, &vsb, &psf, &psi, &psb);

        PackUniformBuffer(vertex, vsf, vsi, vsb, vertexBytes);
        PackUniformBuffer(pixel, psf, psi, psb, pixelBytes);

        MOJOSHADER_sdlUnmapUniformBufferMemory(context_);
    }

    // ---- SdlGpuRenderer hooks (plan_fx.md FX-061) ------------------------------------------
    //
    // Defined here rather than in SdlGpuRenderer.cpp so the whole compiled-effect surface lives in
    // one guarded translation unit; nothing else in the renderer changes when the option is off.

    MOJOSHADER_sdlContext* SdlGpuRenderer::GetMojoShaderContextEXT()
    {
        if (mojoShaderContext_ == nullptr && device_ != nullptr)
        {
            mojoShaderContext_ =
                MOJOSHADER_sdlCreateContext(device_, nullptr, nullptr, nullptr);
        }
        return mojoShaderContext_;
    }

    std::unique_ptr<ICompiledEffectRuntime> SdlGpuRenderer::CreateCompiledEffect(
        const std::uint8_t* effectCode, std::size_t effectCodeBytes)
    {
        return std::make_unique<SdlGpuCompiledEffect>(*this, effectCode, effectCodeBytes);
    }
}

#endif  // CNA_SDL_GPU_COMPILED_EFFECTS
