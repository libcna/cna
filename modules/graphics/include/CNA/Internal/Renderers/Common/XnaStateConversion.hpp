// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Internal::Renderers
{
    /**
     * @brief Converts a raw XNA Blend ordinal to the value renderer backends must consume.
     *
     * @param value Raw Blend value stored by a state object.
     * @return The original valid ordinal, or Blend::Zero for an unknown value.
     */
    [[nodiscard]] constexpr int NormalizeXnaBlendOrdinal(const int value) noexcept
    {
        return value >= 0 && value <= 12 ? value : 1;
    }

    /**
     * @brief Converts a raw XNA BlendFunction ordinal to its native fallback.
     *
     * @param value Raw BlendFunction value stored by a state object.
     * @return The original valid ordinal, or BlendFunction::Add for an unknown value.
     */
    [[nodiscard]] constexpr int NormalizeXnaBlendFunctionOrdinal(const int value) noexcept
    {
        return value >= 0 && value <= 4 ? value : 0;
    }

    /**
     * @brief Converts a raw XNA CompareFunction ordinal to its native fallback.
     *
     * @param value Raw CompareFunction value stored by a state object.
     * @return The original valid ordinal, or CompareFunction::Always for an unknown value.
     */
    [[nodiscard]] constexpr int NormalizeXnaCompareFunctionOrdinal(const int value) noexcept
    {
        return value >= 0 && value <= 7 ? value : 0;
    }

    /**
     * @brief Converts a raw XNA StencilOperation ordinal to its native fallback.
     *
     * @param value Raw StencilOperation value stored by a state object.
     * @return The original valid ordinal, or StencilOperation::Keep for an unknown value.
     */
    [[nodiscard]] constexpr int NormalizeXnaStencilOperationOrdinal(const int value) noexcept
    {
        return value >= 0 && value <= 7 ? value : 0;
    }

    /**
     * @brief Converts a raw XNA CullMode ordinal to its native fallback.
     *
     * @param value Raw CullMode value stored by a state object.
     * @return The original valid ordinal, or CullMode::None for an unknown value.
     */
    [[nodiscard]] constexpr int NormalizeXnaCullModeOrdinal(const int value) noexcept
    {
        return value >= 0 && value <= 2 ? value : 0;
    }

    /**
     * @brief Converts a raw XNA FillMode ordinal to its native fallback.
     *
     * @param value Raw FillMode value stored by a state object.
     * @return The original valid ordinal, or FillMode::Solid for an unknown value.
     */
    [[nodiscard]] constexpr int NormalizeXnaFillModeOrdinal(const int value) noexcept
    {
        return value >= 0 && value <= 1 ? value : 0;
    }

    /**
     * @brief Converts a raw XNA TextureFilter ordinal to its native fallback.
     *
     * @param value Raw TextureFilter value stored by a sampler state.
     * @return The original valid ordinal, or TextureFilter::Linear for an unknown value.
     */
    [[nodiscard]] constexpr int NormalizeXnaTextureFilterOrdinal(const int value) noexcept
    {
        return value >= 0 && value <= 8 ? value : 0;
    }

    /**
     * @brief Converts a raw XNA TextureAddressMode ordinal to its native fallback.
     *
     * @param value Raw TextureAddressMode value stored by a sampler state.
     * @return The original valid ordinal, or TextureAddressMode::Wrap for an unknown value.
     */
    [[nodiscard]] constexpr int NormalizeXnaTextureAddressModeOrdinal(const int value) noexcept
    {
        return value >= 0 && value <= 2 ? value : 0;
    }
}
