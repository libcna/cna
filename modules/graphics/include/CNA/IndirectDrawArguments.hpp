// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

namespace CNA
{
    /**
     * @brief The arguments of a non-indexed indirect draw, in the exact layout the GPU reads.
     *
     * plan_modern.md `MOD-2090`. The whole point of an indirect draw is that these numbers are
     * produced on the GPU and never come back to the CPU, so their memory layout is part of the
     * contract between a compute shader and a draw call rather than an implementation detail. This
     * struct is that layout written down once: a shader declares the same five words in the same
     * order, and a test can fill one from the CPU and check the draw agrees.
     *
     * The order matches `GLDrawArraysIndirectCommand`, D3D12's `D3D12_DRAW_ARGUMENTS` and Vulkan's
     * `VkDrawIndirectCommand`, which are the same four words in the same order on all three.
     */
    struct IndirectDrawArguments
    {
        /** @brief How many vertices to fetch. */
        std::uint32_t VertexCount = 0;
        /** @brief How many instances to draw; 1 for an ordinary draw, never 0 unless nothing should be drawn. */
        std::uint32_t InstanceCount = 0;
        /** @brief The first vertex, in elements of the bound stream. */
        std::uint32_t FirstVertex = 0;
        /**
         * @brief The first instance.
         *
         * **Must be 0 on GL ES.** ES 3.1 has no base-instance parameter and the word is required to
         * be zero; a non-zero value there is undefined rather than diagnosed, which is why it is
         * stated here and cannot be checked anywhere — the value lives in GPU memory by the time
         * the draw runs.
         */
        std::uint32_t BaseInstance = 0;
    };

    /**
     * @brief The arguments of an indexed indirect draw, in the exact layout the GPU reads.
     *
     * plan_modern.md `MOD-2090`. Same contract as @ref IndirectDrawArguments, one word longer, and
     * matching `GLDrawElementsIndirectCommand` / `VkDrawIndexedIndirectCommand`.
     */
    struct IndirectDrawIndexedArguments
    {
        /** @brief How many indices to fetch. */
        std::uint32_t IndexCount = 0;
        /** @brief How many instances to draw. */
        std::uint32_t InstanceCount = 0;
        /** @brief The first index, in index elements. */
        std::uint32_t FirstIndex = 0;
        /** @brief Added to every decoded index, in vertex elements. Signed, as the API is. */
        std::int32_t BaseVertex = 0;
        /** @brief The first instance; must be 0 on GL ES, for the reason @ref IndirectDrawArguments gives. */
        std::uint32_t BaseInstance = 0;
    };

    static_assert(sizeof(IndirectDrawArguments) == 16,
                  "the GPU reads this struct verbatim; it must be exactly four 32-bit words");
    static_assert(sizeof(IndirectDrawIndexedArguments) == 20,
                  "the GPU reads this struct verbatim; it must be exactly five 32-bit words");
} // namespace CNA
