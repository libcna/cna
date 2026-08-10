// SPDX-License-Identifier: MS-PL
#pragma once

#include <type_traits>

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines the color channels for render target blending operations. */
    enum class ColorWriteChannels : int
    {
        /** @brief No channels selected. */
        None  = 0,
        /** @brief Red channel selected. */
        Red   = 1,
        /** @brief Green channel selected. */
        Green = 2,
        /** @brief Blue channel selected. */
        Blue  = 4,
        /** @brief Alpha channel selected. */
        Alpha = 8,
        /** @brief All channels selected. */
        All   = 15,
    };

    /** @brief Combines two ColorWriteChannels flags with bitwise OR. */
    [[nodiscard]] constexpr ColorWriteChannels operator|(ColorWriteChannels l, ColorWriteChannels r)
    {
        using U = std::underlying_type_t<ColorWriteChannels>;
        return static_cast<ColorWriteChannels>(static_cast<U>(l) | static_cast<U>(r));
    }

    /** @brief Masks two ColorWriteChannels flags with bitwise AND. */
    [[nodiscard]] constexpr ColorWriteChannels operator&(ColorWriteChannels l, ColorWriteChannels r)
    {
        using U = std::underlying_type_t<ColorWriteChannels>;
        return static_cast<ColorWriteChannels>(static_cast<U>(l) & static_cast<U>(r));
    }

    /** @brief Returns the bitwise complement of a ColorWriteChannels value. */
    [[nodiscard]] constexpr ColorWriteChannels operator~(ColorWriteChannels v)
    {
        using U = std::underlying_type_t<ColorWriteChannels>;
        return static_cast<ColorWriteChannels>(~static_cast<U>(v));
    }

    /** @brief Combines flags into @p l with bitwise OR-assignment. */
    constexpr ColorWriteChannels& operator|=(ColorWriteChannels& l, ColorWriteChannels r)
    { l = l | r; return l; }

    /** @brief Masks @p l with @p r using bitwise AND-assignment. */
    constexpr ColorWriteChannels& operator&=(ColorWriteChannels& l, ColorWriteChannels r)
    { l = l & r; return l; }
}
