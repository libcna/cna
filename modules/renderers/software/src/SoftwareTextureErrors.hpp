// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Software/SoftwareTextureAllocation.hpp"

#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/OutOfMemoryException.hpp"

#include <string>

namespace CNA::Internal::Renderers::Software
{
    [[noreturn]] inline void ThrowInvalidTextureLayout(
        const SoftwareTextureAllocationRequest& request,
        SoftwareTextureAllocationError error)
    {
        throw System::ArgumentOutOfRangeException(
            "dimensions", std::to_string(request.width) + "x" +
                              std::to_string(request.height),
            std::string("Software texture rejected the requested layout: ") +
                SoftwareTextureAllocationErrorName(error) +
                "; maximum dimension is " +
                std::to_string(SoftwareFramebufferMaxDimension) +
                " and maximum committed storage is " +
                std::to_string(SoftwareTextureMaxBytes) + " bytes.");
    }

    [[noreturn]] inline void ThrowTextureAllocationFailure(
        const SoftwareTextureAllocationRequest& request,
        const SoftwareTextureAllocationLayout& layout)
    {
        throw System::OutOfMemoryException(
            "Software texture could not allocate " +
            std::to_string(layout.totalBytes) + " bytes for " +
            std::to_string(request.width) + "x" + std::to_string(request.height) + ".");
    }

    [[noreturn]] inline void ThrowTexturePixelDataMismatch(
        int width, int height, std::size_t requiredBytes, std::size_t suppliedBytes)
    {
        throw System::ArgumentException(
            "Software texture received " + std::to_string(suppliedBytes) +
            " pixel bytes for a " + std::to_string(width) + "x" + std::to_string(height) +
            " RGBA8 level, which requires at least " + std::to_string(requiredBytes) + ".");
    }
}
