// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief XNA 4.0 `BufferUsage` enum.
     *
     * Mirrors `Microsoft.Xna.Framework.Graphics.BufferUsage` and is accepted
     * by the XNA-shaped `VertexBuffer` / `IndexBuffer` constructors.
     *
     * @note Status: STUB. The CNA backend currently treats every value the
     *       same; the enum exists for API-shape compatibility with XNA 4.0
     *       sample code.
     */
    enum class BufferUsage
    {
        /** Default usage; both reads and writes are allowed. */
        None = 0,
        /** Hint that the application will only write to the buffer. */
        WriteOnly = 1,
    };
}
