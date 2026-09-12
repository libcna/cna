// SPDX-License-Identifier: MS-PL

#if defined(CNA_RLGL_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/Rlgl/RlglCompiledEffect.hpp"

#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/NotSupportedException.hpp"

#include "RlglBridge.hpp"
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

        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

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

        [[nodiscard]] MOJOSHADER_usage ToMojoShaderUsage(
            const VertexElementUsage usage)
        {
            switch (usage)
            {
            case VertexElementUsage::Position: return MOJOSHADER_USAGE_POSITION;
            case VertexElementUsage::Color: return MOJOSHADER_USAGE_COLOR;
            case VertexElementUsage::TextureCoordinate: return MOJOSHADER_USAGE_TEXCOORD;
            case VertexElementUsage::Normal: return MOJOSHADER_USAGE_NORMAL;
            case VertexElementUsage::Binormal: return MOJOSHADER_USAGE_BINORMAL;
            case VertexElementUsage::Tangent: return MOJOSHADER_USAGE_TANGENT;
            case VertexElementUsage::BlendIndices: return MOJOSHADER_USAGE_BLENDINDICES;
            case VertexElementUsage::BlendWeight: return MOJOSHADER_USAGE_BLENDWEIGHT;
            case VertexElementUsage::Depth: return MOJOSHADER_USAGE_DEPTH;
            case VertexElementUsage::Fog: return MOJOSHADER_USAGE_FOG;
            case VertexElementUsage::PointSize: return MOJOSHADER_USAGE_POINTSIZE;
            case VertexElementUsage::Sample: return MOJOSHADER_USAGE_SAMPLE;
            case VertexElementUsage::TessellateFactor: return MOJOSHADER_USAGE_TESSFACTOR;
            }
            throw std::invalid_argument(
                "RLGL compiled effect: unrecognized VertexElementUsage ordinal " +
                std::to_string(static_cast<int>(usage)));
        }

        [[nodiscard]] MOJOSHADER_attributeType ToMojoShaderAttributeType(
            const int scalarType)
        {
            switch (scalarType)
            {
            case 0x1406: return MOJOSHADER_ATTRIBUTE_FLOAT;
            case 0x1401: return MOJOSHADER_ATTRIBUTE_UBYTE;
            case 0x1402: return MOJOSHADER_ATTRIBUTE_SHORT;
            case 0x140B: return MOJOSHADER_ATTRIBUTE_HALF_FLOAT;
            default:
                throw std::invalid_argument(
                    "RLGL compiled effect: unsupported native vertex scalar type");
            }
        }

        [[nodiscard]] const char* SamplerKindName(const MOJOSHADER_samplerType type)
        {
            switch (type)
            {
            case MOJOSHADER_SAMPLER_2D: return "sampler2D";
            case MOJOSHADER_SAMPLER_CUBE: return "samplerCube";
            case MOJOSHADER_SAMPLER_VOLUME: return "sampler3D";
            default: return "unknown sampler";
            }
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
                if (change.texture != nullptr && !resolved.IsValid())
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

    Bridge::CompiledEffectDrawResources& RlglRenderer::GetCompiledEffectDrawResources()
    {
        platformContext_->MakeCurrent();
        if (!compiledEffectDrawResources_)
        {
            compiledEffectDrawResources_ =
                std::make_unique<Bridge::CompiledEffectDrawResources>(
                    Bridge::CreateCompiledEffectDrawResources());
        }
        return *compiledEffectDrawResources_;
    }

    void RlglRenderer::DrawCompiledEffectGeometry(
        const IVertexBufferRenderer& vertexBuffer,
        const IIndexBufferRenderer* const indexBuffer,
        const PrimitiveType primitive, const int elementCount,
        const int firstVertex, const int startIndex, const int baseVertex,
        const GpuDrawParams& params,
        const ITextureRenderer* const spriteBatchSlotZeroTexture,
        const Microsoft::Xna::Framework::Graphics::TextureCollection*
            const spriteBatchTextures)
    {
        auto* const effect = dynamic_cast<RlglCompiledEffect*>(params.compiledEffectRuntime);
        if (effect == nullptr || &effect->renderer_ != this)
        {
            throw std::runtime_error(
                "RLGL compiled effect: the applied runtime belongs to another renderer");
        }
        if (!effect->passActive_)
            throw std::runtime_error("RLGL compiled effect: no pass is currently applied");
        if (params.instanceCount != 1 || params.firstInstance != 0)
        {
            throw System::NotSupportedException(
                "RLGL compiled effect: instanced drawing is deferred to RLGL-049");
        }
        if (params.vertexStreamCount < 0 || params.vertexStreamCount > 1)
        {
            throw System::NotSupportedException(
                "RLGL compiled effect: multi-stream input is deferred to RLGL-049");
        }

        std::size_t stride = GetVertexStride(vertexBuffer);
        std::size_t baseByteOffset = 0;
        if (params.vertexStreamCount == 1)
        {
            const GpuVertexStreamBinding& stream = params.vertexStreams[0];
            if (stream.buffer != &vertexBuffer || stream.instanceFrequency != 0 ||
                stream.strideInBytes <= 0 || stream.vertexOffset < 0)
            {
                throw System::NotSupportedException(
                    "RLGL compiled effect: the primary vertex stream is not an ordinary "
                    "single-stream binding");
            }
            stride = static_cast<std::size_t>(stream.strideInBytes);
            if (static_cast<std::size_t>(stream.vertexOffset) >
                std::numeric_limits<std::size_t>::max() / stride)
            {
                throw std::overflow_error(
                    "RLGL compiled effect: vertex-stream byte offset overflow");
            }
            baseByteOffset = static_cast<std::size_t>(stream.vertexOffset) * stride;
        }
        if (stride == 0 || stride > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
            baseByteOffset > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            throw std::invalid_argument(
                "RLGL compiled effect: vertex stream has no representable stride or offset");
        }

        const std::vector<VertexElement>& declaration = GetVertexDeclaration(vertexBuffer);
        if (declaration.empty())
        {
            throw System::NotSupportedException(
                "RLGL compiled effect: every vertex buffer needs its own VertexDeclaration");
        }

        MakeCompiledEffectContextCurrent();
        MOJOSHADER_glShader* vertexShader = nullptr;
        MOJOSHADER_glShader* pixelShader = nullptr;
        MOJOSHADER_glGetBoundShaders(&vertexShader, &pixelShader);
        if (vertexShader == nullptr || pixelShader == nullptr)
            throw std::runtime_error("RLGL compiled effect: applied pass bound no shader pair");

        Bridge::CompiledEffectDrawResources& resources = GetCompiledEffectDrawResources();
        Bridge::BeginCompiledEffectDraw(resources);
        try
        {
            // MojoShader shadows the bound GL program and enabled arrays. Other RLGL draws bind
            // their own programs and VAOs directly, so force the adapter to rebuild both beliefs
            // on the dedicated compiled-effect VAO for every draw.
            MOJOSHADER_glBindShaders(nullptr, nullptr);
            MOJOSHADER_glBindShaders(vertexShader, pixelShader);

            const MOJOSHADER_parseData* const vertexParseData =
                MOJOSHADER_glGetShaderParseData(vertexShader);
            const MOJOSHADER_parseData* const pixelParseData =
                MOJOSHADER_glGetShaderParseData(pixelShader);
            if (vertexParseData == nullptr || pixelParseData == nullptr)
                throw std::runtime_error("RLGL compiled effect: shader reflection is unavailable");
            if (vertexParseData->sampler_count > 0)
            {
                throw System::NotSupportedException(
                    "RLGL compiled effect: vertex-stage texture sampling is deferred to "
                    "RLGL-049");
            }

            for (int inputIndex = 0;
                 inputIndex < vertexParseData->attribute_count; ++inputIndex)
            {
                const MOJOSHADER_attribute& shaderInput =
                    vertexParseData->attributes[inputIndex];
                const VertexElement* match = nullptr;
                for (const VertexElement& element : declaration)
                {
                    if (ToMojoShaderUsage(element.getVertexElementUsageProperty()) ==
                            shaderInput.usage &&
                        element.getUsageIndexProperty() == shaderInput.index)
                    {
                        match = &element;
                        break;
                    }
                }
                if (match == nullptr)
                {
                    const char* const name = shaderInput.name != nullptr
                        ? shaderInput.name : "<unnamed>";
                    throw System::NotSupportedException(
                        "RLGL compiled effect: vertex shader requires attribute '" +
                        std::string(name) + "' (usage " +
                        std::to_string(static_cast<int>(shaderInput.usage)) + ", index " +
                        std::to_string(shaderInput.index) +
                        "), but the supplied declaration does not contain it");
                }

                const VertexAttributeBinding attribute = DescribeVertexAttribute(
                    *match, 0, static_cast<int>(stride), static_cast<int>(baseByteOffset));
                Bridge::BindCompiledEffectVertexBuffer(GetNativeBufferId(vertexBuffer));
                MOJOSHADER_glSetVertexAttribute(
                    shaderInput.usage, shaderInput.index,
                    static_cast<unsigned int>(attribute.componentCount),
                    ToMojoShaderAttributeType(attribute.scalarType),
                    attribute.normalized ? 1 : 0,
                    static_cast<unsigned int>(attribute.stride),
                    reinterpret_cast<const void*>(
                        static_cast<std::uintptr_t>(attribute.offset)));
            }

            for (int samplerIndex = 0;
                 samplerIndex < pixelParseData->sampler_count; ++samplerIndex)
            {
                const MOJOSHADER_sampler& shaderSampler =
                    pixelParseData->samplers[samplerIndex];
                if (shaderSampler.index < 0 ||
                    shaderSampler.index >= static_cast<int>(samplers_.size()) ||
                    shaderSampler.index >= maxSamplerSlots_)
                {
                    throw System::NotSupportedException(
                        "RLGL compiled effect: pixel sampler register exceeds the live device "
                        "limit");
                }
                if (shaderSampler.type == MOJOSHADER_SAMPLER_VOLUME)
                {
                    throw System::NotSupportedException(
                        "RLGL compiled effect: sampler3D requires the unimplemented RLGL "
                        "Texture3D resource");
                }

                const std::size_t slot = static_cast<std::size_t>(shaderSampler.index);
                Texture* selectedTexture = effect->boundTextures_[slot];
                const ITextureRenderer* texture2D =
                    effect->boundTexture2DResources_[slot].get();
                const ITextureCubeRenderer* textureCube =
                    effect->boundTextureCubeResources_[slot].get();
                ResolvedTexture fallbackTexture;
                if (shaderSampler.index == 0 && spriteBatchSlotZeroTexture != nullptr)
                {
                    selectedTexture = nullptr;
                    texture2D = spriteBatchSlotZeroTexture;
                    textureCube = nullptr;
                }
                else if (texture2D == nullptr && textureCube == nullptr &&
                         spriteBatchTextures != nullptr)
                {
                    selectedTexture = (*spriteBatchTextures)[shaderSampler.index];
                    fallbackTexture = ResolveTexture(selectedTexture);
                    texture2D = fallbackTexture.texture2D.get();
                    textureCube = fallbackTexture.textureCube.get();
                }
                const bool expects2D = shaderSampler.type == MOJOSHADER_SAMPLER_2D;
                const bool expectsCube = shaderSampler.type == MOJOSHADER_SAMPLER_CUBE;
                if (!expects2D && !expectsCube)
                {
                    throw System::NotSupportedException(
                        "RLGL compiled effect: unsupported reflected sampler kind");
                }
                if (selectedTexture != nullptr && texture2D == nullptr && textureCube == nullptr)
                {
                    throw System::NotSupportedException(
                        "RLGL compiled effect: the texture selected for pixel sampler slot " +
                        std::to_string(shaderSampler.index) +
                        " is not an implemented resource of this device");
                }
                if ((texture2D != nullptr && !expects2D) ||
                    (textureCube != nullptr && !expectsCube))
                {
                    throw System::NotSupportedException(
                        "RLGL compiled effect: shader declares " +
                        std::string(SamplerKindName(shaderSampler.type)) + " at slot " +
                        std::to_string(shaderSampler.index) +
                        ", but the effect bound a different texture dimension");
                }

                if (texture2D != nullptr)
                {
                    if (SampledRowsAreBottomUp(*texture2D))
                    {
                        const auto* const target =
                            dynamic_cast<const IRenderTargetRenderer*>(texture2D);
                        if (target == nullptr)
                        {
                            throw System::NotSupportedException(
                                "RLGL compiled effect: rendered Texture2D row order cannot be "
                                "resolved for this resource");
                        }
                        const RenderTargetResourceSnapshot snapshot =
                            GetRenderTargetResourceSnapshotForTesting(*target);
                        const unsigned int sourceFramebuffer =
                            snapshot.multiSampleCount > 0
                                ? snapshot.resolveFramebuffer : snapshot.framebuffer;
                        const unsigned int corrected =
                            Bridge::PrepareCompiledEffectFlippedTexture(
                                resources, shaderSampler.index,
                                sourceFramebuffer, snapshot.colorTexture,
                                snapshot.width, snapshot.height, snapshot.surfaceFormat);
                        Bridge::BindTexture2D(corrected, shaderSampler.index);
                    }
                    else
                    {
                        texture2D->BindGL(shaderSampler.index);
                    }
                }
                else if (textureCube != nullptr)
                {
                    textureCube->BindGL(shaderSampler.index);
                }
                else
                {
                    Bridge::UnbindCompiledEffectTexture(
                        shaderSampler.index, static_cast<int>(shaderSampler.type));
                }

                if (effect->samplerAssigned_[slot])
                {
                    const auto& sampler = effect->boundSamplers_[slot];
                    ApplySamplerState(
                        shaderSampler.index,
                        static_cast<int>(sampler.getFilterProperty()),
                        static_cast<int>(sampler.getAddressUProperty()),
                        static_cast<int>(sampler.getAddressVProperty()),
                        sampler.getMaxAnisotropyProperty());
                    ApplySamplerAddressW(
                        shaderSampler.index,
                        static_cast<int>(sampler.getAddressWProperty()));
                    ApplySamplerMipState(
                        shaderSampler.index, sampler.getMaxMipLevelProperty(),
                        sampler.getMipMapLevelOfDetailBiasProperty());
                }
            }

            MOJOSHADER_glProgramReady();
            int targetWidth = currentRenderTargetCount_ > 0
                ? currentRenderTargetWidth_ : 0;
            int targetHeight = currentRenderTargetCount_ > 0
                ? currentRenderTargetHeight_ : 0;
            if (currentRenderTargetCount_ == 0)
                GetPhysicalSize(targetWidth, targetHeight);
            MOJOSHADER_glProgramViewportInfo(
                targetWidth, targetHeight, targetWidth, targetHeight,
                /*renderTargetBound=*/0);

            Bridge::DrawCompiledEffectGeometry(
                resources,
                indexBuffer != nullptr ? GetNativeBufferId(*indexBuffer) : 0u,
                static_cast<int>(primitive), elementCount,
                firstVertex, startIndex, baseVertex,
                indexBuffer != nullptr && indexBuffer->IsThirtyTwoBit());
        }
        catch (...)
        {
            Bridge::EndCompiledEffectDraw(resources);
            throw;
        }
        Bridge::EndCompiledEffectDraw(resources);
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
