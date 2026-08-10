// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief A usage hint for optimizing memory placement of graphics buffers. */
    enum class BufferUsage
    {
        /** @brief No special usage; the buffer may be read and written. */
        None = 0,
        /** @brief The buffer will not be readable and will be optimized for rendering and writing. */
        WriteOnly = 1,
    };
}
