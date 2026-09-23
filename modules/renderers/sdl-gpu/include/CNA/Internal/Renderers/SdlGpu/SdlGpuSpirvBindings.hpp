// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file SdlGpuSpirvBindings.hpp
 * @brief plans/plan_sdlgpu_modern_graphics.md SMG-0006: the one mapping layer between a portable
 *        SPIR-V payload and `SDL_gpu`'s mandated descriptor-set layout.
 *
 * `SDL_gpu` does not let a shader choose its own descriptor sets. `SDL_CreateGPUShader` and
 * `SDL_CreateGPUComputePipeline` document a fixed layout that every SPIR-V module handed to them
 * must already obey:
 *
 * | stage | set 0 | set 1 | set 2 | set 3 |
 * |---|---|---|---|---|
 * | vertex   | sampled textures, then storage textures, then storage buffers | uniform buffers | — | — |
 * | fragment | — | — | sampled textures, then storage textures, then storage buffers | uniform buffers |
 * | compute  | sampled textures, then READ-ONLY storage textures, then read-only storage buffers | read-WRITE storage textures, then read-write storage buffers | uniform buffers | — |
 *
 * CNA's portable shader packages do not obey it, and are not wrong to: their SPIR-V is compiled
 * from the `*.vulkan.*.glsl` sources, which are written against CNA's **own** Vulkan renderer's
 * descriptor convention (fragment samplers at `set = 1`, storage buffers at `set = 2`, and small
 * per-draw parameters in a `push_constant` block). Two renderers that both consume SPIR-V still
 * disagree about where a resource lives, so something has to translate, and the honest place for
 * that is here rather than in the shader sources -- the package is shared with Vulkan, which is
 * already validated against it, and a second set of numbers baked into the sources would make one
 * of the two renderers wrong (`plans/plan_sdlgpu_modern_graphics.md` Phase 10, "do not assume
 * Vulkan descriptor-set numbering").
 *
 * Two transformations happen, and nothing else:
 *
 * 1. **Descriptor sets and bindings are reassigned** into the table above. Bindings are renumbered
 *    contiguously from zero within each set, in the category order `SDL_gpu` requires, and the
 *    original `(set, binding)` of every resource is reported back so the runtime can bind by what
 *    the shader asked for rather than by what SDL ended up calling it.
 *
 * 2. **A `push_constant` block becomes a uniform buffer.** `SDL_gpu` has no push constants at all;
 *    its per-draw parameters arrive through `SDL_PushGPUVertexUniformData` and friends, which are
 *    uniform buffers. The conversion is a storage-class change plus the two decorations a
 *    descriptor needs; the block's member offsets are already explicit in the SPIR-V and are left
 *    exactly as the compiler wrote them.
 *
 * The second one is only safe because the two layouts were already the same shape. CNA's Vulkan
 * post-process push-constant block is `vec2 viewportSize; mat4 uMatrix; vec4 uVector; float
 * uScalar`, whose compiler-assigned offsets are 0, 16, 80 and 96 -- byte for byte the 128-byte
 * uniform block `SdlGpuEffectRenderer` has always pushed (see its own documentation). Those offsets
 * also satisfy std140 on their own (a `mat4` and a `vec4` both land on 16), so the block is a legal
 * uniform buffer without repacking. A block that did **not** satisfy std140 is refused by name
 * rather than reinterpreted.
 *
 * Nothing here is `SDL_gpu`-version-specific beyond the layout table, and nothing here needs a
 * shader compiler: it is a word-level rewrite of a module that is already SPIR-V.
 */

#include "CNA/CNAHelper.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::SdlGpu
{
    /** @brief The programmable stage a reflected SPIR-V module implements. CNAEXT. */
    enum class SpirvStageEXT : std::uint8_t
    {
        /** @brief The module declared no entry point this renderer can use. */
        Unknown = 0,
        /** @brief A vertex shader. */
        Vertex = 1,
        /** @brief A fragment shader. */
        Fragment = 2,
        /** @brief A compute shader. */
        Compute = 3
    };

    /** @brief One descriptor-bound resource kind, in the categories `SDL_gpu` counts separately. CNAEXT. */
    enum class SpirvResourceKindEXT : std::uint8_t
    {
        /** @brief A combined image/sampler the shader samples. */
        SampledTexture = 0,
        /** @brief A storage image the shader loads from or stores to. */
        StorageTexture = 1,
        /** @brief A shader storage buffer. */
        StorageBuffer = 2,
        /** @brief A uniform/constant buffer, including a converted `push_constant` block. */
        UniformBuffer = 3
    };

    /**
     * @brief Where one resource was declared, and where `SDL_gpu` will expect to find it. CNAEXT.
     *
     * `originalSet`/`originalBinding` are what the shader author wrote, and are what CNA's own
     * binding calls name. `assignedSet`/`assignedBinding` are what the rewritten module declares.
     * The runtime binds through the slot, not the original index, so this pair is the whole
     * contract between a shader package and the draw that uses it.
     */
    struct SpirvResourceBindingEXT
    {
        /** @brief The category `SDL_gpu` places this resource in. */
        SpirvResourceKindEXT kind = SpirvResourceKindEXT::SampledTexture;
        /** @brief Descriptor set the source module declared, or 0 for a converted push block. */
        std::uint32_t originalSet = 0;
        /** @brief Binding the source module declared, or 0 for a converted push block. */
        std::uint32_t originalBinding = 0;
        /** @brief Descriptor set the rewritten module declares. */
        std::uint32_t assignedSet = 0;
        /** @brief Binding the rewritten module declares. */
        std::uint32_t assignedBinding = 0;
        /**
         * @brief Zero-based index within its own `SDL_gpu` category, which is the slot number
         *        every `SDL_Bind*` call actually takes.
         */
        std::uint32_t slot = 0;
        /** @brief How many descriptors this declaration occupies; above one for an array. */
        std::uint32_t arrayLength = 1;
        /** @brief True when the shader never writes through this resource. */
        bool readOnly = true;
        /** @brief True when this entry is a `push_constant` block converted to a uniform buffer. */
        bool convertedFromPushConstant = false;
    };

    /**
     * @brief The rewritten module, its resource map and the counts `SDL_gpu` asks for. CNAEXT.
     *
     * `error` non-empty means nothing else in this value is meaningful; the caller must surface the
     * text rather than fall back to the untransformed module, because a module left in CNA's Vulkan
     * layout does not fail loudly on `SDL_gpu` -- it binds the wrong resources.
     */
    struct SpirvRemapResultEXT
    {
        /** @brief The rewritten SPIR-V words, ready for `SDL_CreateGPUShader`. */
        std::vector<std::uint32_t> words;
        /** @brief Every descriptor-bound resource, in declaration order. */
        std::vector<SpirvResourceBindingEXT> resources;
        /** @brief Stage taken from the module's own `OpEntryPoint`. */
        SpirvStageEXT stage = SpirvStageEXT::Unknown;
        /** @brief `SDL_GPUShaderCreateInfo::num_samplers`. */
        std::uint32_t samplerCount = 0;
        /** @brief Graphics: `num_storage_textures`. Compute: the read-only ones. */
        std::uint32_t readOnlyStorageTextureCount = 0;
        /** @brief Graphics: unused. Compute: `num_readwrite_storage_textures`. */
        std::uint32_t readWriteStorageTextureCount = 0;
        /** @brief Graphics: `num_storage_buffers`. Compute: the read-only ones. */
        std::uint32_t readOnlyStorageBufferCount = 0;
        /** @brief Graphics: unused. Compute: `num_readwrite_storage_buffers`. */
        std::uint32_t readWriteStorageBufferCount = 0;
        /** @brief `num_uniform_buffers`, counting a converted push block. */
        std::uint32_t uniformBufferCount = 0;
        /** @brief Compute workgroup X from `OpExecutionMode LocalSize`; zero elsewhere. */
        std::uint32_t localSizeX = 0;
        /** @brief Compute workgroup Y. */
        std::uint32_t localSizeY = 0;
        /** @brief Compute workgroup Z. */
        std::uint32_t localSizeZ = 0;
        /** @brief True when the module was changed; false means it already obeyed the layout. */
        bool changed = false;
        /** @brief Empty on success; otherwise the reason this module cannot be used. */
        std::string error;
    };

    /**
     * @brief Reflects one SPIR-V module and rewrites it into `SDL_gpu`'s descriptor layout. CNAEXT.
     *
     * @param words Start of the module, including its five-word header.
     * @param wordCount Number of 32-bit words in the module.
     * @return The rewritten module and its resource map, or a value whose `error` names the refusal.
     */
    [[nodiscard]] SpirvRemapResultEXT RemapSpirvForSdlGpuEXT(
        const std::uint32_t* words, std::size_t wordCount);

    /**
     * @brief Convenience overload taking the byte form a shader package payload arrives in. CNAEXT.
     *
     * @param bytes Module bytes; the length must be a whole number of 32-bit words.
     * @param byteCount Number of bytes.
     * @return As @ref RemapSpirvForSdlGpuEXT, with a named refusal for a misaligned length.
     */
    [[nodiscard]] SpirvRemapResultEXT RemapSpirvForSdlGpuEXT(
        const void* bytes, std::size_t byteCount);

    /**
     * @brief Whether a byte range begins with the SPIR-V magic word. CNAEXT.
     *
     * The one reliable test for "this payload is SPIR-V rather than shader text": a `std::string`
     * carrying a module and a `std::string` carrying GLSL are the same C++ type, so the intake path
     * has to look at the content.
     *
     * @param bytes Start of the payload; may be null.
     * @param byteCount Number of bytes available.
     * @return True when at least one whole word is present and it is `0x07230203`.
     */
    [[nodiscard]] bool LooksLikeSpirvEXT(const void* bytes, std::size_t byteCount) noexcept;
}
