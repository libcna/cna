// SPDX-License-Identifier: MS-PL
#pragma once

#include <bit>
#include <cstdint>

namespace CNA::Internal::Renderers
{
    /** @brief XNA's wrapping DWORD sample tally, exposed as a signed Int32. */
    class OcclusionSampleCount
    {
    public:
        /** @brief Starts a tally from a native result. @param samples Native sample count. */
        explicit constexpr OcclusionSampleCount(std::uint64_t samples = 0) noexcept
            : samples_(static_cast<std::uint32_t>(samples)) { }

        /** @brief Accumulates samples modulo 2^32. @param samples Additional passing samples. */
        constexpr void Add(std::uint64_t samples) noexcept
        {
            samples_ += static_cast<std::uint32_t>(samples);
        }

        /** @brief Returns the tally's signed bit interpretation. @return XNA PixelCount. */
        [[nodiscard]] constexpr std::int32_t Value() const noexcept
        {
            return std::bit_cast<std::int32_t>(samples_);
        }

    private:
        std::uint32_t samples_;
    };
}
