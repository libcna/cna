// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Backends/Software/SoftwareFramebufferAllocation.hpp"

#include <cstddef>

namespace CNA::Internal::Backends::Software
{
    /**
     * Maximum committed pixel storage for one CPU texture resource, including every declared mip
     * level down to and including level 0, but excluding container bookkeeping and allocator
     * capacity overhead. Shares GDI-067's 512 MiB framebuffer ceiling: both are one CPU-visible
     * RGBA8 resource on the same backend, and a texture with no documented ceiling of its own let
     * a single-level request as large as 1 GiB pass silently (GDI-076).
     */
    inline constexpr std::size_t SoftwareTextureMaxBytes = SoftwareFramebufferMaxBytes;

    enum class SoftwareTextureAllocationError
    {
        None,
        NonPositiveDimension,
        DimensionLimitExceeded,
        ArithmeticOverflow,
        ByteBudgetExceeded,
    };

    struct SoftwareTextureAllocationRequest
    {
        int width = 0;
        int height = 0;
        /** Declared mip level count, including level 0. At least 1. */
        int declaredLevels = 1;
    };

    /** Side-effect-free result used before any vector allocation. */
    struct SoftwareTextureAllocationLayout
    {
        SoftwareTextureAllocationError error = SoftwareTextureAllocationError::None;
        std::size_t pixelCount = 0;
        /** Level 0 alone, tightly packed RGBA8. */
        std::size_t baseColorBytes = 0;
        /** Every declared level from 1 up to declaredLevels-1, summed. */
        std::size_t mipBytes = 0;
        /** baseColorBytes + mipBytes: the full declared chain's worst-case commitment. */
        std::size_t totalBytes = 0;

        [[nodiscard]] bool IsValid() const
        {
            return error == SoftwareTextureAllocationError::None;
        }
    };

    /**
     * Validates dimensions, every size_t multiplication/addition across the full declared mip
     * chain, and the practical per-resource byte budget. Never allocates.
     */
    [[nodiscard]] SoftwareTextureAllocationLayout PlanSoftwareTextureAllocation(
        const SoftwareTextureAllocationRequest& request) noexcept;

    [[nodiscard]] const char* SoftwareTextureAllocationErrorName(
        SoftwareTextureAllocationError error) noexcept;
}
