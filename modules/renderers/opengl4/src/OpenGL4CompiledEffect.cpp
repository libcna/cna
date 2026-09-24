// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md GL4-0020: the compiled XNA Effect runtime of the OpenGL4
// renderer -- EasyGL's desktop-profile runtime (plans/plan_fx.md FX-062) over this renderer's own
// resources. Built only when CNA_OPENGL4_COMPILED_EFFECTS is on, because MojoShader is a fetched
// dependency this renderer does not otherwise need; the whole translation unit is guarded rather
// than excluded from the source glob, so the renderer's source list stays the plain directory
// contents every other renderer family uses. The renderer-side half (context, draw routes,
// row-order correction, SpriteBatch) is OpenGL4CompiledEffects.cpp.
#if defined(CNA_OPENGL4_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/OpenGL4/OpenGL4CompiledEffect.hpp"

#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/NotSupportedException.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace CNA::Internal::Renderers::OpenGL4
{
    namespace
    {
        /// Same ceiling the shared translation applies to reflected tables.
        constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;

        /// Wires MojoShader's own OpenGL adapter as the backend the effect parser compiles with.
        ///
        /// plans/plan_fx.md FX-062 finding (tools/graphics/mojoshader_gl_probe.cpp): every
        /// MOJOSHADER_gl* function relevant to MOJOSHADER_effectShaderContext omits the leading
        /// context-pointer argument the typedefs declare -- the OpenGL adapter keeps its context as
        /// implicit process-global state (MOJOSHADER_glMakeContextCurrent) -- and
        /// MOJOSHADER_glCompileShader additionally has no `mainfn` parameter. A direct
        /// function-pointer cast into those slots silently misaligns every argument; these
        /// trampolines drop the unused leading argument(s) instead.
        ///
        /// A shader model 1.x pixel shader's parse data can name a sampler dimension other than
        /// the one its constant table declares; the shader is then recompiled with the declared
        /// dimensions as an explicit sampler map, exactly as EasyGL does.
        void* CompileShaderTrampoline(const void* ctx, const char* mainfn,
                                      const unsigned char* tokenbuf, unsigned int bufsize,
                                      const MOJOSHADER_swizzle* swiz, unsigned int swizcount,
                                      const MOJOSHADER_samplerMap* smap, unsigned int smapcount)
        {
            (void) ctx;
            (void) mainfn;
            MOJOSHADER_glShader* shader =
                MOJOSHADER_glCompileShader(tokenbuf, bufsize, swiz, swizcount, smap, smapcount);
            if (shader == nullptr || smapcount != 0)
                return shader;

            const MOJOSHADER_parseData* parseData = MOJOSHADER_glGetShaderParseData(shader);
            if (parseData == nullptr || parseData->error_count != 0 ||
                parseData->shader_type != MOJOSHADER_TYPE_PIXEL || parseData->major_ver != 1)
                return shader;

            std::vector<MOJOSHADER_samplerMap> inferred;
            bool differs = false;
            for (unsigned int symbolIndex = 0; symbolIndex < parseData->symbol_count;
                 ++symbolIndex)
            {
                const MOJOSHADER_symbol& symbol = parseData->symbols[symbolIndex];
                if (symbol.register_set != MOJOSHADER_SYMREGSET_SAMPLER)
                    continue;
                MOJOSHADER_samplerType type = MOJOSHADER_SAMPLER_UNKNOWN;
                if (symbol.info.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER ||
                    symbol.info.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER1D ||
                    symbol.info.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER2D)
                    type = MOJOSHADER_SAMPLER_2D;
                else if (symbol.info.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER3D)
                    type = MOJOSHADER_SAMPLER_VOLUME;
                else if (symbol.info.parameter_type == MOJOSHADER_SYMTYPE_SAMPLERCUBE)
                    type = MOJOSHADER_SAMPLER_CUBE;
                if (type == MOJOSHADER_SAMPLER_UNKNOWN)
                    continue;

                inferred.push_back({static_cast<int>(symbol.register_index), type});
                const MOJOSHADER_sampler* parsedSampler = nullptr;
                for (int samplerIndex = 0; samplerIndex < parseData->sampler_count;
                     ++samplerIndex)
                    if (parseData->samplers[samplerIndex].index ==
                        static_cast<int>(symbol.register_index))
                        parsedSampler = &parseData->samplers[samplerIndex];
                differs = differs || parsedSampler == nullptr || parsedSampler->type != type;
            }
            if (!differs)
                return shader;

            MOJOSHADER_glDeleteShader(shader);
            return MOJOSHADER_glCompileShader(
                tokenbuf, bufsize, swiz, swizcount,
                inferred.data(), static_cast<unsigned int>(inferred.size()));
        }

        void DeleteShaderTrampoline(const void* ctx, void* shader)
        {
            (void) ctx;
            MOJOSHADER_glDeleteShader(static_cast<MOJOSHADER_glShader*>(shader));
        }

        void BindShadersTrampoline(const void* ctx, void* vshader, void* pshader)
        {
            (void) ctx;
            MOJOSHADER_glBindShaders(static_cast<MOJOSHADER_glShader*>(vshader),
                                     static_cast<MOJOSHADER_glShader*>(pshader));
        }

        void GetBoundShadersTrampoline(const void* ctx, void** vshader, void** pshader)
        {
            (void) ctx;
            MOJOSHADER_glGetBoundShaders(reinterpret_cast<MOJOSHADER_glShader**>(vshader),
                                         reinterpret_cast<MOJOSHADER_glShader**>(pshader));
        }

        void MapUniformBufferMemoryTrampoline(const void* ctx, float** vsf, int** vsi,
                                              unsigned char** vsb, float** psf, int** psi,
                                              unsigned char** psb)
        {
            (void) ctx;
            MOJOSHADER_glMapUniformBufferMemory(vsf, vsi, vsb, psf, psi, psb);
        }

        void UnmapUniformBufferMemoryTrampoline(const void* ctx)
        {
            (void) ctx;
            MOJOSHADER_glUnmapUniformBufferMemory();
        }

        const char* GetErrorTrampoline(const void* ctx)
        {
            (void) ctx;
            return MOJOSHADER_glGetError();
        }

        MOJOSHADER_effectShaderContext MakeBackend(MOJOSHADER_glContext* context)
        {
            MOJOSHADER_effectShaderContext backend{};
            backend.shaderContext = context;
            backend.compileShader = CompileShaderTrampoline;
            // shaderAddRef and getParseData are the only two slots whose real GL signature already
            // matches the typedef exactly (neither takes a context argument in either form).
            backend.shaderAddRef = (MOJOSHADER_shaderAddRefFunc) MOJOSHADER_glShaderAddRef;
            backend.deleteShader = DeleteShaderTrampoline;
            backend.getParseData = (MOJOSHADER_getParseDataFunc) MOJOSHADER_glGetShaderParseData;
            backend.bindShaders = BindShadersTrampoline;
            backend.getBoundShaders = GetBoundShadersTrampoline;
            backend.mapUniformBufferMemory = MapUniformBufferMemoryTrampoline;
            backend.unmapUniformBufferMemory = UnmapUniformBufferMemoryTrampoline;
            backend.getError = GetErrorTrampoline;
            return backend;
        }
    }

    // ---- Sampler texture resolution --------------------------------------------------------

    void Detail::CompiledSamplerTexture::BindGL(const int unit) const
    {
        if (cube != nullptr) cube->BindGL(unit);
        else if (volume != nullptr) volume->BindGL(unit);
        else if (texture2D != nullptr) texture2D->BindGL(unit);
    }

    Detail::CompiledSamplerTexture Detail::ResolveCompiledSamplerTexture(Texture* texture)
    {
        CompiledSamplerTexture resolved;
        if (texture == nullptr) return resolved;
        using namespace Microsoft::Xna::Framework::Graphics;
        if (auto* textureCube = dynamic_cast<TextureCube*>(texture))
        {
            ITextureCubeRenderer& renderer = textureCube->GetRenderer();
            if (dynamic_cast<const OpenGL4TextureCubeRenderer*>(&renderer) != nullptr ||
                dynamic_cast<const OpenGL4RenderTargetCubeRenderer*>(&renderer) != nullptr)
            {
                resolved.ownedCube = renderer.shared_from_this();
                resolved.cube = resolved.ownedCube.get();
            }
            return resolved;
        }
        if (auto* texture3D = dynamic_cast<Texture3D*>(texture))
        {
            ITexture3DRenderer& renderer = texture3D->GetRenderer();
            if (dynamic_cast<const OpenGL4Texture3DRenderer*>(&renderer) != nullptr)
            {
                resolved.ownedVolume = renderer.shared_from_this();
                resolved.volume = resolved.ownedVolume.get();
            }
            return resolved;
        }
        if (auto* texture2D = dynamic_cast<Texture2D*>(texture))
        {
            // A RenderTarget2D is a Texture2D whose renderer is an OpenGL4RenderTargetRenderer;
            // both bind their colour texture through BindGL (plans/plan_fx.md FX-099).
            ITextureRenderer& renderer = texture2D->GetRenderer();
            if (dynamic_cast<const OpenGL4TextureRenderer*>(&renderer) != nullptr ||
                dynamic_cast<const OpenGL4RenderTargetRenderer*>(&renderer) != nullptr)
            {
                resolved.ownedTexture2D = renderer.shared_from_this();
                resolved.texture2D = resolved.ownedTexture2D.get();
            }
        }
        return resolved;
    }

    // ---- OpenGL4CompiledEffect -------------------------------------------------------------

    OpenGL4CompiledEffect::OpenGL4CompiledEffect(OpenGL4Renderer& renderer,
                                                 const std::uint8_t* effectCode,
                                                 std::size_t effectCodeLength)
        : renderer_(&renderer)
    {
        if (effectCode == nullptr || effectCodeLength == 0 ||
            effectCodeLength > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument("OpenGL4 compiled effect: invalid bytecode buffer.");
        }
        effectCode_ = std::make_shared<const std::vector<std::uint8_t>>(
            effectCode, effectCode + effectCodeLength);
        CreateNativeEffect();
        textures_.resize(static_cast<std::size_t>(effectData_->param_count), nullptr);
        parameterValues_.resize(static_cast<std::size_t>(effectData_->param_count));
        renderer_->RegisterCompiledEffectEXT(this);
    }

    OpenGL4CompiledEffect::OpenGL4CompiledEffect(OpenGL4Renderer& renderer,
                                                 const OpenGL4CompiledEffect& cloneSource)
        : renderer_(&renderer)
        , effectCode_(cloneSource.effectCode_)
        , parameterValues_(cloneSource.parameterValues_)
        , techniqueIndex_(cloneSource.techniqueIndex_)
    {
        CreateNativeEffect();
        try
        {
            textures_ = cloneSource.textures_;
            boundTextures_ = cloneSource.boundTextures_;
            boundVertexTextures_ = cloneSource.boundVertexTextures_;
            boundTexture2DResources_ = cloneSource.boundTexture2DResources_;
            boundTexture3DResources_ = cloneSource.boundTexture3DResources_;
            boundTextureCubeResources_ = cloneSource.boundTextureCubeResources_;
            boundVertexTexture2DResources_ = cloneSource.boundVertexTexture2DResources_;
            boundVertexTexture3DResources_ = cloneSource.boundVertexTexture3DResources_;
            boundVertexTextureCubeResources_ = cloneSource.boundVertexTextureCubeResources_;
            boundSamplers_ = cloneSource.boundSamplers_;
            boundVertexSamplers_ = cloneSource.boundVertexSamplers_;
            samplerAssigned_ = cloneSource.samplerAssigned_;
            vertexSamplerAssigned_ = cloneSource.vertexSamplerAssigned_;
            for (std::size_t index = 0; index < parameterValues_.size(); ++index)
            {
                const auto& value = parameterValues_[index];
                if (!value.empty())
                    SetParameterValue(static_cast<std::uint32_t>(index), value.data(), value.size());
            }
            SetTechnique(techniqueIndex_);
            renderer_->RegisterCompiledEffectEXT(this);
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

    OpenGL4CompiledEffect::~OpenGL4CompiledEffect()
    {
        if (renderer_ == nullptr)
            return;  // The renderer died first and already released the native effect.
        renderer_->UnregisterCompiledEffectEXT(this);
        if (effectData_ != nullptr)
        {
            renderer_->MakeMojoShaderContextCurrentEXT();
            if (passActive_) MOJOSHADER_effectEndPass(effectData_);
            if (MojoShaderEffect::CanSafelyDeleteNativeEffect(effectData_))
                MOJOSHADER_deleteEffect(effectData_);
            effectData_ = nullptr;
        }
    }

    void OpenGL4CompiledEffect::CreateNativeEffect()
    {
        MOJOSHADER_glContext* context = renderer_->GetMojoShaderContextEXT();
        if (context == nullptr)
        {
            throw std::runtime_error(
                "OpenGL4 compiled effect: MojoShader has no context for this device.");
        }

        renderer_->MakeMojoShaderContextCurrentEXT();
        MOJOSHADER_effectShaderContext backend = MakeBackend(context);
        effectData_ = MOJOSHADER_compileEffect(
            effectCode_->data(), static_cast<unsigned int>(effectCode_->size()),
            nullptr, 0, nullptr, 0, &backend);
        try
        {
            MojoShaderEffect::ValidateNativeEffect(effectData_, "create");
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

    void OpenGL4CompiledEffect::RequireNativeEffect() const
    {
        if (effectData_ == nullptr)
        {
            throw std::runtime_error(
                "OpenGL4 compiled effect: the graphics device that created this effect has been "
                "destroyed.");
        }
    }

    void OpenGL4CompiledEffect::DetachFromRenderer()
    {
        // Desktop OpenGL 4 has no context loss, so this is not EasyGL's release-and-recreate pair:
        // it runs once, from a renderer destroyed before this effect (the CNAEXT engine layer, for
        // one, holds SpriteBatch-owned effects by shared_ptr past the device). Deleting the native
        // effect later would address a MojoShader context that no longer exists.
        if (effectData_ != nullptr)
        {
            if (passActive_) MOJOSHADER_effectEndPass(effectData_);
            if (MojoShaderEffect::CanSafelyDeleteNativeEffect(effectData_))
                MOJOSHADER_deleteEffect(effectData_);
        }
        passActive_ = false;
        effectData_ = nullptr;
        renderer_ = nullptr;
    }

    std::unique_ptr<ICompiledEffectRuntime> OpenGL4CompiledEffect::Clone() const
    {
        RequireNativeEffect();
        return std::unique_ptr<ICompiledEffectRuntime>(
            new OpenGL4CompiledEffect(*renderer_, *this));
    }

    const CompiledEffectDescription& OpenGL4CompiledEffect::GetDescription() const
    {
        return description_;
    }

    void OpenGL4CompiledEffect::SetTechnique(std::uint32_t techniqueIndex)
    {
        RequireNativeEffect();
        if (techniqueIndex >= static_cast<std::uint32_t>(effectData_->technique_count))
            throw std::out_of_range("OpenGL4 compiled effect: technique index is out of range.");
        techniqueIndex_ = techniqueIndex;
        MOJOSHADER_effectSetTechnique(effectData_, &effectData_->techniques[techniqueIndex]);
    }

    void OpenGL4CompiledEffect::SetParameterValue(std::uint32_t runtimeIndex, const void* data,
                                                  std::size_t dataBytes)
    {
        RequireNativeEffect();
        if (runtimeIndex >= static_cast<std::uint32_t>(effectData_->param_count))
            throw std::out_of_range("OpenGL4 compiled effect: parameter index is out of range.");
        MOJOSHADER_effectParam& parameter = effectData_->params[runtimeIndex];
        const std::size_t capacity = static_cast<std::size_t>(parameter.value.value_count) * 4;
        if (dataBytes > capacity)
            throw std::invalid_argument("OpenGL4 compiled effect: parameter value is too large.");
        if (dataBytes > 0 && data == nullptr)
            throw std::invalid_argument("OpenGL4 compiled effect: parameter data is null.");
        if (dataBytes > 0)
        {
            MOJOSHADER_effectSetRawValueHandle(&parameter, data, 0,
                                               static_cast<unsigned int>(dataBytes));
            auto& saved = parameterValues_[runtimeIndex];
            saved.assign(static_cast<const std::uint8_t*>(data),
                         static_cast<const std::uint8_t*>(data) + dataBytes);
        }
    }

    void OpenGL4CompiledEffect::SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture)
    {
        RequireNativeEffect();
        if (runtimeIndex >= textures_.size())
        {
            throw std::out_of_range(
                "OpenGL4 compiled effect: texture parameter index is out of range.");
        }
        const auto parameterType = static_cast<std::underlying_type_t<MOJOSHADER_symbolType>>(
            effectData_->params[runtimeIndex].value.type.parameter_type);
        if (parameterType < MOJOSHADER_SYMTYPE_TEXTURE ||
            parameterType > MOJOSHADER_SYMTYPE_TEXTURECUBE)
        {
            throw std::invalid_argument("OpenGL4 compiled effect: parameter is not a texture.");
        }
        if (texture != nullptr && !Detail::ResolveCompiledSamplerTexture(texture).Resolved())
        {
            throw std::invalid_argument(
                "OpenGL4 compiled effect: texture was not created by the active OpenGL4 "
                "renderer.");
        }
        textures_[runtimeIndex] = texture;
    }

    void OpenGL4CompiledEffect::ApplyPass(std::uint32_t passIndex,
                                          const CompiledEffectDeviceState& deviceState,
                                          CompiledEffectPassStateChanges& changes)
    {
        RequireNativeEffect();
        const MOJOSHADER_effectTechnique& technique = effectData_->techniques[techniqueIndex_];
        if (passIndex >= technique.pass_count)
            throw std::out_of_range("OpenGL4 compiled effect: pass index is out of range.");

        renderer_->MakeMojoShaderContextCurrentEXT();

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

        // plans/plan_fx.md FX-129: MOJOSHADER_glBindShaders returns SILENTLY when a pair fails to
        // link, leaving the PREVIOUS program bound, and nothing downstream can tell -- the draw
        // would validate this draw's vertex declaration against an unrelated effect's shader, or
        // silently draw the wrong program's pixels. Compare what the pass selected with what is
        // bound, here, where the failing pass is still known. A pass with no vertex shader of its
        // own is expected to fail to link ("program lacks a vertex shader"): leaving the previous
        // program current is exactly how such a pass inherits a vertex shader (FX-128). Only a
        // pass that offered MojoShader a complete pair is checked.
        MOJOSHADER_glShader* boundVertexShader = nullptr;
        MOJOSHADER_glShader* boundPixelShader = nullptr;
        MOJOSHADER_glGetBoundShaders(&boundVertexShader, &boundPixelShader);
        if ((effectData_->current_vert != nullptr) && (effectData_->current_pixl != nullptr) &&
            (static_cast<const void*>(boundVertexShader) != effectData_->current_vert ||
             static_cast<const void*>(boundPixelShader) != effectData_->current_pixl))
        {
            const char* const linkError = MOJOSHADER_glGetError();
            throw System::NotSupportedException(
                std::string("CNA OpenGL4: this compiled effect's pass could not be made current "
                            "-- linking its vertex and pixel shader pair failed, so the previously "
                            "bound program is still the current one. GL reported: ") +
                ((linkError != nullptr && linkError[0] != '\0') ? linkError : "(no message)"));
        }

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
                "OpenGL4 compiled effect: native pass state changes exceed the safety limit.");
        }

        MojoShaderEffect::TranslateRenderStates(stateChanges_, deviceState, changes);

        MojoShaderEffect::TranslateSamplers(
            stateChanges_.sampler_state_changes, stateChanges_.sampler_state_change_count,
            /*vertexStage=*/false, kSlots, samplerTextureParameters_, textures_,
            deviceState, changes);
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.vertex_sampler_state_changes,
            stateChanges_.vertex_sampler_state_change_count,
            /*vertexStage=*/true, kSlots, samplerTextureParameters_, textures_,
            deviceState, changes);
        MojoShaderEffect::TranslateLegacySamplerAssignments(
            effectData_, stateChanges_, kSlots, samplerTextureParameters_, textures_,
            deviceState, changes);
        for (const auto& change : changes.legacyBumpMapEnvs)
        {
            auto& state = renderer_->compiledLegacyBumpMapEnvs_[change.slot];
            for (std::size_t component = 0; component < state.matrix.size(); ++component)
                if ((change.assignedMask & (1u << component)) != 0u)
                    state.matrix[component] = change.state.matrix[component];
            if ((change.assignedMask & (1u << 4u)) != 0u)
                state.luminanceScale = change.state.luminanceScale;
            if ((change.assignedMask & (1u << 5u)) != 0u)
                state.luminanceOffset = change.state.luminanceOffset;
        }

        // Fold this pass's assignments into the persistent per-slot state the draw route reads
        // through GetBoundSamplerEXT. A slot this pass does not touch keeps whatever an earlier
        // pass assigned, so only the entries this pass reported are written.
        for (const auto& sampler : changes.samplers)
        {
            if (sampler.slot >= kSlots) continue;
            auto& textureSlot = sampler.vertexStage ? boundVertexTextures_ : boundTextures_;
            auto& texture2DResourceSlot = sampler.vertexStage
                ? boundVertexTexture2DResources_ : boundTexture2DResources_;
            auto& texture3DResourceSlot = sampler.vertexStage
                ? boundVertexTexture3DResources_ : boundTexture3DResources_;
            auto& textureCubeResourceSlot = sampler.vertexStage
                ? boundVertexTextureCubeResources_ : boundTextureCubeResources_;
            auto& samplerSlot = sampler.vertexStage ? boundVertexSamplers_ : boundSamplers_;
            auto& assignedSlot = sampler.vertexStage ? vertexSamplerAssigned_ : samplerAssigned_;
            if (sampler.textureChanged)
            {
                const Detail::CompiledSamplerTexture nativeTexture =
                    Detail::ResolveCompiledSamplerTexture(sampler.texture);
                if (!nativeTexture.Resolved())
                {
                    throw std::runtime_error(
                        "OpenGL4 compiled effect: an applied texture assignment no longer "
                        "resolves to this graphics device.");
                }
                textureSlot[sampler.slot] = sampler.texture;
                texture2DResourceSlot[sampler.slot] = nativeTexture.ownedTexture2D;
                texture3DResourceSlot[sampler.slot] = nativeTexture.ownedVolume;
                textureCubeResourceSlot[sampler.slot] = nativeTexture.ownedCube;
            }
            if (sampler.samplerChanged)
            {
                samplerSlot[sampler.slot] = sampler.sampler;
                assignedSlot[sampler.slot] = true;
            }
        }

        // Native texture and sampler binding does not happen here: this renderer draws
        // immediately, so the draw route binds both itself right before issuing the draw
        // (OpenGL4Renderer::BindCompiledEffectForDrawEXT).
    }

    void OpenGL4CompiledEffect::GetBoundSamplerEXT(
        std::uint32_t slot, bool vertexStage, Texture*& texture,
        Microsoft::Xna::Framework::Graphics::SamplerState& sampler, bool& samplerAssigned) const
    {
        if (slot >= kSlots)
        {
            texture = nullptr;
            samplerAssigned = false;
            return;
        }
        texture = vertexStage ? boundVertexTextures_[slot] : boundTextures_[slot];
        sampler = vertexStage ? boundVertexSamplers_[slot] : boundSamplers_[slot];
        samplerAssigned = vertexStage ? vertexSamplerAssigned_[slot] : samplerAssigned_[slot];
    }
}

#endif  // CNA_OPENGL4_COMPILED_EFFECTS
