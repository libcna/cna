#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "CNA/Logger.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Backends;

    Texture2D::Texture2D()
    {
    }

    Texture2D::Texture2D(const std::string& assetName, GraphicsDevice& graphicsDevice)
    {
        using namespace CNA::Internal::Graphics;
        ImageData data = ImageLoader::Load(assetName);
        backend_ = graphicsDevice.GetBackend().CreateTexture(data);
        if (backend_)
        {
            width = backend_->GetWidth();
            height = backend_->GetHeight();
        }
    }

    Texture2D::Texture2D(GraphicsDevice& graphicsDevice, int w, int h)
        : device_(&graphicsDevice), width(w), height(h)
    {
        CNA::Internal::Graphics::ImageData data;
        data.width  = w;
        data.height = h;
        data.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        backend_ = graphicsDevice.GetBackend().CreateTexture(data);
    }

    void Texture2D::SetData(const Color* data, int elementCount)
    {
        if (!device_ || !data || elementCount <= 0) return;
        CNA::Internal::Graphics::ImageData img;
        img.width  = width;
        img.height = height;
        img.pixels.resize(static_cast<std::size_t>(elementCount) * 4);
        for (int i = 0; i < elementCount; ++i) {
            img.pixels[i * 4 + 0] = data[i].getRProperty();
            img.pixels[i * 4 + 1] = data[i].getGProperty();
            img.pixels[i * 4 + 2] = data[i].getBProperty();
            img.pixels[i * 4 + 3] = data[i].getAProperty();
        }
        backend_ = device_->GetBackend().CreateTexture(img);
    }

    Texture2D::~Texture2D()
    {
    }

    Rectangle Texture2D::getBoundsProperty() const
    {
        return {0, 0, width, height};
    }

    SDL_Texture* Texture2D::GetNativeTextureInternal() const
    {
        return backend_ ? backend_->GetNativeTexture() : nullptr;
    }

    Texture2D Texture2D::CreateFromPixels(GraphicsDevice& device,
                                          int w, int h,
                                          const std::vector<std::uint8_t>& rgba)
    {
        CNA::Internal::Graphics::ImageData data;
        data.width  = w;
        data.height = h;
        data.pixels = rgba;
        Texture2D tex;
        tex.backend_ = device.GetBackend().CreateTexture(data);
        if (tex.backend_)
        {
            tex.width  = tex.backend_->GetWidth();
            tex.height = tex.backend_->GetHeight();
        }
        return tex;
    }
}
