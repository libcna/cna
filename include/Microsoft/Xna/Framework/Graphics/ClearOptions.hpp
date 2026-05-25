#pragma once

#include <type_traits>

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines which buffers are cleared by GraphicsDevice::Clear.
    enum class ClearOptions : int
    {
        /// Clear the render target color buffer.
        Target = 1,

        /// Clear the depth buffer.
        DepthBuffer = 2,

        /// Clear the stencil buffer.
        Stencil = 4
    };

    [[nodiscard]] constexpr ClearOptions operator|(ClearOptions left, ClearOptions right)
    {
        using Underlying = std::underlying_type_t<ClearOptions>;
        return static_cast<ClearOptions>(
            static_cast<Underlying>(left) | static_cast<Underlying>(right)
        );
    }

    [[nodiscard]] constexpr ClearOptions operator&(ClearOptions left, ClearOptions right)
    {
        using Underlying = std::underlying_type_t<ClearOptions>;
        return static_cast<ClearOptions>(
            static_cast<Underlying>(left) & static_cast<Underlying>(right)
        );
    }

    constexpr ClearOptions& operator|=(ClearOptions& left, ClearOptions right)
    {
        left = left | right;
        return left;
    }

    constexpr ClearOptions& operator&=(ClearOptions& left, ClearOptions right)
    {
        left = left & right;
        return left;
    }
}
