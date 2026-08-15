// SPDX-License-Identifier: MS-PL
//
// plan_fx.md FX-061: built only when CNA_SDL_GPU_COMPILED_EFFECTS is on, because MojoShader is a
// fetched dependency this renderer does not otherwise need. The whole translation unit is guarded
// rather than excluded from the source glob, so the renderer's source list stays the plain
// directory contents every other renderer family uses.
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuCompiledEffect.hpp"

#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
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

        // No native sampler or texture binding happens here, unlike the FNA3D backend. This
        // renderer binds textures and samplers as part of building a draw's pipeline, and the
        // compiled-effect draw route that would do so does not exist yet -- see the header.
    }

    void SdlGpuCompiledEffect::GetBoundShadersEXT(MOJOSHADER_sdlShaderData*& vertex,
                                                  MOJOSHADER_sdlShaderData*& pixel) const
    {
        vertex = nullptr;
        pixel = nullptr;
        if (context_ != nullptr) MOJOSHADER_sdlGetBoundShaderData(context_, &vertex, &pixel);
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
