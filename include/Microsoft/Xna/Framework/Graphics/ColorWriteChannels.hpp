#pragma once

#include <type_traits>

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines the color channels for render target blending operations.
    enum class ColorWriteChannels : int
    {
        None  = 0,
        Red   = 1,
        Green = 2,
        Blue  = 4,
        Alpha = 8,
        All   = 15,
    };

    [[nodiscard]] constexpr ColorWriteChannels operator|(ColorWriteChannels l, ColorWriteChannels r)
    {
        using U = std::underlying_type_t<ColorWriteChannels>;
        return static_cast<ColorWriteChannels>(static_cast<U>(l) | static_cast<U>(r));
    }

    [[nodiscard]] constexpr ColorWriteChannels operator&(ColorWriteChannels l, ColorWriteChannels r)
    {
        using U = std::underlying_type_t<ColorWriteChannels>;
        return static_cast<ColorWriteChannels>(static_cast<U>(l) & static_cast<U>(r));
    }

    [[nodiscard]] constexpr ColorWriteChannels operator~(ColorWriteChannels v)
    {
        using U = std::underlying_type_t<ColorWriteChannels>;
        return static_cast<ColorWriteChannels>(~static_cast<U>(v));
    }

    constexpr ColorWriteChannels& operator|=(ColorWriteChannels& l, ColorWriteChannels r)
    { l = l | r; return l; }

    constexpr ColorWriteChannels& operator&=(ColorWriteChannels& l, ColorWriteChannels r)
    { l = l & r; return l; }
}
