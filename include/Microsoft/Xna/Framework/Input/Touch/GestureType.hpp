// SPDX-License-Identifier: MS-PL
#pragma once

#include <type_traits>

namespace Microsoft::Xna::Framework::Input::Touch
{
    /// Defines the gesture types that can be enabled and read from TouchPanel.
    enum class GestureType : int
    {
        /// No gesture.
        None = 0,

        /// Single tap.
        Tap = 1,

        /// Double tap.
        DoubleTap = 2,

        /// Hold gesture.
        Hold = 4,

        /// Horizontal drag.
        HorizontalDrag = 8,

        /// Vertical drag.
        VerticalDrag = 16,

        /// Free-form drag.
        FreeDrag = 32,

        /// Pinch gesture.
        Pinch = 64,

        /// Flick gesture.
        Flick = 128,

        /// Drag completion.
        DragComplete = 256,

        /// Pinch completion.
        PinchComplete = 512
    };

    [[nodiscard]] constexpr GestureType operator|(GestureType left, GestureType right)
    {
        using Underlying = std::underlying_type_t<GestureType>;
        return static_cast<GestureType>(
            static_cast<Underlying>(left) | static_cast<Underlying>(right)
        );
    }

    [[nodiscard]] constexpr GestureType operator&(GestureType left, GestureType right)
    {
        using Underlying = std::underlying_type_t<GestureType>;
        return static_cast<GestureType>(
            static_cast<Underlying>(left) & static_cast<Underlying>(right)
        );
    }

    constexpr GestureType& operator|=(GestureType& left, GestureType right)
    {
        left = left | right;
        return left;
    }

    constexpr GestureType& operator&=(GestureType& left, GestureType right)
    {
        left = left & right;
        return left;
    }
}
