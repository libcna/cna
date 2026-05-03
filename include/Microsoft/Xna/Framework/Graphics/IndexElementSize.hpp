#pragma once

namespace Microsoft::Xna::Framework::Graphics {

    /**
     * @brief XNA 4.0 `IndexElementSize` enum.
     *
     * Mirrors `Microsoft.Xna.Framework.Graphics.IndexElementSize`.
     *
     * @note Status: PARTIAL. Only `SixteenBits` is implemented by the CNA
     *       backend; `ThirtyTwoBits` is exposed for API-shape compatibility
     *       and currently rejected by `IndexBuffer` at construction time
     *       with a clear message.
     */
    enum class IndexElementSize {
        /** 16-bit indices (`std::uint16_t`). Fully supported. */
        SixteenBits = 16,
        /** 32-bit indices (`std::uint32_t`). TODO: not yet implemented. */
        ThirtyTwoBits = 32,
    };
}
