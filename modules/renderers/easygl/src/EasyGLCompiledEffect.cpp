// SPDX-License-Identifier: MS-PL
//
// plan_fx.md FX-062: built only when CNA_EASYGL_COMPILED_EFFECTS is on, because MojoShader is a
// fetched dependency this renderer does not otherwise need. The whole translation unit is guarded
// rather than excluded from the source glob, so the renderer's source list stays the plain
// directory contents every other renderer family uses.
#if defined(CNA_EASYGL_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/EasyGL/EasyGLCompiledEffect.hpp"

#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace CNA::Internal::Renderers::EasyGL
{
    namespace
    {
        /// Same ceiling the shared translation applies to reflected tables.
        constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;

        /// Resolves a public texture to the EasyGL resource behind it, or null if it is not one.
        const ITextureRenderer* AsEasyGLTexture(Texture* texture)
        {
            if (texture == nullptr) return nullptr;
            using namespace Microsoft::Xna::Framework::Graphics;
            if (auto* texture2D = dynamic_cast<Texture2D*>(texture))
                return dynamic_cast<const EasyGLTextureRenderer*>(&texture2D->GetRenderer());
            // plan_fx.md FX-062, matching FX-071's own scope: 3D/cube sampler binding for a
            // compiled effect is refused explicitly at draw time rather than silently mishandled,
            // so this resolver only needs to recognize a 2D texture's own renderer identity here.
            return nullptr;
        }

        /// Wires MojoShader's own OpenGL adapter as the backend the effect parser compiles with.
        ///
        /// plan_fx.md FX-062 finding (tools/graphics/mojoshader_gl_probe.cpp): unlike the SDL_GPU/
        /// D3D11 adapters, every MOJOSHADER_gl* function relevant to MOJOSHADER_effectShaderContext
        /// omits the leading context-pointer argument the typedefs declare -- the OpenGL adapter
        /// keeps its context as implicit thread-local state (MOJOSHADER_glMakeContextCurrent), not
        /// an explicit per-call parameter, and MOJOSHADER_glCompileShader additionally has no
        /// `mainfn` parameter at all. A direct function-pointer cast into those slots silently
        /// misaligns every argument; these trampolines drop the unused leading argument(s) instead.
        void* CompileShaderTrampoline(const void* ctx, const char* mainfn,
                                      const unsigned char* tokenbuf, unsigned int bufsize,
                                      const MOJOSHADER_swizzle* swiz, unsigned int swizcount,
                                      const MOJOSHADER_samplerMap* smap, unsigned int smapcount)
        {
            (void) ctx;
            (void) mainfn;
            return MOJOSHADER_glCompileShader(tokenbuf, bufsize, swiz, swizcount, smap, smapcount);
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

        /// MOJOSHADER_glGetProcAddress's signature carries an opaque `data` parameter alongside the
        /// function name; this is where that pointer becomes useful: it lets the trampoline forward
        /// to EasyGLRenderer's own loader (CNA::Platform::GlProcAddressLoader, a plain
        /// void*(*)(const char*), one parameter narrower) without any static/global capture, and
        /// without needing EasyGLPlatformContext's full definition here at all.
        void* GlProcAddressTrampoline(const char* fnname, void* data)
        {
            auto loader = reinterpret_cast<CNA::Platform::GlProcAddressLoader>(data);
            return loader != nullptr ? loader(fnname) : nullptr;
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

    EasyGLCompiledEffect::EasyGLCompiledEffect(EasyGLRenderer& renderer,
                                               const std::uint8_t* effectCode,
                                               std::size_t effectCodeLength)
        : renderer_(renderer)
        , context_(renderer.GetMojoShaderContextEXT())
    {
        if (effectCode == nullptr || effectCodeLength == 0 ||
            effectCodeLength > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument("EasyGL compiled effect: invalid bytecode buffer.");
        }
        if (context_ == nullptr)
        {
            throw std::runtime_error(
                "EasyGL compiled effect: MojoShader has no context for this device.");
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

    EasyGLCompiledEffect::EasyGLCompiledEffect(EasyGLRenderer& renderer,
                                               const EasyGLCompiledEffect& cloneSource)
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

    EasyGLCompiledEffect::~EasyGLCompiledEffect()
    {
        if (effectData_ != nullptr)
        {
            if (passActive_) MOJOSHADER_effectEndPass(effectData_);
            if (MojoShaderEffect::CanSafelyDeleteNativeEffect(effectData_))
                MOJOSHADER_deleteEffect(effectData_);
            effectData_ = nullptr;
        }
    }

    std::unique_ptr<ICompiledEffectRuntime> EasyGLCompiledEffect::Clone() const
    {
        return std::unique_ptr<ICompiledEffectRuntime>(
            new EasyGLCompiledEffect(renderer_, *this));
    }

    const CompiledEffectDescription& EasyGLCompiledEffect::GetDescription() const
    {
        return description_;
    }

    void EasyGLCompiledEffect::SetTechnique(std::uint32_t techniqueIndex)
    {
        if (techniqueIndex >= static_cast<std::uint32_t>(effectData_->technique_count))
        {
            throw std::out_of_range("EasyGL compiled effect: technique index is out of range.");
        }
        techniqueIndex_ = techniqueIndex;
        MOJOSHADER_effectSetTechnique(effectData_, &effectData_->techniques[techniqueIndex]);
    }

    void EasyGLCompiledEffect::SetParameterValue(std::uint32_t runtimeIndex, const void* data,
                                                 std::size_t dataBytes)
    {
        if (runtimeIndex >= static_cast<std::uint32_t>(effectData_->param_count))
            throw std::out_of_range("EasyGL compiled effect: parameter index is out of range.");
        MOJOSHADER_effectParam& parameter = effectData_->params[runtimeIndex];
        const std::size_t capacity = static_cast<std::size_t>(parameter.value.value_count) * 4;
        if (dataBytes > capacity)
            throw std::invalid_argument("EasyGL compiled effect: parameter value is too large.");
        if (dataBytes > 0 && data == nullptr)
            throw std::invalid_argument("EasyGL compiled effect: parameter data is null.");
        if (dataBytes > 0)
        {
            MOJOSHADER_effectSetRawValueHandle(&parameter, data, 0,
                                               static_cast<unsigned int>(dataBytes));
        }
    }

    void EasyGLCompiledEffect::SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture)
    {
        if (runtimeIndex >= textures_.size())
        {
            throw std::out_of_range(
                "EasyGL compiled effect: texture parameter index is out of range.");
        }
        const auto parameterType = static_cast<std::underlying_type_t<MOJOSHADER_symbolType>>(
            effectData_->params[runtimeIndex].value.type.parameter_type);
        if (parameterType < MOJOSHADER_SYMTYPE_TEXTURE ||
            parameterType > MOJOSHADER_SYMTYPE_TEXTURECUBE)
        {
            throw std::invalid_argument("EasyGL compiled effect: parameter is not a texture.");
        }
        if (texture != nullptr && AsEasyGLTexture(texture) == nullptr)
        {
            throw std::invalid_argument(
                "EasyGL compiled effect: texture was not created by the active EasyGL renderer, "
                "or is not a 2D texture (3D/cube compiled-effect sampler binding is not yet "
                "supported).");
        }
        textures_[runtimeIndex] = texture;
    }

    void EasyGLCompiledEffect::ApplyPass(std::uint32_t passIndex,
                                         const CompiledEffectDeviceState& deviceState,
                                         CompiledEffectPassStateChanges& changes)
    {
        const MOJOSHADER_effectTechnique& technique = effectData_->techniques[techniqueIndex_];
        if (passIndex >= technique.pass_count)
            throw std::out_of_range("EasyGL compiled effect: pass index is out of range.");

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
                "EasyGL compiled effect: native pass state changes exceed the safety limit.");
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

        // plan_fx.md FX-062: fold this pass's assignments into the persistent per-slot state a
        // draw route reads through GetBoundSamplerEXT. Matches real XNA behavior -- a slot this
        // pass does not touch keeps whatever an earlier pass assigned, so only entries this pass
        // actually reported (samplerChanged/textureChanged) are written here.
        for (const auto& sampler : changes.samplers)
        {
            if (sampler.slot >= maxSlots) continue;
            auto& textureSlot = sampler.vertexStage ? boundVertexTextures_ : boundTextures_;
            auto& samplerSlot = sampler.vertexStage ? boundVertexSamplers_ : boundSamplers_;
            if (sampler.textureChanged) textureSlot[sampler.slot] = sampler.texture;
            if (sampler.samplerChanged) samplerSlot[sampler.slot] = sampler.sampler;
        }

        // Native sampler/texture binding does not happen here. This renderer draws immediately
        // (no deferred command queue), so the draw route binds textures and samplers itself, right
        // before issuing the draw, through GetBoundSamplerEXT above -- see EasyGLRenderer.cpp's
        // compiled-effect branch of DrawPrimitivesEx/DrawIndexedPrimitivesEx.
    }

    void EasyGLCompiledEffect::GetBoundSamplerEXT(
        std::uint32_t slot, bool vertexStage, Texture*& texture,
        Microsoft::Xna::Framework::Graphics::SamplerState& sampler) const
    {
        using Microsoft::Xna::Framework::Graphics::SamplerStateCollection;
        if (slot >= static_cast<std::uint32_t>(SamplerStateCollection::MaxSamplers))
        {
            texture = nullptr;
            return;
        }
        texture = vertexStage ? boundVertexTextures_[slot] : boundTextures_[slot];
        sampler = vertexStage ? boundVertexSamplers_[slot] : boundSamplers_[slot];
    }

    // ---- EasyGLRenderer hooks (plan_fx.md FX-062) ------------------------------------------
    //
    // Defined here rather than in EasyGLRenderer.cpp so the whole compiled-effect surface lives in
    // one guarded translation unit; nothing else in the renderer changes when the option is off.

    MOJOSHADER_glContext* EasyGLRenderer::GetMojoShaderContextEXT()
    {
        if (mojoShaderContext_ != nullptr)
            return mojoShaderContext_;

        // MOJOSHADER_glCreateContext looks up the GL functions it needs itself, through this same
        // loader every other GL call in this renderer resolves through -- passed as `lookup_d` so
        // GlProcAddressTrampoline above can forward to it without a static/global capture.
        // MOJOSHADER_glBestProfile picks the right GLSL/GLSLES/GLSLES3 dialect for whichever of the
        // five CNA_GL_PROFILE_* identities actually created this GL context, rather than hard-
        // coding one -- confirmed correct for GLES3 by tools/graphics/mojoshader_gl_probe.cpp's
        // existence gate.
        void* loaderData = reinterpret_cast<void*>(GetProcAddressLoaderEXT());
        const char* profile = MOJOSHADER_glBestProfile(
            GlProcAddressTrampoline, loaderData, nullptr, nullptr, nullptr);
        if (profile == nullptr)
            return nullptr;
        mojoShaderContext_ = MOJOSHADER_glCreateContext(
            profile, GlProcAddressTrampoline, loaderData, nullptr, nullptr, nullptr);
        if (mojoShaderContext_ != nullptr)
            MOJOSHADER_glMakeContextCurrent(mojoShaderContext_);
        return mojoShaderContext_;
    }

    std::unique_ptr<ICompiledEffectRuntime> EasyGLRenderer::CreateCompiledEffect(
        const std::uint8_t* effectCode, std::size_t effectCodeBytes)
    {
        return std::make_unique<EasyGLCompiledEffect>(*this, effectCode, effectCodeBytes);
    }
}

#endif  // CNA_EASYGL_COMPILED_EFFECTS
