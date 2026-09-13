// SPDX-License-Identifier: MS-PL

#if defined(CNA_DIRECTX12_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/DirectX12/D3D12CompiledEffect.hpp"

#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Texture3D.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12TextureCube.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Textures.hpp"
#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DVertexFormatHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "System/InvalidCastException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace CNA::Internal::Renderers::DirectX12
{
    namespace
    {
        constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;
        constexpr std::size_t kMaximumCompiledEffectBytes = 64u * 1024u * 1024u;

        void* MOJOSHADERCALL BackendCompileShader(
            const void* context, const char* mainFunction, const unsigned char* tokens,
            unsigned int tokenBytes, const MOJOSHADER_swizzle* swizzles,
            unsigned int swizzleCount, const MOJOSHADER_samplerMap* samplerMap,
            unsigned int samplerMapCount)
        {
            auto* backend = static_cast<D3D12MojoShaderContextEXT*>(const_cast<void*>(context));
            const MOJOSHADER_parseData* parsed = MOJOSHADER_parse(
                MOJOSHADER_PROFILE_HLSL, mainFunction, tokens, tokenBytes, swizzles,
                swizzleCount, samplerMap, samplerMapCount, nullptr, nullptr, nullptr);
            if (parsed == nullptr)
            {
                backend->lastError = "MojoShader returned no HLSL parse result.";
                return nullptr;
            }
            if (parsed->error_count > 0 || parsed->output == nullptr)
            {
                backend->lastError = parsed->error_count > 0 && parsed->errors != nullptr &&
                                             parsed->errors[0].error != nullptr
                    ? parsed->errors[0].error
                    : "MojoShader produced no HLSL output.";
                MOJOSHADER_freeParseData(parsed);
                return nullptr;
            }
            auto* shader = new D3D12CompiledShaderEXT{};
            shader->parseData = parsed;
            return shader;
        }

        void MOJOSHADERCALL BackendShaderAddRef(void* shader)
        {
            if (shader != nullptr) ++static_cast<D3D12CompiledShaderEXT*>(shader)->refCount;
        }

        void MOJOSHADERCALL BackendDeleteShader(const void* context, void* shader)
        {
            if (shader == nullptr) return;
            auto* compiled = static_cast<D3D12CompiledShaderEXT*>(shader);
            if (--compiled->refCount > 0) return;
            auto* backend = static_cast<D3D12MojoShaderContextEXT*>(const_cast<void*>(context));
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
                      static_cast<D3D12CompiledShaderEXT*>(shader)->parseData)
                : nullptr;
        }

        void MOJOSHADERCALL BackendBindShaders(const void* context, void* vertex, void* pixel)
        {
            auto* backend = static_cast<D3D12MojoShaderContextEXT*>(const_cast<void*>(context));
            backend->boundVertex = static_cast<D3D12CompiledShaderEXT*>(vertex);
            backend->boundPixel = static_cast<D3D12CompiledShaderEXT*>(pixel);
        }

        void MOJOSHADERCALL BackendGetBoundShaders(const void* context, void** vertex, void** pixel)
        {
            const auto* backend = static_cast<const D3D12MojoShaderContextEXT*>(context);
            if (vertex != nullptr) *vertex = backend->boundVertex;
            if (pixel != nullptr) *pixel = backend->boundPixel;
        }

        void MOJOSHADERCALL BackendMapUniformBufferMemory(
            const void* context, float** vsf, int** vsi, unsigned char** vsb,
            float** psf, int** psi, unsigned char** psb)
        {
            auto* backend = static_cast<D3D12MojoShaderContextEXT*>(const_cast<void*>(context));
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
            return static_cast<const D3D12MojoShaderContextEXT*>(context)->lastError.c_str();
        }

        MOJOSHADER_effectShaderContext MakeBackend(D3D12MojoShaderContextEXT* context)
        {
            MOJOSHADER_effectShaderContext backend{};
            backend.shaderContext = context;
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

        std::string BlobText(ID3DBlob* blob)
        {
            return blob != nullptr
                ? std::string(static_cast<const char*>(blob->GetBufferPointer()),
                              blob->GetBufferSize())
                : std::string{};
        }

        bool IsIdentifierCharacter(char value)
        {
            return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
        }

        void ReplaceIdentifier(std::string& source, const std::string& from,
                               const std::string& to)
        {
            if (from.empty() || from == to) return;
            std::size_t position = 0;
            while ((position = source.find(from, position)) != std::string::npos)
            {
                const bool left = position > 0 && IsIdentifierCharacter(source[position - 1]);
                const std::size_t end = position + from.size();
                const bool right = end < source.size() && IsIdentifierCharacter(source[end]);
                if (!left && !right)
                {
                    source.replace(position, from.size(), to);
                    position += to.size();
                }
                else
                {
                    position = end;
                }
            }
        }

        std::string RewritePixelShader(const MOJOSHADER_parseData* vertex,
                                       const MOJOSHADER_parseData* pixel)
        {
            std::string pixelSource(pixel->output, pixel->output_len);
            if (pixel->attribute_count <= 0) return pixelSource;
            const std::string vertexSource(vertex->output, vertex->output_len);

            const std::string vertexStructName = std::string(vertex->mainfn) + "_Output";
            const std::size_t vertexName = vertexSource.find(vertexStructName);
            const std::size_t vertexOpen = vertexSource.find('{', vertexName);
            const std::size_t vertexClose = vertexSource.find('}', vertexOpen);
            const std::string pixelStructName = std::string(pixel->mainfn) + "_Input";
            const std::size_t pixelName = pixelSource.find(pixelStructName);
            const std::size_t pixelOpen = pixelSource.find('{', pixelName);
            const std::size_t pixelClose = pixelSource.find('}', pixelOpen);
            if (vertexName == std::string::npos || vertexOpen == std::string::npos ||
                vertexClose == std::string::npos || pixelName == std::string::npos ||
                pixelOpen == std::string::npos || pixelClose == std::string::npos)
            {
                throw System::NotSupportedException(
                    "DirectX12 compiled effect: MojoShader HLSL interface could not be linked.");
            }

            std::string tail = pixelSource.substr(pixelClose);
            bool needsFrontFace = false;
            for (int inputIndex = 0; inputIndex < pixel->attribute_count; ++inputIndex)
            {
                const MOJOSHADER_attribute& input = pixel->attributes[inputIndex];
                if (input.name != nullptr && std::strcmp(input.name, "vFace") == 0)
                {
                    needsFrontFace = true;
                    continue;
                }
                const MOJOSHADER_attribute* match = nullptr;
                for (int outputIndex = 0; outputIndex < vertex->output_count; ++outputIndex)
                {
                    const MOJOSHADER_attribute& output = vertex->outputs[outputIndex];
                    if ((input.usage == output.usage && input.index == output.index) ||
                        (input.name != nullptr && std::strcmp(input.name, "vPos") == 0 &&
                         output.usage == MOJOSHADER_USAGE_POSITION && output.index == 0))
                    {
                        match = &output;
                        break;
                    }
                }
                if (match == nullptr)
                {
                    throw System::NotSupportedException(
                        "DirectX12 compiled effect: pixel input has no matching vertex output.");
                }
                ReplaceIdentifier(tail, std::string("m_") + input.name,
                                  std::string("m_") + match->name);
                ReplaceIdentifier(tail, input.name, match->name);
            }

            std::string members = vertexSource.substr(
                vertexOpen + 1, vertexClose - vertexOpen - 1);
            if (needsFrontFace) members += "\n\tbool m_vFace : SV_IsFrontFace;\n";
            pixelSource.replace(pixelOpen + 1, pixelClose - pixelOpen - 1, members);
            pixelSource.replace(pixelOpen + 1 + members.size(), std::string::npos, tail);
            return pixelSource;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> CompileHlsl(
            const std::string& source, const char* name, const char* target)
        {
            Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
            Microsoft::WRL::ComPtr<ID3DBlob> diagnostics;
            const HRESULT hr = D3DCompile(
                source.data(), source.size(), name, nullptr, nullptr, name, target,
                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_SKIP_OPTIMIZATION, 0,
                bytecode.GetAddressOf(), diagnostics.GetAddressOf());
            if (FAILED(hr))
            {
                std::string message = BlobText(diagnostics.Get());
                if (message.empty()) message = "D3DCompile rejected MojoShader HLSL.";
                throw System::NotSupportedException(
                    std::string("DirectX12 compiled effect: ") + message);
            }
            return bytecode;
        }

        struct ResolvedTexture
        {
            MOJOSHADER_samplerType kind = MOJOSHADER_SAMPLER_2D;
            std::shared_ptr<ITextureRenderer> texture2D;
            std::shared_ptr<ITexture3DRenderer> texture3D;
            std::shared_ptr<ITextureCubeRenderer> textureCube;

            [[nodiscard]] bool IsValid() const
            {
                return texture2D != nullptr || texture3D != nullptr || textureCube != nullptr;
            }

            [[nodiscard]] ID3D12Resource* Resource() const
            {
                if (const auto* texture = dynamic_cast<const D3D12TextureRenderer*>(
                        texture2D.get())) return texture->GetResourceEXT();
                if (const auto* target = dynamic_cast<const D3D12RenderTargetRenderer*>(
                        texture2D.get())) return target->GetSampleableColorResourceEXT();
                if (const auto* texture = dynamic_cast<const D3D12Texture3DRenderer*>(
                        texture3D.get())) return texture->GetResourceEXT();
                if (const auto* texture = dynamic_cast<const D3D12TextureCubeRenderer*>(
                        textureCube.get())) return texture->GetResourceEXT();
                if (const auto* target = dynamic_cast<const D3D12RenderTargetCubeRenderer*>(
                        textureCube.get())) return target->GetSampleableColorResourceEXT();
                return nullptr;
            }
        };

        ResolvedTexture ResolveTexture(DirectX12Renderer& renderer, Texture* texture)
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
            ID3D12Resource* resource = result.Resource();
            if (!result.IsValid() || resource == nullptr) return {};
            Microsoft::WRL::ComPtr<ID3D12Device> owner;
            if (FAILED(resource->GetDevice(IID_PPV_ARGS(owner.GetAddressOf()))) ||
                owner.Get() != renderer.GetDeviceEXT()) return {};
            return result;
        }

        void PackUniforms(const MOJOSHADER_parseData* parseData,
                          const float* regF, const int* regI, const unsigned char* regB,
                          std::vector<std::uint8_t>& output)
        {
            output.clear();
            if (parseData == nullptr || parseData->uniform_count <= 0) return;
            std::size_t bytes = 0;
            for (int index = 0; index < parseData->uniform_count; ++index)
            {
                const int span = parseData->uniforms[index].array_count != 0
                    ? parseData->uniforms[index].array_count : 1;
                bytes += static_cast<std::size_t>(span) * 16u;
            }
            output.assign(bytes, 0);
            std::size_t offset = 0;
            for (int uniformIndex = 0; uniformIndex < parseData->uniform_count; ++uniformIndex)
            {
                const MOJOSHADER_uniform& uniform = parseData->uniforms[uniformIndex];
                if (uniform.constant) continue;
                const int span = uniform.array_count != 0 ? uniform.array_count : 1;
                const std::size_t spanBytes = static_cast<std::size_t>(span) * 16u;
                if (uniform.type == MOJOSHADER_UNIFORM_FLOAT && uniform.index >= 0 &&
                    uniform.index + span <= D3D12MojoShaderContextEXT::kMaxFloat4Registers)
                {
                    std::memcpy(output.data() + offset, regF + uniform.index * 4, spanBytes);
                }
                else if (uniform.type == MOJOSHADER_UNIFORM_INT && uniform.index >= 0 &&
                         uniform.index + span <= D3D12MojoShaderContextEXT::kMaxInt4Registers)
                {
                    std::memcpy(output.data() + offset, regI + uniform.index * 4, spanBytes);
                }
                else if (uniform.type == MOJOSHADER_UNIFORM_BOOL && uniform.index >= 0)
                {
                    for (int item = 0; item < span &&
                         uniform.index + item < D3D12MojoShaderContextEXT::kMaxBoolRegisters; ++item)
                    {
                        const std::uint32_t value = regB[uniform.index + item] != 0 ? 1u : 0u;
                        std::memcpy(output.data() + offset + static_cast<std::size_t>(item) * 16u,
                                    &value, sizeof(value));
                    }
                }
                offset += spanBytes;
            }
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
                "DirectX12 compiled effect: invalid VertexElementUsage ordinal.");
        }

        D3D12_PRIMITIVE_TOPOLOGY NativeTopology(PrimitiveType primitive)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                case PrimitiveType::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                case PrimitiveType::LineList: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
                case PrimitiveType::LineStrip: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
                case PrimitiveType::PointListEXT: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            }
            throw std::invalid_argument(
                "DirectX12 compiled effect: invalid PrimitiveType ordinal.");
        }

        D3D12_PRIMITIVE_TOPOLOGY_TYPE NativeTopologyType(PrimitiveType primitive)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:
                case PrimitiveType::TriangleStrip:
                    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                case PrimitiveType::LineList:
                case PrimitiveType::LineStrip:
                    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                case PrimitiveType::PointListEXT:
                    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            }
            throw std::invalid_argument(
                "DirectX12 compiled effect: invalid PrimitiveType ordinal.");
        }

        int ElementCount(PrimitiveType primitive, int primitiveCount)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList: return primitiveCount * 3;
                case PrimitiveType::TriangleStrip: return primitiveCount + 2;
                case PrimitiveType::LineList: return primitiveCount * 2;
                case PrimitiveType::LineStrip: return primitiveCount + 1;
                case PrimitiveType::PointListEXT: return primitiveCount;
            }
            return 0;
        }

        void AdvanceVertexBufferView(D3D12_VERTEX_BUFFER_VIEW& view, int elementOffset)
        {
            if (elementOffset <= 0 || view.StrideInBytes == 0) return;
            const std::uint64_t byteOffset =
                static_cast<std::uint64_t>(elementOffset) * view.StrideInBytes;
            if (byteOffset >= view.SizeInBytes)
            {
                view.BufferLocation += view.SizeInBytes;
                view.SizeInBytes = 0;
                return;
            }
            view.BufferLocation += byteOffset;
            view.SizeInBytes -= static_cast<UINT>(byteOffset);
        }

        D3D12_GPU_DESCRIPTOR_HANDLE TextureHandle(const ITextureRenderer* texture)
        {
            if (const auto* plain = dynamic_cast<const D3D12TextureRenderer*>(texture))
                return plain->GetShaderResourceViewGpuHandleEXT();
            if (const auto* target = dynamic_cast<const D3D12RenderTargetRenderer*>(texture))
                return target->GetShaderResourceViewGpuHandleEXT();
            return {};
        }

        D3D12_GPU_DESCRIPTOR_HANDLE TextureHandle(const ITexture3DRenderer* texture)
        {
            if (const auto* volume = dynamic_cast<const D3D12Texture3DRenderer*>(texture))
                return volume->GetShaderResourceViewGpuHandleEXT();
            return {};
        }

        D3D12_GPU_DESCRIPTOR_HANDLE TextureHandle(const ITextureCubeRenderer* texture)
        {
            if (const auto* cube = dynamic_cast<const D3D12TextureCubeRenderer*>(texture))
                return cube->GetShaderResourceViewGpuHandleEXT();
            if (const auto* target = dynamic_cast<const D3D12RenderTargetCubeRenderer*>(texture))
                return target->GetShaderResourceViewGpuHandleEXT();
            return {};
        }

        ID3D12Resource* TextureResource(const ITextureRenderer* texture)
        {
            if (const auto* plain = dynamic_cast<const D3D12TextureRenderer*>(texture))
                return plain->GetResourceEXT();
            if (const auto* target = dynamic_cast<const D3D12RenderTargetRenderer*>(texture))
                return target->GetSampleableColorResourceEXT();
            return nullptr;
        }

        ID3D12Resource* TextureResource(const ITexture3DRenderer* texture)
        {
            if (const auto* volume = dynamic_cast<const D3D12Texture3DRenderer*>(texture))
                return volume->GetResourceEXT();
            return nullptr;
        }

        ID3D12Resource* TextureResource(const ITextureCubeRenderer* texture)
        {
            if (const auto* cube = dynamic_cast<const D3D12TextureCubeRenderer*>(texture))
                return cube->GetResourceEXT();
            if (const auto* target = dynamic_cast<const D3D12RenderTargetCubeRenderer*>(texture))
                return target->GetSampleableColorResourceEXT();
            return nullptr;
        }
    }

    D3D12CompiledEffect::D3D12CompiledEffect(DirectX12Renderer& renderer,
                                             const std::uint8_t* effectCode,
                                             std::size_t effectCodeLength)
        : renderer_(renderer), ownerLifetime_(renderer.GetLifetimeTokenEXT())
    {
        BuildDescriptionAndBackend(effectCode, effectCodeLength);
        renderer_.RegisterRecoverableResourceEXT(this);
    }

    D3D12CompiledEffect::D3D12CompiledEffect(
        DirectX12Renderer& renderer, const D3D12CompiledEffect& source)
        : renderer_(renderer), ownerLifetime_(renderer.GetLifetimeTokenEXT()),
          contextOwner_(source.contextOwner_), context_(contextOwner_.get()),
          techniqueIndex_(source.techniqueIndex_),
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
            MojoShaderEffect::ValidateNativeEffect(effectData_, "DirectX12 compiled-effect clone");
            description_ = MojoShaderEffect::BuildDescription(effectData_);
            samplerTextureParameters_ =
                MojoShaderEffect::BuildSamplerTextureParameterMap(effectData_);
            textures_ = source.textures_;
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

    void D3D12CompiledEffect::BuildDescriptionAndBackend(
        const std::uint8_t* effectCode, std::size_t effectCodeLength)
    {
        if (effectCode == nullptr || effectCodeLength == 0)
            throw std::invalid_argument("DirectX12 compiled effect: bytecode must not be empty.");
        if (effectCodeLength > kMaximumCompiledEffectBytes ||
            effectCodeLength > std::numeric_limits<unsigned int>::max())
        {
            throw std::invalid_argument(
                "DirectX12 compiled effect: bytecode exceeds CNA's safety limit.");
        }
        context_ = renderer_.GetMojoShaderContextEXT();
        contextOwner_ = renderer_.mojoShaderContext_;
        if (context_ == nullptr)
            throw std::runtime_error(
                "DirectX12 compiled effect: renderer has no MojoShader backend context.");
        MOJOSHADER_effectShaderContext backend = MakeBackend(context_);
        effectData_ = MOJOSHADER_compileEffect(
            effectCode, static_cast<unsigned int>(effectCodeLength),
            nullptr, 0, nullptr, 0, &backend);
        try
        {
            MojoShaderEffect::ValidateNativeEffect(effectData_, "DirectX12 compiled effect");
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

    D3D12CompiledEffect::~D3D12CompiledEffect()
    {
        if (!ownerLifetime_.expired()) renderer_.UnregisterRecoverableResourceEXT(this);
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

    std::unique_ptr<ICompiledEffectRuntime> D3D12CompiledEffect::Clone() const
    {
        return std::unique_ptr<ICompiledEffectRuntime>(
            new D3D12CompiledEffect(renderer_, *this));
    }

    const CompiledEffectDescription& D3D12CompiledEffect::GetDescription() const
    {
        return description_;
    }

    void D3D12CompiledEffect::SetTechnique(std::uint32_t techniqueIndex)
    {
        if (effectData_ == nullptr ||
            techniqueIndex >= static_cast<std::uint32_t>(effectData_->technique_count))
            throw std::out_of_range(
                "DirectX12 compiled effect: technique index is out of range.");
        techniqueIndex_ = techniqueIndex;
        MOJOSHADER_effectSetTechnique(effectData_, &effectData_->techniques[techniqueIndex]);
    }

    void D3D12CompiledEffect::SetParameterValue(
        std::uint32_t runtimeIndex, const void* data, std::size_t dataBytes)
    {
        if (effectData_ == nullptr ||
            runtimeIndex >= static_cast<std::uint32_t>(effectData_->param_count))
            throw std::out_of_range(
                "DirectX12 compiled effect: parameter index is out of range.");
        MOJOSHADER_effectParam& parameter = effectData_->params[runtimeIndex];
        const std::size_t capacity = static_cast<std::size_t>(parameter.value.value_count) * 4u;
        if (dataBytes > capacity)
            throw std::invalid_argument(
                "DirectX12 compiled effect: parameter value is too large.");
        if (dataBytes > 0 && data == nullptr)
            throw std::invalid_argument(
                "DirectX12 compiled effect: parameter data is null.");
        if (dataBytes > 0)
            MOJOSHADER_effectSetRawValueHandle(
                &parameter, data, 0, static_cast<unsigned int>(dataBytes));
    }

    void D3D12CompiledEffect::SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture)
    {
        if (runtimeIndex >= textures_.size())
            throw std::out_of_range(
                "DirectX12 compiled effect: texture parameter index is out of range.");
        const auto parameterType = static_cast<std::underlying_type_t<MOJOSHADER_symbolType>>(
            effectData_->params[runtimeIndex].value.type.parameter_type);
        if (parameterType < MOJOSHADER_SYMTYPE_TEXTURE ||
            parameterType > MOJOSHADER_SYMTYPE_TEXTURECUBE)
            throw std::invalid_argument(
                "DirectX12 compiled effect: parameter is not a texture.");
        if (texture != nullptr)
        {
            const ResolvedTexture resolved = ResolveTexture(renderer_, texture);
            if (!resolved.IsValid())
                throw std::invalid_argument(
                    "DirectX12 compiled effect: texture belongs to another renderer.");
            const auto declared = static_cast<MOJOSHADER_symbolType>(parameterType);
            const bool declaredAny = declared == MOJOSHADER_SYMTYPE_TEXTURE;
            const bool matches = declaredAny ||
                (declared == MOJOSHADER_SYMTYPE_TEXTURE2D &&
                 resolved.kind == MOJOSHADER_SAMPLER_2D) ||
                (declared == MOJOSHADER_SYMTYPE_TEXTURE3D &&
                 resolved.kind == MOJOSHADER_SAMPLER_VOLUME) ||
                (declared == MOJOSHADER_SYMTYPE_TEXTURECUBE &&
                 resolved.kind == MOJOSHADER_SAMPLER_CUBE);
            if (!matches)
                throw System::InvalidCastException(
                    "DirectX12 compiled effect: texture dimensions do not match the parameter.");
        }
        textures_[runtimeIndex] = texture;
    }

    void D3D12CompiledEffect::ApplyPass(
        std::uint32_t passIndex, const CompiledEffectDeviceState& deviceState,
        CompiledEffectPassStateChanges& changes)
    {
        const MOJOSHADER_effectTechnique& technique = effectData_->techniques[techniqueIndex_];
        if (passIndex >= technique.pass_count)
            throw std::out_of_range("DirectX12 compiled effect: pass index is out of range.");
        if (passActive_)
        {
            MOJOSHADER_effectEndPass(effectData_);
            MOJOSHADER_effectEnd(effectData_);
            passActive_ = false;
        }
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
                "DirectX12 compiled effect: native pass state exceeds the safety limit.");

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
                        "DirectX12 compiled effect: applied texture no longer resolves.");
                binding.source = change.texture;
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

    void D3D12CompiledEffect::CaptureUniformSnapshotEXT(
        std::vector<std::uint8_t>& vertexBytes,
        std::vector<std::uint8_t>& pixelBytes) const
    {
        if (context_ == nullptr || context_->boundVertex == nullptr ||
            context_->boundPixel == nullptr)
            throw System::NotSupportedException(
                "DirectX12 compiled effect: applied pass bound no complete shader pair.");
        PackUniforms(context_->boundVertex->parseData, context_->vsRegF.data(),
                     context_->vsRegI.data(), context_->vsRegB.data(), vertexBytes);
        PackUniforms(context_->boundPixel->parseData, context_->psRegF.data(),
                     context_->psRegI.data(), context_->psRegB.data(), pixelBytes);
    }

    D3D12CompiledEffect::PairResources& D3D12CompiledEffect::GetOrCreatePairEXT()
    {
        if (context_ == nullptr || context_->boundVertex == nullptr ||
            context_->boundPixel == nullptr)
            throw System::NotSupportedException(
                "DirectX12 compiled effect: applied pass bound no complete shader pair.");
        const auto key = std::make_pair(context_->boundVertex, context_->boundPixel);
        PairResources& pair = pairs_[key];
        pair.vertex = key.first;
        pair.pixel = key.second;
        if (!pair.vertexBytecode)
        {
            const auto* parsed = pair.vertex->parseData;
            pair.vertexBytecode = CompileHlsl(
                std::string(parsed->output, parsed->output_len), parsed->mainfn, "vs_4_0");
        }
        if (!pair.pixelBytecode)
        {
            const auto* parsed = pair.pixel->parseData;
            pair.pixelBytecode = CompileHlsl(
                RewritePixelShader(pair.vertex->parseData, parsed), parsed->mainfn, "ps_4_0");
        }
        if (pair.programId == 0) pair.programId = NextD3D12CustomProgramIdEXT();
        if (pair.rootSignature) return pair;

        std::vector<D3D12_ROOT_PARAMETER1> parameters;
        std::vector<D3D12_DESCRIPTOR_RANGE1> ranges;
        const int totalSamplers = pair.vertex->parseData->sampler_count +
                                  pair.pixel->parseData->sampler_count;
        parameters.reserve(static_cast<std::size_t>(2 + totalSamplers * 2));
        ranges.reserve(static_cast<std::size_t>(totalSamplers * 2));

        const auto addConstants = [&](const MOJOSHADER_parseData* parsed,
                                      D3D12_SHADER_VISIBILITY visibility, int& root)
        {
            if (parsed->uniform_count <= 0) return;
            D3D12_ROOT_PARAMETER1 parameter{};
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameter.Descriptor.ShaderRegister = 0;
            parameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
            parameter.ShaderVisibility = visibility;
            root = static_cast<int>(parameters.size());
            parameters.push_back(parameter);
        };
        addConstants(pair.vertex->parseData, D3D12_SHADER_VISIBILITY_VERTEX,
                     pair.vertexConstantRoot);
        addConstants(pair.pixel->parseData, D3D12_SHADER_VISIBILITY_PIXEL,
                     pair.pixelConstantRoot);

        const auto addTables = [&](const MOJOSHADER_parseData* parsed, bool vertexStage,
                                   D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
                                   std::vector<RootBinding>& bindings)
        {
            for (int index = 0; index < parsed->sampler_count; ++index)
            {
                const MOJOSHADER_sampler& sampler = parsed->samplers[index];
                if (sampler.index < 0 || sampler.index >= static_cast<int>(kSamplerSlots))
                    throw System::NotSupportedException(
                        "DirectX12 compiled effect: sampler register is out of range.");
                D3D12_DESCRIPTOR_RANGE1 range{};
                range.RangeType = rangeType;
                range.NumDescriptors = 1;
                range.BaseShaderRegister = static_cast<UINT>(sampler.index);
                range.Flags = rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV
                    ? D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC
                    : D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
                ranges.push_back(range);
                D3D12_ROOT_PARAMETER1 parameter{};
                parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                parameter.DescriptorTable.NumDescriptorRanges = 1;
                parameter.DescriptorTable.pDescriptorRanges = &ranges.back();
                parameter.ShaderVisibility = vertexStage
                    ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_PIXEL;
                bindings.push_back({static_cast<std::uint32_t>(sampler.index),
                                    static_cast<std::uint32_t>(parameters.size()),
                                    sampler.type, vertexStage});
                parameters.push_back(parameter);
            }
        };
        addTables(pair.vertex->parseData, true, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                  pair.resources);
        addTables(pair.pixel->parseData, false, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                  pair.resources);
        addTables(pair.vertex->parseData, true, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
                  pair.samplers);
        addTables(pair.pixel->parseData, false, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
                  pair.samplers);

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC description{};
        description.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        description.Desc_1_1.NumParameters = static_cast<UINT>(parameters.size());
        description.Desc_1_1.pParameters = parameters.data();
        description.Desc_1_1.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Microsoft::WRL::ComPtr<ID3DBlob> serialized;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        HRESULT hr = D3D12SerializeVersionedRootSignature(
            &description, serialized.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error(
                "DirectX12 compiled effect: root signature serialization failed: " +
                BlobText(errors.Get()));
        hr = renderer_.GetDeviceEXT()->CreateRootSignature(
            0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(pair.rootSignature.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error(
                "DirectX12 compiled effect: root signature creation failed.");
        return pair;
    }

    void D3D12CompiledEffect::ReleaseDeviceResourcesEXT() noexcept
    {
        for (auto& [key, pair] : pairs_) pair.rootSignature.Reset();
    }

    void D3D12CompiledEffect::RecreateDeviceResourcesEXT()
    {
        // Root signatures are recreated lazily for the first draw on the replacement device.
    }

    D3D12MojoShaderContextEXT* DirectX12Renderer::GetMojoShaderContextEXT()
    {
        if (!mojoShaderContext_)
            mojoShaderContext_ = std::make_shared<D3D12MojoShaderContextEXT>();
        return mojoShaderContext_.get();
    }

    std::unique_ptr<ICompiledEffectRuntime> DirectX12Renderer::CreateCompiledEffect(
        const std::uint8_t* effectCode, std::size_t effectCodeBytes)
    {
        return std::make_unique<D3D12CompiledEffect>(*this, effectCode, effectCodeBytes);
    }

    void DirectX12Renderer::RecordCompiledEffectDrawEXT(
        const IVertexBufferRenderer& fallback,
        const IIndexBufferRenderer* indexBuffer,
        PrimitiveType primitive,
        int primitiveCount,
        int instanceCount,
        const GpuDrawParams& params,
        ICompiledEffectRuntime& runtime,
        const ITextureRenderer* spriteBatchSlotZeroTexture,
        const Microsoft::Xna::Framework::Graphics::TextureCollection* spriteBatchTextures)
    {
        auto* effect = dynamic_cast<D3D12CompiledEffect*>(&runtime);
        if (effect == nullptr || effect->context_ != mojoShaderContext_.get())
            throw System::NotSupportedException(
                "DirectX12 compiled effect: applied effect belongs to another renderer.");
        if (!boundColorResource_)
            throw System::NotSupportedException(
                "DirectX12 compiled effect: no render target is bound.");

        auto& fallbackBuffer = static_cast<const D3D12VertexBufferRenderer&>(fallback);
        struct Stream
        {
            const D3D12VertexBufferRenderer* buffer = nullptr;
            int slot = 0;
            int instanceFrequency = 0;
            int vertexOffset = 0;
        };
        std::vector<Stream> streams;
        if (params.vertexStreamCount == 0)
        {
            streams.push_back({&fallbackBuffer, 0, 0, 0});
        }
        else
        {
            streams.reserve(static_cast<std::size_t>(params.vertexStreamCount));
            for (int index = 0; index < params.vertexStreamCount; ++index)
            {
                const auto& source = params.vertexStreams[static_cast<std::size_t>(index)];
                if (source.buffer == nullptr || source.slot < 0 ||
                    source.slot >= kMaxVertexStreams)
                    throw System::NotSupportedException(
                        "DirectX12 compiled effect: invalid vertex stream binding.");
                streams.push_back({
                    static_cast<const D3D12VertexBufferRenderer*>(source.buffer),
                    source.slot, source.instanceFrequency, source.vertexOffset});
            }
        }

        D3D12CompiledEffect::PairResources& pair = effect->GetOrCreatePairEXT();
        const MOJOSHADER_parseData* vertexParse = pair.vertex->parseData;
        std::vector<D3DCommon::D3DVertexInputElement> selected;
        selected.reserve(static_cast<std::size_t>(vertexParse->attribute_count));
        for (int attributeIndex = 0; attributeIndex < vertexParse->attribute_count;
             ++attributeIndex)
        {
            const MOJOSHADER_attribute& attribute = vertexParse->attributes[attributeIndex];
            bool found = false;
            D3DCommon::D3DVertexInputElement candidate;
            for (const Stream& stream : streams)
            {
                const auto& declaration = stream.buffer->GetDeclarationEXT().GetElements();
                if (declaration.empty())
                    throw System::NotSupportedException(
                        "DirectX12 compiled effect: each stream needs a VertexDeclaration.");
                for (const auto& element : declaration)
                {
                    if (ToMojoShaderUsage(element.getVertexElementUsageProperty()) !=
                            attribute.usage ||
                        element.getUsageIndexProperty() != attribute.index)
                        continue;
                    if (found)
                        throw System::NotSupportedException(
                            "DirectX12 compiled effect: duplicate vertex semantic across streams.");
                    candidate = {element, stream.slot, stream.instanceFrequency, false};
                    found = true;
                }
            }
            if (!found)
                throw System::NotSupportedException(
                    "DirectX12 compiled effect: VertexDeclaration lacks shader input usage " +
                    std::to_string(static_cast<int>(attribute.usage)) + " index " +
                    std::to_string(attribute.index) + ".");
            selected.push_back(candidate);
        }

        D3D12PipelineStateDesc pipelineDescription;
        pipelineDescription.customProgramId = pair.programId;
        pipelineDescription.customVertexShaderBytecode =
            pair.vertexBytecode->GetBufferPointer();
        pipelineDescription.customVertexShaderBytecodeSize =
            pair.vertexBytecode->GetBufferSize();
        pipelineDescription.customPixelShaderBytecode =
            pair.pixelBytecode->GetBufferPointer();
        pipelineDescription.customPixelShaderBytecodeSize =
            pair.pixelBytecode->GetBufferSize();
        pipelineDescription.strideInBytes = fallbackBuffer.GetStrideEXT();
        pipelineDescription.vertexInputElements = selected;
        pipelineDescription.topologyType = static_cast<int>(NativeTopologyType(primitive));
        FillPsoStateFromCurrentEXT(pipelineDescription);
        const auto pipeline = psoCache_.GetOrCreate(
            device_.Get(), pair.rootSignature.Get(), pipelineDescription);
        if (!pipeline)
            throw System::NotSupportedException(
                "DirectX12 compiled effect: shader pair does not match the draw pipeline.");

        std::vector<std::uint8_t> vertexConstants;
        std::vector<std::uint8_t> pixelConstants;
        effect->CaptureUniformSnapshotEXT(vertexConstants, pixelConstants);
        const D3D12_GPU_VIRTUAL_ADDRESS vertexConstantAddress =
            pair.vertexConstantRoot >= 0
                ? AllocateFrameConstantDataEXT(vertexConstants.data(), vertexConstants.size()) : 0;
        const D3D12_GPU_VIRTUAL_ADDRESS pixelConstantAddress =
            pair.pixelConstantRoot >= 0
                ? AllocateFrameConstantDataEXT(pixelConstants.data(), pixelConstants.size()) : 0;

        struct ResourceBinding
        {
            std::uint32_t root = 0;
            D3D12_GPU_DESCRIPTOR_HANDLE handle{};
            ID3D12Resource* resource = nullptr;
        };
        std::vector<ResourceBinding> resources;
        resources.reserve(pair.resources.size());
        for (const auto& root : pair.resources)
        {
            D3D12CompiledEffect::TextureBinding binding = root.vertexStage
                ? effect->boundVertexTextures_[root.slot] : effect->boundTextures_[root.slot];
            D3D12_GPU_DESCRIPTOR_HANDLE handle{};
            ID3D12Resource* resource = nullptr;
            MOJOSHADER_samplerType kind = MOJOSHADER_SAMPLER_2D;
            if (!root.vertexStage && root.slot == 0 && spriteBatchSlotZeroTexture != nullptr)
            {
                handle = TextureHandle(spriteBatchSlotZeroTexture);
                resource = TextureResource(spriteBatchSlotZeroTexture);
            }
            else if (binding.textureCube)
            {
                handle = TextureHandle(binding.textureCube.get());
                resource = TextureResource(binding.textureCube.get());
                kind = MOJOSHADER_SAMPLER_CUBE;
            }
            else if (binding.texture3D)
            {
                handle = TextureHandle(binding.texture3D.get());
                resource = TextureResource(binding.texture3D.get());
                kind = MOJOSHADER_SAMPLER_VOLUME;
            }
            else if (binding.texture2D)
            {
                handle = TextureHandle(binding.texture2D.get());
                resource = TextureResource(binding.texture2D.get());
            }
            else if (spriteBatchTextures != nullptr)
            {
                const ResolvedTexture fallbackTexture = ResolveTexture(
                    *this, (*spriteBatchTextures)[static_cast<int>(root.slot)]);
                if (fallbackTexture.textureCube)
                {
                    handle = TextureHandle(fallbackTexture.textureCube.get());
                    resource = TextureResource(fallbackTexture.textureCube.get());
                    kind = MOJOSHADER_SAMPLER_CUBE;
                }
                else if (fallbackTexture.texture3D)
                {
                    handle = TextureHandle(fallbackTexture.texture3D.get());
                    resource = TextureResource(fallbackTexture.texture3D.get());
                    kind = MOJOSHADER_SAMPLER_VOLUME;
                }
                else if (fallbackTexture.texture2D)
                {
                    handle = TextureHandle(fallbackTexture.texture2D.get());
                    resource = TextureResource(fallbackTexture.texture2D.get());
                }
            }
            if (handle.ptr == 0 || resource == nullptr)
                throw System::NotSupportedException(
                    "DirectX12 compiled effect: sampler has no D3D12 texture binding.");
            if (kind != root.kind)
                throw System::InvalidCastException(
                    "DirectX12 compiled effect: sampled texture dimensions do not match shader.");
            resources.push_back({root.rootParameter, handle, resource});
        }

        struct SamplerBinding
        {
            std::uint32_t root = 0;
            std::uint32_t descriptor = 0;
        };
        std::vector<SamplerBinding> samplers;
        samplers.reserve(pair.samplers.size());
        for (const auto& root : pair.samplers)
        {
            const bool assigned = root.vertexStage
                ? effect->vertexSamplerAssigned_[root.slot]
                : effect->samplerAssigned_[root.slot];
            int filter = currentSamplerFilter_[root.slot];
            int addressU = currentSamplerAddressU_[root.slot];
            int addressV = currentSamplerAddressV_[root.slot];
            int anisotropy = currentSamplerMaxAnisotropy_[root.slot];
            int addressW = currentSamplerAddressW_[root.slot];
            int maxMip = currentSamplerMaxMipLevel_[root.slot];
            float lodBias = currentSamplerLodBias_[root.slot];
            if (assigned)
            {
                const auto& state = root.vertexStage
                    ? effect->boundVertexSamplers_[root.slot]
                    : effect->boundSamplers_[root.slot];
                filter = static_cast<int>(state.getFilterProperty());
                addressU = static_cast<int>(state.getAddressUProperty());
                addressV = static_cast<int>(state.getAddressVProperty());
                anisotropy = state.getMaxAnisotropyProperty();
                addressW = static_cast<int>(state.getAddressWProperty());
                maxMip = state.getMaxMipLevelProperty();
                lodBias = state.getMipMapLevelOfDetailBiasProperty();
            }
            const std::uint32_t descriptor = samplerCache_.GetOrCreateIndex(
                filter, addressU, addressV, anisotropy, addressW, maxMip, lodBias,
                [this](const D3D12_SAMPLER_DESC& description)
                {
                    const std::uint32_t index = heaps_->sampler.Allocate();
                    device_->CreateSampler(&description, heaps_->sampler.StagingCpuHandle(index));
                    heaps_->sampler.Publish(index);
                    return index;
                });
            samplers.push_back({root.rootParameter, descriptor});
        }

        ID3D12GraphicsCommandList* commandList = GetFrameCommandListEXT();
        RetainFrameObjectEXT(pair.rootSignature.Get());
        RetainFrameObjectEXT(pipeline.Get());
        for (const Stream& stream : streams)
            RetainFrameObjectEXT(stream.buffer->GetResourceEXT());
        if (indexBuffer != nullptr)
            RetainFrameObjectEXT(
                static_cast<const D3D12IndexBufferRenderer*>(indexBuffer)->GetResourceEXT());
        for (const auto& resource : resources) RetainFrameObjectEXT(resource.resource);

        TransitionAndBindRenderTargetsEXT(commandList);
        const D3D12_VIEWPORT viewport = GetEffectiveViewportEXT();
        const D3D12_RECT scissor = GetEffectiveScissorEXT();
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);
        commandList->SetGraphicsRootSignature(pair.rootSignature.Get());
        commandList->SetPipelineState(pipeline.Get());
        commandList->OMSetStencilRef(static_cast<UINT>(currentReferenceStencil_));
        commandList->OMSetBlendFactor(currentBlendFactor_);
        commandList->IASetPrimitiveTopology(NativeTopology(primitive));

        D3D12_VERTEX_BUFFER_VIEW views[kMaxVertexStreams]{};
        for (const Stream& stream : streams)
        {
            views[stream.slot] = stream.buffer->GetViewEXT();
            AdvanceVertexBufferView(views[stream.slot], stream.vertexOffset);
        }
        commandList->IASetVertexBuffers(0, static_cast<UINT>(kMaxVertexStreams), views);
        if (indexBuffer != nullptr)
        {
            const auto view =
                static_cast<const D3D12IndexBufferRenderer*>(indexBuffer)->GetViewEXT();
            commandList->IASetIndexBuffer(&view);
        }
        if (pair.vertexConstantRoot >= 0)
            commandList->SetGraphicsRootConstantBufferView(
                static_cast<UINT>(pair.vertexConstantRoot), vertexConstantAddress);
        if (pair.pixelConstantRoot >= 0)
            commandList->SetGraphicsRootConstantBufferView(
                static_cast<UINT>(pair.pixelConstantRoot), pixelConstantAddress);

        ID3D12DescriptorHeap* descriptorHeaps[2]{};
        UINT descriptorHeapCount = 0;
        if (!resources.empty()) descriptorHeaps[descriptorHeapCount++] = GetCbvSrvUavHeapEXT();
        if (!samplers.empty()) descriptorHeaps[descriptorHeapCount++] = GetSamplerHeapEXT();
        if (descriptorHeapCount > 0)
            commandList->SetDescriptorHeaps(descriptorHeapCount, descriptorHeaps);
        for (const auto& resource : resources)
            commandList->SetGraphicsRootDescriptorTable(resource.root, resource.handle);
        for (const auto& sampler : samplers)
            commandList->SetGraphicsRootDescriptorTable(
                sampler.root, heaps_->sampler.GpuHandle(sampler.descriptor));

        const UINT count = static_cast<UINT>(ElementCount(primitive, primitiveCount));
        const UINT instances = static_cast<UINT>(std::max(1, instanceCount));
        if (indexBuffer != nullptr)
            commandList->DrawIndexedInstanced(
                count, instances, static_cast<UINT>(params.startIndex),
                static_cast<INT>(params.baseVertex), 0);
        else
            commandList->DrawInstanced(
                count, instances, static_cast<UINT>(params.vertexStart), 0);
    }
}

#endif  // CNA_DIRECTX12_COMPILED_EFFECTS
