// SPDX-License-Identifier: MS-PL
//
// plan_fx.md FX-065: compiled XNA Effect Framework bytecode on the Vulkan renderer.
//
// This backend differs from the SDL_GPU and OpenGL ones in the way that matters most to read the
// code: **MojoShader ships no Vulkan adapter.** There is no `mojoshader_vulkan.c` beside
// `mojoshader_opengl.c` and `mojoshader_sdlgpu.c`, so there is no ready-made
// `MOJOSHADER_effectShaderContext` to hand the effect parser and no library-side shader/program
// bookkeeping to lean on. The nine-function backend below is CNA's own, written directly against
// `MOJOSHADER_parse()` with the portable SPIR-V profile, and everything the other two get from
// their adapter -- shader ref-counting, the bound vertex/pixel pair, the flat constant register
// files handed out through `mapUniformBufferMemory` -- is implemented here.
//
// FX-064's existence gate (`tools/graphics/mojoshader_vulkan_probe.cpp`) proved that shape against
// a real Vulkan device with the Khronos validation layer enabled throughout, rendering all three
// technique/pass combinations of `CnaConformanceEffect.fx` to pixels byte-identical to the SDL_GPU
// and OpenGL backends'. This file is that prototype turned into a runtime.
//
// The whole translation unit is guarded rather than excluded from the source glob, so the
// renderer's source list stays the plain directory contents every other renderer family uses.

#pragma once

