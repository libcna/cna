// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file D3D12CompiledEffect.hpp
 * @brief Compiled XNA Effect Framework bytecode executed through MojoShader HLSL on D3D12.
 */

#if defined(CNA_DIRECTX12_COMPILED_EFFECTS)

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"
#include "CNA/Internal/Renderers/D3DCommon/ID3DDeviceRecoverableEXT.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12PipelineStateCache.hpp"

#include "mojoshader.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#ifdef ERROR
#undef ERROR
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers
{
    class ITextureRenderer;
    class ITexture3DRenderer;
    class ITextureCubeRenderer;
}

namespace CNA::Internal::Renderers::DirectX12
{
    class DirectX12Renderer;

    /** @brief One MojoShader HLSL parse result retained by the Effect Framework runtime. */
    struct D3D12CompiledShaderEXT
    {
        /** @brief Parsed HLSL and shader reflection. */
        const MOJOSHADER_parseData* parseData = nullptr;
        /** @brief Effect-parser reference count. */
        int refCount = 1;
    };

    /** @brief Renderer-wide shader binding and Direct3D 9 constant-register state. */
    struct D3D12MojoShaderContextEXT
    {
        /** @brief Direct3D 9 Shader Model 3 float4-register ceiling. */
        static constexpr int kMaxFloat4Registers = 256;
        /** @brief Direct3D 9 Shader Model 3 int4-register ceiling. */
        static constexpr int kMaxInt4Registers = 16;
        /** @brief Direct3D 9 Shader Model 3 bool-register ceiling. */
        static constexpr int kMaxBoolRegisters = 16;

        D3D12CompiledShaderEXT* boundVertex = nullptr;
        D3D12CompiledShaderEXT* boundPixel = nullptr;
        std::array<float, kMaxFloat4Registers * 4> vsRegF{};
        std::array<int, kMaxInt4Registers * 4> vsRegI{};
        std::array<unsigned char, kMaxBoolRegisters> vsRegB{};
        std::array<float, kMaxFloat4Registers * 4> psRegF{};
        std::array<int, kMaxInt4Registers * 4> psRegI{};
        std::array<unsigned char, kMaxBoolRegisters> psRegB{};
        std::string lastError;
    };

    /** @brief One device-local compiled XNA effect translated to D3D12-accepted DXBC. */
    class D3D12CompiledEffect final : public ICompiledEffectRuntime,
                                      public D3DCommon::ID3DDeviceRecoverableEXT
    {
    public:
        /**
         * @brief Parses Effect Framework bytecode for the owning D3D12 renderer.
         * @param renderer Owning renderer.
         * @param effectCode Compiled effect bytes.
         * @param effectCodeLength Number of bytes in @p effectCode.
         */
        D3D12CompiledEffect(DirectX12Renderer& renderer,
                            const std::uint8_t* effectCode,
                            std::size_t effectCodeLength);

        /** @brief Releases the parsed effect and device-local root signatures. */
        ~D3D12CompiledEffect() override;

        D3D12CompiledEffect(const D3D12CompiledEffect&) = delete;
        D3D12CompiledEffect& operator=(const D3D12CompiledEffect&) = delete;

        /** @brief Creates an independent copy with the same values and technique. */
        [[nodiscard]] std::unique_ptr<ICompiledEffectRuntime> Clone() const override;
        /** @brief Returns reflection used by the public Effect object graph. */
        [[nodiscard]] const CompiledEffectDescription& GetDescription() const override;
        /** @brief Selects a reflected technique by zero-based index. */
        void SetTechnique(std::uint32_t techniqueIndex) override;
        /** @brief Replaces one reflected parameter's raw padded value. */
        void SetParameterValue(std::uint32_t runtimeIndex,
                               const void* data,
                               std::size_t dataBytes) override;
        /** @brief Associates a texture parameter with a D3D12-owned texture. */
        void SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture) override;
        /** @brief Applies one pass and returns every public device state it assigns. */
        void ApplyPass(std::uint32_t passIndex,
                       const CompiledEffectDeviceState& deviceState,
                       CompiledEffectPassStateChanges& changes) override;

        /** @brief Releases root signatures tied to the removed D3D12 device. */
        void ReleaseDeviceResourcesEXT() noexcept override;
        /** @brief Leaves device-independent HLSL and DXBC ready for lazy root recreation. */
        void RecreateDeviceResourcesEXT() override;

    private:
        friend class DirectX12Renderer;

        enum class TextureKind
        {
            None,
            Texture2D,
            Texture3D,
            TextureCube,
        };

        struct TextureBinding
        {
            TextureKind kind = TextureKind::None;
            Texture* source = nullptr;
            std::shared_ptr<ITextureRenderer> texture2D;
            std::shared_ptr<ITexture3DRenderer> texture3D;
            std::shared_ptr<ITextureCubeRenderer> textureCube;
        };

        struct RootBinding
        {
            std::uint32_t slot = 0;
            std::uint32_t rootParameter = 0;
            MOJOSHADER_samplerType kind = MOJOSHADER_SAMPLER_2D;
            bool vertexStage = false;
        };

        struct PairResources
        {
            D3D12CompiledShaderEXT* vertex = nullptr;
            D3D12CompiledShaderEXT* pixel = nullptr;
            Microsoft::WRL::ComPtr<ID3DBlob> vertexBytecode;
            Microsoft::WRL::ComPtr<ID3DBlob> pixelBytecode;
            Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
            std::vector<RootBinding> resources;
            std::vector<RootBinding> samplers;
            std::uint64_t programId = 0;
            int vertexConstantRoot = -1;
            int pixelConstantRoot = -1;
        };

        D3D12CompiledEffect(DirectX12Renderer& renderer,
                            const D3D12CompiledEffect& cloneSource);
        void BuildDescriptionAndBackend(const std::uint8_t* effectCode,
                                        std::size_t effectCodeLength);
        PairResources& GetOrCreatePairEXT();
        void CaptureUniformSnapshotEXT(std::vector<std::uint8_t>& vertexBytes,
                                       std::vector<std::uint8_t>& pixelBytes) const;

        DirectX12Renderer& renderer_;
        std::weak_ptr<void> ownerLifetime_;
        std::shared_ptr<D3D12MojoShaderContextEXT> contextOwner_;
        D3D12MojoShaderContextEXT* context_ = nullptr;
        MOJOSHADER_effect* effectData_ = nullptr;
        MOJOSHADER_effectStateChanges stateChanges_{};
        CompiledEffectDescription description_;
        std::unordered_map<std::string, std::uint32_t> samplerTextureParameters_;
        std::vector<Texture*> textures_;
        std::uint32_t techniqueIndex_ = 0;
        bool passActive_ = false;
        std::map<std::pair<D3D12CompiledShaderEXT*, D3D12CompiledShaderEXT*>, PairResources>
            pairs_;

        static constexpr std::size_t kSamplerSlots =
            Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers;
        std::array<TextureBinding, kSamplerSlots> boundTextures_{};
        std::array<TextureBinding, kSamplerSlots> boundVertexTextures_{};
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState, kSamplerSlots>
            boundSamplers_{};
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState, kSamplerSlots>
            boundVertexSamplers_{};
        std::array<bool, kSamplerSlots> samplerAssigned_{};
        std::array<bool, kSamplerSlots> vertexSamplerAssigned_{};
    };
}

#endif  // CNA_DIRECTX12_COMPILED_EFFECTS
