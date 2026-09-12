// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file RlglCompiledEffect.hpp
 * @brief Optional classic XNA Effect Framework runtime for the standalone-rlgl renderer.
 */

#if defined(CNA_RLGL_COMPILED_EFFECTS)

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"

#include "mojoshader.h"

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
    class ITextureCubeRenderer;
}

namespace CNA::Internal::Renderers::Rlgl
{
    class RlglRenderer;

    /**
     * @brief One device-bound XNA Effect Framework binary compiled by MojoShader for RLGL's GL context.
     */
    class RlglCompiledEffect final : public ICompiledEffectRuntime
    {
    public:
        /**
         * @brief Parses and compiles an Effect Framework binary for the renderer's GL context.
         * @param renderer Owning RLGL renderer.
         * @param effectCode Compiled XNA effect bytes.
         * @param effectCodeLength Number of bytes at @p effectCode.
         */
        RlglCompiledEffect(RlglRenderer& renderer,
                           const std::uint8_t* effectCode,
                           std::size_t effectCodeLength);

        /** @brief Releases the MojoShader effect while its device context is alive. */
        ~RlglCompiledEffect() override;

        /** @brief Compiled effect runtimes cannot be copy-constructed directly; use Clone. */
        RlglCompiledEffect(const RlglCompiledEffect&) = delete;

        /** @brief Compiled effect runtimes cannot be copy-assigned. */
        RlglCompiledEffect& operator=(const RlglCompiledEffect&) = delete;

        /**
         * @brief Creates an independent runtime with the same current parameters and technique.
         * @return Newly compiled device-local clone.
         */
        [[nodiscard]] std::unique_ptr<ICompiledEffectRuntime> Clone() const override;

        /**
         * @brief Returns renderer-neutral parameters, techniques, passes, and annotations.
         * @return Immutable reflected description.
         */
        [[nodiscard]] const CompiledEffectDescription& GetDescription() const override;

        /**
         * @brief Selects a reflected technique.
         * @param techniqueIndex Zero-based technique index.
         */
        void SetTechnique(std::uint32_t techniqueIndex) override;

        /**
         * @brief Replaces one parameter's padded Effect Framework value storage.
         * @param runtimeIndex Reflected parameter index.
         * @param data New raw value cells.
         * @param dataBytes Number of bytes at @p data.
         */
        void SetParameterValue(std::uint32_t runtimeIndex,
                               const void* data,
                               std::size_t dataBytes) override;

        /**
         * @brief Associates a reflected texture parameter with an RLGL texture resource.
         * @param runtimeIndex Reflected parameter index.
         * @param texture Texture wrapper, or null to clear the parameter value.
         */
        void SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture) override;

        /**
         * @brief Applies a pass and translates its legacy render and sampler state assignments.
         * @param passIndex Zero-based pass index in the selected technique.
         * @param deviceState Current GraphicsDevice state used as the translation baseline.
         * @param changes Receives only the state groups assigned by the pass.
         */
        void ApplyPass(std::uint32_t passIndex,
                       const CompiledEffectDeviceState& deviceState,
                       CompiledEffectPassStateChanges& changes) override;

        /**
         * @brief Returns the persistent texture and sampler assigned to one shader register.
         * @param slot Sampler register index.
         * @param vertexStage True for a vertex sampler and false for a pixel sampler.
         * @param texture Receives the public texture selected by the pass.
         * @param sampler Receives the assigned sampler value.
         * @param samplerAssigned Receives whether any applied pass assigned the sampler state.
         */
        CNAEXT void GetBoundSamplerEXT(
            std::uint32_t slot, bool vertexStage, Texture*& texture,
            Microsoft::Xna::Framework::Graphics::SamplerState& sampler,
            bool& samplerAssigned) const;

    private:
        friend class RlglRenderer;

        RlglCompiledEffect(RlglRenderer& renderer, const RlglCompiledEffect& cloneSource);
        void CreateNativeEffect();

        RlglRenderer& renderer_;
        MOJOSHADER_glContext* context_ = nullptr;
        MOJOSHADER_effect* effectData_ = nullptr;
        std::shared_ptr<const std::vector<std::uint8_t>> effectCode_;
        std::vector<std::vector<std::uint8_t>> parameterValues_;
        MOJOSHADER_effectStateChanges stateChanges_{};
        CompiledEffectDescription description_;
        std::unordered_map<std::string, std::uint32_t> samplerTextureParameters_;
        std::vector<Texture*> textures_;
        std::uint32_t techniqueIndex_ = 0;
        bool passActive_ = false;
        std::array<Texture*, Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            boundTextures_{};
        std::array<Texture*, Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            boundVertexTextures_{};
        std::array<std::shared_ptr<ITextureRenderer>,
                   Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            boundTexture2DResources_{};
        std::array<std::shared_ptr<ITextureCubeRenderer>,
                   Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            boundTextureCubeResources_{};
        std::array<std::shared_ptr<ITextureRenderer>,
                   Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            boundVertexTexture2DResources_{};
        std::array<std::shared_ptr<ITextureCubeRenderer>,
                   Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            boundVertexTextureCubeResources_{};
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState,
                   Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            boundSamplers_{};
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState,
                   Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            boundVertexSamplers_{};
        std::array<bool, Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            samplerAssigned_{};
        std::array<bool, Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            vertexSamplerAssigned_{};
    };
}

#endif
