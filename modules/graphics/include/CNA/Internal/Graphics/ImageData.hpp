#pragma once
#include <vector>
#include <cstdint>

namespace CNA::Internal::Graphics
{
    /**
     * @brief Minimal internal image data structure.
     * Contains width, height, exact level-zero transfer bytes, and the public SurfaceFormat
     * ordinal. Existing decoders leave surfaceFormat at zero (`Color`).
     */
    struct ImageData
    {
        int width;
        int height;
        std::vector<uint8_t> pixels;
        int mipLevels = 1; // Task 924: real mip level count backends should allocate for
        int surfaceFormat = 0;
    };
}
