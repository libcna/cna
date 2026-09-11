// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA
{
    /**
     * @brief Identifies the language or binary format of an explicit shader payload.
     *
     * plans/plan_modern.md `MOD-2210`. Values are public and append-only so shader packages can
     * describe several renderer variants without deriving a language from renderer identity or
     * inspecting payload contents.
     */
    enum class ShaderLanguageEXT : int
    {
        /** @brief No declared shader language. Never accepted as an executable payload. */
        Unknown = 0,
        /** @brief Desktop OpenGL Shading Language source text. */
        GlslDesktop = 1,
        /** @brief OpenGL ES or WebGL Shading Language source text. */
        GlslEs = 2,
        /** @brief Vulkan-oriented OpenGL Shading Language source text. */
        GlslVulkan = 3,
        /** @brief Direct3D High Level Shader Language source text. */
        Hlsl = 4,
        /** @brief Metal Shading Language source text. */
        Msl = 5,
        /** @brief WebGPU Shading Language source text. */
        Wgsl = 6,
        /** @brief Standard Portable Intermediate Representation binary words. */
        SpirV = 7,
        /** @brief DirectX Intermediate Language binary data. */
        Dxil = 8,
        /** @brief Number of declared language identities; not itself a shader language. */
        Count = 9
    };

    /**
     * @brief Identifies the programmable pipeline stage an explicit shader payload implements.
     *
     * Values are public and append-only. `Fragment` is used instead of the XNA-era `Pixel` name
     * because this enum also describes modern renderer payloads and is independent of XNA sampler
     * collection stages.
     */
    enum class ShaderStageEXT : int
    {
        /** @brief No declared shader stage. Never accepted as an executable payload. */
        Unknown = 0,
        /** @brief Vertex-processing stage. */
        Vertex = 1,
        /** @brief Fragment- or pixel-processing stage. */
        Fragment = 2,
        /** @brief Compute-dispatch stage. */
        Compute = 3,
        /** @brief Number of declared stage identities; not itself a shader stage. */
        Count = 4
    };
}
