// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Specifies the buffer usage mode for vertex and index buffers.
    enum class BufferUsage
    {
        /// Default usage; both reads and writes are allowed.
        None = 0,
        /// Hint that the application will only write to the buffer.
        WriteOnly = 1,
    };
}
