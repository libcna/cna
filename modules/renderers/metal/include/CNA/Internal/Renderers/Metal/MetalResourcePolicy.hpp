// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <limits>

namespace CNA::Internal::Renderers::Metal
{
    /**
     * @brief Computes a native buffer byte count without signed conversion or multiplication wrap.
     *
     * @param elementCount Number of buffer elements.
     * @param elementSize Size of one element in bytes.
     * @param byteCount Receives the complete byte count only on success.
     * @return True when both inputs are positive and their product fits size_t.
     */
    [[nodiscard]] inline bool TryMetalBufferByteCount(
        int elementCount,
        std::size_t elementSize,
        std::size_t& byteCount) noexcept
    {
        if (elementCount <= 0 || elementSize == 0 ||
            elementSize > std::numeric_limits<std::size_t>::max()
                              / static_cast<std::size_t>(elementCount))
        {
            return false;
        }
        byteCount = static_cast<std::size_t>(elementCount) * elementSize;
        return true;
    }
}
