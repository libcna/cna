// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-167..171: compiled XNA Effect Framework bytecode on the WebGPU renderer.
//
// This backend is shaped like the Vulkan one (`plans/plan_fx.md` `FX-065`) rather than like the OpenGL or
// SDL_GPU ones, because it faces the same two facts: MojoShader ships no WebGPU adapter, so the
// nine-function `MOJOSHADER_effectShaderContext` is CNA's own, written directly against
// `MOJOSHADER_parse()` with the portable SPIR-V profile; and the renderer defers its draws to
// `Present()`, so a pass has to SNAPSHOT its uniforms and sampler bindings at apply time rather
// than read them back when the draw is finally recorded.
//
// One thing is genuinely different from Vulkan, and it is the whole reason `WEBGPU-166` existed:
// **WGSL has no combined image sampler.** MojoShader's SPIR-V emits one `OpTypeSampledImage` global
// per D3D9 sampler register -- Vulkan's `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` shape -- and
// naga's SPIR-V frontend refuses to load one, reporting `invalid id %N`. Every linked pass therefore
// runs through `MojoShaderEffect::SplitCombinedImageSamplers()` before its shader modules are
// created, which turns each combined global into a texture binding plus a sampler binding and
// reports which bindings it chose. That rewrite is the only translation step between MojoShader's
// output and a WebGPU shader module; everything else passes naga untouched.
//
// MojoShader's four fixed descriptor sets (`mojoshader_profile_spirv.h`) map one-to-one onto
// WebGPU's four bind groups, which is exactly core WebGPU's `maxBindGroups`:
//
//     group 0 -- vertex-stage samplers      group 1 -- vertex-stage uniform block
//     group 2 -- pixel-stage samplers       group 3 -- pixel-stage uniform block
//
// The whole translation unit is guarded rather than excluded from the source glob, so the
// renderer's source list stays the plain directory contents every other renderer family uses.

#pragma once

#if defined(CNA_WEBGPU_COMPILED_EFFECTS)

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include "mojoshader.h"

