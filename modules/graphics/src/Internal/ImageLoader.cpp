#include "CNA/Internal/Graphics/ImageLoader.hpp"

// Keep every stb symbol local to this translation unit. The content module also instantiates the
// vendored headers for glTF import, so external linkage here would create duplicate definitions in
// applications that use both content and graphics.
#include <math.h>
#include <stdarg.h>
#include <string.h>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Graphics
{
    namespace
    {
        using DecodedPixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;

        [[nodiscard]] std::size_t PixelByteCount(const int width, const int height)
        {
            if (width <= 0 || height <= 0)
                throw std::invalid_argument("ImageLoader: image dimensions must be positive");

            const auto w = static_cast<std::size_t>(width);
            const auto h = static_cast<std::size_t>(height);
            if (width > std::numeric_limits<int>::max() / 4
                || w > std::numeric_limits<std::size_t>::max() / h
                || w * h > std::numeric_limits<std::size_t>::max() / 4u)
            {
                throw std::overflow_error("ImageLoader: RGBA image dimensions are too large");
            }
            return w * h * 4u;
        }

        void ValidateRgba(const uint8_t* pixels, const int width, const int height)
        {
            if (pixels == nullptr)
                throw std::invalid_argument("ImageLoader: RGBA pixels must not be null");
            (void)PixelByteCount(width, height);
        }

        [[nodiscard]] std::string StbFailure(const std::string& operation)
        {
            const char* reason = stbi_failure_reason();
            return operation + (reason != nullptr ? ": " + std::string(reason) : ": unknown error");
        }

        [[nodiscard]] ImageData CopyDecoded(
            stbi_uc* decoded, const int width, const int height, const std::string& operation)
        {
            DecodedPixels owned(decoded, stbi_image_free);
            if (!owned)
                throw std::runtime_error(StbFailure(operation));

            ImageData result;
            result.width = width;
            result.height = height;
            const std::size_t bytes = PixelByteCount(width, height);
            result.pixels.assign(owned.get(), owned.get() + bytes);
            return result;
        }

        [[nodiscard]] ImageData ResizeRegion(
            const uint8_t* pixels, const int width, const int height,
            const int cropX, const int cropY, const int cropWidth, const int cropHeight,
            const int targetWidth, const int targetHeight)
        {
            ValidateRgba(pixels, width, height);
            if (targetWidth <= 0 || targetHeight <= 0)
                throw std::invalid_argument("ImageLoader: target dimensions must be positive");
            if (cropX < 0 || cropY < 0 || cropWidth <= 0 || cropHeight <= 0
                || cropX > width - cropWidth || cropY > height - cropHeight)
            {
                throw std::invalid_argument("ImageLoader: crop rectangle lies outside the source image");
            }

            ImageData result;
            result.width = targetWidth;
            result.height = targetHeight;
            result.pixels.resize(PixelByteCount(targetWidth, targetHeight));

            if (cropX == 0 && cropY == 0 && cropWidth == width && cropHeight == height
                && targetWidth == width && targetHeight == height)
            {
                std::copy_n(pixels, result.pixels.size(), result.pixels.data());
                return result;
            }

            // Pixel-centre bilinear mapping. Sampling is clamped to the selected crop, so a cover
            // resize cannot bleed either discarded edge back into the output.
            const double scaleX = static_cast<double>(cropWidth) / targetWidth;
            const double scaleY = static_cast<double>(cropHeight) / targetHeight;
            const int maximumX = cropX + cropWidth - 1;
            const int maximumY = cropY + cropHeight - 1;
            for (int y = 0; y < targetHeight; ++y)
            {
                const double sourceY = std::clamp(
                    cropY + (static_cast<double>(y) + 0.5) * scaleY - 0.5,
                    static_cast<double>(cropY), static_cast<double>(maximumY));
                const int y0 = static_cast<int>(std::floor(sourceY));
                const int y1 = std::min(y0 + 1, maximumY);
                const double fy = sourceY - y0;

                for (int x = 0; x < targetWidth; ++x)
                {
                    const double sourceX = std::clamp(
                        cropX + (static_cast<double>(x) + 0.5) * scaleX - 0.5,
                        static_cast<double>(cropX), static_cast<double>(maximumX));
                    const int x0 = static_cast<int>(std::floor(sourceX));
                    const int x1 = std::min(x0 + 1, maximumX);
                    const double fx = sourceX - x0;

                    const std::size_t topLeft =
                        (static_cast<std::size_t>(y0) * width + x0) * 4u;
                    const std::size_t topRight =
                        (static_cast<std::size_t>(y0) * width + x1) * 4u;
                    const std::size_t bottomLeft =
                        (static_cast<std::size_t>(y1) * width + x0) * 4u;
                    const std::size_t bottomRight =
                        (static_cast<std::size_t>(y1) * width + x1) * 4u;
                    const std::size_t destination =
                        (static_cast<std::size_t>(y) * targetWidth + x) * 4u;

                    for (std::size_t channel = 0; channel < 4u; ++channel)
                    {
                        const double top = pixels[topLeft + channel]
                            + (pixels[topRight + channel] - pixels[topLeft + channel]) * fx;
                        const double bottom = pixels[bottomLeft + channel]
                            + (pixels[bottomRight + channel] - pixels[bottomLeft + channel]) * fx;
                        result.pixels[destination + channel] = static_cast<uint8_t>(
                            std::clamp(std::lround(top + (bottom - top) * fy), 0L, 255L));
                    }
                }
            }
            return result;
        }

        struct EncodeBuffer
        {
            std::vector<uint8_t> bytes;
            bool allocationFailed = false;
        };

        void AppendEncodedBytes(void* context, void* data, const int size) noexcept
        {
            auto& destination = *static_cast<EncodeBuffer*>(context);
            if (destination.allocationFailed || data == nullptr || size <= 0)
                return;
            try
            {
                const auto* first = static_cast<const uint8_t*>(data);
                destination.bytes.insert(destination.bytes.end(), first, first + size);
            }
            catch (...)
            {
                destination.allocationFailed = true;
            }
        }

        template<typename Encoder>
        [[nodiscard]] std::vector<uint8_t> EncodeExactSize(
            const uint8_t* pixels, const int width, const int height,
            const int targetWidth, const int targetHeight, Encoder&& encoder,
            const char* failureMessage)
        {
            ValidateRgba(pixels, width, height);
            const uint8_t* source = pixels;
            ImageData resized;
            if (targetWidth != width || targetHeight != height)
            {
                resized = ResizeRegion(
                    pixels, width, height, 0, 0, width, height, targetWidth, targetHeight);
                source = resized.pixels.data();
            }
            else if (targetWidth <= 0 || targetHeight <= 0)
            {
                throw std::invalid_argument("ImageLoader: target dimensions must be positive");
            }

            EncodeBuffer output;
            const int encoded = encoder(output, source, targetWidth, targetHeight);
            if (output.allocationFailed)
                throw std::bad_alloc();
            if (encoded == 0 || output.bytes.empty())
                throw std::runtime_error(failureMessage);
            return output.bytes;
        }
    }

    ImageData ImageLoader::Load(const std::string& assetName)
    {
        int width = 0;
        int height = 0;
        int sourceChannels = 0;
        stbi_uc* decoded = stbi_load(
            assetName.c_str(), &width, &height, &sourceChannels, STBI_rgb_alpha);
        return CopyDecoded(decoded, width, height, "Failed to load image: " + assetName);
    }

    ImageData ImageLoader::LoadFromMemory(const uint8_t* data, const std::size_t size)
    {
        if (data == nullptr || size == 0)
            throw std::invalid_argument("ImageLoader::LoadFromMemory: buffer must not be empty");
        if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::invalid_argument("ImageLoader::LoadFromMemory: buffer is too large");

        int width = 0;
        int height = 0;
        int sourceChannels = 0;
        stbi_uc* decoded = stbi_load_from_memory(
            data, static_cast<int>(size), &width, &height, &sourceChannels, STBI_rgb_alpha);
        return CopyDecoded(decoded, width, height, "Failed to load image from memory");
    }

    ImageData ImageLoader::ResizeRgba(const uint8_t* pixels, const int width, const int height,
                                      const int targetWidth, const int targetHeight, const bool zoom)
    {
        ValidateRgba(pixels, width, height);
        if (targetWidth <= 0 || targetHeight <= 0)
            throw std::invalid_argument("ImageLoader: target dimensions must be positive");

        const bool scaleWidth = zoom ? (width < height) : (width > height);
        const double scale = scaleWidth
            ? static_cast<double>(targetWidth) / width
            : static_cast<double>(targetHeight) / height;

        if (!zoom)
        {
            return ResizeRegion(
                pixels, width, height, 0, 0, width, height,
                static_cast<int>(width * scale), static_cast<int>(height * scale));
        }

        int cropX = 0;
        int cropY = 0;
        int cropWidth = width;
        int cropHeight = height;
        if (scaleWidth)
        {
            cropY = height / 2 - static_cast<int>((targetHeight / scale) / 2);
            cropHeight = static_cast<int>(targetHeight / scale);
        }
        else
        {
            cropX = width / 2 - static_cast<int>((targetWidth / scale) / 2);
            cropWidth = static_cast<int>(targetWidth / scale);
        }
        return ResizeRegion(
            pixels, width, height, cropX, cropY, cropWidth, cropHeight,
            targetWidth, targetHeight);
    }

    std::vector<uint8_t> ImageLoader::EncodePng(
        const uint8_t* pixels, const int width, const int height,
        const int targetWidth, const int targetHeight)
    {
        return EncodeExactSize(
            pixels, width, height, targetWidth, targetHeight,
            [](EncodeBuffer& output, const uint8_t* source, const int w, const int h) {
                return stbi_write_png_to_func(
                    AppendEncodedBytes, &output, w, h, 4, source, w * 4);
            },
            "ImageLoader: PNG encoding failed");
    }

    void ImageLoader::SavePng(const uint8_t* pixels, const int width, const int height,
                              const std::string& filename)
    {
        ValidateRgba(pixels, width, height);
        if (stbi_write_png(filename.c_str(), width, height, 4, pixels, width * 4) == 0)
            throw std::runtime_error("ImageLoader: failed to save PNG: " + filename);
    }

    std::vector<uint8_t> ImageLoader::EncodeJpeg(
        const uint8_t* pixels, const int width, const int height,
        const int targetWidth, const int targetHeight, const int quality)
    {
        return EncodeExactSize(
            pixels, width, height, targetWidth, targetHeight,
            [quality](EncodeBuffer& output, const uint8_t* source, const int w, const int h) {
                return stbi_write_jpg_to_func(
                    AppendEncodedBytes, &output, w, h, 4, source, quality);
            },
            "ImageLoader: JPEG encoding failed");
    }

    void ImageLoader::SaveJpeg(const uint8_t* pixels, const int width, const int height,
                               const std::string& filename, const int quality)
    {
        ValidateRgba(pixels, width, height);
        if (stbi_write_jpg(filename.c_str(), width, height, 4, pixels, quality) == 0)
            throw std::runtime_error("ImageLoader: failed to save JPEG: " + filename);
    }
}
