#include "CNA/Internal/Graphics/ImageLoader.hpp"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

namespace CNA::Internal::Graphics
{
    namespace
    {
        using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
        using IoStream = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;

        [[nodiscard]] Surface Own(SDL_Surface* surface)
        {
            return Surface(surface, SDL_DestroySurface);
        }

        [[nodiscard]] IoStream Own(SDL_IOStream* stream)
        {
            return IoStream(stream, SDL_CloseIO);
        }

        [[nodiscard]] Surface CreateRgbaSurface(const uint8_t* pixels, int width, int height)
        {
            if (!pixels || width <= 0 || height <= 0)
                throw std::invalid_argument("ImageLoader: RGBA pixels and dimensions must be valid");

            auto surface = Own(SDL_CreateSurfaceFrom(
                width, height, SDL_PIXELFORMAT_RGBA32,
                const_cast<uint8_t*>(pixels), width * 4));
            if (!surface)
                throw std::runtime_error(std::string("SDL_CreateSurfaceFrom failed: ") + SDL_GetError());
            return surface;
        }

        [[nodiscard]] ImageData CopyRgbaSurface(const SDL_Surface& surface)
        {
            ImageData data;
            data.width = surface.w;
            data.height = surface.h;
            data.pixels.resize(static_cast<std::size_t>(data.width)
                               * static_cast<std::size_t>(data.height) * 4u);

            const auto* source = static_cast<const uint8_t*>(surface.pixels);
            const std::size_t rowBytes = static_cast<std::size_t>(data.width) * 4u;
            for (int y = 0; y < data.height; ++y)
            {
                std::memcpy(data.pixels.data() + static_cast<std::size_t>(y) * rowBytes,
                            source + static_cast<std::size_t>(y) * surface.pitch,
                            rowBytes);
            }
            return data;
        }

        [[nodiscard]] ImageData SurfaceToImageData(Surface surface, const std::string& label)
        {
            auto converted = Own(SDL_ConvertSurface(surface.get(), SDL_PIXELFORMAT_RGBA32));
            if (!converted)
                throw std::runtime_error("Failed to convert image to RGBA: " + label);
            return CopyRgbaSurface(*converted);
        }

        [[nodiscard]] Surface ScaleExact(const uint8_t* pixels, int width, int height,
                                         int targetWidth, int targetHeight)
        {
            auto source = CreateRgbaSurface(pixels, width, height);
            if (targetWidth == width && targetHeight == height)
                return source;
            if (targetWidth <= 0 || targetHeight <= 0)
                throw std::invalid_argument("ImageLoader: target dimensions must be positive");

            auto scaled = Own(SDL_ScaleSurface(
                source.get(), targetWidth, targetHeight, SDL_SCALEMODE_LINEAR));
            if (!scaled)
                throw std::runtime_error(std::string("SDL_ScaleSurface failed: ") + SDL_GetError());
            return scaled;
        }

