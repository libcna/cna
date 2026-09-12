// SPDX-License-Identifier: MS-PL

#if defined(CNA_RLGL_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/Rlgl/RlglCompiledEffect.hpp"

#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/NotSupportedException.hpp"

#include "RlglResources.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace CNA::Internal::Renderers::Rlgl
{
    namespace
    {
        constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;

        struct ResolvedTexture
        {
            std::shared_ptr<ITextureRenderer> texture2D;
            std::shared_ptr<ITextureCubeRenderer> textureCube;

            [[nodiscard]] bool IsValid() const noexcept
            {
                return texture2D != nullptr || textureCube != nullptr;
            }
        };

        [[nodiscard]] ResolvedTexture ResolveTexture(Texture* texture)
        {
            ResolvedTexture result;
            if (texture == nullptr) return result;

            using namespace Microsoft::Xna::Framework::Graphics;
            if (auto* cube = dynamic_cast<TextureCube*>(texture))
            {
                ITextureCubeRenderer& renderer = cube->GetRenderer();
                if (dynamic_cast<const IRlglTextureResource*>(&renderer) != nullptr)
                    result.textureCube = renderer.shared_from_this();
                return result;
            }
            if (dynamic_cast<Texture3D*>(texture) != nullptr)
                return result;
            if (auto* texture2D = dynamic_cast<Texture2D*>(texture))
            {
                ITextureRenderer& renderer = texture2D->GetRenderer();
                if (dynamic_cast<const IRlglTextureResource*>(&renderer) != nullptr)
                    result.texture2D = renderer.shared_from_this();
            }
            return result;
        }

        void* CompileShader(const void* context, const char* mainFunction,
                            const unsigned char* tokenBuffer, unsigned int byteCount,
                            const MOJOSHADER_swizzle* swizzles, unsigned int swizzleCount,
                            const MOJOSHADER_samplerMap* samplerMap,
                            unsigned int samplerMapCount)
        {
            (void)context;
            (void)mainFunction;
            return MOJOSHADER_glCompileShader(
                tokenBuffer, byteCount, swizzles, swizzleCount, samplerMap, samplerMapCount);
        }

        void DeleteShader(const void* context, void* shader)
        {
            (void)context;
            MOJOSHADER_glDeleteShader(static_cast<MOJOSHADER_glShader*>(shader));
        }

        void BindShaders(const void* context, void* vertexShader, void* pixelShader)
        {
            (void)context;
            MOJOSHADER_glBindShaders(
                static_cast<MOJOSHADER_glShader*>(vertexShader),
                static_cast<MOJOSHADER_glShader*>(pixelShader));
        }

        void GetBoundShaders(const void* context, void** vertexShader, void** pixelShader)
        {
            (void)context;
            MOJOSHADER_glGetBoundShaders(
                reinterpret_cast<MOJOSHADER_glShader**>(vertexShader),
                reinterpret_cast<MOJOSHADER_glShader**>(pixelShader));
        }

        void MapUniforms(const void* context, float** vertexFloat, int** vertexInt,
                         unsigned char** vertexBool, float** pixelFloat, int** pixelInt,
                         unsigned char** pixelBool)
        {
            (void)context;
            MOJOSHADER_glMapUniformBufferMemory(
                vertexFloat, vertexInt, vertexBool, pixelFloat, pixelInt, pixelBool);
        }

        void UnmapUniforms(const void* context)
        {
            (void)context;
            MOJOSHADER_glUnmapUniformBufferMemory();
        }

        const char* GetError(const void* context)
        {
            (void)context;
            return MOJOSHADER_glGetError();
        }

        void* GetGlProcAddress(const char* name, void* data)
        {
            const auto loader = reinterpret_cast<CNA::Platform::GlProcAddressLoader>(data);
            return loader != nullptr ? loader(name) : nullptr;
        }

        [[nodiscard]] MOJOSHADER_effectShaderContext MakeBackend(
            MOJOSHADER_glContext* context)
        {
            MOJOSHADER_effectShaderContext backend{};
            backend.shaderContext = context;
            backend.compileShader = CompileShader;
            backend.shaderAddRef =
                reinterpret_cast<MOJOSHADER_shaderAddRefFunc>(MOJOSHADER_glShaderAddRef);
            backend.deleteShader = DeleteShader;
            backend.getParseData =
                reinterpret_cast<MOJOSHADER_getParseDataFunc>(MOJOSHADER_glGetShaderParseData);
            backend.bindShaders = BindShaders;
            backend.getBoundShaders = GetBoundShaders;
            backend.mapUniformBufferMemory = MapUniforms;
            backend.unmapUniformBufferMemory = UnmapUniforms;
            backend.getError = GetError;
            return backend;
        }
    }

    RlglCompiledEffect::RlglCompiledEffect(
        RlglRenderer& renderer, const std::uint8_t* effectCode,
        const std::size_t effectCodeLength)
        : renderer_(renderer)
    {
        if (effectCode == nullptr || effectCodeLength == 0 ||
            effectCodeLength > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument("RLGL compiled effect: invalid bytecode buffer.");
        }
        effectCode_ = std::make_shared<const std::vector<std::uint8_t>>(
            effectCode, effectCode + effectCodeLength);
        CreateNativeEffect();
        textures_.resize(static_cast<std::size_t>(effectData_->param_count), nullptr);
        parameterValues_.resize(static_cast<std::size_t>(effectData_->param_count));
    }

    RlglCompiledEffect::RlglCompiledEffect(
        RlglRenderer& renderer, const RlglCompiledEffect& cloneSource)
        : renderer_(renderer)
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
            boundTextureCubeResources_ = cloneSource.boundTextureCubeResources_;
            boundVertexTexture2DResources_ = cloneSource.boundVertexTexture2DResources_;
            boundVertexTextureCubeResources_ = cloneSource.boundVertexTextureCubeResources_;
            boundSamplers_ = cloneSource.boundSamplers_;
            boundVertexSamplers_ = cloneSource.boundVertexSamplers_;
            samplerAssigned_ = cloneSource.samplerAssigned_;
            vertexSamplerAssigned_ = cloneSource.vertexSamplerAssigned_;
            for (std::size_t index = 0; index < parameterValues_.size(); ++index)
            {
                const auto& value = parameterValues_[index];
                if (!value.empty())
                    SetParameterValue(
                        static_cast<std::uint32_t>(index), value.data(), value.size());
            }
            SetTechnique(techniqueIndex_);
        }
        catch (...)
        {
            if (MojoShaderEffect::CanSafelyDeleteNativeEffect(effectData_))
                MOJOSHADER_deleteEffect(effectData_);
            effectData_ = nullptr;
            throw;
        }
    }

    RlglCompiledEffect::~RlglCompiledEffect()
    {
        if (effectData_ == nullptr) return;
        MOJOSHADER_glMakeContextCurrent(context_);
        if (passActive_)
        {
            MOJOSHADER_effectEndPass(effectData_);
            MOJOSHADER_effectEnd(effectData_);
        }
        if (MojoShaderEffect::CanSafelyDeleteNativeEffect(effectData_))
            MOJOSHADER_deleteEffect(effectData_);
        effectData_ = nullptr;
    }

    void RlglCompiledEffect::CreateNativeEffect()
    {
        context_ = static_cast<MOJOSHADER_glContext*>(renderer_.GetMojoShaderContext());
        if (context_ == nullptr)
            throw std::runtime_error("RLGL compiled effect: MojoShader context creation failed.");

        renderer_.MakeCompiledEffectContextCurrent();
        MOJOSHADER_effectShaderContext backend = MakeBackend(context_);
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
            context_ = nullptr;
            throw;
        }
    }

    std::unique_ptr<ICompiledEffectRuntime> RlglCompiledEffect::Clone() const
    {
        return std::unique_ptr<ICompiledEffectRuntime>(
            new RlglCompiledEffect(renderer_, *this));
    }

    const CompiledEffectDescription& RlglCompiledEffect::GetDescription() const
    {
        return description_;
    }

    void RlglCompiledEffect::SetTechnique(const std::uint32_t techniqueIndex)
    {
        if (techniqueIndex >= static_cast<std::uint32_t>(effectData_->technique_count))
            throw std::out_of_range("RLGL compiled effect: technique index is out of range.");
        techniqueIndex_ = techniqueIndex;
        MOJOSHADER_effectSetTechnique(effectData_, &effectData_->techniques[techniqueIndex]);
    }

    void RlglCompiledEffect::SetParameterValue(
        const std::uint32_t runtimeIndex, const void* data, const std::size_t dataBytes)
    {
        if (runtimeIndex >= static_cast<std::uint32_t>(effectData_->param_count))
            throw std::out_of_range("RLGL compiled effect: parameter index is out of range.");
        MOJOSHADER_effectParam& parameter = effectData_->params[runtimeIndex];
        const std::size_t capacity = static_cast<std::size_t>(parameter.value.value_count) * 4u;
        if (dataBytes > capacity)
            throw std::invalid_argument("RLGL compiled effect: parameter value is too large.");
        if (dataBytes > 0 && data == nullptr)
            throw std::invalid_argument("RLGL compiled effect: parameter data is null.");
        if (dataBytes == 0) return;

        MOJOSHADER_effectSetRawValueHandle(
            &parameter, data, 0, static_cast<unsigned int>(dataBytes));
        auto& saved = parameterValues_[runtimeIndex];
        saved.assign(
            static_cast<const std::uint8_t*>(data),
            static_cast<const std::uint8_t*>(data) + dataBytes);
    }

    void RlglCompiledEffect::SetParameterTexture(
        const std::uint32_t runtimeIndex, Texture* texture)
    {
        if (runtimeIndex >= textures_.size())
            throw std::out_of_range("RLGL compiled effect: texture index is out of range.");
        const auto type = static_cast<std::underlying_type_t<MOJOSHADER_symbolType>>(
            effectData_->params[runtimeIndex].value.type.parameter_type);
        if (type < MOJOSHADER_SYMTYPE_TEXTURE || type > MOJOSHADER_SYMTYPE_TEXTURECUBE)
            throw std::invalid_argument("RLGL compiled effect: parameter is not a texture.");
        if (texture != nullptr && !ResolveTexture(texture).IsValid())
        {
            throw std::invalid_argument(
                "RLGL compiled effect: texture is not an implemented resource of this device.");
        }
        textures_[runtimeIndex] = texture;
    }

    void RlglCompiledEffect::ApplyPass(
        const std::uint32_t passIndex, const CompiledEffectDeviceState& deviceState,
        CompiledEffectPassStateChanges& changes)
    {
        renderer_.MakeCompiledEffectContextCurrent();
        const MOJOSHADER_effectTechnique& technique = effectData_->techniques[techniqueIndex_];
        if (passIndex >= technique.pass_count)
            throw std::out_of_range("RLGL compiled effect: pass index is out of range.");

        if (passActive_)
        {
            MOJOSHADER_effectEndPass(effectData_);
            MOJOSHADER_effectEnd(effectData_);
            passActive_ = false;
        }

        std::memset(&stateChanges_, 0, sizeof(stateChanges_));
        unsigned int passCount = 0;
        MOJOSHADER_effectBegin(effectData_, &passCount, 0, &stateChanges_);
        MOJOSHADER_effectBeginPass(effectData_, passIndex);
        passActive_ = true;

        MOJOSHADER_glShader* boundVertex = nullptr;
        MOJOSHADER_glShader* boundPixel = nullptr;
        MOJOSHADER_glGetBoundShaders(&boundVertex, &boundPixel);
        if (effectData_->current_vert != nullptr && effectData_->current_pixl != nullptr &&
            (static_cast<const void*>(boundVertex) != effectData_->current_vert ||
             static_cast<const void*>(boundPixel) != effectData_->current_pixl))
        {
            const char* error = MOJOSHADER_glGetError();
            throw System::NotSupportedException(
                std::string("RLGL compiled effect: pass shader link failed; GL reported: ") +
                ((error != nullptr && error[0] != '\0') ? error : "(no message)"));
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
                "RLGL compiled effect: pass state changes exceed the safety limit.");
        }

        MojoShaderEffect::TranslateRenderStates(stateChanges_, deviceState, changes);
        constexpr std::size_t maxSlots = static_cast<std::size_t>(
            Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers);
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.sampler_state_changes, stateChanges_.sampler_state_change_count,
            false, maxSlots, samplerTextureParameters_, textures_, deviceState, changes);
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.vertex_sampler_state_changes,
            stateChanges_.vertex_sampler_state_change_count,
            true, maxSlots, samplerTextureParameters_, textures_, deviceState, changes);
        MojoShaderEffect::TranslateLegacySamplerAssignments(
            effectData_, stateChanges_, maxSlots, samplerTextureParameters_, textures_,
            deviceState, changes);

        for (const auto& change : changes.samplers)
        {
            if (change.slot >= maxSlots) continue;
            auto& textureSlot = change.vertexStage ? boundVertexTextures_ : boundTextures_;
            auto& texture2DSlot = change.vertexStage
                ? boundVertexTexture2DResources_ : boundTexture2DResources_;
            auto& textureCubeSlot = change.vertexStage
                ? boundVertexTextureCubeResources_ : boundTextureCubeResources_;
            auto& samplerSlot = change.vertexStage ? boundVertexSamplers_ : boundSamplers_;
            auto& assignedSlot = change.vertexStage
                ? vertexSamplerAssigned_ : samplerAssigned_;
            if (change.textureChanged)
            {
                const ResolvedTexture resolved = ResolveTexture(change.texture);
                if (!resolved.IsValid())
                {
                    throw std::runtime_error(
                        "RLGL compiled effect: applied texture no longer resolves to this device.");
                }
                textureSlot[change.slot] = change.texture;
                texture2DSlot[change.slot] = resolved.texture2D;
                textureCubeSlot[change.slot] = resolved.textureCube;
            }
            if (change.samplerChanged)
            {
                samplerSlot[change.slot] = change.sampler;
                assignedSlot[change.slot] = true;
            }
        }
    }

    void RlglCompiledEffect::GetBoundSamplerEXT(
        const std::uint32_t slot, const bool vertexStage, Texture*& texture,
        Microsoft::Xna::Framework::Graphics::SamplerState& sampler,
        bool& samplerAssigned) const
    {
        constexpr std::uint32_t maxSlots = static_cast<std::uint32_t>(
            Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers);
        if (slot >= maxSlots)
        {
            texture = nullptr;
            samplerAssigned = false;
            return;
        }
        texture = vertexStage ? boundVertexTextures_[slot] : boundTextures_[slot];
        sampler = vertexStage ? boundVertexSamplers_[slot] : boundSamplers_[slot];
        samplerAssigned = vertexStage ? vertexSamplerAssigned_[slot] : samplerAssigned_[slot];
    }

    void* RlglRenderer::GetMojoShaderContext()
    {
        platformContext_->MakeCurrent();
        if (mojoShaderContext_ == nullptr)
        {
            void* loader = reinterpret_cast<void*>(platformContext_->GetLoader());
            mojoShaderContext_ = MOJOSHADER_glCreateContext(
                MOJOSHADER_PROFILE_GLSL120, GetGlProcAddress, loader,
                nullptr, nullptr, nullptr);
        }
        if (mojoShaderContext_ != nullptr)
        {
            MOJOSHADER_glMakeContextCurrent(
                static_cast<MOJOSHADER_glContext*>(mojoShaderContext_));
        }
        return mojoShaderContext_;
    }

    void RlglRenderer::MakeCompiledEffectContextCurrent()
    {
        platformContext_->MakeCurrent();
        if (mojoShaderContext_ == nullptr)
            throw std::runtime_error("RLGL compiled effect: MojoShader context is not initialized.");
        MOJOSHADER_glMakeContextCurrent(
            static_cast<MOJOSHADER_glContext*>(mojoShaderContext_));
    }

    void RlglRenderer::DestroyCompiledEffectContext() noexcept
    {
        if (mojoShaderContext_ == nullptr) return;
        MOJOSHADER_glMakeContextCurrent(nullptr);
        MOJOSHADER_glDestroyContext(
            static_cast<MOJOSHADER_glContext*>(mojoShaderContext_));
        mojoShaderContext_ = nullptr;
    }

    std::unique_ptr<ICompiledEffectRuntime> RlglRenderer::CreateCompiledEffect(
        const std::uint8_t* effectCode, const std::size_t effectCodeBytes)
    {
        return std::make_unique<RlglCompiledEffect>(*this, effectCode, effectCodeBytes);
    }
}

#endif
