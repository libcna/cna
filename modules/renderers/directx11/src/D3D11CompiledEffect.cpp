// SPDX-License-Identifier: MS-PL

#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/DirectX11/D3D11CompiledEffect.hpp"

#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11Textures.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DVertexFormatHelper.hpp"
#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace CNA::Internal::Renderers::DirectX11
{
    namespace
    {
        constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;

        std::string FormatHr(HRESULT hr)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
            return buffer;
        }

        MOJOSHADER_effectShaderContext MakeBackend(MOJOSHADER_d3d11Context* context)
        {
            MOJOSHADER_effectShaderContext backend{};
            backend.shaderContext = context;
            backend.compileShader =
                (MOJOSHADER_compileShaderFunc) MOJOSHADER_d3d11CompileShader;
            backend.shaderAddRef =
                (MOJOSHADER_shaderAddRefFunc) MOJOSHADER_d3d11ShaderAddRef;
            backend.deleteShader =
                (MOJOSHADER_deleteShaderFunc) MOJOSHADER_d3d11DeleteShader;
            backend.getParseData =
                (MOJOSHADER_getParseDataFunc) MOJOSHADER_d3d11GetShaderParseData;
            backend.bindShaders =
                (MOJOSHADER_bindShadersFunc) MOJOSHADER_d3d11BindShaders;
            backend.getBoundShaders =
                (MOJOSHADER_getBoundShadersFunc) MOJOSHADER_d3d11GetBoundShaders;
            backend.mapUniformBufferMemory =
                (MOJOSHADER_mapUniformBufferMemoryFunc) MOJOSHADER_d3d11MapUniformBufferMemory;
            backend.unmapUniformBufferMemory =
                (MOJOSHADER_unmapUniformBufferMemoryFunc) MOJOSHADER_d3d11UnmapUniformBufferMemory;
            backend.getError =
                (MOJOSHADER_getErrorFunc) MOJOSHADER_d3d11GetError;
            return backend;
        }

        MOJOSHADER_usage ToMojoShaderUsage(
            Microsoft::Xna::Framework::Graphics::VertexElementUsage usage)
        {
            using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
            switch (usage)
            {
                case VertexElementUsage::Position:          return MOJOSHADER_USAGE_POSITION;
                case VertexElementUsage::Color:             return MOJOSHADER_USAGE_COLOR;
                case VertexElementUsage::TextureCoordinate: return MOJOSHADER_USAGE_TEXCOORD;
                case VertexElementUsage::Normal:            return MOJOSHADER_USAGE_NORMAL;
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
                "DirectX11 compiled effect: invalid VertexElementUsage ordinal.");
        }

        struct ResolvedSamplerTexture
        {
            MOJOSHADER_samplerType kind = MOJOSHADER_SAMPLER_2D;
            std::shared_ptr<ITextureRenderer> texture2D;
            std::shared_ptr<ITexture3DRenderer> texture3D;
            std::shared_ptr<ITextureCubeRenderer> textureCube;
            const ITextureRenderer* borrowedTexture2D = nullptr;

            [[nodiscard]] bool Resolved() const
            {
                return texture2D != nullptr || texture3D != nullptr ||
                       textureCube != nullptr || borrowedTexture2D != nullptr;
            }

            [[nodiscard]] ID3D11ShaderResourceView* Srv() const
            {
                const ITextureRenderer* texture = texture2D != nullptr
                    ? texture2D.get() : borrowedTexture2D;
                if (const auto* plain = dynamic_cast<const D3D11TextureRenderer*>(texture))
                    return plain->GetShaderResourceViewEXT();
                if (const auto* target =
                        dynamic_cast<const D3D11RenderTargetRenderer*>(texture))
                    return target->GetShaderResourceViewEXT();
                if (const auto* volume =
                        dynamic_cast<const D3D11Texture3DRenderer*>(texture3D.get()))
                    return volume->GetShaderResourceViewEXT();
                if (const auto* cube =
                        dynamic_cast<const D3D11TextureCubeRenderer*>(textureCube.get()))
                    return cube->GetShaderResourceViewEXT();
                if (const auto* targetCube =
                        dynamic_cast<const D3D11RenderTargetCubeRenderer*>(textureCube.get()))
                    return targetCube->GetShaderResourceViewEXT();
                return nullptr;
            }
        };

        bool SrvBelongsToDevice(ID3D11ShaderResourceView* srv, ID3D11Device* expected)
        {
            if (srv == nullptr || expected == nullptr) return false;
            ID3D11Device* actual = nullptr;
            srv->GetDevice(&actual);
            const bool result = actual == expected;
            if (actual != nullptr) actual->Release();
            return result;
        }

        ResolvedSamplerTexture ResolveSamplerTexture(DirectX11Renderer& renderer,
                                                      Texture* texture)
        {
            ResolvedSamplerTexture result;
            if (texture == nullptr) return result;
            using namespace Microsoft::Xna::Framework::Graphics;
            if (auto* cube = dynamic_cast<TextureCube*>(texture))
            {
                auto& native = cube->GetRenderer();
                result.textureCube = native.shared_from_this();
                result.kind = MOJOSHADER_SAMPLER_CUBE;
            }
            else if (auto* volume = dynamic_cast<Texture3D*>(texture))
            {
                auto& native = volume->GetRenderer();
                result.texture3D = native.shared_from_this();
                result.kind = MOJOSHADER_SAMPLER_VOLUME;
            }
            else if (auto* texture2D = dynamic_cast<Texture2D*>(texture))
            {
                auto& native = texture2D->GetRenderer();
                result.texture2D = native.shared_from_this();
                result.kind = MOJOSHADER_SAMPLER_2D;
            }
            if (!result.Resolved() ||
                !SrvBelongsToDevice(result.Srv(), renderer.GetDeviceEXT()))
            {
                return ResolvedSamplerTexture{};
            }
            return result;
        }

        const char* SamplerKindName(MOJOSHADER_samplerType kind)
        {
            switch (kind)
            {
                case MOJOSHADER_SAMPLER_CUBE: return "TextureCube";
                case MOJOSHADER_SAMPLER_VOLUME: return "Texture3D";
                default: return "Texture2D";
            }
        }

        std::uint64_t HashInputLayout(
            const std::vector<D3D11_INPUT_ELEMENT_DESC>& elements)
        {
            std::uint64_t hash = 1469598103934665603ULL;
            const auto append = [&hash](const void* data, std::size_t bytes)
            {
                const auto* source = static_cast<const std::uint8_t*>(data);
                for (std::size_t index = 0; index < bytes; ++index)
                {
                    hash ^= source[index];
                    hash *= 1099511628211ULL;
                }
            };
            for (const auto& element : elements)
            {
                append(element.SemanticName, std::strlen(element.SemanticName));
                append(&element.SemanticIndex, sizeof(element.SemanticIndex));
                append(&element.Format, sizeof(element.Format));
                append(&element.InputSlot, sizeof(element.InputSlot));
                append(&element.AlignedByteOffset, sizeof(element.AlignedByteOffset));
                append(&element.InputSlotClass, sizeof(element.InputSlotClass));
                append(&element.InstanceDataStepRate, sizeof(element.InstanceDataStepRate));
            }
            return hash;
        }
    }

    D3D11CompiledEffect::D3D11CompiledEffect(DirectX11Renderer& renderer,
                                             const std::uint8_t* effectCode,
                                             std::size_t effectCodeLength)
        : renderer_(renderer)
        , ownerLifetime_(renderer.GetLifetimeTokenEXT())
    {
        if (effectCode == nullptr || effectCodeLength == 0 ||
            effectCodeLength > std::numeric_limits<unsigned int>::max())
        {
            throw std::invalid_argument(
                "DirectX11 compiled effect: invalid bytecode buffer.");
        }
        effectCode_ = std::make_shared<const std::vector<std::uint8_t>>(
            effectCode, effectCode + effectCodeLength);
        CreateNativeEffectEXT();
        textures_.resize(static_cast<std::size_t>(effectData_->param_count), nullptr);
        parameterValues_.resize(static_cast<std::size_t>(effectData_->param_count));
        renderer_.RegisterRecoverableResourceEXT(this);
    }

    D3D11CompiledEffect::D3D11CompiledEffect(
        DirectX11Renderer& renderer, const D3D11CompiledEffect& source)
        : renderer_(renderer)
        , ownerLifetime_(renderer.GetLifetimeTokenEXT())
        , effectCode_(source.effectCode_)
        , parameterValues_(source.parameterValues_)
        , textures_(source.textures_)
        , techniqueIndex_(source.techniqueIndex_)
        , boundTextures_(source.boundTextures_)
        , boundVertexTextures_(source.boundVertexTextures_)
        , boundTexture2DResources_(source.boundTexture2DResources_)
        , boundTexture3DResources_(source.boundTexture3DResources_)
        , boundTextureCubeResources_(source.boundTextureCubeResources_)
        , boundVertexTexture2DResources_(source.boundVertexTexture2DResources_)
        , boundVertexTexture3DResources_(source.boundVertexTexture3DResources_)
        , boundVertexTextureCubeResources_(source.boundVertexTextureCubeResources_)
        , boundSamplers_(source.boundSamplers_)
        , boundVertexSamplers_(source.boundVertexSamplers_)
        , samplerAssigned_(source.samplerAssigned_)
        , vertexSamplerAssigned_(source.vertexSamplerAssigned_)
    {
        CreateNativeEffectEXT();
        try
        {
            for (std::size_t index = 0; index < parameterValues_.size(); ++index)
            {
                const auto& value = parameterValues_[index];
                if (!value.empty())
                    SetParameterValue(static_cast<std::uint32_t>(index),
                                      value.data(), value.size());
            }
            SetTechnique(techniqueIndex_);
            renderer_.RegisterRecoverableResourceEXT(this);
        }
        catch (...)
        {
            if (MojoShaderEffect::CanSafelyDeleteNativeEffect(effectData_))
                MOJOSHADER_deleteEffect(effectData_);
            effectData_ = nullptr;
            throw;
        }
    }

    D3D11CompiledEffect::~D3D11CompiledEffect()
    {
        if (!ownerLifetime_.expired())
        {
            renderer_.UnregisterRecoverableResourceEXT(this);
            ReleaseDeviceResourcesEXT();
        }
    }

    void D3D11CompiledEffect::CreateNativeEffectEXT()
    {
        context_ = renderer_.GetMojoShaderContextEXT();
        if (context_ == nullptr)
        {
            throw System::NotSupportedException(
                "DirectX11 compiled effect: MojoShader could not load d3dcompiler_47.dll or "
                "create its D3D11 context.");
        }
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

    void D3D11CompiledEffect::ReleaseDeviceResourcesEXT() noexcept
    {
        passActive_ = false;
        inputLayouts_.clear();
        if (effectData_ != nullptr &&
            MojoShaderEffect::CanSafelyDeleteNativeEffect(effectData_))
        {
            MOJOSHADER_deleteEffect(effectData_);
        }
        effectData_ = nullptr;
        context_ = nullptr;
    }

    void D3D11CompiledEffect::RecreateDeviceResourcesEXT()
    {
        CreateNativeEffectEXT();
        for (std::size_t index = 0; index < parameterValues_.size(); ++index)
        {
            const auto& value = parameterValues_[index];
            if (!value.empty())
            {
                MOJOSHADER_effectSetRawValueHandle(
                    &effectData_->params[index], value.data(), 0,
                    static_cast<unsigned int>(value.size()));
            }
        }
        SetTechnique(techniqueIndex_);
    }

    std::unique_ptr<ICompiledEffectRuntime> D3D11CompiledEffect::Clone() const
    {
        return std::unique_ptr<ICompiledEffectRuntime>(
            new D3D11CompiledEffect(renderer_, *this));
    }

    const CompiledEffectDescription& D3D11CompiledEffect::GetDescription() const
    {
        return description_;
    }

    void D3D11CompiledEffect::SetTechnique(std::uint32_t techniqueIndex)
    {
        if (techniqueIndex >= static_cast<std::uint32_t>(effectData_->technique_count))
            throw std::out_of_range(
                "DirectX11 compiled effect: technique index is out of range.");
        techniqueIndex_ = techniqueIndex;
        MOJOSHADER_effectSetTechnique(effectData_, &effectData_->techniques[techniqueIndex]);
    }

    void D3D11CompiledEffect::SetParameterValue(std::uint32_t runtimeIndex,
                                                const void* data,
                                                std::size_t dataBytes)
    {
        if (runtimeIndex >= static_cast<std::uint32_t>(effectData_->param_count))
            throw std::out_of_range(
                "DirectX11 compiled effect: parameter index is out of range.");
        MOJOSHADER_effectParam& parameter = effectData_->params[runtimeIndex];
        const std::size_t capacity = static_cast<std::size_t>(parameter.value.value_count) * 4;
        if (dataBytes > capacity)
            throw std::invalid_argument(
                "DirectX11 compiled effect: parameter value is too large.");
        if (dataBytes > 0 && data == nullptr)
            throw std::invalid_argument(
                "DirectX11 compiled effect: parameter data is null.");
        if (dataBytes == 0) return;
        MOJOSHADER_effectSetRawValueHandle(
            &parameter, data, 0, static_cast<unsigned int>(dataBytes));
        auto& saved = parameterValues_[runtimeIndex];
        saved.assign(static_cast<const std::uint8_t*>(data),
                     static_cast<const std::uint8_t*>(data) + dataBytes);
    }

    void D3D11CompiledEffect::SetParameterTexture(std::uint32_t runtimeIndex,
                                                  Texture* texture)
    {
        if (runtimeIndex >= textures_.size())
            throw std::out_of_range(
                "DirectX11 compiled effect: texture parameter index is out of range.");
        const auto parameterType = static_cast<std::underlying_type_t<MOJOSHADER_symbolType>>(
            effectData_->params[runtimeIndex].value.type.parameter_type);
        if (parameterType < MOJOSHADER_SYMTYPE_TEXTURE ||
            parameterType > MOJOSHADER_SYMTYPE_TEXTURECUBE)
        {
            throw std::invalid_argument(
                "DirectX11 compiled effect: parameter is not a texture.");
        }
        if (texture != nullptr && !ResolveSamplerTexture(renderer_, texture).Resolved())
        {
            throw std::invalid_argument(
                "DirectX11 compiled effect: texture was not created by this D3D11 device.");
        }
        textures_[runtimeIndex] = texture;
    }

    void D3D11CompiledEffect::ApplyPass(
        std::uint32_t passIndex, const CompiledEffectDeviceState& deviceState,
        CompiledEffectPassStateChanges& changes)
    {
        const MOJOSHADER_effectTechnique& technique = effectData_->techniques[techniqueIndex_];
        if (passIndex >= technique.pass_count)
            throw std::out_of_range(
                "DirectX11 compiled effect: pass index is out of range.");
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
                "DirectX11 compiled effect: native pass state changes exceed the safety limit.");
        }

        MojoShaderEffect::TranslateRenderStates(stateChanges_, deviceState, changes);
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.sampler_state_changes,
            stateChanges_.sampler_state_change_count, false, kSamplerSlots,
            samplerTextureParameters_, textures_, deviceState, changes);
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.vertex_sampler_state_changes,
            stateChanges_.vertex_sampler_state_change_count, true, kSamplerSlots,
            samplerTextureParameters_, textures_, deviceState, changes);
        MojoShaderEffect::TranslateLegacySamplerAssignments(
            effectData_, stateChanges_, kSamplerSlots, samplerTextureParameters_, textures_,
            deviceState, changes);

        for (const auto& sampler : changes.samplers)
        {
            if (sampler.slot >= kSamplerSlots) continue;
            auto& textureSlot = sampler.vertexStage ? boundVertexTextures_ : boundTextures_;
            auto& texture2DSlot = sampler.vertexStage
                ? boundVertexTexture2DResources_ : boundTexture2DResources_;
            auto& texture3DSlot = sampler.vertexStage
                ? boundVertexTexture3DResources_ : boundTexture3DResources_;
            auto& textureCubeSlot = sampler.vertexStage
                ? boundVertexTextureCubeResources_ : boundTextureCubeResources_;
            auto& samplerSlot = sampler.vertexStage ? boundVertexSamplers_ : boundSamplers_;
            auto& assignedSlot = sampler.vertexStage
                ? vertexSamplerAssigned_ : samplerAssigned_;
            if (sampler.textureChanged)
            {
                const ResolvedSamplerTexture resolved =
                    ResolveSamplerTexture(renderer_, sampler.texture);
                if (!resolved.Resolved())
                {
                    throw std::runtime_error(
                        "DirectX11 compiled effect: an applied texture assignment no longer "
                        "resolves to this graphics device.");
                }
                textureSlot[sampler.slot] = sampler.texture;
                texture2DSlot[sampler.slot] = resolved.texture2D;
                texture3DSlot[sampler.slot] = resolved.texture3D;
                textureCubeSlot[sampler.slot] = resolved.textureCube;
            }
            if (sampler.samplerChanged)
            {
                samplerSlot[sampler.slot] = sampler.sampler;
                assignedSlot[sampler.slot] = true;
            }
        }
    }

    ID3D11InputLayout* D3D11CompiledEffect::BindShadersAndLayoutEXT(
        const std::vector<D3D11_INPUT_ELEMENT_DESC>& elements)
    {
        MOJOSHADER_d3d11Shader* vertexShader = nullptr;
        MOJOSHADER_d3d11Shader* pixelShader = nullptr;
        MOJOSHADER_d3d11GetBoundShaders(context_, &vertexShader, &pixelShader);
        if (vertexShader == nullptr || pixelShader == nullptr)
        {
            throw System::NotSupportedException(
                "DirectX11 compiled effect: the applied pass bound no complete shader pair.");
        }
        if (elements.empty())
        {
            throw System::NotSupportedException(
                "DirectX11 compiled effect: the vertex shader has no matched input elements.");
        }

        const std::uint64_t layoutHash = HashInputLayout(elements);
        const std::uint64_t cacheKey = layoutHash ^
            (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(vertexShader)) *
             0x9E3779B185EBCA87ULL);

        // Stock and ShaderEffect draws bind native D3D11 shaders behind MojoShader's back.
        // Rebinding the current pair marks both stages dirty before ProgramReady restores them.
        MOJOSHADER_d3d11BindShaders(context_, vertexShader, pixelShader);
        void* bytecode = nullptr;
        int bytecodeLength = 0;
        if (MOJOSHADER_d3d11CompileVertexShader(
                context_, layoutHash, const_cast<D3D11_INPUT_ELEMENT_DESC*>(elements.data()),
                static_cast<int>(elements.size()), &bytecode, &bytecodeLength) < 0)
        {
            throw System::NotSupportedException(
                std::string("DirectX11 compiled effect: vertex shader compilation failed: ") +
                MOJOSHADER_d3d11GetError(context_));
        }

        auto found = inputLayouts_.find(cacheKey);
        if (found == inputLayouts_.end())
        {
            Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
            const HRESULT hr = renderer_.GetDeviceEXT()->CreateInputLayout(
                elements.data(), static_cast<UINT>(elements.size()), bytecode,
                static_cast<SIZE_T>(bytecodeLength), layout.GetAddressOf());
            if (FAILED(hr))
            {
                throw System::NotSupportedException(
                    "DirectX11 compiled effect: input layout creation failed, hr=" +
                    FormatHr(hr));
            }
            found = inputLayouts_.emplace(cacheKey, std::move(layout)).first;
        }

        if (MOJOSHADER_d3d11ProgramReady(context_, layoutHash) < 0)
        {
            throw System::NotSupportedException(
                std::string("DirectX11 compiled effect: shader binding failed: ") +
                MOJOSHADER_d3d11GetError(context_));
        }
        renderer_.GetContextEXT()->IASetInputLayout(found->second.Get());
        return found->second.Get();
    }

    MOJOSHADER_d3d11Context* DirectX11Renderer::GetMojoShaderContextEXT()
    {
        if (mojoShaderContext_ == nullptr)
        {
            mojoShaderContext_ = MOJOSHADER_d3d11CreateContext(
                device_.Get(), context_.Get(), nullptr, nullptr, nullptr);
        }
        return mojoShaderContext_;
    }

    std::unique_ptr<ICompiledEffectRuntime> DirectX11Renderer::CreateCompiledEffect(
        const std::uint8_t* effectCode, std::size_t effectCodeBytes)
    {
        return std::make_unique<D3D11CompiledEffect>(
            *this, effectCode, effectCodeBytes);
    }

    void DirectX11Renderer::BindCompiledEffectForDrawEXT(
        const D3D11VertexBufferRenderer& fallback, const GpuDrawParams& params,
        ICompiledEffectRuntime& runtime, const ITextureRenderer* spriteBatchSlotZeroTexture,
        const Microsoft::Xna::Framework::Graphics::TextureCollection* spriteBatchTextures)
    {
        auto* effect = dynamic_cast<D3D11CompiledEffect*>(&runtime);
        if (effect == nullptr || effect->context_ != mojoShaderContext_)
        {
            throw System::NotSupportedException(
                "DirectX11 compiled effect: the applied effect belongs to another renderer.");
        }

        MOJOSHADER_d3d11Shader* vertexShader = nullptr;
        MOJOSHADER_d3d11Shader* pixelShader = nullptr;
        MOJOSHADER_d3d11GetBoundShaders(effect->context_, &vertexShader, &pixelShader);
        if (vertexShader == nullptr || pixelShader == nullptr)
        {
            throw System::NotSupportedException(
                "DirectX11 compiled effect: the applied pass bound no complete shader pair.");
        }
        const MOJOSHADER_parseData* vertexParse =
            MOJOSHADER_d3d11GetShaderParseData(vertexShader);
        const MOJOSHADER_parseData* pixelParse =
            MOJOSHADER_d3d11GetShaderParseData(pixelShader);
        if (vertexParse == nullptr || pixelParse == nullptr)
            throw std::runtime_error(
                "DirectX11 compiled effect: bound shader reflection is missing.");

        struct Stream
        {
            const D3D11VertexBufferRenderer* buffer;
            int slot;
            int instanceFrequency;
        };
        std::vector<Stream> streams;
        if (params.vertexStreamCount == 0)
        {
            streams.push_back({&fallback, 0, 0});
        }
        else
        {
            streams.reserve(static_cast<std::size_t>(params.vertexStreamCount));
            for (int index = 0; index < params.vertexStreamCount; ++index)
            {
                const auto& source = params.vertexStreams[static_cast<std::size_t>(index)];
                if (source.buffer == nullptr || source.slot < 0 ||
                    source.slot >= kMaxVertexStreams)
                {
                    throw System::NotSupportedException(
                        "DirectX11 compiled effect: a vertex stream binding is invalid.");
                }
                streams.push_back({
                    static_cast<const D3D11VertexBufferRenderer*>(source.buffer),
                    source.slot, source.instanceFrequency});
            }
        }

        std::vector<D3DCommon::D3DVertexInputElement> selected;
        selected.reserve(static_cast<std::size_t>(vertexParse->attribute_count));
        for (int attributeIndex = 0; attributeIndex < vertexParse->attribute_count;
             ++attributeIndex)
        {
            const auto& attribute = vertexParse->attributes[attributeIndex];
            const D3DCommon::D3DVertexInputElement* match = nullptr;
            D3DCommon::D3DVertexInputElement candidate;
            for (const Stream& stream : streams)
            {
                const auto& declaration = stream.buffer->GetDeclarationEXT().GetElements();
                if (declaration.empty())
                {
                    throw System::NotSupportedException(
                        "DirectX11 compiled effect: every vertex stream requires an explicit "
                        "VertexDeclaration.");
                }
                for (const auto& element : declaration)
                {
                    if (ToMojoShaderUsage(element.getVertexElementUsageProperty()) !=
                            attribute.usage ||
                        element.getUsageIndexProperty() != attribute.index)
                    {
                        continue;
                    }
                    if (match != nullptr)
                    {
                        throw System::NotSupportedException(
                            "DirectX11 compiled effect: duplicate vertex semantic across streams.");
                    }
                    candidate = {element, stream.slot, stream.instanceFrequency, false};
                    match = &candidate;
                }
            }
            if (match == nullptr)
            {
                throw System::NotSupportedException(
                    "DirectX11 compiled effect: the bound VertexDeclaration does not provide "
                    "shader input usage " + std::to_string(static_cast<int>(attribute.usage)) +
                    " index " + std::to_string(attribute.index) + ".");
            }
            selected.push_back(candidate);
        }

        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        if (!D3DCommon::InputElementsForLayout(selected, elements))
        {
            throw System::NotSupportedException(
                "DirectX11 compiled effect: the matched vertex layout cannot be represented.");
        }
        (void) effect->BindShadersAndLayoutEXT(elements);

        const auto bindSamplers = [&](const MOJOSHADER_parseData* parseData, bool vertexStage)
        {
            for (int index = 0; index < parseData->sampler_count; ++index)
            {
                const MOJOSHADER_sampler& reflected = parseData->samplers[index];
                if (reflected.index < 0 ||
                    reflected.index >= static_cast<int>(D3D11CompiledEffect::kSamplerSlots))
                {
                    throw System::NotSupportedException(
                        "DirectX11 compiled effect: a sampler register is out of range.");
                }
                const std::size_t slot = static_cast<std::size_t>(reflected.index);
                ResolvedSamplerTexture selectedTexture;
                if (vertexStage)
                {
                    selectedTexture.texture2D = effect->boundVertexTexture2DResources_[slot];
                    selectedTexture.texture3D = effect->boundVertexTexture3DResources_[slot];
                    selectedTexture.textureCube = effect->boundVertexTextureCubeResources_[slot];
                }
                else
                {
                    selectedTexture.texture2D = effect->boundTexture2DResources_[slot];
                    selectedTexture.texture3D = effect->boundTexture3DResources_[slot];
                    selectedTexture.textureCube = effect->boundTextureCubeResources_[slot];
                }
                if (selectedTexture.textureCube != nullptr)
                    selectedTexture.kind = MOJOSHADER_SAMPLER_CUBE;
                else if (selectedTexture.texture3D != nullptr)
                    selectedTexture.kind = MOJOSHADER_SAMPLER_VOLUME;

                if (!vertexStage && slot == 0 && spriteBatchSlotZeroTexture != nullptr)
                {
                    selectedTexture = {};
                    selectedTexture.borrowedTexture2D = spriteBatchSlotZeroTexture;
                    selectedTexture.kind = MOJOSHADER_SAMPLER_2D;
                }
                else if (!selectedTexture.Resolved() && spriteBatchTextures != nullptr)
                {
                    selectedTexture = ResolveSamplerTexture(
                        *this, (*spriteBatchTextures)[static_cast<int>(slot)]);
                }

                ID3D11ShaderResourceView* srv = selectedTexture.Srv();
                if (selectedTexture.Resolved() && srv == nullptr)
                {
                    throw System::NotSupportedException(
                        "DirectX11 compiled effect: the selected sampler texture has no D3D11 SRV.");
                }
                if (selectedTexture.Resolved() && selectedTexture.kind != reflected.type)
                {
                    throw System::NotSupportedException(
                        "DirectX11 compiled effect: shader sampler slot " +
                        std::to_string(slot) + " requires " + SamplerKindName(reflected.type) +
                        ", but the bound texture is " + SamplerKindName(selectedTexture.kind) + ".");
                }
                const UINT nativeSlot = static_cast<UINT>(slot);
                if (vertexStage)
                    context_->VSSetShaderResources(nativeSlot, 1, &srv);
                else
                    context_->PSSetShaderResources(nativeSlot, 1, &srv);

                const bool samplerAssigned = vertexStage
                    ? effect->vertexSamplerAssigned_[slot] : effect->samplerAssigned_[slot];
                if (samplerAssigned)
                {
                    const auto& state = vertexStage
                        ? effect->boundVertexSamplers_[slot] : effect->boundSamplers_[slot];
                    auto nativeSampler = samplerCache_.GetOrCreate(
                        device_.Get(), static_cast<int>(state.getFilterProperty()),
                        static_cast<int>(state.getAddressUProperty()),
                        static_cast<int>(state.getAddressVProperty()),
                        state.getMaxAnisotropyProperty(),
                        static_cast<int>(state.getAddressWProperty()),
                        state.getMaxMipLevelProperty(),
                        state.getMipMapLevelOfDetailBiasProperty());
                    ID3D11SamplerState* sampler = nativeSampler.Get();
                    if (vertexStage)
                        context_->VSSetSamplers(nativeSlot, 1, &sampler);
                    else
                        context_->PSSetSamplers(nativeSlot, 1, &sampler);
                }
            }
        };
        bindSamplers(pixelParse, false);
        bindSamplers(vertexParse, true);
    }
}

#endif  // CNA_DIRECTX11_COMPILED_EFFECTS
