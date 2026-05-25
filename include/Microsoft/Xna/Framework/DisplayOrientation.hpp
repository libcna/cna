#pragma once

#include <type_traits>

namespace Microsoft::Xna::Framework
{
    /// Defines the orientation flags that can be used by a display.
    enum class DisplayOrientation : int
    {
        /// Default display orientation.
        Default = 0,

        /// Landscape orientation rotated counterclockwise; width is greater than height.
        LandscapeLeft = 1,

        /// Landscape orientation rotated clockwise; width is greater than height.
        LandscapeRight = 2,

        /// Portrait orientation; height is greater than width.
        Portrait = 4
    };

    [[nodiscard]] constexpr DisplayOrientation operator|(DisplayOrientation left, DisplayOrientation right)
    {
        using Underlying = std::underlying_type_t<DisplayOrientation>;
        return static_cast<DisplayOrientation>(
            static_cast<Underlying>(left) | static_cast<Underlying>(right)
        );
    }

    [[nodiscard]] constexpr DisplayOrientation operator&(DisplayOrientation left, DisplayOrientation right)
    {
        using Underlying = std::underlying_type_t<DisplayOrientation>;
        return static_cast<DisplayOrientation>(
            static_cast<Underlying>(left) & static_cast<Underlying>(right)
        );
    }

    [[nodiscard]] constexpr DisplayOrientation operator~(DisplayOrientation value)
    {
        using Underlying = std::underlying_type_t<DisplayOrientation>;
        return static_cast<DisplayOrientation>(~static_cast<Underlying>(value));
    }

    constexpr DisplayOrientation& operator|=(DisplayOrientation& left, DisplayOrientation right)
    {
        left = left | right;
        return left;
    }

    constexpr DisplayOrientation& operator&=(DisplayOrientation& left, DisplayOrientation right)
    {
        left = left & right;
        return left;
    }
}
