// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA {

    /**
     * @brief Which memory accesses a barrier must order after a compute dispatch.
     *
     * plans/plan_modern.md `MOD-1503`. A bitmask of ordinals rather than a native constant, for the
     * same reason every other value crossing the renderer boundary is: `IGraphicsRenderer` may not
     * name a backend's own enumerations, and each renderer translates these into its own bits.
     *
     * A dispatch writes to memory that the rest of the pipeline may already have cached. The bit
     * to ask for names *how the data will be read next*, not how it was written: a buffer a
     * compute shader filled and a draw call will read as vertices needs `VertexAttribArray`, even
     * though the write was a storage-buffer write.
     */
    enum class GraphicsMemoryBarrier
    {
        /** @brief Order nothing; a caller that passes this means it. */
        None = 0,
        /** @brief The data will be read as vertex attributes by a following draw. */
        VertexAttribArray = 1 << 0,
        /** @brief The data will be read as indices by a following draw. */
        ElementArray = 1 << 1,
        /** @brief The data will be read as uniforms. */
        Uniform = 1 << 2,
        /** @brief The data will be sampled as a texture. */
        TextureFetch = 1 << 3,
        /** @brief The data will be read or written through an image binding. */
        ShaderImageAccess = 1 << 4,
        /** @brief The data will be read or written as a storage buffer by another dispatch. */
        ShaderStorage = 1 << 5,
        /** @brief The data will be read back to the CPU, or written from it. */
        BufferUpdate = 1 << 6,
        /** @brief The data will be read as a framebuffer attachment. */
        Framebuffer = 1 << 7,
        /**
         * @brief The data will be read as the arguments of an indirect draw or dispatch.
         *
         * plans/plan_modern.md `MOD-2090`. Distinct from @ref ShaderStorage even though the same buffer
         * usually carries both roles: writing a draw's vertex count through a storage binding and
         * then *fetching* it as a command are two different accesses, and ordering only the first
         * lets the command fetch read the previous frame's numbers on hardware that separates the
         * two caches.
         */
        IndirectCommand = 1 << 8,
        /** @brief Every access above. The safe answer, and the slow one. */
        All = VertexAttribArray | ElementArray | Uniform | TextureFetch | ShaderImageAccess
            | ShaderStorage | BufferUpdate | Framebuffer | IndirectCommand,
    };

    /**
     * @brief Combines two barrier masks.
     * @param left  The first mask.
     * @param right The second mask.
     * @return Their union.
     */
    [[nodiscard]] constexpr GraphicsMemoryBarrier operator|(const GraphicsMemoryBarrier left,
                                                            const GraphicsMemoryBarrier right)
    {
        return static_cast<GraphicsMemoryBarrier>(static_cast<int>(left) | static_cast<int>(right));
    }

    /**
     * @brief Intersects two barrier masks.
     * @param left  The first mask.
     * @param right The second mask.
     * @return Their intersection.
     */
    [[nodiscard]] constexpr GraphicsMemoryBarrier operator&(const GraphicsMemoryBarrier left,
                                                            const GraphicsMemoryBarrier right)
    {
        return static_cast<GraphicsMemoryBarrier>(static_cast<int>(left) & static_cast<int>(right));
    }

    /**
     * @brief Tests whether a mask contains a bit.
     * @param mask The mask to test.
     * @param bit  The bit to look for.
     * @return True when every bit of @p bit is present in @p mask.
     */
    [[nodiscard]] constexpr bool HasBarrier(const GraphicsMemoryBarrier mask,
                                            const GraphicsMemoryBarrier bit)
    {
        return (static_cast<int>(mask) & static_cast<int>(bit)) == static_cast<int>(bit);
    }

} // namespace CNA
