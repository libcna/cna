// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file D3D11CompiledEffect.hpp
 * @brief Compiled XNA Effect Framework bytecode executed by MojoShader's D3D11 adapter.
 */

#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"
#include "CNA/Internal/Renderers/D3DCommon/ID3DDeviceRecoverableEXT.hpp"

#include "mojoshader.h"

#include <d3d11.h>
#include <wrl/client.h>

#ifdef ERROR
#undef ERROR
#endif

#include <array>
#include <cstddef>
#include <cstdint>
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

namespace CNA::Internal::Renderers::DirectX11
{
    class DirectX11Renderer;

    /** @brief One device-local compiled XNA effect backed by MojoShader's D3D11 adapter. */
    class D3D11CompiledEffect final : public ICompiledEffectRuntime,
                                      public D3DCommon::ID3DDeviceRecoverableEXT
    {
    public:
        /**
         * @brief Parses and compiles Effect Framework bytecode for a D3D11 device.
         * @param renderer Owning D3D11 renderer.
         * @param effectCode Compiled effect bytes.
         * @param effectCodeLength Number of bytes in @p effectCode.
         */
        D3D11CompiledEffect(DirectX11Renderer& renderer,
                            const std::uint8_t* effectCode,
                            std::size_t effectCodeLength);

        /** @brief Releases the parsed effect and its native shaders. */
        ~D3D11CompiledEffect() override;

        D3D11CompiledEffect(const D3D11CompiledEffect&) = delete;
        D3D11CompiledEffect& operator=(const D3D11CompiledEffect&) = delete;

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

        /** @brief Associates a texture parameter with a D3D11-owned texture. */
        void SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture) override;

        /** @brief Applies one pass and returns every public device state it assigns. */
        void ApplyPass(std::uint32_t passIndex,
                       const CompiledEffectDeviceState& deviceState,
                       CompiledEffectPassStateChanges& changes) override;

        /** @brief Releases device-dependent effect and input-layout resources. */
        void ReleaseDeviceResourcesEXT() noexcept override;

        /** @brief Recompiles the effect and restores values after device recreation. */
        void RecreateDeviceResourcesEXT() override;

    private:
        friend class DirectX11Renderer;

        D3D11CompiledEffect(DirectX11Renderer& renderer,
                            const D3D11CompiledEffect& cloneSource);
        void CreateNativeEffectEXT();
        [[nodiscard]] ID3D11InputLayout* BindShadersAndLayoutEXT(
            const std::vector<D3D11_INPUT_ELEMENT_DESC>& elements);

        DirectX11Renderer& renderer_;
        std::weak_ptr<void> ownerLifetime_;
        MOJOSHADER_d3d11Context* context_ = nullptr;
        MOJOSHADER_effect* effectData_ = nullptr;
        std::shared_ptr<const std::vector<std::uint8_t>> effectCode_;
        std::vector<std::vector<std::uint8_t>> parameterValues_;
        MOJOSHADER_effectStateChanges stateChanges_{};
        CompiledEffectDescription description_;
        std::unordered_map<std::string, std::uint32_t> samplerTextureParameters_;
        std::vector<Texture*> textures_;
        std::uint32_t techniqueIndex_ = 0;
        bool passActive_ = false;

        std::unordered_map<std::uint64_t, Microsoft::WRL::ComPtr<ID3D11InputLayout>>
            inputLayouts_;

        static constexpr std::size_t kSamplerSlots =
            Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers;
        std::array<Texture*, kSamplerSlots> boundTextures_{};
        std::array<Texture*, kSamplerSlots> boundVertexTextures_{};
        std::array<std::shared_ptr<ITextureRenderer>, kSamplerSlots> boundTexture2DResources_{};
        std::array<std::shared_ptr<ITexture3DRenderer>, kSamplerSlots> boundTexture3DResources_{};
        std::array<std::shared_ptr<ITextureCubeRenderer>, kSamplerSlots> boundTextureCubeResources_{};
        std::array<std::shared_ptr<ITextureRenderer>, kSamplerSlots>
            boundVertexTexture2DResources_{};
        std::array<std::shared_ptr<ITexture3DRenderer>, kSamplerSlots>
            boundVertexTexture3DResources_{};
        std::array<std::shared_ptr<ITextureCubeRenderer>, kSamplerSlots>
            boundVertexTextureCubeResources_{};
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState, kSamplerSlots>
            boundSamplers_{};
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState, kSamplerSlots>
            boundVertexSamplers_{};
        std::array<bool, kSamplerSlots> samplerAssigned_{};
        std::array<bool, kSamplerSlots> vertexSamplerAssigned_{};
    };
}

#endif  // CNA_DIRECTX11_COMPILED_EFFECTS
