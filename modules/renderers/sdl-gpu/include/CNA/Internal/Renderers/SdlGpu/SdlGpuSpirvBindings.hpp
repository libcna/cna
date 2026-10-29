// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file SdlGpuSpirvBindings.hpp
 * @brief SMG-0006: the one mapping layer between a portable
 *        SPIR-V payload and `SDL_gpu`'s mandated descriptor-set layout.
 *
 * `SDL_gpu` does not let a shader choose its own descriptor sets. `SDL_CreateGPUShader` documents
 * a fixed layout that every SPIR-V module handed to it must already obey:
 *
 * | stage | set 0 | set 1 | set 2 | set 3 |
 * |---|---|---|---|---|
 * | vertex   | sampled textures | uniform buffers | — | — |
 * | fragment | — | — | sampled textures | uniform buffers |
 *
 * (`SDL_gpu` places storage textures and storage buffers after the sampled textures in the same
 * set. A `ShaderEffect` has no way to bind either, so a module that declares one is refused by
 * name instead of being created with a descriptor nothing will ever fill.)
 *
 * CNA's portable shader packages do not obey it, and are not wrong to: their SPIR-V is compiled
 * from the `*.vulkan.*.glsl` sources, which are written against the descriptor convention of
 * CNA's **own** (since retired) Vulkan renderer (fragment samplers at `set = 1` and small per-draw
 * parameters in a `push_constant` block). Two renderers that both consumed SPIR-V disagreed
 * about where a resource lives, so something had to translate, and the honest place for that was
 * here rather than in the shader sources -- the package was shared with Vulkan, which was
 * validated against it, and a second set of numbers baked into the sources would have made one
 * of the two renderers wrong (SDL_GPU modern-graphics Phase 10, "do not assume
 * Vulkan descriptor-set numbering"). The sources keep that convention now that the Vulkan
 * renderer is retired (WebGPU's WGSL payloads are derived from the same sources), so the
 * translation stays here.
 *
 * Resource translation is shared by compiled GLSL and portable bytecode:
 *
 * 1. **Descriptor sets and bindings are reassigned** into the table above. Bindings are renumbered
 *    contiguously from zero within each set, and the original `(set, binding)` of every
 *    resource is reported back so the runtime can bind by what the shader asked for rather than
 *    by what SDL ended up calling it.
 *
 * 2. **A `push_constant` block becomes a uniform buffer.** `SDL_gpu` has no push constants at all;
 *    its per-draw parameters arrive through `SDL_PushGPUVertexUniformData` and friends, which are
 *    uniform buffers. The conversion is a storage-class change plus the two decorations a
 *    descriptor needs; the block's member offsets are already explicit in the SPIR-V and are left
 *    exactly as the compiler wrote them.
 *
 * The second one is only safe because the two layouts were already the same shape. The portable
 * post-process packages' push-constant block is `vec2 viewportSize; mat4 uMatrix; vec4 uVector; float
 * uScalar`, whose compiler-assigned offsets are 0, 16, 80 and 96 -- byte for byte the 128-byte
 * uniform block `SdlGpuEffectRenderer` has always pushed (see its own documentation). Those offsets
 * also satisfy std140 on their own (a `mat4` and a `vec4` both land on 16), so the block is a legal
 * uniform buffer without repacking. A block that did **not** satisfy std140 is refused by name
 * rather than reinterpreted.
 *
 * Portable vertex bytecode also receives its established clip-space Y correction. Source GLSL
 * already uses SDL's Y-up coordinates and explicitly disables that correction. Sampled image
 * dimensions and original bindings are checked against ShaderEffect's bindable units before
 * a driver sees the module; unsupported descriptor arrays/image shapes fail with a diagnostic.
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
        Fragment = 2
    };

    /** @brief One descriptor-bound resource kind, in the categories `SDL_gpu` counts separately. CNAEXT. */
    enum class SpirvResourceKindEXT : std::uint8_t
    {
        /** @brief A combined image/sampler the shader samples. */
        SampledTexture = 0,
        /** @brief A uniform/constant buffer, including a converted `push_constant` block. */
        UniformBuffer = 1
    };

    /** @brief Sampled image dimensions supported by ShaderEffect's texture setters. */
    enum class SpirvTextureDimensionEXT : std::uint8_t
    {
        /** @brief Not a sampled texture. */
        None = 0,
        /** @brief A two-dimensional texture. */
        Texture2D = 1,
        /** @brief A volume texture. */
        Texture3D = 2,
        /** @brief A cube texture. */
        Cube = 3
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
         *        every binding call actually takes.
         */
        std::uint32_t slot = 0;
        /** @brief Number of descriptor slots; sampler descriptor arrays are refused. */
        std::uint32_t arrayLength = 1;
        /** @brief True when this entry is a `push_constant` block converted to a uniform buffer. */
        bool convertedFromPushConstant = false;
        /** @brief Dimension reflected from OpTypeImage, not inferred from the compacted slot. */
        SpirvTextureDimensionEXT textureDimension = SpirvTextureDimensionEXT::None;
        /** @brief ShaderEffect texture/sampler unit, resolved from the original descriptor. */
        std::uint32_t textureUnit = 0;
        /** @brief This declaration samples SpriteBatch's own two-dimensional texture. */
        bool usesSpriteTexture = false;
    };

    /**
     * @brief The rewritten module, its resource map and the counts `SDL_gpu` asks for. CNAEXT.
     *
     * `error` non-empty means nothing else in this value is meaningful; the caller must surface the
     * text rather than fall back to the untransformed module, because a module left in the portable
     * packages' Vulkan-convention layout does not fail loudly on `SDL_gpu` -- it binds the wrong
     * resources.
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
        /** @brief `num_uniform_buffers`, counting a converted push block. */
        std::uint32_t uniformBufferCount = 0;
        /**
         * @brief `location` of every vertex input the module declares, ascending.
         *
         * A 3D custom effect's vertex layout is whatever its own shader asks for, not the fixed
         * sprite one -- so the pipeline's attribute list is built from these rather than assumed,
         * and an element the shader does not consume is omitted instead of bound to nothing.
         * Empty for a fragment module.
         */
        std::vector<std::uint32_t> vertexInputLocations;
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
     * @param flipClipSpaceY Apply the portable bytecode's Y correction; false for native GLSL.
     * @return The rewritten module and its resource map, or a value whose `error` names the refusal.
     */
    [[nodiscard]] SpirvRemapResultEXT RemapSpirvForSdlGpuEXT(
        const std::uint32_t* words, std::size_t wordCount, bool flipClipSpaceY = true);

    /**
     * @brief Convenience overload taking the byte form a shader package payload arrives in. CNAEXT.
     *
     * @param bytes Module bytes; the length must be a whole number of 32-bit words.
     * @param byteCount Number of bytes.
     * @param flipClipSpaceY Apply the portable bytecode's Y correction; false for native GLSL.
     * @return As @ref RemapSpirvForSdlGpuEXT, with a named refusal for a misaligned length.
     */
    [[nodiscard]] SpirvRemapResultEXT RemapSpirvForSdlGpuEXT(
        const void* bytes, std::size_t byteCount, bool flipClipSpaceY = true);

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
