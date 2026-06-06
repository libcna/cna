#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines texture addressing modes for texture coordinates outside the [0, 1] range.
    enum class TextureAddressMode
    {
        Wrap,
        Clamp,
        Mirror,
    };
}