// The header's own name differs between wgpu-native packages and emdawnwebgpu, exactly as
// WebGPURenderer.hpp already has to account for.
#if __has_include(<webgpu/webgpu.h>)
#include <webgpu/webgpu.h>
#elif __has_include(<webgpu-headers/webgpu.h>)
#include <webgpu-headers/webgpu.h>
#elif __has_include(<webgpu.h>)
#include <webgpu.h>
#else
#error "CNA WebGPU compiled effects require webgpu.h"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::WebGPU
{
    class WebGPURenderer;

    /**
     * @brief One shader compiled by CNA's own MojoShader SPIR-V backend. CNAEXT.
     *
     * plans/plan_webgpu.md WEBGPU-167. MojoShader's own adapters own a type like this; there is no
     * WebGPU adapter, so the reference counting the effect parser expects
     * (`shaderAddRef`/`deleteShader`) lives here. There is deliberately no `WGPUShaderModule` on
     * this type: linking patches the SPIR-V in place and the combined-sampler split rewrites it
     * again, so a module belongs to the FINISHED BYTES rather than to the shader, and the renderer
     * owns one per distinct body (`GetOrCreateCompiledEffectShaderModuleEXT`).
     */
    struct WebGPUCompiledShaderEXT
    {
        /** @brief MojoShader's reflection and SPIR-V output for this shader. */
        const MOJOSHADER_parseData* parseData = nullptr;
        /** @brief Effect-parser reference count; the shader dies at zero. */
        int refcount = 1;
    };

    /**
     * @brief The MojoShader effect backend's per-renderer state. CNAEXT.
     *
     * plans/plan_webgpu.md WEBGPU-167. One instance per `WebGPURenderer`, shared by every compiled
     * effect that renderer owns -- the same arrangement `MOJOSHADER_glContext` gives the OpenGL
     * backend. The constant register files are therefore SHARED, which is exactly why a deferred
     * draw must snapshot its uniforms at apply time (see
     * WebGPUCompiledEffect::CaptureUniformSnapshotEXT).
     */
    struct WebGPUMojoShaderContextEXT
    {
        /** @brief Direct3D 9 Shader Model 3's own constant-register ceilings. */
        static constexpr int kMaxFloat4Registers = 256;
        /** @brief Direct3D 9 Shader Model 3's own constant-register ceilings. */
        static constexpr int kMaxInt4Registers = 16;
        /** @brief Direct3D 9 Shader Model 3's own constant-register ceilings. */
        static constexpr int kMaxBoolRegisters = 16;

        /** @brief Currently bound vertex shader, or null. */
        WebGPUCompiledShaderEXT* boundVertex = nullptr;
        /** @brief Currently bound pixel shader, or null. */
        WebGPUCompiledShaderEXT* boundPixel = nullptr;
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
     * @brief One D3D9 sampler register after the combined-sampler split, as a draw must bind it.
     * CNAEXT.
     */
    struct WebGPUCompiledSamplerBindingEXT
    {
        /** @brief The D3D9 sampler register (`s0`..`s15`) this pair came from. */
        std::uint32_t slot = 0;
        /** @brief Bind-group binding the texture view goes to. */
        std::uint32_t textureBinding = 0;
        /** @brief Bind-group binding the sampler goes to. */
        std::uint32_t samplerBinding = 0;
        /** @brief View dimension the shader declared, so a mismatched texture is refused by name. */
        WGPUTextureViewDimension viewDimension = WGPUTextureViewDimension_2D;
    };

    /**
     * @brief One vertex stream of a linked compiled pass, ready to become a `WGPUVertexBufferLayout`.
     * CNAEXT.
     */
    struct WebGPUCompiledVertexStreamLayoutEXT
    {
        /** @brief The attributes this stream feeds, with the shader's own locations. */
        std::vector<WGPUVertexAttribute> attributes;
        /** @brief This stream's byte stride. */
        std::uint64_t arrayStride = 0;
        /** @brief True for a per-instance stream. */
        bool perInstance = false;
    };

    /**
     * @brief One compiled XNA effect owned by the WebGPU renderer.
     *
     * plans/plan_webgpu.md WEBGPU-167..171. Mirrors `VulkanCompiledEffect`'s shape deliberately:
     * both renderers defer GPU submission, so both must snapshot a pass's uniforms and sampler
     * bindings at apply time rather than reading them back when the draw is finally recorded.
     */
    class WebGPUCompiledEffect final : public ICompiledEffectRuntime
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
        WebGPUCompiledEffect(WebGPURenderer& renderer,
                             const std::uint8_t* effectCode,
                             std::size_t effectCodeLength);

        /** @brief Releases the native effect and its shaders. */
        ~WebGPUCompiledEffect() override;

        WebGPUCompiledEffect(const WebGPUCompiledEffect&) = delete;
        WebGPUCompiledEffect& operator=(const WebGPUCompiledEffect&) = delete;

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
         * @throws System::InvalidCastException if the texture's dimension is not the declared one.
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
        CNAEXT void GetBoundShadersEXT(WebGPUCompiledShaderEXT*& vertex,
                                       WebGPUCompiledShaderEXT*& pixel) const;

        /**
         * @brief CNAEXT. Packs the applied pass's constant registers into their uniform-buffer bytes.
         *
         * plans/plan_webgpu.md WEBGPU-168. This renderer records a draw's commands at `Present()`,
         * long after `ApplyPass()` ran, and the constant register files are shared by every effect
         * this renderer owns -- so a later apply would otherwise decide what an earlier draw renders
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
         * A compiled effect's bindings are arbitrary and do not travel through `GpuDrawParams`'
         * fixed slots, and a pass that reassigns nothing leaves an earlier pass's binding standing.
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
         * @brief CNAEXT. One vertex stream a compiled draw reads its attributes from.
         *
         * A compiled effect derives its vertex input from the DECLARATIONS the draw supplies rather
         * than from a byte stride, so it can bind a per-instance stream alongside the per-vertex one
         * without the renderer's stock stride-to-layout table having anything to say about it.
         */
        struct CompiledVertexStreamEXT
        {
            /** @brief This stream's declared elements; offsets are relative to this stream. */
            const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>* elements =
                nullptr;
            /** @brief This stream's byte stride. */
            std::uint32_t stride = 0;
            /** @brief True for a per-instance stream. */
            bool perInstance = false;
        };

        /**
         * @brief CNAEXT. What one compiled-effect draw needs from the applied pass.
         *
         * plans/plan_webgpu.md WEBGPU-169. Produced by @ref LinkAndGetShadersEXT and stored by value
         * on the deferred draw command, because the draw is recorded at `Present()` long after the
         * pass was applied.
         */
        struct LinkedPassEXT
        {
            /** @brief Vertex shader module over the linked and split SPIR-V. */
            WGPUShaderModule vertexModule = nullptr;
            /** @brief Fragment shader module over the linked and split SPIR-V. */
            WGPUShaderModule pixelModule = nullptr;
            /** @brief Vertex-stage entry point; MojoShader names it `ShaderFunction<N>`, never "main". */
            std::string vertexEntryPoint;
            /** @brief Fragment-stage entry point; MojoShader names it `ShaderFunction<N>`. */
            std::string pixelEntryPoint;
            /** @brief One layout per supplied stream, in the order the caller supplied them. */
            std::vector<WebGPUCompiledVertexStreamLayoutEXT> streams;
            /** @brief Pixel-stage sampler pairs, ascending by register. */
            std::vector<WebGPUCompiledSamplerBindingEXT> pixelSamplers;
            /** @brief Vertex-stage sampler pairs, ascending by register. */
            std::vector<WebGPUCompiledSamplerBindingEXT> vertexSamplers;
            /** @brief Whether the vertex shader declares a uniform block. */
            bool vertexHasUniforms = false;
            /** @brief Whether the pixel shader declares a uniform block. */
            bool pixelHasUniforms = false;
            /** @brief Identity of the shader pair plus vertex layout, for the pipeline cache key. */
            std::uint64_t pipelineKey = 0;
        };

        /**
         * @brief CNAEXT. Links the applied pass's shader pair and returns everything a draw needs.
         *
         * plans/plan_webgpu.md WEBGPU-169. `MOJOSHADER_linkSPIRVShaders` is a separate, explicit step
         * from parsing: it patches the vertex shader's input types to the actual vertex format,
         * links vertex outputs to pixel inputs, and returns the size of the internal patch table
         * that must be subtracted from `output_len` before the SPIR-V is used. Only then does the
         * combined-sampler split run and the modules become meaningful, which is why they are
         * created here rather than at parse time.
         *
         * @param streams The caller's bound streams, per-vertex first. A shader input is claimed
         *        by the first stream whose declaration carries its usage and usage index.
         * @return The linked pass.
         * @throws std::runtime_error if no pass is applied or linking fails.
         * @throws System::NotSupportedException if no stream supplies an input the vertex shader
         *         consumes, or if the pass declares a vertex-stage sampler.
         */
        CNAEXT [[nodiscard]] LinkedPassEXT LinkAndGetShadersEXT(
            const std::vector<CompiledVertexStreamEXT>& streams) const;

    private:
        WebGPUCompiledEffect(WebGPURenderer& renderer, const WebGPUCompiledEffect& cloneSource);

        void BuildDescriptionAndBackend(const std::uint8_t* effectCode,
                                        std::size_t effectCodeLength);

        WebGPURenderer& renderer_;
        WebGPUMojoShaderContextEXT* context_ = nullptr;
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

#endif  // CNA_WEBGPU_COMPILED_EFFECTS
