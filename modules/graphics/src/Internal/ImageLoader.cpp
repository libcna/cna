#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Graphics
{
    static ImageData surfaceToImageData(SDL_Surface* surface, const std::string& label)
    {
        SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        if (!converted)
        {
            SDL_DestroySurface(surface);
            throw std::runtime_error("Failed to convert image to RGBA: " + label);
        }

        ImageData data;
        data.width  = converted->w;
        data.height = converted->h;
        std::size_t sz = static_cast<std::size_t>(data.width * data.height * 4);
        data.pixels.assign(
            static_cast<uint8_t*>(converted->pixels),
            static_cast<uint8_t*>(converted->pixels) + sz);

        SDL_DestroySurface(converted);
        SDL_DestroySurface(surface);
        return data;
    }

    ImageData ImageLoader::Load(const std::string& assetName)
    {
        SDL_Surface* surface = IMG_Load(assetName.c_str());
        if (!surface)
            throw std::runtime_error("Failed to load image: " + assetName + " - " + SDL_GetError());
        return surfaceToImageData(surface, assetName);
    }

    ImageData ImageLoader::LoadFromMemory(const uint8_t* data, std::size_t size)
    {
        SDL_IOStream* io = SDL_IOFromConstMem(data, size);
        if (!io)
            throw std::runtime_error(std::string("SDL_IOFromConstMem failed: ") + SDL_GetError());

        SDL_Surface* surface = IMG_Load_IO(io, true);
        if (!surface)
            throw std::runtime_error(std::string("IMG_LoadIO failed: ") + SDL_GetError());

        return surfaceToImageData(surface, "<memory>");
    }
}
