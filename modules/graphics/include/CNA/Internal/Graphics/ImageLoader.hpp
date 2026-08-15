#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
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

        /// Resize RGBA8 pixels using the FNA fit/cover rules used by Texture2D::FromStream.
        static ImageData ResizeRgba(const uint8_t* pixels, int width, int height,
                                    int targetWidth, int targetHeight, bool zoom);

        /// Resize RGBA8 pixels to the exact target size and encode them as PNG.
        static std::vector<uint8_t> EncodePng(const uint8_t* pixels, int width, int height,
                                              int targetWidth, int targetHeight);

        /// Encode RGBA8 pixels as PNG at their original size and write them to a file.
        static void SavePng(const uint8_t* pixels, int width, int height,
                            const std::string& filename);

        /// Resize RGBA8 pixels to the exact target size and encode them as JPEG.
        static std::vector<uint8_t> EncodeJpeg(const uint8_t* pixels, int width, int height,
                                               int targetWidth, int targetHeight, int quality);

        /// Encode RGBA8 pixels as JPEG at their original size and write them to a file.
        static void SaveJpeg(const uint8_t* pixels, int width, int height,
                             const std::string& filename, int quality);
    };
}
