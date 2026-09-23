// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu_modern_graphics.md WMG-0004: resource, layout and entry-point reflection over
// the WGSL this renderer is handed.
//
// WHY THE RENDERER READS ITS OWN SHADER SOURCE
//
// WebGPU gives two ways to build a pipeline layout: an explicit one, which must name every binding
// the shaders use with the right type, and `layout: auto`, which derives one from the shader. The
// second does not work for this renderer. An automatically derived bind-group layout belongs to the
// ONE pipeline it came from, and a `ShaderEffect` or `ComputeShader` owns many pipelines (one per
// render-target shape, blend state, vertex layout ...), all of which must accept the same bind
// groups. So the layout has to be explicit, and the only complete description of what the shader
// binds is the shader itself. wgpu-native exposes no reflection of a compiled module either.
//
// WGSL makes that tractable where GLSL would not: every resource is a module-scope `var` carrying
// `@group(G) @binding(B)`, every uniform/storage block is a named `struct` with a layout WGSL
// defines completely (WGSL spec, "Memory Layout"), and every entry point is an attributed `fn`. This
// file parses exactly that surface -- declarations, struct layouts and entry-point interfaces -- and
// deliberately nothing inside function bodies. What it cannot classify it reports by name rather
// than guessing, so a shader outside the surface fails at compile time with a reason.
//
// It is renderer-internal and depends only on `webgpu.h` enumerations, so it is unit-testable
// without a device (`WebGPUWgslReflectionTests.cpp`).

#pragma once

