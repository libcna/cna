#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "CNA/Logger.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "System/IO/Stream.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Backends;
    using namespace CNA::Internal::Graphics;

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

    void Texture2D::storeCpuPixels(const uint8_t* rgba, int pixelCount)
    {
        cpuPixels_.assign(rgba, rgba + static_cast<std::size_t>(pixelCount) * 4);
    }

    // -----------------------------------------------------------------------
    // Constructors
    // -----------------------------------------------------------------------

    Texture2D::Texture2D() = default;

    Texture2D::Texture2D(const std::string& assetName, GraphicsDevice& graphicsDevice)
    {
        ImageData data = ImageLoader::Load(assetName);
        device_  = &graphicsDevice;
        width    = data.width;
        height   = data.height;
        storeCpuPixels(data.pixels.data(), width * height);
        backend_ = graphicsDevice.GetBackend().CreateTexture(data);
    }

    Texture2D::Texture2D(const std::string& assetName)
    {
        ImageData data = ImageLoader::Load(assetName);
        width    = data.width;
        height   = data.height;
        storeCpuPixels(data.pixels.data(), width * height);
        // No GraphicsDevice — backend stays null until attached.
    }

    Texture2D::Texture2D(GraphicsDevice& graphicsDevice, int w, int h)
        : device_(&graphicsDevice), width(w), height(h)
    {
        ImageData data;
        data.width  = w;
        data.height = h;
        data.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        cpuPixels_ = data.pixels;
        backend_   = graphicsDevice.GetBackend().CreateTexture(data);
    }

    Texture2D::~Texture2D() = default;

    // -----------------------------------------------------------------------
    // Properties
    // -----------------------------------------------------------------------

    Rectangle Texture2D::getBoundsProperty() const
    {
        return {0, 0, width, height};
    }

    // -----------------------------------------------------------------------
    // SetData
    // -----------------------------------------------------------------------

    void Texture2D::SetData(const Color* data, int elementCount)
    {
        if (!device_ || !data || elementCount <= 0) return;
        ImageData img;
        img.width  = width;
        img.height = height;
        img.pixels.resize(static_cast<std::size_t>(elementCount) * 4);
        for (int i = 0; i < elementCount; ++i)
        {
            img.pixels[i * 4 + 0] = data[i].getRProperty();
            img.pixels[i * 4 + 1] = data[i].getGProperty();
            img.pixels[i * 4 + 2] = data[i].getBProperty();
            img.pixels[i * 4 + 3] = data[i].getAProperty();
        }
        cpuPixels_ = img.pixels;
        backend_   = device_->GetBackend().CreateTexture(img);
    }

    void Texture2D::SetDataRGBA(const uint8_t* data, int pixelCount)
    {
        if (!backend_ || !data || pixelCount <= 0) return;
        storeCpuPixels(data, pixelCount);
        backend_->UpdatePixels(data, width * 4);
    }

    // -----------------------------------------------------------------------
    // GetData
    // -----------------------------------------------------------------------

    void Texture2D::GetData(Color* data, int startIndex, int elementCount) const
    {
        if (!data || elementCount <= 0)
            throw std::invalid_argument("data must not be null and elementCount must be > 0");
        if (cpuPixels_.empty())
            throw std::runtime_error("Texture2D::GetData: no CPU-side pixel data available");

        int total = width * height;
        if (startIndex + elementCount > total)
            throw std::out_of_range("Texture2D::GetData: index out of range");

        for (int i = 0; i < elementCount; ++i)
        {
            int src = (startIndex + i) * 4;
            data[i] = Color(cpuPixels_[src + 0],
                            cpuPixels_[src + 1],
                            cpuPixels_[src + 2],
                            cpuPixels_[src + 3]);
        }
    }

    void Texture2D::GetData(Color* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    // -----------------------------------------------------------------------
    // FromStream
    // -----------------------------------------------------------------------

    Texture2D Texture2D::FromStream(GraphicsDevice& graphicsDevice, System::IO::Stream& stream)
    {
        using System::IO::intcs;
        using System::IO::bytecs;

        intcs len = stream.getLengthProperty();
        if (len <= 0)
            throw std::runtime_error("Texture2D::FromStream: stream is empty or length unknown");

        std::vector<bytecs> buf(static_cast<std::size_t>(len));
        stream.Read(buf.data(), 0, len);

        ImageData img = ImageLoader::LoadFromMemory(
            reinterpret_cast<const uint8_t*>(buf.data()),
            static_cast<std::size_t>(len));

        Texture2D tex;
        tex.device_  = &graphicsDevice;
        tex.width    = img.width;
        tex.height   = img.height;
        tex.storeCpuPixels(img.pixels.data(), img.width * img.height);
        tex.backend_ = graphicsDevice.GetBackend().CreateTexture(img);
        return tex;
    }

    // -----------------------------------------------------------------------
    // SaveAsPng
    // -----------------------------------------------------------------------

    void Texture2D::SaveAsPng(const std::string& filename) const
    {
        if (cpuPixels_.empty())
            throw std::runtime_error("Texture2D::SaveAsPng: no CPU-side pixel data available");

        SDL_Surface* surface = SDL_CreateSurfaceFrom(
            width, height, SDL_PIXELFORMAT_RGBA32,
            const_cast<uint8_t*>(cpuPixels_.data()), width * 4);

        if (!surface)
            throw std::runtime_error(std::string("SDL_CreateSurfaceFrom failed: ") + SDL_GetError());

        if (!IMG_SavePNG(surface, filename.c_str()))
        {
            SDL_DestroySurface(surface);
            throw std::runtime_error(std::string("IMG_SavePNG failed: ") + SDL_GetError());
        }
        SDL_DestroySurface(surface);
    }

    // -----------------------------------------------------------------------
    // NOXNA helpers
    // -----------------------------------------------------------------------

    SDL_Texture* Texture2D::GetNativeTextureInternal() const
    {
        return backend_ ? backend_->GetNativeTexture() : nullptr;
    }

    Texture2D Texture2D::CreateFromPixels(GraphicsDevice& device,
                                          int w, int h,
                                          const std::vector<std::uint8_t>& rgba)
    {
        ImageData data;
        data.width  = w;
        data.height = h;
        data.pixels = rgba;
        Texture2D tex;
        tex.device_     = &device;
        tex.width       = w;
        tex.height      = h;
        tex.cpuPixels_  = rgba;
        tex.backend_    = device.GetBackend().CreateTexture(data);
        return tex;
    }
}