        [[nodiscard]] std::vector<uint8_t> CopyDynamicIo(SDL_IOStream& stream)
        {
            const Sint64 encodedSize = SDL_TellIO(&stream);
            if (encodedSize <= 0)
                throw std::runtime_error("ImageLoader: encoder produced no data");

            const auto* encoded = static_cast<const uint8_t*>(
                SDL_GetPointerProperty(
                    SDL_GetIOProperties(&stream),
                    SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
            if (!encoded)
                throw std::runtime_error("ImageLoader: encoded memory is unavailable");

            return std::vector<uint8_t>(encoded, encoded + static_cast<std::size_t>(encodedSize));
        }
    }

    ImageData ImageLoader::Load(const std::string& assetName)
    {
        auto surface = Own(IMG_Load(assetName.c_str()));
        if (!surface)
            throw std::runtime_error("Failed to load image: " + assetName + " - " + SDL_GetError());
        return SurfaceToImageData(std::move(surface), assetName);
    }

    ImageData ImageLoader::LoadFromMemory(const uint8_t* data, std::size_t size)
    {
        SDL_IOStream* io = SDL_IOFromConstMem(data, size);
        if (!io)
            throw std::runtime_error(std::string("SDL_IOFromConstMem failed: ") + SDL_GetError());

        // IMG_Load_IO owns and closes io on both success and failure when closeio is true.
        auto surface = Own(IMG_Load_IO(io, true));
        if (!surface)
            throw std::runtime_error(std::string("IMG_LoadIO failed: ") + SDL_GetError());
        return SurfaceToImageData(std::move(surface), "<memory>");
    }

    ImageData ImageLoader::ResizeRgba(const uint8_t* pixels, int width, int height,
                                      int targetWidth, int targetHeight, bool zoom)
    {
        auto source = CreateRgbaSurface(pixels, width, height);
        if (targetWidth <= 0 || targetHeight <= 0)
            throw std::invalid_argument("ImageLoader: target dimensions must be positive");

        // Mirrors FNA3D_Image_Load's forceW/forceH/zoom resize-and-crop logic.
        const bool scaleWidth = zoom ? (width < height) : (width > height);
        const float scale = scaleWidth
            ? static_cast<float>(targetWidth) / static_cast<float>(width)
            : static_cast<float>(targetHeight) / static_cast<float>(height);

        int finalWidth;
        int finalHeight;
        SDL_Rect crop{0, 0, width, height};
        if (zoom)
        {
            finalWidth = targetWidth;
            finalHeight = targetHeight;
            if (scaleWidth)
            {
                crop.y = height / 2 - static_cast<int>((targetHeight / scale) / 2);
                crop.h = static_cast<int>(targetHeight / scale);
            }
            else
            {
                crop.x = width / 2 - static_cast<int>((targetWidth / scale) / 2);
                crop.w = static_cast<int>(targetWidth / scale);
            }
        }
        else
        {
            finalWidth = static_cast<int>(width * scale);
            finalHeight = static_cast<int>(height * scale);
        }

        auto scaled = Own(SDL_CreateSurface(finalWidth, finalHeight, SDL_PIXELFORMAT_RGBA32));
        if (!scaled)
            throw std::runtime_error(std::string("SDL_CreateSurface failed: ") + SDL_GetError());

        SDL_SetSurfaceBlendMode(source.get(), SDL_BLENDMODE_NONE);
        const bool blitOk = zoom
            ? SDL_BlitSurfaceScaled(source.get(), &crop, scaled.get(), nullptr, SDL_SCALEMODE_LINEAR)
            : SDL_BlitSurfaceScaled(source.get(), nullptr, scaled.get(), nullptr, SDL_SCALEMODE_LINEAR);
        if (!blitOk)
            throw std::runtime_error(std::string("SDL_BlitSurfaceScaled failed: ") + SDL_GetError());
        return CopyRgbaSurface(*scaled);
    }

    std::vector<uint8_t> ImageLoader::EncodePng(
        const uint8_t* pixels, int width, int height, int targetWidth, int targetHeight)
    {
        auto surface = ScaleExact(pixels, width, height, targetWidth, targetHeight);
        auto destination = Own(SDL_IOFromDynamicMem());
        if (!destination)
            throw std::runtime_error(std::string("SDL_IOFromDynamicMem failed: ") + SDL_GetError());
        if (!IMG_SavePNG_IO(surface.get(), destination.get(), false))
            throw std::runtime_error(std::string("IMG_SavePNG_IO failed: ") + SDL_GetError());
        return CopyDynamicIo(*destination);
    }

    void ImageLoader::SavePng(const uint8_t* pixels, int width, int height,
                              const std::string& filename)
    {
        auto surface = CreateRgbaSurface(pixels, width, height);
        if (!IMG_SavePNG(surface.get(), filename.c_str()))
            throw std::runtime_error(std::string("IMG_SavePNG failed: ") + SDL_GetError());
    }

    std::vector<uint8_t> ImageLoader::EncodeJpeg(
        const uint8_t* pixels, int width, int height, int targetWidth, int targetHeight, int quality)
    {
        auto surface = ScaleExact(pixels, width, height, targetWidth, targetHeight);
        auto destination = Own(SDL_IOFromDynamicMem());
        if (!destination)
            throw std::runtime_error(std::string("SDL_IOFromDynamicMem failed: ") + SDL_GetError());
        if (!IMG_SaveJPG_IO(surface.get(), destination.get(), false, quality))
            throw std::runtime_error(std::string("IMG_SaveJPG_IO failed: ") + SDL_GetError());
        return CopyDynamicIo(*destination);
    }

    void ImageLoader::SaveJpeg(const uint8_t* pixels, int width, int height,
                               const std::string& filename, int quality)
    {
        auto surface = CreateRgbaSurface(pixels, width, height);
        if (!IMG_SaveJPG(surface.get(), filename.c_str(), quality))
            throw std::runtime_error(std::string("IMG_SaveJPG failed: ") + SDL_GetError());
    }
}