#if __has_include(<webgpu/webgpu.h>)
#include <webgpu/webgpu.h>
#elif __has_include(<webgpu-headers/webgpu.h>)
#include <webgpu-headers/webgpu.h>
#elif __has_include(<webgpu.h>)
#include <webgpu.h>
#else
#error "CNA WebGPU WGSL reflection requires webgpu.h"
#endif

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::WebGPU
{
    /** @brief The kind of resource one module-scope WGSL `var` binds. */
    enum class WgslResourceKind
    {
        /** @brief `var<uniform>`. */
        UniformBuffer,
        /** @brief `var<storage, read_write>`. */
        StorageBuffer,
        /** @brief `var<storage>` or `var<storage, read>`. */
        ReadOnlyStorageBuffer,
        /** @brief `sampler`. */
        Sampler,
        /** @brief `sampler_comparison`. */
        ComparisonSampler,
        /** @brief Any sampled `texture_*` type, depth textures included. */
        SampledTexture,
        /** @brief `texture_storage_*`. */
        StorageTexture
    };

    /** @brief WGSL size and alignment of one type, per the WGSL memory-layout rules. */
    struct WgslTypeLayout
    {
        /** @brief Size in bytes (for a runtime-sized array: the size of one element stride). */
        std::uint32_t size = 0;
        /** @brief Required alignment in bytes. */
        std::uint32_t align = 1;
        /** @brief True for a runtime-sized array or a struct that ends in one. */
        bool runtimeSized = false;
    };

    /** @brief One member of a reflected struct, with its resolved byte offset. */
    struct WgslStructMember
    {
        /** @brief Member name as written. */
        std::string name;
        /** @brief Member type as written, whitespace-normalised. */
        std::string type;
        /** @brief Byte offset from the start of the struct. */
        std::uint32_t offset = 0;
        /** @brief Size in bytes (the `@size` attribute when present). */
        std::uint32_t size = 0;
    };

    /** @brief One reflected `struct` declaration. */
    struct WgslStruct
    {
        /** @brief Struct name. */
        std::string name;
        /** @brief Members in declaration order. */
        std::vector<WgslStructMember> members;
        /** @brief Size and alignment of the whole struct. */
        WgslTypeLayout layout{};
    };

    /** @brief One `@group(G) @binding(B)` module-scope resource. */
    struct WgslResourceBinding
    {
        /** @brief Bind-group index. */
        std::uint32_t group = 0;
        /** @brief Binding index inside the group. */
        std::uint32_t binding = 0;
        /** @brief Variable name. */
        std::string name;
        /** @brief Resource kind. */
        WgslResourceKind kind = WgslResourceKind::UniformBuffer;
        /** @brief The declared type, whitespace-normalised. */
        std::string type;
        /** @brief For a sampled or storage texture: its view dimension. */
        WGPUTextureViewDimension viewDimension = WGPUTextureViewDimension_Undefined;
        /** @brief For a sampled texture: float, depth, sint or uint. Float means "declared f32". */
        WGPUTextureSampleType sampleType = WGPUTextureSampleType_Undefined;
        /** @brief For a sampled texture: whether it is a `texture_multisampled_*`. */
        bool multisampled = false;
        /** @brief For a storage texture: its texel format. */
        WGPUTextureFormat storageFormat = WGPUTextureFormat_Undefined;
        /** @brief For a storage texture: its declared access. */
        WGPUStorageTextureAccess storageAccess = WGPUStorageTextureAccess_Undefined;
        /** @brief For a buffer: the minimum binding size the declared type needs. */
        std::uint64_t minBindingSize = 0;
    };

    /** @brief One `@location(N)` input of a vertex entry point. */
    struct WgslVertexInput
    {
        /** @brief The input location. */
        std::uint32_t location = 0;
        /** @brief The declared type, whitespace-normalised (`vec4<f32>`, `vec4<u32>` ...). */
        std::string type;
    };

    /** @brief One entry point of the module. */
    struct WgslEntryPoint
    {
        /** @brief The pipeline stage the entry point is attributed with. */
        WGPUShaderStage stage = WGPUShaderStage_None;
        /** @brief Function name. */
        std::string name;
        /** @brief Compute only: the literal `@workgroup_size`, missing axes defaulting to 1. */
        std::array<std::uint32_t, 3> workgroupSize{1, 1, 1};
        /** @brief Vertex only: every `@location` input, parameters and struct members alike. */
        std::vector<WgslVertexInput> vertexInputs;
        /** @brief Fragment only: one past the highest `@location` output (0 when none). */
        std::uint32_t colorOutputCount = 0;
    };

    /** @brief Everything reflected from one WGSL module. */
    struct WgslModuleReflection
    {
        /** @brief True when the whole declaration surface was understood. */
        bool ok = false;
        /** @brief Why reflection failed; empty when @ref ok. */
        std::string error;
        /** @brief Every module-scope resource, in declaration order. */
        std::vector<WgslResourceBinding> resources;
        /** @brief Every entry point, in declaration order. */
        std::vector<WgslEntryPoint> entryPoints;
        /** @brief Every struct, by name. */
        std::unordered_map<std::string, WgslStruct> structs;

        /**
         * @brief Finds the resource at one group/binding pair.
         * @param group Bind-group index.
         * @param binding Binding index.
         * @return The resource, or null when the module declares none there.
         */
        [[nodiscard]] const WgslResourceBinding* FindResource(
            std::uint32_t group, std::uint32_t binding) const noexcept;

        /**
         * @brief Finds the first entry point of one stage.
         * @param stage The stage.
         * @return The entry point, or null when the module has none of that stage.
         */
        [[nodiscard]] const WgslEntryPoint* FindEntryPoint(WGPUShaderStage stage) const noexcept;
    };

    /**
     * @brief Reflects the declaration surface of one WGSL module.
     *
     * Parses struct declarations (with WGSL layout rules, including `@align`/`@size`),
     * `@group/@binding` resources, `alias` declarations and attributed entry points. Function bodies
     * are skipped. A declaration whose type cannot be classified makes the result not-ok with a
     * named reason.
     *
     * @param source Complete WGSL module text.
     * @return The reflection; check @ref WgslModuleReflection::ok.
     */
    [[nodiscard]] WgslModuleReflection ReflectWgsl(std::string_view source);

    /**
     * @brief Computes the WGSL size and alignment of a type expression.
     *
     * @param type The type as written (`vec3<f32>`, `array<mat4x4<f32>, 4>`, a struct name ...).
     * @param structs Structs the type may name.
     * @param aliases `alias` declarations the type may name.
     * @return The layout, or nothing when the type is not a host-shareable type this file knows.
     */
    [[nodiscard]] std::optional<WgslTypeLayout> WgslLayoutOf(
        std::string_view type, const std::unordered_map<std::string, WgslStruct>& structs,
        const std::unordered_map<std::string, std::string>& aliases = {});

    /**
     * @brief Maps a WGSL texel-format name (`rgba8unorm`, `r32float` ...) to its WebGPU format.
     * @param name Format name as written in a `texture_storage_*` type.
     * @return The format, or `WGPUTextureFormat_Undefined` for an unknown name.
     */
    [[nodiscard]] WGPUTextureFormat WgslTexelFormatFromName(std::string_view name) noexcept;
}
