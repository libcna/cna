// SPDX-License-Identifier: MS-PL
#pragma once

#include <type_traits>

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines the buffers cleared by a GraphicsDevice::Clear operation. */
    enum class ClearOptions : int
    {
        /** @brief Color buffer. */
        Target = 1,

        /** @brief Depth buffer. */
        DepthBuffer = 2,

        /** @brief Stencil buffer. */
        Stencil = 4
    };

    /** @brief Combines two ClearOptions flags with bitwise OR. */
    [[nodiscard]] constexpr ClearOptions operator|(ClearOptions left, ClearOptions right)
    {
        using Underlying = std::underlying_type_t<ClearOptions>;
        return static_cast<ClearOptions>(
            static_cast<Underlying>(left) | static_cast<Underlying>(right)
        );
    }

    /** @brief Masks two ClearOptions flags with bitwise AND. */
    [[nodiscard]] constexpr ClearOptions operator&(ClearOptions left, ClearOptions right)
    {
        using Underlying = std::underlying_type_t<ClearOptions>;
        return static_cast<ClearOptions>(
            static_cast<Underlying>(left) & static_cast<Underlying>(right)
        );
    }

    /** @brief Inverts a ClearOptions flag mask. */
    [[nodiscard]] constexpr ClearOptions operator~(ClearOptions value)
    {
        using Underlying = std::underlying_type_t<ClearOptions>;
        return static_cast<ClearOptions>(~static_cast<Underlying>(value));
    }

    /** @brief Combines flags into @p left with bitwise OR-assignment. */
    constexpr ClearOptions& operator|=(ClearOptions& left, ClearOptions right)
    {
        left = left | right;
        return left;
    }

    /** @brief Masks @p left with @p right using bitwise AND-assignment. */
    constexpr ClearOptions& operator&=(ClearOptions& left, ClearOptions right)
    {
        left = left & right;
        return left;
    }
}
