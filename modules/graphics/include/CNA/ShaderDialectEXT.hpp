// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA
{
    /**
     * @brief The shader payload dialect a custom `ShaderEffect` must be written in, as
     *        GraphicsDevice::GetShaderDialectEXT() reports it for the active renderer.
     *
     * A `ShaderEffect` is renderer-specific: the framework hands its two string payloads to the
     * renderer, whose own compiler or bytecode loader decides. This is the supported way for an
     * application to ask which dialect to supply, rather than inferring it from the build's
     * renderer identity -- which is wrong in a multi-renderer build, where the identity is not
     * the active renderer, and meaningless for a renderer that chooses its native API per process
     * (FNA3D).
     *
     * `Unknown` is the honest default and what every renderer that has not declared a dialect
     * answers. It does not mean "no shaders"; it means the renderer has not stated a dialect, and
     * an application should not guess.
     *
     * Values are public and append-only; the C API mirrors them as `CNA_SHADER_DIALECT_*`.
     */
    enum class ShaderDialectEXT : int
    {
        /** @brief Not declared by this renderer. */
        Unknown = 0,
        /** @brief Desktop OpenGL GLSL (`#version 3xx core` / `4xx core`). */
        GlslDesktop = 1,
        /** @brief OpenGL ES / WebGL GLSL (`#version 100` / `300 es`). */
        GlslEs = 2,
        /** @brief GLSL compiled to SPIR-V: explicit `location`/`set`/`binding` are mandatory. */
        GlslVulkan = 3,
        /** @brief Direct3D High Level Shader Language. */
        Hlsl = 4,
        /** @brief Metal Shading Language. */
        Msl = 5,
        /** @brief WebGPU Shading Language. */
        Wgsl = 6,
        /** @brief Already-compiled SPIR-V bytecode, not Vulkan GLSL source text. */
        SpirV = 7
    };
}
