// SPDX-License-Identifier: MS-PL

#if defined(CNA_DIRECTX9_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/DirectX9/D3D9CompiledEffect.hpp"

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9StateMapping.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Textures.hpp"
#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/InvalidCastException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace CNA::Internal::Renderers::DirectX9
{
    namespace
    {
        constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;
        constexpr std::size_t kMaximumCompiledEffectBytes = 64u * 1024u * 1024u;
        constexpr int kD3D9VertexSamplerCount = 4;

        std::string FormatHr(HRESULT result)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "0x%08lX",
                          static_cast<unsigned long>(result));
            return buffer;
        }

        void CheckHr(HRESULT result, const char* operation)
        {
            if (FAILED(result))
            {
                throw System::NotSupportedException(
                    std::string("DirectX9 compiled effect: ") + operation +
                    " failed, hr=" + FormatHr(result));
            }
        }

        void* MOJOSHADERCALL BackendCompileShader(
            const void* context, const char* mainFunction, const unsigned char* tokens,
            unsigned int tokenBytes, const MOJOSHADER_swizzle* swizzles,
            unsigned int swizzleCount, const MOJOSHADER_samplerMap* samplerMap,
            unsigned int samplerMapCount)
        {
            auto* backend = static_cast<D3D9MojoShaderContextEXT*>(const_cast<void*>(context));
            const MOJOSHADER_parseData* parsed = nullptr;
            try
            {
                parsed = MOJOSHADER_parse(
                    MOJOSHADER_PROFILE_HLSL, mainFunction, tokens, tokenBytes, swizzles,
                    swizzleCount, samplerMap, samplerMapCount, nullptr, nullptr, nullptr);
                if (parsed == nullptr)
                {
                    backend->lastError = "MojoShader returned no shader reflection.";
                    return nullptr;
                }
                if (parsed->error_count > 0)
                {
                    backend->lastError = parsed->errors != nullptr &&
                                                 parsed->errors[0].error != nullptr
                        ? parsed->errors[0].error
                        : "MojoShader rejected the D3D9 shader tokens.";
                    MOJOSHADER_freeParseData(parsed);
                    return nullptr;
                }
                if (tokens == nullptr || tokenBytes < sizeof(DWORD) ||
                    tokenBytes % sizeof(DWORD) != 0 || backend->device == nullptr)
                {
                    backend->lastError = "D3D9 shader token storage is invalid.";
                    MOJOSHADER_freeParseData(parsed);
                    return nullptr;
                }

                std::vector<DWORD> alignedTokens(tokenBytes / sizeof(DWORD));
                std::memcpy(alignedTokens.data(), tokens, tokenBytes);
                auto shader = std::make_unique<D3D9CompiledShaderEXT>();
                shader->parseData = parsed;
                HRESULT result = E_INVALIDARG;
                if (parsed->shader_type == MOJOSHADER_TYPE_VERTEX)
                {
                    result = backend->device->CreateVertexShader(
                        alignedTokens.data(), shader->vertexShader.GetAddressOf());
                }
                else if (parsed->shader_type == MOJOSHADER_TYPE_PIXEL)
                {
                    result = backend->device->CreatePixelShader(
                        alignedTokens.data(), shader->pixelShader.GetAddressOf());
                }
                else
                {
                    backend->lastError = "Effect contains an unsupported shader stage.";
                    MOJOSHADER_freeParseData(parsed);
                    return nullptr;
                }
                if (FAILED(result))
                {
                    backend->lastError = "CreateVertexShader/CreatePixelShader failed, hr=" +
                        FormatHr(result);
                    MOJOSHADER_freeParseData(parsed);
                    return nullptr;
                }
                return shader.release();
            }
            catch (const std::exception& error)
            {
                if (parsed != nullptr) MOJOSHADER_freeParseData(parsed);
                backend->lastError = error.what();
                return nullptr;
            }
            catch (...)
            {
                if (parsed != nullptr) MOJOSHADER_freeParseData(parsed);
                backend->lastError = "Unexpected D3D9 shader compilation failure.";
                return nullptr;
            }
        }

        void MOJOSHADERCALL BackendShaderAddRef(void* shader)
        {
            if (shader != nullptr) ++static_cast<D3D9CompiledShaderEXT*>(shader)->refCount;
        }

        void MOJOSHADERCALL BackendDeleteShader(const void* context, void* shader)
        {
            if (shader == nullptr) return;
            auto* compiled = static_cast<D3D9CompiledShaderEXT*>(shader);
            if (--compiled->refCount > 0) return;
            auto* backend = static_cast<D3D9MojoShaderContextEXT*>(const_cast<void*>(context));
            if (backend != nullptr)
            {
                if (backend->boundVertex == compiled) backend->boundVertex = nullptr;
                if (backend->boundPixel == compiled) backend->boundPixel = nullptr;
            }
            MOJOSHADER_freeParseData(compiled->parseData);
            delete compiled;
        }

        MOJOSHADER_parseData* MOJOSHADERCALL BackendGetParseData(void* shader)
        {
            return shader != nullptr
                ? const_cast<MOJOSHADER_parseData*>(
                      static_cast<D3D9CompiledShaderEXT*>(shader)->parseData)
                : nullptr;
        }

        void MOJOSHADERCALL BackendBindShaders(const void* context, void* vertex, void* pixel)
        {
            auto* backend = static_cast<D3D9MojoShaderContextEXT*>(const_cast<void*>(context));
            backend->boundVertex = static_cast<D3D9CompiledShaderEXT*>(vertex);
            backend->boundPixel = static_cast<D3D9CompiledShaderEXT*>(pixel);
        }

        void MOJOSHADERCALL BackendGetBoundShaders(const void* context, void** vertex, void** pixel)
        {
            const auto* backend = static_cast<const D3D9MojoShaderContextEXT*>(context);
            if (vertex != nullptr) *vertex = backend->boundVertex;
            if (pixel != nullptr) *pixel = backend->boundPixel;
        }

        void MOJOSHADERCALL BackendMapUniformBufferMemory(
            const void* context, float** vsf, int** vsi, unsigned char** vsb,
            float** psf, int** psi, unsigned char** psb)
        {
            auto* backend = static_cast<D3D9MojoShaderContextEXT*>(const_cast<void*>(context));
            *vsf = backend->vsRegF.data();
            *vsi = backend->vsRegI.data();
            *vsb = backend->vsRegB.data();
            *psf = backend->psRegF.data();
            *psi = backend->psRegI.data();
            *psb = backend->psRegB.data();
        }

        void MOJOSHADERCALL BackendUnmapUniformBufferMemory(const void*) {}

        const char* MOJOSHADERCALL BackendGetError(const void* context)
        {
            return static_cast<const D3D9MojoShaderContextEXT*>(context)->lastError.c_str();
        }

        MOJOSHADER_effectShaderContext MakeBackend(D3D9MojoShaderContextEXT* context)
        {
            MOJOSHADER_effectShaderContext backend{};
            backend.compileShader = BackendCompileShader;
            backend.shaderAddRef = BackendShaderAddRef;
            backend.deleteShader = BackendDeleteShader;
            backend.getParseData = BackendGetParseData;
            backend.bindShaders = BackendBindShaders;
            backend.getBoundShaders = BackendGetBoundShaders;
            backend.mapUniformBufferMemory = BackendMapUniformBufferMemory;
            backend.unmapUniformBufferMemory = BackendUnmapUniformBufferMemory;
            backend.getError = BackendGetError;
            backend.shaderContext = context;
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
                "DirectX9 compiled effect: invalid VertexElementUsage ordinal.");
        }

        BYTE ToD3D9Usage(Microsoft::Xna::Framework::Graphics::VertexElementUsage usage)
        {
            using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
            switch (usage)
            {
                case VertexElementUsage::Position:          return D3DDECLUSAGE_POSITION;
                case VertexElementUsage::Color:             return D3DDECLUSAGE_COLOR;
                case VertexElementUsage::TextureCoordinate: return D3DDECLUSAGE_TEXCOORD;
                case VertexElementUsage::Normal:            return D3DDECLUSAGE_NORMAL;
                case VertexElementUsage::Binormal:           return D3DDECLUSAGE_BINORMAL;
                case VertexElementUsage::Tangent:            return D3DDECLUSAGE_TANGENT;
                case VertexElementUsage::BlendIndices:       return D3DDECLUSAGE_BLENDINDICES;
                case VertexElementUsage::BlendWeight:        return D3DDECLUSAGE_BLENDWEIGHT;
                case VertexElementUsage::Depth:              return D3DDECLUSAGE_DEPTH;
                case VertexElementUsage::Fog:                return D3DDECLUSAGE_FOG;
                case VertexElementUsage::PointSize:          return D3DDECLUSAGE_PSIZE;
                case VertexElementUsage::Sample:             return D3DDECLUSAGE_SAMPLE;
                case VertexElementUsage::TessellateFactor:   return D3DDECLUSAGE_TESSFACTOR;
            }
            throw std::invalid_argument(
                "DirectX9 compiled effect: invalid VertexElementUsage ordinal.");
        }

        BYTE ToD3D9Type(Microsoft::Xna::Framework::Graphics::VertexElementFormat format)
        {
            using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
            switch (format)
            {
                case VertexElementFormat::Single:           return D3DDECLTYPE_FLOAT1;
                case VertexElementFormat::Vector2:          return D3DDECLTYPE_FLOAT2;
                case VertexElementFormat::Vector3:          return D3DDECLTYPE_FLOAT3;
                case VertexElementFormat::Vector4:          return D3DDECLTYPE_FLOAT4;
                case VertexElementFormat::Color:            return D3DDECLTYPE_UBYTE4N;
                case VertexElementFormat::Byte4:            return D3DDECLTYPE_UBYTE4;
                case VertexElementFormat::Short2:           return D3DDECLTYPE_SHORT2;
                case VertexElementFormat::Short4:           return D3DDECLTYPE_SHORT4;
                case VertexElementFormat::NormalizedShort2: return D3DDECLTYPE_SHORT2N;
                case VertexElementFormat::NormalizedShort4: return D3DDECLTYPE_SHORT4N;
                case VertexElementFormat::HalfVector2:      return D3DDECLTYPE_FLOAT16_2;
                case VertexElementFormat::HalfVector4:      return D3DDECLTYPE_FLOAT16_4;
            }
            throw std::invalid_argument(
                "DirectX9 compiled effect: invalid VertexElementFormat ordinal.");
        }

        D3DPRIMITIVETYPE ToD3D9Topology(PrimitiveType primitive)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return D3DPT_TRIANGLELIST;
                case PrimitiveType::TriangleStrip: return D3DPT_TRIANGLESTRIP;
                case PrimitiveType::LineList:      return D3DPT_LINELIST;
                case PrimitiveType::LineStrip:     return D3DPT_LINESTRIP;
                case PrimitiveType::PointListEXT:  return D3DPT_POINTLIST;
            }
            throw std::invalid_argument(
                "DirectX9 compiled effect: invalid PrimitiveType ordinal.");
        }

        struct ResolvedTexture
        {
            MOJOSHADER_samplerType kind = MOJOSHADER_SAMPLER_2D;
            std::shared_ptr<ITextureRenderer> texture2D;
            std::shared_ptr<ITexture3DRenderer> texture3D;
            std::shared_ptr<ITextureCubeRenderer> textureCube;
            const ITextureRenderer* borrowedTexture2D = nullptr;

            [[nodiscard]] bool IsValid() const
            {
                return texture2D != nullptr || texture3D != nullptr ||
                       textureCube != nullptr || borrowedTexture2D != nullptr;
            }

            [[nodiscard]] IDirect3DBaseTexture9* Native() const
            {
                const ITextureRenderer* texture = texture2D != nullptr
                    ? texture2D.get() : borrowedTexture2D;
                if (const auto* plain = dynamic_cast<const D3D9TextureRenderer*>(texture))
                    return plain->GetTextureEXT();
                if (const auto* target = dynamic_cast<const D3D9RenderTargetRenderer*>(texture))
                    return target->GetTextureEXT();
                if (const auto* volume =
                        dynamic_cast<const D3D9Texture3DRenderer*>(texture3D.get()))
                    return volume->GetTextureEXT();
                if (const auto* cube =
                        dynamic_cast<const D3D9TextureCubeRenderer*>(textureCube.get()))
                    return cube->GetTextureEXT();
                if (const auto* targetCube =
                        dynamic_cast<const D3D9RenderTargetCubeRenderer*>(textureCube.get()))
                    return targetCube->GetTextureEXT();
                return nullptr;
            }
        };

        bool TextureBelongsToDevice(IDirect3DBaseTexture9* texture, IDirect3DDevice9* expected)
        {
            if (texture == nullptr || expected == nullptr) return false;
            Microsoft::WRL::ComPtr<IDirect3DDevice9> actual;
            return SUCCEEDED(texture->GetDevice(actual.GetAddressOf())) && actual.Get() == expected;
        }

        ResolvedTexture ResolveTexture(DirectX9Renderer& renderer, Texture* texture)
        {
            ResolvedTexture result;
            if (texture == nullptr) return result;
            using namespace Microsoft::Xna::Framework::Graphics;
            if (auto* cube = dynamic_cast<TextureCube*>(texture))
            {
                result.textureCube = cube->GetRenderer().shared_from_this();
                result.kind = MOJOSHADER_SAMPLER_CUBE;
            }
            else if (auto* volume = dynamic_cast<Texture3D*>(texture))
            {
                result.texture3D = volume->GetRenderer().shared_from_this();
                result.kind = MOJOSHADER_SAMPLER_VOLUME;
            }
            else if (auto* texture2D = dynamic_cast<Texture2D*>(texture))
            {
                result.texture2D = texture2D->GetRenderer().shared_from_this();
                result.kind = MOJOSHADER_SAMPLER_2D;
            }
            if (!result.IsValid() ||
                !TextureBelongsToDevice(result.Native(), renderer.GetDeviceEXT())) return {};
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

        DWORD NativeSamplerSlot(int slot, bool vertexStage)
        {
            return vertexStage
                ? static_cast<DWORD>(D3DVERTEXTEXTURESAMPLER0 + slot)
                : static_cast<DWORD>(slot);
        }

        void ApplyNativeSampler(IDirect3DDevice9* device, DWORD slot,
                                const Microsoft::Xna::Framework::Graphics::SamplerState& state,
                                DWORD maximumAnisotropy)
        {
            const D3D9FilterTriple filters =
                TextureFilterToD3D9(static_cast<int>(state.getFilterProperty()));
            CheckHr(device->SetSamplerState(slot, D3DSAMP_MINFILTER,
                                            static_cast<DWORD>(filters.min)),
                    "SetSamplerState(MINFILTER)");
            CheckHr(device->SetSamplerState(slot, D3DSAMP_MAGFILTER,
                                            static_cast<DWORD>(filters.mag)),
                    "SetSamplerState(MAGFILTER)");
            CheckHr(device->SetSamplerState(slot, D3DSAMP_MIPFILTER,
                                            static_cast<DWORD>(filters.mip)),
                    "SetSamplerState(MIPFILTER)");
            CheckHr(device->SetSamplerState(
                        slot, D3DSAMP_ADDRESSU, static_cast<DWORD>(TextureAddressModeToD3D9(
                            static_cast<int>(state.getAddressUProperty())))),
                    "SetSamplerState(ADDRESSU)");
            CheckHr(device->SetSamplerState(
                        slot, D3DSAMP_ADDRESSV, static_cast<DWORD>(TextureAddressModeToD3D9(
                            static_cast<int>(state.getAddressVProperty())))),
                    "SetSamplerState(ADDRESSV)");
            CheckHr(device->SetSamplerState(
                        slot, D3DSAMP_ADDRESSW, static_cast<DWORD>(TextureAddressModeToD3D9(
                            static_cast<int>(state.getAddressWProperty())))),
                    "SetSamplerState(ADDRESSW)");
            CheckHr(device->SetSamplerState(
                        slot, D3DSAMP_MAXANISOTROPY,
                        static_cast<DWORD>(std::clamp(
                            state.getMaxAnisotropyProperty(), 1,
                            static_cast<int>(std::max<DWORD>(1, maximumAnisotropy))))),
                    "SetSamplerState(MAXANISOTROPY)");
            CheckHr(device->SetSamplerState(
                        slot, D3DSAMP_MAXMIPLEVEL,
                        static_cast<DWORD>(std::max(0, state.getMaxMipLevelProperty()))),
                    "SetSamplerState(MAXMIPLEVEL)");
            CheckHr(device->SetSamplerState(
                        slot, D3DSAMP_MIPMAPLODBIAS,
                        std::bit_cast<DWORD>(state.getMipMapLevelOfDetailBiasProperty())),
                    "SetSamplerState(MIPMAPLODBIAS)");
        }

        void UploadUniforms(IDirect3DDevice9* device, const MOJOSHADER_parseData* parseData,
                            const D3D9MojoShaderContextEXT& context, bool vertexStage)
        {
            if (parseData == nullptr) return;
            const float* floatRegisters = vertexStage
                ? context.vsRegF.data() : context.psRegF.data();
            const int* intRegisters = vertexStage
                ? context.vsRegI.data() : context.psRegI.data();
            const unsigned char* boolRegisters = vertexStage
                ? context.vsRegB.data() : context.psRegB.data();
            for (int index = 0; index < parseData->uniform_count; ++index)
            {
                const MOJOSHADER_uniform& uniform = parseData->uniforms[index];
                if (uniform.constant) continue;
                const int span = uniform.array_count != 0 ? uniform.array_count : 1;
                if (uniform.index < 0 || span <= 0) continue;
                HRESULT result = E_INVALIDARG;
                if (uniform.type == MOJOSHADER_UNIFORM_FLOAT &&
                    uniform.index + span <= D3D9MojoShaderContextEXT::kMaxFloat4Registers)
                {
                    result = vertexStage
                        ? device->SetVertexShaderConstantF(
                              static_cast<UINT>(uniform.index), floatRegisters + uniform.index * 4,
                              static_cast<UINT>(span))
                        : device->SetPixelShaderConstantF(
                              static_cast<UINT>(uniform.index), floatRegisters + uniform.index * 4,
                              static_cast<UINT>(span));
                }
                else if (uniform.type == MOJOSHADER_UNIFORM_INT &&
                         uniform.index + span <= D3D9MojoShaderContextEXT::kMaxInt4Registers)
                {
                    result = vertexStage
                        ? device->SetVertexShaderConstantI(
                              static_cast<UINT>(uniform.index), intRegisters + uniform.index * 4,
                              static_cast<UINT>(span))
                        : device->SetPixelShaderConstantI(
                              static_cast<UINT>(uniform.index), intRegisters + uniform.index * 4,
                              static_cast<UINT>(span));
                }
                else if (uniform.type == MOJOSHADER_UNIFORM_BOOL &&
                         uniform.index + span <= D3D9MojoShaderContextEXT::kMaxBoolRegisters)
                {
                    std::array<BOOL, D3D9MojoShaderContextEXT::kMaxBoolRegisters> values{};
                    for (int item = 0; item < span; ++item)
                        values[static_cast<std::size_t>(item)] =
                            boolRegisters[uniform.index + item] != 0 ? TRUE : FALSE;
                    result = vertexStage
                        ? device->SetVertexShaderConstantB(
                              static_cast<UINT>(uniform.index), values.data(),
                              static_cast<UINT>(span))
                        : device->SetPixelShaderConstantB(
                              static_cast<UINT>(uniform.index), values.data(),
                              static_cast<UINT>(span));
                }
                CheckHr(result, vertexStage
                    ? "SetVertexShaderConstant" : "SetPixelShaderConstant");
            }
        }
    }

    D3D9CompiledEffect::D3D9CompiledEffect(DirectX9Renderer& renderer,
                                           const std::uint8_t* effectCode,
                                           std::size_t effectCodeLength)
        : renderer_(renderer), ownerLifetime_(renderer.GetLifetimeTokenEXT())
    {
        BuildDescriptionAndBackend(effectCode, effectCodeLength);
    }

    D3D9CompiledEffect::D3D9CompiledEffect(
        DirectX9Renderer& renderer, const D3D9CompiledEffect& source)
        : renderer_(renderer), ownerLifetime_(renderer.GetLifetimeTokenEXT()),
          contextOwner_(source.contextOwner_), context_(contextOwner_.get()),
          textures_(source.textures_), techniqueIndex_(source.techniqueIndex_),
          boundTextures_(source.boundTextures_),
          boundVertexTextures_(source.boundVertexTextures_),
          boundSamplers_(source.boundSamplers_),
          boundVertexSamplers_(source.boundVertexSamplers_),
          samplerAssigned_(source.samplerAssigned_),
          vertexSamplerAssigned_(source.vertexSamplerAssigned_)
    {
        effectData_ = MOJOSHADER_cloneEffect(source.effectData_);
        try
        {
            MojoShaderEffect::ValidateNativeEffect(effectData_, "DirectX9 compiled-effect clone");
            description_ = MojoShaderEffect::BuildDescription(effectData_);
            samplerTextureParameters_ =
                MojoShaderEffect::BuildSamplerTextureParameterMap(effectData_);
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

    void D3D9CompiledEffect::BuildDescriptionAndBackend(
        const std::uint8_t* effectCode, std::size_t effectCodeLength)
    {
        if (effectCode == nullptr || effectCodeLength == 0)
            throw std::invalid_argument("DirectX9 compiled effect: bytecode must not be empty.");
        if (effectCodeLength > kMaximumCompiledEffectBytes ||
            effectCodeLength > std::numeric_limits<unsigned int>::max())
            throw std::invalid_argument(
                "DirectX9 compiled effect: bytecode exceeds CNA's safety limit.");

        context_ = renderer_.GetMojoShaderContextEXT();
        contextOwner_ = renderer_.mojoShaderContext_;
        if (context_ == nullptr || context_->device == nullptr)
            throw std::runtime_error(
                "DirectX9 compiled effect: renderer has no MojoShader backend context.");
        MOJOSHADER_effectShaderContext backend = MakeBackend(context_);
        effectData_ = MOJOSHADER_compileEffect(
            effectCode, static_cast<unsigned int>(effectCodeLength),
            nullptr, 0, nullptr, 0, &backend);
        try
        {
            MojoShaderEffect::ValidateNativeEffect(effectData_, "DirectX9 compiled effect");
            description_ = MojoShaderEffect::BuildDescription(effectData_);
            samplerTextureParameters_ =
                MojoShaderEffect::BuildSamplerTextureParameterMap(effectData_);
            textures_.assign(description_.parameters.size(), nullptr);
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

    D3D9CompiledEffect::~D3D9CompiledEffect()
    {
        if (effectData_ == nullptr) return;
        if (passActive_)
        {
            MOJOSHADER_effectEndPass(effectData_);
            MOJOSHADER_effectEnd(effectData_);
        }
        if (MojoShaderEffect::CanSafelyDeleteNativeEffect(effectData_))
            MOJOSHADER_deleteEffect(effectData_);
        effectData_ = nullptr;
    }

    std::unique_ptr<ICompiledEffectRuntime> D3D9CompiledEffect::Clone() const
    {
        if (ownerLifetime_.expired())
            throw std::runtime_error(
                "DirectX9 compiled effect: cannot clone after the graphics device was destroyed.");
        return std::unique_ptr<ICompiledEffectRuntime>(
            new D3D9CompiledEffect(renderer_, *this));
    }

    const CompiledEffectDescription& D3D9CompiledEffect::GetDescription() const
    {
        return description_;
    }

    void D3D9CompiledEffect::SetTechnique(std::uint32_t techniqueIndex)
    {
        if (effectData_ == nullptr ||
            techniqueIndex >= static_cast<std::uint32_t>(effectData_->technique_count))
            throw std::out_of_range(
                "DirectX9 compiled effect: technique index is out of range.");
        techniqueIndex_ = techniqueIndex;
        MOJOSHADER_effectSetTechnique(effectData_, &effectData_->techniques[techniqueIndex]);
    }

    void D3D9CompiledEffect::SetParameterValue(
        std::uint32_t runtimeIndex, const void* data, std::size_t dataBytes)
    {
        if (effectData_ == nullptr ||
            runtimeIndex >= static_cast<std::uint32_t>(effectData_->param_count))
            throw std::out_of_range(
                "DirectX9 compiled effect: parameter index is out of range.");
        MOJOSHADER_effectParam& parameter = effectData_->params[runtimeIndex];
        const std::size_t capacity = static_cast<std::size_t>(parameter.value.value_count) * 4u;
        if (dataBytes > capacity)
            throw std::invalid_argument(
                "DirectX9 compiled effect: parameter value is too large.");
        if (dataBytes > 0 && data == nullptr)
            throw std::invalid_argument(
                "DirectX9 compiled effect: parameter data is null.");
        if (dataBytes > 0)
            MOJOSHADER_effectSetRawValueHandle(
                &parameter, data, 0, static_cast<unsigned int>(dataBytes));
    }

    void D3D9CompiledEffect::SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture)
    {
        if (runtimeIndex >= textures_.size())
            throw std::out_of_range(
                "DirectX9 compiled effect: texture parameter index is out of range.");
        const auto parameterType = static_cast<std::underlying_type_t<MOJOSHADER_symbolType>>(
            effectData_->params[runtimeIndex].value.type.parameter_type);
        if (parameterType < MOJOSHADER_SYMTYPE_TEXTURE ||
            parameterType > MOJOSHADER_SYMTYPE_TEXTURECUBE)
            throw std::invalid_argument(
                "DirectX9 compiled effect: parameter is not a texture.");
        if (texture != nullptr)
        {
            const ResolvedTexture resolved = ResolveTexture(renderer_, texture);
            if (!resolved.IsValid())
                throw std::invalid_argument(
                    "DirectX9 compiled effect: texture belongs to another renderer.");
            const auto declared = static_cast<MOJOSHADER_symbolType>(parameterType);
            const bool matches = declared == MOJOSHADER_SYMTYPE_TEXTURE ||
                (declared == MOJOSHADER_SYMTYPE_TEXTURE2D &&
                 resolved.kind == MOJOSHADER_SAMPLER_2D) ||
                (declared == MOJOSHADER_SYMTYPE_TEXTURE3D &&
                 resolved.kind == MOJOSHADER_SAMPLER_VOLUME) ||
                (declared == MOJOSHADER_SYMTYPE_TEXTURECUBE &&
                 resolved.kind == MOJOSHADER_SAMPLER_CUBE);
            if (!matches)
                throw System::InvalidCastException(
                    "DirectX9 compiled effect: texture dimensions do not match the parameter.");
        }
        textures_[runtimeIndex] = texture;
    }

    void D3D9CompiledEffect::ApplyPass(
        std::uint32_t passIndex, const CompiledEffectDeviceState& deviceState,
        CompiledEffectPassStateChanges& changes)
    {
        const MOJOSHADER_effectTechnique& technique = effectData_->techniques[techniqueIndex_];
        if (passIndex >= technique.pass_count)
            throw std::out_of_range(
                "DirectX9 compiled effect: pass index is out of range.");
        if (passActive_)
        {
            MOJOSHADER_effectEndPass(effectData_);
            MOJOSHADER_effectEnd(effectData_);
            passActive_ = false;
        }

        if (deviceState.samplerStates != nullptr)
        {
            for (std::size_t slot = 0; slot < kSamplerSlots; ++slot)
                if (!samplerAssigned_[slot])
                    boundSamplers_[slot] = (*deviceState.samplerStates)[static_cast<int>(slot)];
        }
        if (deviceState.vertexSamplerStates != nullptr)
        {
            for (std::size_t slot = 0; slot < kSamplerSlots; ++slot)
                if (!vertexSamplerAssigned_[slot])
                    boundVertexSamplers_[slot] =
                        (*deviceState.vertexSamplerStates)[static_cast<int>(slot)];
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
            throw std::runtime_error(
                "DirectX9 compiled effect: native pass state exceeds the safety limit.");

        MojoShaderEffect::TranslateRenderStates(stateChanges_, deviceState, changes);
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.sampler_state_changes, stateChanges_.sampler_state_change_count,
            false, kSamplerSlots, samplerTextureParameters_, textures_, deviceState, changes);
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.vertex_sampler_state_changes,
            stateChanges_.vertex_sampler_state_change_count,
            true, kSamplerSlots, samplerTextureParameters_, textures_, deviceState, changes);
        MojoShaderEffect::TranslateLegacySamplerAssignments(
            effectData_, stateChanges_, kSamplerSlots, samplerTextureParameters_, textures_,
            deviceState, changes);

        for (const CompiledEffectSamplerChange& change : changes.samplers)
        {
            if (change.slot >= kSamplerSlots) continue;
            TextureBinding& binding = change.vertexStage
                ? boundVertexTextures_[change.slot] : boundTextures_[change.slot];
            auto& sampler = change.vertexStage
                ? boundVertexSamplers_[change.slot] : boundSamplers_[change.slot];
            auto& assigned = change.vertexStage
                ? vertexSamplerAssigned_[change.slot] : samplerAssigned_[change.slot];
            if (change.textureChanged)
            {
                const ResolvedTexture resolved = ResolveTexture(renderer_, change.texture);
                if (!resolved.IsValid())
                    throw std::runtime_error(
                        "DirectX9 compiled effect: applied texture no longer resolves.");
                binding.kind = resolved.kind == MOJOSHADER_SAMPLER_CUBE
                    ? TextureKind::TextureCube
                    : resolved.kind == MOJOSHADER_SAMPLER_VOLUME
                        ? TextureKind::Texture3D : TextureKind::Texture2D;
                binding.texture2D = resolved.texture2D;
                binding.texture3D = resolved.texture3D;
                binding.textureCube = resolved.textureCube;
            }
            if (change.samplerChanged)
            {
                sampler = change.sampler;
                assigned = true;
            }
        }
    }

    IDirect3DVertexDeclaration9* D3D9CompiledEffect::GetOrCreateDeclarationEXT(
        const std::vector<std::uint64_t>& key,
        const std::vector<D3DVERTEXELEMENT9>& elements)
    {
        auto found = declarations_.find(key);
        if (found != declarations_.end()) return found->second.Get();
        Microsoft::WRL::ComPtr<IDirect3DVertexDeclaration9> declaration;
        CheckHr(context_->device->CreateVertexDeclaration(
                    elements.data(), declaration.GetAddressOf()),
                "CreateVertexDeclaration");
        IDirect3DVertexDeclaration9* result = declaration.Get();
        declarations_.emplace(key, std::move(declaration));
        return result;
    }

    D3D9MojoShaderContextEXT* DirectX9Renderer::GetMojoShaderContextEXT()
    {
        if (!mojoShaderContext_)
        {
            mojoShaderContext_ = std::make_shared<D3D9MojoShaderContextEXT>();
            mojoShaderContext_->device = device_;
        }
        return mojoShaderContext_.get();
    }

    std::unique_ptr<ICompiledEffectRuntime> DirectX9Renderer::CreateCompiledEffect(
        const std::uint8_t* effectCode, std::size_t effectCodeBytes)
    {
        return std::make_unique<D3D9CompiledEffect>(*this, effectCode, effectCodeBytes);
    }

    void DirectX9Renderer::DrawCompiledEffectEXT(
        const IVertexBufferRenderer& fallback, const IIndexBufferRenderer* indexBuffer,
        PrimitiveType primitive, int primitiveCount, int instanceCount,
        const GpuDrawParams& params, ICompiledEffectRuntime& runtime,
        const ITextureRenderer* spriteBatchSlotZeroTexture,
        const Microsoft::Xna::Framework::Graphics::TextureCollection* spriteBatchTextures)
    {
        auto* effect = dynamic_cast<D3D9CompiledEffect*>(&runtime);
        if (effect == nullptr || effect->context_ != mojoShaderContext_.get() ||
            effect->context_ == nullptr || effect->context_->device.Get() != device_.Get())
            throw System::NotSupportedException(
                "DirectX9 compiled effect: applied effect belongs to another renderer.");
        D3D9CompiledShaderEXT* vertexShader = effect->context_->boundVertex;
        D3D9CompiledShaderEXT* pixelShader = effect->context_->boundPixel;
        if (vertexShader == nullptr || pixelShader == nullptr ||
            vertexShader->vertexShader == nullptr || pixelShader->pixelShader == nullptr)
            throw System::NotSupportedException(
                "DirectX9 compiled effect: applied pass bound no complete shader pair.");

        struct Stream
        {
            const D3D9VertexBufferRenderer* buffer = nullptr;
            int slot = 0;
            int stride = 0;
            int vertexOffset = 0;
            int instanceFrequency = 0;
        };
        const auto& fallbackBuffer = static_cast<const D3D9VertexBufferRenderer&>(fallback);
        std::vector<Stream> streams;
        if (params.vertexStreamCount == 0)
        {
            streams.push_back({&fallbackBuffer, 0,
                static_cast<int>(fallbackBuffer.GetStrideEXT()), 0, 0});
        }
        else
        {
            streams.reserve(static_cast<std::size_t>(params.vertexStreamCount));
            for (int index = 0; index < params.vertexStreamCount; ++index)
            {
                const auto& source = params.vertexStreams[static_cast<std::size_t>(index)];
                if (source.buffer == nullptr || source.slot < 0 ||
                    source.slot >= static_cast<int>(caps_.MaxStreams) ||
                    source.strideInBytes <= 0 || source.vertexOffset < 0)
                    throw System::NotSupportedException(
                        "DirectX9 compiled effect: invalid vertex stream binding.");
                streams.push_back({
                    static_cast<const D3D9VertexBufferRenderer*>(source.buffer), source.slot,
                    source.strideInBytes, source.vertexOffset, source.instanceFrequency});
            }
        }

        std::vector<D3DVERTEXELEMENT9> elements;
        elements.reserve(static_cast<std::size_t>(vertexShader->parseData->attribute_count) + 1u);
        for (int attributeIndex = 0;
             attributeIndex < vertexShader->parseData->attribute_count; ++attributeIndex)
        {
            const MOJOSHADER_attribute& attribute =
                vertexShader->parseData->attributes[attributeIndex];
            const Stream* matchedStream = nullptr;
            const Microsoft::Xna::Framework::Graphics::VertexElement* matchedElement = nullptr;
            for (const Stream& stream : streams)
            {
                const auto& declaration = stream.buffer->GetDeclarationEXT().GetElements();
                if (declaration.empty())
                    throw System::NotSupportedException(
                        "DirectX9 compiled effect: every vertex stream needs an explicit "
                        "VertexDeclaration.");
                for (const auto& element : declaration)
                {
                    if (ToMojoShaderUsage(element.getVertexElementUsageProperty()) !=
                            attribute.usage ||
                        element.getUsageIndexProperty() != attribute.index) continue;
                    if (matchedElement != nullptr)
                        throw System::NotSupportedException(
                            "DirectX9 compiled effect: duplicate vertex semantic across streams.");
                    matchedStream = &stream;
                    matchedElement = &element;
                }
            }
            if (matchedElement == nullptr || matchedStream == nullptr ||
                matchedElement->getOffsetProperty() < 0 ||
                matchedElement->getOffsetProperty() > std::numeric_limits<WORD>::max() ||
                matchedElement->getUsageIndexProperty() < 0 ||
                matchedElement->getUsageIndexProperty() > 15)
                throw System::NotSupportedException(
                    "DirectX9 compiled effect: bound VertexDeclaration does not provide a "
                    "representable shader input.");
            elements.push_back({
                static_cast<WORD>(matchedStream->slot),
                static_cast<WORD>(matchedElement->getOffsetProperty()),
                ToD3D9Type(matchedElement->getVertexElementFormatProperty()),
                D3DDECLMETHOD_DEFAULT,
                ToD3D9Usage(matchedElement->getVertexElementUsageProperty()),
                static_cast<BYTE>(matchedElement->getUsageIndexProperty())});
        }
        if (elements.empty())
            throw System::NotSupportedException(
                "DirectX9 compiled effect: vertex shader has no matched input elements.");
        std::sort(elements.begin(), elements.end(),
            [](const D3DVERTEXELEMENT9& left, const D3DVERTEXELEMENT9& right)
            {
                return left.Stream < right.Stream ||
                    (left.Stream == right.Stream && left.Offset < right.Offset);
            });
        std::vector<std::uint64_t> declarationKey;
        declarationKey.reserve(elements.size());
        static_assert(sizeof(D3DVERTEXELEMENT9) == sizeof(std::uint64_t));
        for (const D3DVERTEXELEMENT9& element : elements)
        {
            std::uint64_t packed = 0;
            std::memcpy(&packed, &element, sizeof(packed));
            declarationKey.push_back(packed);
        }
        elements.push_back(D3DDECL_END());

        CheckHr(device_->SetVertexShader(vertexShader->vertexShader.Get()), "SetVertexShader");
        CheckHr(device_->SetPixelShader(pixelShader->pixelShader.Get()), "SetPixelShader");
        UploadUniforms(device_.Get(), vertexShader->parseData, *effect->context_, true);
        UploadUniforms(device_.Get(), pixelShader->parseData, *effect->context_, false);
        CheckHr(device_->SetVertexDeclaration(
                    effect->GetOrCreateDeclarationEXT(declarationKey, elements)),
                "SetVertexDeclaration");

        for (const Stream& stream : streams)
        {
            CheckHr(device_->SetStreamSource(
                        static_cast<UINT>(stream.slot), stream.buffer->GetBufferEXT(),
                        static_cast<UINT>(stream.vertexOffset * stream.stride),
                        static_cast<UINT>(stream.stride)),
                    "SetStreamSource");
            const UINT frequency = instanceCount > 1
                ? (stream.instanceFrequency > 0
                    ? D3DSTREAMSOURCE_INSTANCEDATA |
                        static_cast<UINT>(std::max(1, stream.instanceFrequency))
                    : D3DSTREAMSOURCE_INDEXEDDATA |
                        static_cast<UINT>(std::max(1, instanceCount)))
                : 1u;
            CheckHr(device_->SetStreamSourceFreq(static_cast<UINT>(stream.slot), frequency),
                    "SetStreamSourceFreq");
        }

        const auto bindSamplers = [&](const MOJOSHADER_parseData* parseData, bool vertexStage)
        {
            for (int index = 0; index < parseData->sampler_count; ++index)
            {
                const MOJOSHADER_sampler& reflected = parseData->samplers[index];
                const int stageLimit = vertexStage
                    ? kD3D9VertexSamplerCount
                    : std::min<int>(static_cast<int>(D3D9CompiledEffect::kSamplerSlots),
                                    static_cast<int>(caps_.MaxSimultaneousTextures));
                if (reflected.index < 0 || reflected.index >= stageLimit)
                    throw System::NotSupportedException(
                        "DirectX9 compiled effect: sampler register is out of range.");
                const std::size_t slot = static_cast<std::size_t>(reflected.index);
                const D3D9CompiledEffect::TextureBinding& binding = vertexStage
                    ? effect->boundVertexTextures_[slot] : effect->boundTextures_[slot];
                ResolvedTexture selected;
                selected.texture2D = binding.texture2D;
                selected.texture3D = binding.texture3D;
                selected.textureCube = binding.textureCube;
                if (binding.kind == D3D9CompiledEffect::TextureKind::Texture3D)
                    selected.kind = MOJOSHADER_SAMPLER_VOLUME;
                else if (binding.kind == D3D9CompiledEffect::TextureKind::TextureCube)
                    selected.kind = MOJOSHADER_SAMPLER_CUBE;
                if (!vertexStage && slot == 0 && spriteBatchSlotZeroTexture != nullptr)
                {
                    selected = {};
                    selected.borrowedTexture2D = spriteBatchSlotZeroTexture;
                }
                else if (!selected.IsValid() && !vertexStage && spriteBatchTextures != nullptr)
                {
                    selected = ResolveTexture(
                        *this, (*spriteBatchTextures)[static_cast<int>(slot)]);
                }
                IDirect3DBaseTexture9* nativeTexture = selected.Native();
                if (selected.IsValid() && nativeTexture == nullptr)
                    throw System::NotSupportedException(
                        "DirectX9 compiled effect: selected sampler texture has no native resource.");
                if (selected.IsValid() && selected.kind != reflected.type)
                    throw System::NotSupportedException(
                        "DirectX9 compiled effect: shader sampler slot " +
                        std::to_string(slot) + " requires " + SamplerKindName(reflected.type) +
                        ", but the bound texture is " + SamplerKindName(selected.kind) + ".");
                const DWORD nativeSlot = NativeSamplerSlot(static_cast<int>(slot), vertexStage);
                CheckHr(device_->SetTexture(nativeSlot, nativeTexture), "SetTexture");
                ApplyNativeSampler(device_.Get(), nativeSlot,
                    vertexStage ? effect->boundVertexSamplers_[slot]
                                : effect->boundSamplers_[slot],
                    caps_.MaxAnisotropy);
            }
        };

        try
        {
            bindSamplers(pixelShader->parseData, false);
            bindSamplers(vertexShader->parseData, true);
            if (indexBuffer != nullptr)
            {
                const auto& nativeIndex =
                    static_cast<const D3D9IndexBufferRenderer&>(*indexBuffer);
                CheckHr(device_->SetIndices(nativeIndex.GetBufferEXT()), "SetIndices");
                const int availableVertices = std::max(
                    0, fallbackBuffer.GetVertexCount() - std::max(0, params.baseVertex));
                CheckHr(device_->DrawIndexedPrimitive(
                            ToD3D9Topology(primitive), static_cast<INT>(params.baseVertex), 0,
                            static_cast<UINT>(availableVertices),
                            static_cast<UINT>(params.startIndex),
                            static_cast<UINT>(primitiveCount)),
                        "DrawIndexedPrimitive");
            }
            else
            {
                CheckHr(device_->DrawPrimitive(
                            ToD3D9Topology(primitive), static_cast<UINT>(params.vertexStart),
                            static_cast<UINT>(primitiveCount)),
                        "DrawPrimitive");
            }
        }
        catch (...)
        {
            for (const Stream& stream : streams)
                device_->SetStreamSourceFreq(static_cast<UINT>(stream.slot), 1);
            throw;
        }
        for (const Stream& stream : streams)
            CheckHr(device_->SetStreamSourceFreq(static_cast<UINT>(stream.slot), 1),
                    "SetStreamSourceFreq(reset)");
    }
}

#endif  // CNA_DIRECTX9_COMPILED_EFFECTS
