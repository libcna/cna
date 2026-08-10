// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>

namespace CNA::Internal::Backends::Software
{
    /** Practical single-axis ceiling shared with IGraphicsBackend::GetMaxTextureDimension(). */
    inline constexpr int SoftwareFramebufferMaxDimension = 16384;

    /**
     * Maximum committed pixel storage for one CPU framebuffer, including generated mip levels
     * when requested but excluding container bookkeeping and allocator capacity overhead. The
     * 512 MiB ceiling keeps one surface practical on a 32-bit process while permitting a
     * 3840x2160 GDI backbuffer with RGBA8, stencil and 4x MSAA.
     */
    inline constexpr std::size_t SoftwareFramebufferMaxBytes =
        static_cast<std::size_t>(512u) * 1024u * 1024u;

    enum class SoftwareFramebufferAllocationError
    {
        None,
        NonPositiveDimension,
        DimensionLimitExceeded,
        UnsupportedSampleCount,
        ArithmeticOverflow,
        ByteBudgetExceeded,
    };

    struct SoftwareFramebufferAllocationRequest
    {
        int width = 0;
        int height = 0;
        bool allocateDepth = true;
        bool allocateStencil = true;
        int multiSampleCount = 0;
        bool mipMap = false;
    };

    /** Side-effect-free result used before any vector or Win32-facing byte conversion. */
    struct SoftwareFramebufferAllocationLayout
    {
        SoftwareFramebufferAllocationError error = SoftwareFramebufferAllocationError::None;
        std::size_t pixelCount = 0;
        std::size_t colorBytes = 0;
        std::size_t depthElementCount = 0;
        std::size_t depthBytes = 0;
        std::size_t stencilBytes = 0;
        std::size_t multiSampleBytes = 0;
        std::size_t mipBytes = 0;
        std::size_t totalBytes = 0;

        [[nodiscard]] bool IsValid() const
        {
            return error == SoftwareFramebufferAllocationError::None;
        }
    };

    /**
     * Validates dimensions, every size_t multiplication/addition, and the practical byte budget.
     * This function never allocates and is intentionally buildable in a standalone 32-bit test.
     */
    [[nodiscard]] SoftwareFramebufferAllocationLayout PlanSoftwareFramebufferAllocation(
        const SoftwareFramebufferAllocationRequest& request) noexcept;

    [[nodiscard]] const char* SoftwareFramebufferAllocationErrorName(
        SoftwareFramebufferAllocationError error) noexcept;
}
