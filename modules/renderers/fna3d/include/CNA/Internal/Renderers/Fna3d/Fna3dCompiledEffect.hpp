// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dApi.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::Fna3d
{
    class Fna3dRenderer;

    /**
     * @brief Device-local execution and reflection for one D3D9 Effect Framework binary.
     *
     * FNA3D already embeds MojoShader's Effect Framework compiler. This class deliberately keeps
     * every MojoShader/FNA3D type on the renderer side of ICompiledEffectRuntime: the shared XNA
     * object model sees stable indices and renderer-neutral descriptions only.
     */
    class Fna3dCompiledEffect final : public ICompiledEffectRuntime
    {
    public:
        Fna3dCompiledEffect(Fna3dRenderer& renderer, const std::uint8_t* effectCode,
                            std::size_t effectCodeLength);
        ~Fna3dCompiledEffect() override;

        Fna3dCompiledEffect(const Fna3dCompiledEffect&) = delete;
        Fna3dCompiledEffect& operator=(const Fna3dCompiledEffect&) = delete;

        [[nodiscard]] std::unique_ptr<ICompiledEffectRuntime> Clone() const override;
        [[nodiscard]] const CompiledEffectDescription& GetDescription() const override;
        void SetTechnique(std::uint32_t techniqueIndex) override;
        void SetParameterValue(std::uint32_t runtimeIndex, const void* data,
                               std::size_t dataBytes) override;
        void SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture) override;
        void ApplyPass(std::uint32_t passIndex,
                       const CompiledEffectDeviceState& deviceState,
                       CompiledEffectPassStateChanges& changes) override;

    private:
        explicit Fna3dCompiledEffect(Fna3dRenderer& renderer,
                                     const Fna3dCompiledEffect& cloneSource);

        void ValidateNativeEffect(const char* operation);
        void BuildDescription();
        void BuildSamplerMap();
        void ApplyRenderStates(const CompiledEffectDeviceState& deviceState,
                               CompiledEffectPassStateChanges& changes);
        void ApplySamplers(const MOJOSHADER_samplerStateRegister* changeList,
                           std::uint32_t count, bool vertexStage,
                           const CompiledEffectDeviceState& deviceState,
                           CompiledEffectPassStateChanges& changes);
        void ApplyNativeSampler(std::size_t slot, bool vertexStage,
                                const SamplerState& sampler, Texture* texture);

        Fna3dRenderer& renderer_;
        FNA3D_Device* device_ = nullptr;
        FNA3D_Effect* effect_ = nullptr;
        MOJOSHADER_effect* effectData_ = nullptr;
        MOJOSHADER_effectStateChanges stateChanges_{};
        CompiledEffectDescription description_;
        std::unordered_map<std::string, std::uint32_t> samplerTextureParameters_;
        std::vector<Texture*> textures_;
        std::uint32_t techniqueIndex_ = 0;
    };
}
