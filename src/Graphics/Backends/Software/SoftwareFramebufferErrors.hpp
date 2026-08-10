// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Backends/Software/SoftwareFramebufferAllocation.hpp"

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/OutOfMemoryException.hpp"

#include <string>

namespace CNA::Internal::Backends::Software
{
    [[noreturn]] inline void ThrowInvalidFramebufferLayout(
        const SoftwareFramebufferAllocationRequest& request,
        SoftwareFramebufferAllocationError error)
    {
        throw System::ArgumentOutOfRangeException(
            "dimensions", std::to_string(request.width) + "x" +
                              std::to_string(request.height),
            std::string("Software framebuffer rejected the requested layout: ") +
                SoftwareFramebufferAllocationErrorName(error) +
                "; maximum dimension is " +
                std::to_string(SoftwareFramebufferMaxDimension) +
                " and maximum committed storage is " +
                std::to_string(SoftwareFramebufferMaxBytes) + " bytes.");
    }

    [[noreturn]] inline void ThrowFramebufferAllocationFailure(
        const SoftwareFramebufferAllocationRequest& request,
        const SoftwareFramebufferAllocationLayout& layout)
    {
        throw System::OutOfMemoryException(
            "Software framebuffer could not allocate " +
            std::to_string(layout.totalBytes) + " bytes for " +
            std::to_string(request.width) + "x" + std::to_string(request.height) +
            (request.multiSampleCount == 4 ? " with 4x MSAA." : "."));
    }
}
