// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA {

    /**
     * @brief How a compute shader will access a texture bound as an image.
     *
     * plans/plan_modern.md `MOD-1504`. Declaring the access is not a formality: a driver may keep a
     * write-only image's previous contents undefined, and a read-write binding costs coherence a
     * write-only one does not.
     */
    enum class GraphicsImageAccess
    {
        /** @brief The shader only reads the image. */
        ReadOnly = 0,
        /** @brief The shader only writes the image; its previous contents may be undefined. */
        WriteOnly = 1,
        /** @brief The shader both reads and writes the image. */
        ReadWrite = 2,
    };

} // namespace CNA
