// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace CNA::Platform::Common {

    /**
     * @brief Validates the memory layout shared by every surface presenter.
     *
     * A presenter cannot know the allocation size behind @ref SurfaceFrame::pixels, but it can
     * reject every layout that is intrinsically impossible: signed pitch arithmetic overflow,
     * backwards rows, rows shorter than their RGBA payload, and an address span that cannot be
     * represented by `size_t`. Keeping this at the platform-neutral edge prevents SDL and the
     * terminal quantiser from quietly assigning different meanings to the same contract value.
     *
     * @param frame The frame to validate.
     * @param operation The operation name used in a PlatformException.
     * @return The effective positive stride in bytes.
     * @throws PlatformException If the frame layout is malformed.
     */
    inline int ValidateSurfaceFrame(const SurfaceFrame& frame, const char* operation)
    {
        if (frame.pixels == nullptr)
        {
            throw PlatformException(operation, "the frame has no pixels");
        }
        if (frame.width <= 0 || frame.height <= 0)
        {
            throw PlatformException(operation, "the frame has a non-positive size");
        }

        constexpr int kBytesPerPixel = 4;
        if (frame.width > std::numeric_limits<int>::max() / kBytesPerPixel)
        {
            throw PlatformException(operation, "the packed row size does not fit in an int");
        }

        const int packedStride = frame.width * kBytesPerPixel;
        if (frame.strideBytes < 0)
        {
            throw PlatformException(operation, "the frame has a negative row stride");
        }
        if (frame.strideBytes != 0 && frame.strideBytes < packedStride)
        {
            throw PlatformException(operation, "the row stride is shorter than one RGBA row");
        }

        const int stride = frame.strideBytes == 0 ? packedStride : frame.strideBytes;
        const std::size_t rowsBeforeLast = static_cast<std::size_t>(frame.height - 1);
        const std::size_t unsignedStride = static_cast<std::size_t>(stride);
        const std::size_t lastRowBytes = static_cast<std::size_t>(packedStride);
        if (rowsBeforeLast >
            (std::numeric_limits<std::size_t>::max() - lastRowBytes) / unsignedStride)
        {
            throw PlatformException(operation, "the frame's address span is too large");
        }

        // The terminal quantiser accumulates exact integer channel sums. Reject a theoretical
        // frame whose population could overflow that accumulator before any pixel is touched.
        const std::uint64_t pixels = static_cast<std::uint64_t>(frame.width) *
                                     static_cast<std::uint64_t>(frame.height);
        if (pixels > std::numeric_limits<std::uint64_t>::max() / 255u)
        {
            throw PlatformException(operation, "the frame has too many pixels to process safely");
        }

        return stride;
    }

} // namespace CNA::Platform::Common
