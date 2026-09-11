// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file D3D9CompiledEffect.hpp
 * @brief Compiled XNA Effect Framework bytecode executed as native D3D9 shaders.
 */

#if defined(CNA_DIRECTX9_COMPILED_EFFECTS)

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"

#include "mojoshader.h"

#include <d3d9.h>
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

namespace CNA::Internal::Renderers::DirectX9
{
    class DirectX9Renderer;

    /** @brief One parsed shader and its native D3D9 shader object. */
    struct D3D9CompiledShaderEXT
    {
        /** @brief MojoShader reflection retained by the Effect Framework runtime. */
        const MOJOSHADER_parseData* parseData = nullptr;
        /** @brief Native vertex shader when this object represents a vertex stage. */
        Microsoft::WRL::ComPtr<IDirect3DVertexShader9> vertexShader;
        /** @brief Native pixel shader when this object represents a pixel stage. */
        Microsoft::WRL::ComPtr<IDirect3DPixelShader9> pixelShader;
        /** @brief Effect-parser reference count. */
        int refCount = 1;
    };

    /** @brief Shared native device, bound stages, and D3D9 constant-register storage. */
    struct D3D9MojoShaderContextEXT
    {
        /** @brief Direct3D 9 Shader Model 3 float4-register ceiling. */
        static constexpr int kMaxFloat4Registers = 256;
        /** @brief Direct3D 9 Shader Model 3 int4-register ceiling. */
        static constexpr int kMaxInt4Registers = 16;
        /** @brief Direct3D 9 Shader Model 3 bool-register ceiling. */
        static constexpr int kMaxBoolRegisters = 16;

        /** @brief Device retained until every compiled effect has been destroyed. */
        Microsoft::WRL::ComPtr<IDirect3DDevice9> device;
        /** @brief Vertex stage selected by the most recently applied pass. */
        D3D9CompiledShaderEXT* boundVertex = nullptr;
        /** @brief Pixel stage selected by the most recently applied pass. */
        D3D9CompiledShaderEXT* boundPixel = nullptr;
        /** @brief Vertex float4 register file populated by MojoShader. */
        std::array<float, kMaxFloat4Registers * 4> vsRegF{};
        /** @brief Vertex int4 register file populated by MojoShader. */
        std::array<int, kMaxInt4Registers * 4> vsRegI{};
        /** @brief Vertex bool register file populated by MojoShader. */
        std::array<unsigned char, kMaxBoolRegisters> vsRegB{};
        /** @brief Pixel float4 register file populated by MojoShader. */
        std::array<float, kMaxFloat4Registers * 4> psRegF{};
        /** @brief Pixel int4 register file populated by MojoShader. */
        std::array<int, kMaxInt4Registers * 4> psRegI{};
        /** @brief Pixel bool register file populated by MojoShader. */
        std::array<unsigned char, kMaxBoolRegisters> psRegB{};
        /** @brief Last callback diagnostic returned to MojoShader. */
        std::string lastError;
    };

    /** @brief One device-local compiled XNA effect using its original D3D9 token streams. */
    class D3D9CompiledEffect final : public ICompiledEffectRuntime
    {
    public:
        /**
         * @brief Parses and creates the native shaders for compiled Effect Framework bytecode.
         * @param renderer Owning D3D9 renderer.
         * @param effectCode Compiled effect bytes.
         * @param effectCodeLength Number of bytes in @p effectCode.
         */
        D3D9CompiledEffect(DirectX9Renderer& renderer,
                           const std::uint8_t* effectCode,
                           std::size_t effectCodeLength);

        /** @brief Releases the parsed effect and native shaders. */
        ~D3D9CompiledEffect() override;

        D3D9CompiledEffect(const D3D9CompiledEffect&) = delete;
        D3D9CompiledEffect& operator=(const D3D9CompiledEffect&) = delete;

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
        /** @brief Associates a texture parameter with a D3D9-owned texture. */
        void SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture) override;
        /** @brief Applies one pass and returns every public device state it assigns. */
        void ApplyPass(std::uint32_t passIndex,
                       const CompiledEffectDeviceState& deviceState,
                       CompiledEffectPassStateChanges& changes) override;

    private:
        friend class DirectX9Renderer;

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
            std::shared_ptr<ITextureRenderer> texture2D;
            std::shared_ptr<ITexture3DRenderer> texture3D;
            std::shared_ptr<ITextureCubeRenderer> textureCube;
        };

        D3D9CompiledEffect(DirectX9Renderer& renderer,
                           const D3D9CompiledEffect& cloneSource);
        void BuildDescriptionAndBackend(const std::uint8_t* effectCode,
                                        std::size_t effectCodeLength);
        [[nodiscard]] IDirect3DVertexDeclaration9* GetOrCreateDeclarationEXT(
            const std::vector<std::uint64_t>& key,
            const std::vector<D3DVERTEXELEMENT9>& elements);

        DirectX9Renderer& renderer_;
        std::weak_ptr<void> ownerLifetime_;
        std::shared_ptr<D3D9MojoShaderContextEXT> contextOwner_;
        D3D9MojoShaderContextEXT* context_ = nullptr;
        MOJOSHADER_effect* effectData_ = nullptr;
        MOJOSHADER_effectStateChanges stateChanges_{};
        CompiledEffectDescription description_;
        std::unordered_map<std::string, std::uint32_t> samplerTextureParameters_;
        std::vector<Texture*> textures_;
        std::uint32_t techniqueIndex_ = 0;
        bool passActive_ = false;
        std::map<std::vector<std::uint64_t>,
                 Microsoft::WRL::ComPtr<IDirect3DVertexDeclaration9>> declarations_;

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

#endif  // CNA_DIRECTX9_COMPILED_EFFECTS
