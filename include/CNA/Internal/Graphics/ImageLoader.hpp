#pragma once
#include <string>
#include "CNA/Internal/Graphics/ImageData.hpp"

namespace CNA::Internal::Graphics
{
    /**
     * @brief Shared/internal layer for image loading.
     * Uses SDL_image internally but hides it from backends.
     */
    class ImageLoader
    {
    public:
        /**
         * @brief Load an image from file and decode it into RGBA8.
         *
         * @param assetName Full or relative path to the image file.
         * @return Decoded ImageData structure.
         */
        static ImageData Load(const std::string& assetName);
    };
}
