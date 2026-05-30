#pragma once
#include <vector>
#include <cstdint>

namespace CNA::Internal::Graphics
{
    /**
     * @brief Minimal internal image data structure.
     * Contains width, height, and RGBA8 pixel data.
     */
    struct ImageData
    {
        int width;
        int height;
        std::vector<uint8_t> pixels; // RGBA8
    };
}
