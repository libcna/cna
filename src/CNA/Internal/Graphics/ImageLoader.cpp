#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Graphics
{
    ImageData ImageLoader::Load(const std::string& assetName)
    {
        SDL_Surface* surface = IMG_Load(assetName.c_str());
        if (!surface)
        {
            throw std::runtime_error("Failed to load image: " + assetName + " Error: " + SDL_GetError());
        }

        // Convert to RGBA32
        SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        if (!converted)
        {
            SDL_DestroySurface(surface);
            throw std::runtime_error("Failed to convert image to RGBA: " + assetName);
        }

        ImageData data;
        data.width = converted->w;
        data.height = converted->h;

        // Copy pixel data
        size_t size = static_cast<size_t>(data.width * data.height * 4);
        data.pixels.assign(static_cast<uint8_t*>(converted->pixels), static_cast<uint8_t*>(converted->pixels) + size);

        SDL_DestroySurface(converted);
        SDL_DestroySurface(surface);

        return data;
    }
}
