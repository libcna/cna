#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "CNA/Internal/Graphics/ImageData.hpp"

namespace CNA::Internal::Graphics
{
    class ImageLoader
    {
    public:
        /// Load an image from a file path and decode it into RGBA8.
        static ImageData Load(const std::string& assetName);

        /// Load an image from an in-memory buffer and decode it into RGBA8.
        static ImageData LoadFromMemory(const uint8_t* data, std::size_t size);
    };
}
