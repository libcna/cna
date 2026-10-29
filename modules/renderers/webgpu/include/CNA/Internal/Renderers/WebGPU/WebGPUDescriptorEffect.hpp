// SPDX-License-Identifier: MS-PL
//
// The descriptor-contract `ShaderEffect` route: the explicit bind-group and pipeline layouts built
// from a program's WGSL, and everything a draw by such an effect captures at the public call.
//
// This renderer defers draws -- a bind cycle's draws are recorded into render passes only when the
// cycle is flushed -- so nothing a queued draw reads may point into effect state a later call can
// change. The snapshot below is a copy, the uniform arrays are a shared immutable snapshot
// (copy-on-write in the effect), and every texture is an already-resolved view with its own
// keep-alive. The layouts are shared by every pipeline a program builds and by every queued draw
// that binds resources for it, so they outlive both the program object and any accepted work.

#pragma once

#include "CNA/Internal/Renderers/WebGPU/WebGPUSampledTexture.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPUWgslReflection.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Internal::Renderers::WebGPU
{
    /** @brief One binding of a program layout, merged across the stages that declare it. */
    struct WebGPUBindingSlotEXT
    {
        /** @brief Bind-group index. */
        std::uint32_t group = 0;
        /** @brief Binding index. */
        std::uint32_t binding = 0;
        /** @brief What the shader declared. */
        WgslResourceKind kind = WgslResourceKind::UniformBuffer;
        /** @brief Every stage that declares it. */
        WGPUShaderStage visibility = WGPUShaderStage_None;
        /** @brief Sampled-texture view dimension. */
        WGPUTextureViewDimension viewDimension = WGPUTextureViewDimension_Undefined;
        /** @brief Sampled-texture sample type. */
        WGPUTextureSampleType sampleType = WGPUTextureSampleType_Undefined;
        /** @brief Sampled texture is multisampled. */
        bool multisampled = false;
        /** @brief Buffer minimum binding size. */
        std::uint64_t minBindingSize = 0;
        /**
         * @brief WEBGPUPERF-0005: a uniform block the layout declares with
         *        a dynamic offset, so a draw binds it inside the flush's uniform arena.
         */
        bool dynamicOffset = false;
        /** @brief Variable name, for diagnostics. */
        std::string name;
        /** @brief Declared type, whitespace-normalised. */
        std::string type;
    };

    /**
     * @brief Explicit bind-group and pipeline layouts derived from a program's WGSL.
     *
     * Shared (through `shared_ptr`) by every pipeline a program builds and by every queued command
     * that binds resources for it, so the native layouts outlive both the program object and any
     * accepted work.
     */
    class WebGPUProgramLayoutEXT
    {
    public:
        /** @brief The maximum bind groups a program may use; WebGPU's guaranteed `maxBindGroups`. */
        static constexpr std::uint32_t kMaxGroups = 4;

        /**
         * @brief Builds the layouts for a program from its stages' reflections.
         *
         * A binding declared by several stages must be declared identically; its visibility is the
         * union of those stages.
         *
         * @param device The device to create the layouts on.
         * @param stages Each stage's reflection and its stage flag.
         * @param error Receives the reason on failure.
         * @param maxDynamicUniformBlocks WEBGPUPERF-0005: when the program declares no more uniform
         *        blocks than this, every one of them is declared with a dynamic offset (and its slot
         *        says so); 0, the default, declares none. Storage blocks are never dynamic.
         * @return The layout, or null with @p error set.
         */
        [[nodiscard]] static std::shared_ptr<WebGPUProgramLayoutEXT> Create(
            WGPUDevice device,
            const std::vector<std::pair<const WgslModuleReflection*, WGPUShaderStage>>& stages,
            std::string& error, std::uint32_t maxDynamicUniformBlocks = 0);

        /** @brief Releases the native layouts. */
        ~WebGPUProgramLayoutEXT();

        WebGPUProgramLayoutEXT(const WebGPUProgramLayoutEXT&) = delete;
        WebGPUProgramLayoutEXT& operator=(const WebGPUProgramLayoutEXT&) = delete;

        /** @brief The pipeline layout. @return Never null for a created layout. */
        [[nodiscard]] WGPUPipelineLayout PipelineLayout() const noexcept { return pipelineLayout_; }
        /** @brief The number of bind groups the pipeline layout declares (highest used + 1). */
        [[nodiscard]] std::uint32_t GroupCount() const noexcept { return groupCount_; }
        /**
         * @brief One group's bind-group layout (an empty layout for a gap).
         * @param group Group index below @ref GroupCount.
         * @return The layout.
         */
        [[nodiscard]] WGPUBindGroupLayout GroupLayout(std::uint32_t group) const noexcept
        {
            return layouts_[group];
        }
        /**
         * @brief One group's bindings, ordered by binding index.
         * @param group Group index below @ref kMaxGroups.
         * @return The bindings.
         */
        [[nodiscard]] const std::vector<WebGPUBindingSlotEXT>& Group(std::uint32_t group) const
        {
            return groups_[group];
        }
        /**
         * @brief Finds one binding.
         * @param group Group index.
         * @param binding Binding index.
         * @return The binding, or null when the program declares none there.
         */
        [[nodiscard]] const WebGPUBindingSlotEXT* Find(std::uint32_t group,
                                                        std::uint32_t binding) const noexcept;

    private:
        WebGPUProgramLayoutEXT() = default;

        std::array<std::vector<WebGPUBindingSlotEXT>, kMaxGroups> groups_{};
        std::array<WGPUBindGroupLayout, kMaxGroups> layouts_{};
        WGPUPipelineLayout pipelineLayout_ = nullptr;
        std::uint32_t groupCount_ = 0;
    };

    /** @brief One texture unit's `SamplerState`, captured by value at a public draw call. */
    struct WebGPUSamplerStateEXT
    {
        /** @brief XNA `TextureFilter` ordinal. */
        int filter = 0;
        /** @brief XNA `TextureAddressMode` for U. */
        int addressU = 1;
        /** @brief ... for V. */
        int addressV = 1;
        /** @brief ... for W. */
        int addressW = 1;
        /** @brief `SamplerState.MaxMipLevel`. */
        int maxMipLevel = 0;
        /** @brief `SamplerState.MaxAnisotropy`. */
        int maxAnisotropy = 4;
    };

    /** @brief The four uniform arrays and the engine matrices a descriptor-contract effect carries. */
    struct WebGPUEffectArraysEXT
    {
        /** @brief `SetUniformFloatArray` values, one float each. */
        std::vector<float> floats;
        /** @brief `SetUniformVec2Array` values, two floats each. */
        std::vector<float> vec2s;
        /** @brief `SetUniformVec3Array` values, three floats each. */
        std::vector<float> vec3s;
        /** @brief `SetUniformMat4Array` values, sixteen floats each. */
        std::vector<float> mat4s;
        /** @brief The six engine-owned named matrices, column-major. */
        std::array<float, 6 * 16> engineMatrices{};
    };

    /**
     * @brief Everything a descriptor-contract `ShaderEffect` draw reads, captured at the draw call.
     *
     * The scalar block is a copy, the arrays are a shared immutable snapshot (copy-on-write in the
     * effect), and every texture is an already-resolved view with its own keep-alive.
     */
    struct WebGPUDescriptorEffectSnapshotEXT
    {
        /** @brief The 128-byte scalar block: viewport size, matrix, vector and scalar slots. */
        std::array<float, 32> scalars{};
        /** @brief The uniform arrays, or null when the effect never set one. */
        std::shared_ptr<const WebGPUEffectArraysEXT> arrays;
        /** @brief Sampled 2D textures bound to units 0..3. */
        std::array<WebGPUSampledTextureEXT, 4> textures2D{};
        /** @brief Cube textures bound to units 0..3. */
        std::array<WebGPUSampledTextureEXT, 4> cubes{};
        /** @brief Volume textures bound to units 0..3. */
        std::array<WebGPUSampledTextureEXT, 4> volumes{};
        /** @brief The device's sampler state per unit at the draw. */
        std::array<WebGPUSamplerStateEXT, 16> samplers{};
        /** @brief The draw's own texture: `GpuDrawParams::texture0`, or the sprite's. */
        WebGPUSampledTextureEXT primaryTexture;
        /** @brief Sprite route only: six vertices as position (pixels), uv and colour. */
        std::array<float, 6 * 8> spriteVertices{};
        /** @brief Sprite route only: whether @ref spriteVertices was filled. */
        bool hasSpriteVertices = false;
    };
}
