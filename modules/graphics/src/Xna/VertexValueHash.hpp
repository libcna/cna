// SPDX-License-Identifier: MS-PL
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace CNA::Internal::Graphics
{
    [[nodiscard]] inline std::uint32_t VertexFloatWord(float value)
    {
        return std::bit_cast<std::uint32_t>(value);
    }

    template<typename... Words>
    [[nodiscard]] std::size_t SmartVertexHash(Words... words)
    {
        // XNA hashes logical 32-bit struct words. CNA's C++ wrappers have unrelated vtables/padding.
        const std::uint32_t hash = (std::uint32_t{0} ^ ... ^ static_cast<std::uint32_t>(words));
        return hash == 0
            ? static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
            : static_cast<std::size_t>(hash);
    }
}
