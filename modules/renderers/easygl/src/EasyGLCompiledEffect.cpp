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
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace CNA::Internal::Renderers::EasyGL
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

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

        MOJOSHADER_usage ToMojoShaderUsage(VertexElementUsage usage)
        {
            switch (usage)
            {
                case VertexElementUsage::Position:          return MOJOSHADER_USAGE_POSITION;
                case VertexElementUsage::Color:              return MOJOSHADER_USAGE_COLOR;
                case VertexElementUsage::TextureCoordinate:  return MOJOSHADER_USAGE_TEXCOORD;
                case VertexElementUsage::Normal:             return MOJOSHADER_USAGE_NORMAL;
                case VertexElementUsage::Binormal:           return MOJOSHADER_USAGE_BINORMAL;
                case VertexElementUsage::Tangent:            return MOJOSHADER_USAGE_TANGENT;
                case VertexElementUsage::BlendIndices:       return MOJOSHADER_USAGE_BLENDINDICES;
                case VertexElementUsage::BlendWeight:        return MOJOSHADER_USAGE_BLENDWEIGHT;
                case VertexElementUsage::Depth:              return MOJOSHADER_USAGE_DEPTH;
                case VertexElementUsage::Fog:                return MOJOSHADER_USAGE_FOG;
                case VertexElementUsage::PointSize:          return MOJOSHADER_USAGE_POINTSIZE;
                case VertexElementUsage::Sample:             return MOJOSHADER_USAGE_SAMPLE;
                case VertexElementUsage::TessellateFactor:   return MOJOSHADER_USAGE_TESSFACTOR;
            }
            throw std::invalid_argument(
                "EasyGL compiled effect: unrecognized VertexElementUsage ordinal " +
                std::to_string(static_cast<int>(usage)));
        }

        /// (component count, whether the wire format needs a float upconvert, normalized) for one
        /// XNA VertexElementFormat's MOJOSHADER_glSetVertexAttribute call.
        struct GlAttributeFormat
        {
            unsigned int size = 0;
            MOJOSHADER_attributeType type = MOJOSHADER_ATTRIBUTE_FLOAT;
            int normalized = 0;
        };

        GlAttributeFormat ToGlAttributeFormat(VertexElementFormat format)
        {
            switch (format)
            {
                case VertexElementFormat::Single:  return {1, MOJOSHADER_ATTRIBUTE_FLOAT, 0};
                case VertexElementFormat::Vector2: return {2, MOJOSHADER_ATTRIBUTE_FLOAT, 0};
                case VertexElementFormat::Vector3: return {3, MOJOSHADER_ATTRIBUTE_FLOAT, 0};
                case VertexElementFormat::Vector4: return {4, MOJOSHADER_ATTRIBUTE_FLOAT, 0};
                case VertexElementFormat::Color:   return {4, MOJOSHADER_ATTRIBUTE_UBYTE, 1};
                case VertexElementFormat::Byte4:   return {4, MOJOSHADER_ATTRIBUTE_UBYTE, 0};
                case VertexElementFormat::Short2:  return {2, MOJOSHADER_ATTRIBUTE_SHORT, 0};
                case VertexElementFormat::Short4:  return {4, MOJOSHADER_ATTRIBUTE_SHORT, 0};
                case VertexElementFormat::NormalizedShort2:
                    return {2, MOJOSHADER_ATTRIBUTE_SHORT, 1};
                case VertexElementFormat::NormalizedShort4:
                    return {4, MOJOSHADER_ATTRIBUTE_SHORT, 1};
                case VertexElementFormat::HalfVector2:
                    return {2, MOJOSHADER_ATTRIBUTE_HALF_FLOAT, 0};
                case VertexElementFormat::HalfVector4:
                    return {4, MOJOSHADER_ATTRIBUTE_HALF_FLOAT, 0};
            }
            throw std::invalid_argument(
                "EasyGL compiled effect: unrecognized VertexElementFormat ordinal " +
                std::to_string(static_cast<int>(format)));
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
            auto& assignedSlot = sampler.vertexStage ? vertexSamplerAssigned_ : samplerAssigned_;
            if (sampler.textureChanged) textureSlot[sampler.slot] = sampler.texture;
            if (sampler.samplerChanged)
            {
                samplerSlot[sampler.slot] = sampler.sampler;
                assignedSlot[sampler.slot] = true;
            }
        }

        // Native sampler/texture binding does not happen here. This renderer draws immediately
        // (no deferred command queue), so the draw route binds textures and samplers itself, right
        // before issuing the draw, through GetBoundSamplerEXT above -- see EasyGLRenderer.cpp's
        // compiled-effect branch of DrawPrimitivesEx/DrawIndexedPrimitivesEx.
    }

    void EasyGLCompiledEffect::GetBoundSamplerEXT(
        std::uint32_t slot, bool vertexStage, Texture*& texture,
        Microsoft::Xna::Framework::Graphics::SamplerState& sampler, bool& samplerAssigned) const
    {
        using Microsoft::Xna::Framework::Graphics::SamplerStateCollection;
        if (slot >= static_cast<std::uint32_t>(SamplerStateCollection::MaxSamplers))
        {
            texture = nullptr;
            samplerAssigned = false;
            return;
        }
        texture = vertexStage ? boundVertexTextures_[slot] : boundTextures_[slot];
        sampler = vertexStage ? boundVertexSamplers_[slot] : boundSamplers_[slot];
        samplerAssigned = vertexStage ? vertexSamplerAssigned_[slot] : samplerAssigned_[slot];
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

    ::easygl::VertexArray& EasyGLRenderer::EnsureCompiledEffectVaoEXT()
    {
        if (!compiledEffectVaoCreated_)
        {
            compiledEffectVao_.create();
            compiledEffectVaoCreated_ = true;
        }
        return compiledEffectVao_;
    }

    void EasyGLRenderer::BindCompiledEffectForDrawEXT(
        const CompiledEffectStreamEXT* streams, std::size_t streamCount,
        ICompiledEffectRuntime& runtime, const ITextureRenderer* spriteBatchSlotZeroTexture)
    {
        auto* effect = dynamic_cast<EasyGLCompiledEffect*>(&runtime);
        if (effect == nullptr)
        {
            throw std::runtime_error(
                "EasyGL compiled effect: the applied effect was not created by this renderer.");
        }
        if (streams == nullptr || streamCount == 0)
        {
            throw std::runtime_error(
                "EasyGL compiled effect: a compiled-effect draw needs at least one vertex stream.");
        }

        MOJOSHADER_glShader* vertexShader = nullptr;
        MOJOSHADER_glShader* pixelShader = nullptr;
        MOJOSHADER_glGetBoundShaders(&vertexShader, &pixelShader);
        if (vertexShader == nullptr || pixelShader == nullptr)
        {
            throw std::runtime_error(
                "EasyGL compiled effect: the applied pass bound no shader pair.");
        }

        const MOJOSHADER_parseData* vertexParseData = MOJOSHADER_glGetShaderParseData(vertexShader);
        const MOJOSHADER_parseData* pixelParseData = MOJOSHADER_glGetShaderParseData(pixelShader);
        if (vertexParseData == nullptr || pixelParseData == nullptr)
        {
            throw std::runtime_error(
                "EasyGL compiled effect: the applied pass's shaders have no reflection.");
        }

        // plan_fx.md FX-062, matching FX-071's own scope: a compiled effect's vertex shader
        // sampling a texture is refused explicitly rather than silently mishandled -- this
        // renderer's compiled-effect draw route does not bind vertex-stage sampler textures.
        if (vertexParseData->sampler_count > 0)
        {
            throw System::NotSupportedException(
                "CNA EasyGL: this compiled effect's vertex shader samples a texture, which this "
                "renderer's compiled-effect draw route does not yet support.");
        }

        // Vertex attributes: MOJOSHADER_glSetVertexAttribute no-ops for an attribute the bound
        // program's vertex shader does not use, so every declared element could be bound
        // unconditionally -- but a shader input no bound stream supplies has to fail loudly
        // rather than sample stale/undefined vertex data.
        //
        // plan_fx.md FX-082: the search runs across every bound stream, and each match is bound
        // from ITS OWN buffer, at ITS OWN stride, from ITS OWN VertexOffset. MojoShader's OpenGL
        // adapter calls glVertexAttribPointer immediately (not deferred to ProgramReady), so the
        // array buffer bound at this moment is what each attribute captures -- which is exactly
        // what makes a genuine multi-stream compiled-effect draw expressible here.
        struct BoundAttribute
        {
            MOJOSHADER_usage usage;
            int index;
            unsigned int divisor;
        };
        std::vector<BoundAttribute> boundAttributes;
        boundAttributes.reserve(static_cast<std::size_t>(std::max(vertexParseData->attribute_count, 0)));

        for (int i = 0; i < vertexParseData->attribute_count; ++i)
        {
            const MOJOSHADER_attribute& shaderInput = vertexParseData->attributes[i];
            const VertexElement* match = nullptr;
            const CompiledEffectStreamEXT* matchStream = nullptr;
            for (std::size_t s = 0; s < streamCount && match == nullptr; ++s)
            {
                const CompiledEffectStreamEXT& stream = streams[s];
                if (stream.buffer == nullptr) continue;
                for (const VertexElement& element : stream.buffer->GetDeclarationElements())
                {
                    if (ToMojoShaderUsage(element.getVertexElementUsageProperty()) == shaderInput.usage &&
                        element.getUsageIndexProperty() == shaderInput.index)
                    {
                        match = &element;
                        matchStream = &stream;
                        break;
                    }
                }
            }
            if (match == nullptr)
            {
                const char* name = shaderInput.name != nullptr ? shaderInput.name : "<unnamed>";
                throw System::NotSupportedException(
                    "CNA EasyGL: this compiled effect's vertex shader requires attribute '" +
                    std::string(name) + "' (usage " + std::to_string(static_cast<int>(shaderInput.usage)) +
                    ", index " + std::to_string(shaderInput.index) +
                    "), but none of the " + std::to_string(streamCount) +
                    " vertex stream(s) supplied to this draw declares an element with that usage "
                    "and usage index.");
            }

            const GlAttributeFormat glFormat = ToGlAttributeFormat(match->getVertexElementFormatProperty());
            const void* offset = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(matchStream->baseByteOffset) +
                static_cast<std::uintptr_t>(match->getOffsetProperty()));
            matchStream->buffer->vbo.bind(::easygl::BufferTarget::Array);
            MOJOSHADER_glSetVertexAttribute(
                shaderInput.usage, shaderInput.index, glFormat.size, glFormat.type,
                glFormat.normalized, static_cast<unsigned int>(matchStream->stride), offset);
            boundAttributes.push_back(
                BoundAttribute{shaderInput.usage, shaderInput.index,
                               matchStream->instanceFrequency});
        }

        // Pixel-stage sampler textures: every reflected sampler must have a texture bound (a
        // reflected-but-unbound slot is a real error, not a "sample black" default), and 3D/cube
        // textures are refused here for the same reason vertex-stage sampling is above -- the
        // resolver only recognizes a 2D texture's own renderer identity.
        for (int i = 0; i < pixelParseData->sampler_count; ++i)
        {
            const MOJOSHADER_sampler& sampler = pixelParseData->samplers[i];
            Texture* texture = nullptr;
            Microsoft::Xna::Framework::Graphics::SamplerState samplerState;
            bool samplerAssigned = false;
            effect->GetBoundSamplerEXT(static_cast<std::uint32_t>(sampler.index), /*vertexStage=*/false,
                                       texture, samplerState, samplerAssigned);
            const ITextureRenderer* nativeTexture = AsEasyGLTexture(texture);
            // plan_fx.md FX-080: SpriteBatch overwrites slot 0 with the drawn texture after the
            // effect's pass applies, exactly as FNA's SpriteBatch.DrawPrimitives does with
            // GraphicsDevice.Textures[0] -- unconditionally, whatever the effect bound there.
            if (sampler.index == 0 && spriteBatchSlotZeroTexture != nullptr)
                nativeTexture = spriteBatchSlotZeroTexture;
            if (nativeTexture == nullptr)
            {
                throw System::NotSupportedException(
                    "CNA EasyGL: this compiled effect's pixel shader samples slot " +
                    std::to_string(sampler.index) + " ('" +
                    (sampler.name != nullptr ? sampler.name : "<unnamed>") +
                    "'), but no 2D texture is bound there (an unbound slot, or a 3D/cube texture, "
                    "which this renderer's compiled-effect draw route does not yet support).");
            }
            nativeTexture->BindGL(sampler.index);
            // plan_fx.md FX-083: the pass's own sampler_state block reaches the GPU here. Before
            // this, only the texture object's creation-time GL filter/wrap applied and an
            // effect-declared MinFilter/AddressU/MaxAnisotropy was silently ignored at draw time
            // even though it was published correctly on GraphicsDevice.SamplerStates. A slot no
            // pass has assigned keeps whatever the game (or SpriteBatch.Begin) selected.
            if (samplerAssigned)
            {
                ApplySamplerState(static_cast<int>(sampler.index),
                                  static_cast<int>(samplerState.getFilterProperty()),
                                  static_cast<int>(samplerState.getAddressUProperty()),
                                  static_cast<int>(samplerState.getAddressVProperty()),
                                  samplerState.getMaxAnisotropyProperty());
                ApplySamplerAddressW(static_cast<int>(sampler.index),
                                     static_cast<int>(samplerState.getAddressWProperty()));
                ApplySamplerMipState(static_cast<int>(sampler.index),
                                     samplerState.getMaxMipLevelProperty(),
                                     samplerState.getMipMapLevelOfDetailBiasProperty());
            }
        }

        // Pushes uniforms from the shared register files (already populated by ApplyPass()'s
        // internal MOJOSHADER_effectCommitChanges call) into the bound program, and enables the
        // vertex attribute arrays MOJOSHADER_glSetVertexAttribute flagged above.
        MOJOSHADER_glProgramReady();

        // plan_fx.md FX-082: instance step rates. MOJOSHADER_glSetVertexAttribDivisor asserts on
        // ctx->have_GL_ARB_instanced_arrays, which the pinned adapter only sets for desktop GL
        // 3.3+ or an explicit GL_ARB_instanced_arrays string -- never for an OpenGL ES 3 context,
        // where the entry point is core under a different name. The public
        // MOJOSHADER_glGetVertexAttribLocation gives the same answer without that gate, so the
        // divisor is set through this renderer's own array object. It is written for EVERY bound
        // attribute, including the per-vertex ones: the compiled-effect array object is shared by
        // all compiled draws, so a divisor left over from a previous instanced draw would
        // otherwise still be in force.
        for (const BoundAttribute& attribute : boundAttributes)
        {
            const int location =
                MOJOSHADER_glGetVertexAttribLocation(attribute.usage, attribute.index);
            if (location >= 0)
                compiledEffectVao_.set_attribute_divisor(
                    static_cast<unsigned int>(location), attribute.divisor);
        }

        // GL/D3D9 coordinate-system fixups the generated GLSL applies via injected uniforms
        // (vpFlip, and a D3D9 [0,1] -> GL [-1,1] depth remap): unlike the Effect Framework's
        // ordinary register-file uniforms, these are NOT populated by MOJOSHADER_effectCommitChanges
        // at all -- they need this separate call, after ProgramReady, every time the render target
        // could have changed (tools/graphics/mojoshader_gl_probe.cpp's existence-gate finding).
        //
        // plan_fx.md FX-088: `renderTargetBound` is reported as 0 even when one IS bound, and that
        // is deliberate. The flag makes MojoShader negate `gl_Position.y`, which is how FNA3D's
        // OpenGL driver emulates Direct3D 9's top-down render targets -- and FNA3D pairs it with an
        // inverted front face so winding still works out. EasyGL has a different, equally complete
        // orientation model: geometry is never flipped for an FBO, and the bottom-up texel order
        // is corrected where it is observed instead (`uRtFlipV` when sampling a render target,
        // a row flip in `GetData`). Reporting a bound render target here would mirror compiled-
        // effect geometry against every other draw this renderer issues -- which showed up as a
        // SpriteBatch quad culled away entirely, its winding reversed by that negation.
        //
        // The sizes still describe the real target, so `VPOS`'s own flip (vposFlip = (-1, height))
        // converts `gl_FragCoord.y` using the height actually being rendered into.
        int targetW = 0, targetH = 0;
        if (!GetCurrentRenderTarget2DSize(targetW, targetH))
            getPhysicalSize(targetW, targetH);
        MOJOSHADER_glProgramViewportInfo(targetW, targetH, targetW, targetH,
                                         /*renderTargetBound=*/0);
    }
}

#endif  // CNA_EASYGL_COMPILED_EFFECTS
