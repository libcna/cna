// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Backends/Software/SoftwareTextureAllocation.hpp"

#include <algorithm>
#include <limits>

namespace CNA::Internal::Backends::Software
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

    SoftwareTextureAllocationLayout PlanSoftwareTextureAllocation(
        const SoftwareTextureAllocationRequest& request) noexcept
    {
        SoftwareTextureAllocationLayout layout;
        if (request.width <= 0 || request.height <= 0)
        {
            layout.error = SoftwareTextureAllocationError::NonPositiveDimension;
            return layout;
        }
        if (request.width > SoftwareFramebufferMaxDimension ||
            request.height > SoftwareFramebufferMaxDimension)
        {
            layout.error = SoftwareTextureAllocationError::DimensionLimitExceeded;
            return layout;
        }

        if (!CheckedMultiply(static_cast<std::size_t>(request.width),
                             static_cast<std::size_t>(request.height), layout.pixelCount) ||
            !CheckedMultiply(layout.pixelCount, 4u, layout.baseColorBytes))
        {
            layout.error = SoftwareTextureAllocationError::ArithmeticOverflow;
            return layout;
        }

        const int declaredLevels = std::max(1, request.declaredLevels);
        int mipWidth = request.width;
        int mipHeight = request.height;
        for (int level = 1; level < declaredLevels; ++level)
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
                layout.error = SoftwareTextureAllocationError::ArithmeticOverflow;
                return layout;
            }
            if (mipWidth == 1 && mipHeight == 1)
                break;
        }

        if (!CheckedAdd(layout.baseColorBytes, layout.mipBytes, layout.totalBytes))
        {
            layout.error = SoftwareTextureAllocationError::ArithmeticOverflow;
            return layout;
        }
        if (layout.totalBytes > SoftwareTextureMaxBytes)
        {
            layout.error = SoftwareTextureAllocationError::ByteBudgetExceeded;
            return layout;
        }
        return layout;
    }

    const char* SoftwareTextureAllocationErrorName(SoftwareTextureAllocationError error) noexcept
    {
        switch (error)
        {
            case SoftwareTextureAllocationError::None: return "none";
            case SoftwareTextureAllocationError::NonPositiveDimension:
                return "dimensions must be positive";
            case SoftwareTextureAllocationError::DimensionLimitExceeded:
                return "dimension limit exceeded";
            case SoftwareTextureAllocationError::ArithmeticOverflow:
                return "allocation arithmetic overflow";
            case SoftwareTextureAllocationError::ByteBudgetExceeded:
                return "texture byte budget exceeded";
        }
        return "unknown allocation error";
    }
}
