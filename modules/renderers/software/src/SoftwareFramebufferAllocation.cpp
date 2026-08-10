// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareFramebufferAllocation.hpp"

#include <limits>

namespace CNA::Internal::Renderers::Software
{
    namespace
    {
        [[nodiscard]] bool CheckedMultiply(std::size_t left, std::size_t right,
                                           std::size_t& result) noexcept
        {
            if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
                return false;
            result = left * right;
            return true;
        }

        [[nodiscard]] bool CheckedAdd(std::size_t left, std::size_t right,
                                      std::size_t& result) noexcept
        {
            if (right > std::numeric_limits<std::size_t>::max() - left)
                return false;
            result = left + right;
            return true;
        }
    }

    SoftwareFramebufferAllocationLayout PlanSoftwareFramebufferAllocation(
        const SoftwareFramebufferAllocationRequest& request) noexcept
    {
        SoftwareFramebufferAllocationLayout layout;
        if (request.width <= 0 || request.height <= 0)
        {
            layout.error = SoftwareFramebufferAllocationError::NonPositiveDimension;
            return layout;
        }
        if (request.width > SoftwareFramebufferMaxDimension ||
            request.height > SoftwareFramebufferMaxDimension)
        {
            layout.error = SoftwareFramebufferAllocationError::DimensionLimitExceeded;
            return layout;
        }
        if (request.multiSampleCount != 0 && request.multiSampleCount != 4)
        {
            layout.error = SoftwareFramebufferAllocationError::UnsupportedSampleCount;
            return layout;
        }

        if (!CheckedMultiply(static_cast<std::size_t>(request.width),
                             static_cast<std::size_t>(request.height), layout.pixelCount) ||
            !CheckedMultiply(layout.pixelCount, 4u, layout.colorBytes))
        {
            layout.error = SoftwareFramebufferAllocationError::ArithmeticOverflow;
            return layout;
        }

        if (request.allocateDepth)
        {
            layout.depthElementCount = layout.pixelCount;
            if (!CheckedMultiply(layout.depthElementCount, sizeof(float), layout.depthBytes))
            {
                layout.error = SoftwareFramebufferAllocationError::ArithmeticOverflow;
                return layout;
            }
        }
        if (request.allocateStencil)
            layout.stencilBytes = layout.pixelCount;
        if (request.multiSampleCount == 4 &&
            !CheckedMultiply(layout.pixelCount, 16u, layout.multiSampleBytes))
        {
            layout.error = SoftwareFramebufferAllocationError::ArithmeticOverflow;
            return layout;
        }

        if (request.mipMap)
        {
            int mipWidth = request.width;
            int mipHeight = request.height;
            while (mipWidth > 1 || mipHeight > 1)
            {
                mipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
                mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
                std::size_t mipPixels = 0;
                std::size_t mipLevelBytes = 0;
                if (!CheckedMultiply(static_cast<std::size_t>(mipWidth),
                                     static_cast<std::size_t>(mipHeight), mipPixels) ||
                    !CheckedMultiply(mipPixels, 4u, mipLevelBytes) ||
                    !CheckedAdd(layout.mipBytes, mipLevelBytes, layout.mipBytes))
                {
                    layout.error = SoftwareFramebufferAllocationError::ArithmeticOverflow;
                    return layout;
                }
            }
        }

        if (!CheckedAdd(layout.colorBytes, layout.depthBytes, layout.totalBytes) ||
            !CheckedAdd(layout.totalBytes, layout.stencilBytes, layout.totalBytes) ||
            !CheckedAdd(layout.totalBytes, layout.multiSampleBytes, layout.totalBytes) ||
            !CheckedAdd(layout.totalBytes, layout.mipBytes, layout.totalBytes))
        {
            layout.error = SoftwareFramebufferAllocationError::ArithmeticOverflow;
            return layout;
        }
        if (layout.totalBytes > SoftwareFramebufferMaxBytes)
        {
            layout.error = SoftwareFramebufferAllocationError::ByteBudgetExceeded;
            return layout;
        }
        return layout;
    }

    const char* SoftwareFramebufferAllocationErrorName(
        SoftwareFramebufferAllocationError error) noexcept
    {
        switch (error)
        {
            case SoftwareFramebufferAllocationError::None: return "none";
            case SoftwareFramebufferAllocationError::NonPositiveDimension:
                return "dimensions must be positive";
            case SoftwareFramebufferAllocationError::DimensionLimitExceeded:
                return "dimension limit exceeded";
            case SoftwareFramebufferAllocationError::UnsupportedSampleCount:
                return "unsupported sample count";
            case SoftwareFramebufferAllocationError::ArithmeticOverflow:
                return "allocation arithmetic overflow";
            case SoftwareFramebufferAllocationError::ByteBudgetExceeded:
                return "framebuffer byte budget exceeded";
        }
        return "unknown allocation error";
    }
}