#if defined(CNA_VULKAN_COMPILED_EFFECTS)

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include "mojoshader.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::Vulkan
{
    class VulkanRenderer;

    /**
     * @brief One shader compiled by CNA's own MojoShader SPIR-V backend. CNAEXT.
     *
     * plan_fx.md FX-065. MojoShader's other adapters own a type like this; the Vulkan one does not
     * exist, so the reference counting the effect parser expects (`shaderAddRef`/`deleteShader`)
     * lives here. `VkShaderModule` creation is deferred to the first draw that needs it, because
     * parsing happens while the effect is built and a module is only useful once a pipeline wants
     * one.
     */
    struct VulkanCompiledShaderEXT
    {
        /** @brief MojoShader's reflection and SPIR-V output for this shader. */
        const MOJOSHADER_parseData* parseData = nullptr;
        /** @brief Lazily created module over @ref parseData's SPIR-V words. */
        VkShaderModule module = VK_NULL_HANDLE;
        /** @brief Effect-parser reference count; the shader dies at zero. */
        int refcount = 1;
    };

    /**
     * @brief The MojoShader effect backend's per-renderer state. CNAEXT.
     *
     * plan_fx.md FX-065. One instance per `VulkanRenderer`, shared by every compiled effect that
     * renderer owns -- the same arrangement `MOJOSHADER_glContext` and `MOJOSHADER_sdlContext` give
     * the other two backends. The constant register files are therefore SHARED, which is exactly
     * why a deferred draw must snapshot its uniforms at apply time (see
     * VulkanCompiledEffect::CaptureUniformSnapshotEXT).
     */
    struct VulkanMojoShaderContextEXT
    {
        /** @brief Direct3D 9 Shader Model 3's own constant-register ceilings. */
        static constexpr int kMaxFloat4Registers = 256;
        /** @brief Direct3D 9 Shader Model 3's own constant-register ceilings. */
        static constexpr int kMaxInt4Registers = 16;
        /** @brief Direct3D 9 Shader Model 3's own constant-register ceilings. */
        static constexpr int kMaxBoolRegisters = 16;

        /** @brief The device every shader module is created on. */
        VkDevice device = VK_NULL_HANDLE;
        /** @brief Currently bound vertex shader, or null. */
        VulkanCompiledShaderEXT* boundVertex = nullptr;
        /** @brief Currently bound pixel shader, or null. */
        VulkanCompiledShaderEXT* boundPixel = nullptr;
        /** @brief Vertex-stage float4 constant register file. */
        std::array<float, kMaxFloat4Registers * 4> vsRegF{};
        /** @brief Vertex-stage int4 constant register file. */
        std::array<int, kMaxInt4Registers * 4> vsRegI{};
        /** @brief Vertex-stage bool constant register file. */
        std::array<unsigned char, kMaxBoolRegisters> vsRegB{};
        /** @brief Pixel-stage float4 constant register file. */
        std::array<float, kMaxFloat4Registers * 4> psRegF{};
        /** @brief Pixel-stage int4 constant register file. */
        std::array<int, kMaxInt4Registers * 4> psRegI{};
        /** @brief Pixel-stage bool constant register file. */
        std::array<unsigned char, kMaxBoolRegisters> psRegB{};
        /** @brief Last parser/translation failure, returned through the backend's getError hook. */
        std::string lastError;
    };

    /**
     * @brief One compiled XNA effect owned by the Vulkan renderer.
     *
     * plan_fx.md FX-065. Mirrors `SdlGpuCompiledEffect`'s shape deliberately: both renderers defer
     * GPU submission, so both must snapshot a pass's uniforms and sampler bindings at apply time
     * rather than reading them back when the draw is finally recorded.
     */
    class VulkanCompiledEffect final : public ICompiledEffectRuntime
    {
    public:
        /**
         * @brief Parses an Effect Framework binary and compiles its shaders to SPIR-V.
         *
         * @param renderer Owning renderer; supplies the shared backend context and the device.
         * @param effectCode Compiled effect bytes.
         * @param effectCodeLength Number of bytes in @p effectCode.
         * @throws std::invalid_argument if the buffer is empty or implausibly large.
         * @throws std::runtime_error if the parser rejects the content.
         */
        VulkanCompiledEffect(VulkanRenderer& renderer,
                             const std::uint8_t* effectCode,
                             std::size_t effectCodeLength);

        /** @brief Releases the native effect and its shaders. */
        ~VulkanCompiledEffect() override;

        VulkanCompiledEffect(const VulkanCompiledEffect&) = delete;
        VulkanCompiledEffect& operator=(const VulkanCompiledEffect&) = delete;

        /** @brief Creates an independent copy carrying the same current values. */
        [[nodiscard]] std::unique_ptr<ICompiledEffectRuntime> Clone() const override;

        /** @brief Returns the reflection the public Effect object graph is built from. */
        [[nodiscard]] const CompiledEffectDescription& GetDescription() const override;

        /**
         * @brief Selects a technique by its stable zero-based index.
         * @param techniqueIndex Index into the reflected technique list.
         * @throws std::out_of_range if the index is not a technique of this effect.
         */
        void SetTechnique(std::uint32_t techniqueIndex) override;

        /**
         * @brief Replaces one top-level parameter's padded raw value storage.
         * @param runtimeIndex Reflected parameter index.
         * @param data Padded value bytes.
         * @param dataBytes Number of bytes at @p data.
         * @throws std::out_of_range if the parameter does not exist.
         * @throws std::invalid_argument if the value does not fit the parameter's storage.
         */
        void SetParameterValue(std::uint32_t runtimeIndex,
                               const void* data,
                               std::size_t dataBytes) override;

        /**
         * @brief Associates a texture parameter with a texture this renderer owns.
         * @param runtimeIndex Reflected parameter index.
         * @param texture Texture to bind, or null to clear.
         * @throws std::out_of_range if the parameter does not exist.
         * @throws std::invalid_argument if the parameter is not a texture, or the texture was not
         *         created by this renderer.
         */
        void SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture) override;

        /**
         * @brief Applies one pass of the selected technique and reports the state it assigned.
         * @param passIndex Zero-based pass index inside the selected technique.
         * @param deviceState State groups currently selected on the owning GraphicsDevice.
         * @param changes Receives every state group the pass assigned.
         * @throws std::out_of_range if the pass does not exist.
         * @throws std::runtime_error if the native apply reports implausible state changes.
         */
        void ApplyPass(std::uint32_t passIndex,
                       const CompiledEffectDeviceState& deviceState,
                       CompiledEffectPassStateChanges& changes) override;

        /**
         * @brief CNAEXT. Returns the shader pair the last applied pass bound, or nulls.
         *
         * @param vertex Receives the vertex shader, or null.
         * @param pixel Receives the pixel shader, or null.
         */
        CNAEXT void GetBoundShadersEXT(VulkanCompiledShaderEXT*& vertex,
                                       VulkanCompiledShaderEXT*& pixel) const;

        /**
         * @brief CNAEXT. Packs the applied pass's constant registers into their uniform-buffer bytes.
         *
         * plan_fx.md FX-065. This renderer records a draw's commands at `Present()`, long after
         * `ApplyPass()` ran, and the constant register files are shared by every effect this
         * renderer owns -- so a later apply would otherwise decide what an earlier draw renders
         * with. Packing here, while the registers still hold exactly what this apply wrote, is what
         * makes a deferred compiled draw self-contained.
         *
         * The layout is MojoShader's own SPIR-V uniform block: each declared uniform occupies
         * `array_count` (or one) consecutive 16-byte slots, in declaration order, with a bool
         * register occupying only the low four bytes of its slot.
         *
         * @param vertexBytes Receives the vertex-shader uniform bytes; empty if it declares none.
         * @param pixelBytes Receives the pixel-shader uniform bytes; empty if it declares none.
         */
        CNAEXT void CaptureUniformSnapshotEXT(std::vector<std::uint8_t>& vertexBytes,
                                              std::vector<std::uint8_t>& pixelBytes) const;

        /**
         * @brief CNAEXT. Returns the texture and sampler state currently bound to one sampler slot.
         *
         * Same contract, and same reason, as `SdlGpuCompiledEffect::GetBoundSamplerEXT`: a compiled
         * effect's bindings are arbitrary and do not travel through `GpuDrawParams`' fixed slots,
         * and a pass that reassigns nothing leaves an earlier pass's binding standing.
         *
         * @param slot Sampler register index.
         * @param vertexStage True for a vertex-stage sampler, false for a pixel-stage one.
         * @param texture Receives the bound texture, or null if the slot was never assigned.
         * @param sampler Receives the bound sampler state.
         * @param samplerAssigned Receives whether a pass actually assigned this slot's state.
         */
        CNAEXT void GetBoundSamplerEXT(
            std::uint32_t slot, bool vertexStage, Texture*& texture,
            Microsoft::Xna::Framework::Graphics::SamplerState& sampler,
            bool* samplerAssigned = nullptr) const;

        /**
         * @brief CNAEXT. What one compiled-effect draw needs from the applied pass.
         *
         * plan_fx.md FX-065. Produced by @ref LinkAndGetShadersEXT and stored by value on the
         * deferred draw command, because the draw is recorded at `Present()` long after the pass
         * was applied.
         */
        struct LinkedPassEXT
        {
            /** @brief Vertex shader module over the linked SPIR-V. */
            VkShaderModule vertexModule = VK_NULL_HANDLE;
            /** @brief Fragment shader module over the linked SPIR-V. */
            VkShaderModule pixelModule = VK_NULL_HANDLE;
            /** @brief Vertex-stage entry point name. */
            const char* vertexEntryPoint = "main";
            /** @brief Fragment-stage entry point name. */
            const char* pixelEntryPoint = "main";
            /** @brief One attribute per shader input, in the shader's own location order. */
            std::vector<VkVertexInputAttributeDescription> vertexAttributes;
            /** @brief Reflected pixel-stage samplers, in ascending register order. */
            std::vector<MOJOSHADER_sampler> pixelSamplers;
            /** @brief Whether the vertex shader declares a uniform block. */
            bool vertexHasUniforms = false;
            /** @brief Whether the pixel shader declares a uniform block. */
            bool pixelHasUniforms = false;
            /** @brief Identity of the shader pair, for the pipeline cache key. */
            std::uint64_t pipelineKey = 0;
        };

        /**
         * @brief CNAEXT. Links the applied pass's shader pair and returns everything a draw needs.
         *
         * plan_fx.md FX-065. `MOJOSHADER_linkSPIRVShaders` is a separate, explicit step from
         * parsing: it patches the vertex shader's input types to the actual vertex format, links
         * vertex outputs to pixel inputs, and returns the size of the internal patch table that
         * must be subtracted from `output_len` before the SPIR-V is handed to Vulkan. Only after it
         * has run are the modules meaningful, which is why they are created here rather than at
         * parse time.
         *
         * @param declaredElements The caller's `VertexDeclaration` elements for this draw.
         * @return The linked pass.
         * @throws std::runtime_error if no pass is applied or linking fails.
         * @throws System::NotSupportedException if @p declaredElements does not supply an input the
         *         vertex shader consumes.
         */
        CNAEXT [[nodiscard]] LinkedPassEXT LinkAndGetShadersEXT(
            const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>&
                declaredElements) const;

    private:
        VulkanCompiledEffect(VulkanRenderer& renderer, const VulkanCompiledEffect& cloneSource);

        void BuildDescriptionAndBackend(const std::uint8_t* effectCode,
                                        std::size_t effectCodeLength);

        VulkanRenderer& renderer_;
        VulkanMojoShaderContextEXT* context_ = nullptr;
        MOJOSHADER_effect* effectData_ = nullptr;
        MOJOSHADER_effectStateChanges stateChanges_{};
        CompiledEffectDescription description_;
        std::unordered_map<std::string, std::uint32_t> samplerTextureParameters_;
        std::vector<Texture*> textures_;
        std::uint32_t techniqueIndex_ = 0;
        bool passActive_ = false;
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState, 16> boundSamplers_{};
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState, 16> boundVertexSamplers_{};
        std::array<Texture*, 16> boundSamplerTextures_{};
        std::array<Texture*, 16> boundVertexSamplerTextures_{};
        std::array<bool, 16> samplerAssigned_{};
        std::array<bool, 16> vertexSamplerAssigned_{};
    };
}

#endif  // CNA_VULKAN_COMPILED_EFFECTS
