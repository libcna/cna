// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    Texture::Texture(GraphicsDevice* device)
        : GraphicsResource(device)
    {
    }

    int Texture::GetBlockSizeSquaredEXT(SurfaceFormat format)
    {
        switch (format)
        {
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
            case SurfaceFormat::Dxt5SrgbEXT:
            case SurfaceFormat::Bc7EXT:
            case SurfaceFormat::Bc7SrgbEXT:
                return 16;
            case SurfaceFormat::Alpha8:
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::Color:
            case SurfaceFormat::Single:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::NormalizedByte4:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::ColorBgraEXT:
            case SurfaceFormat::ColorSrgbEXT:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::HdrBlendable:
            case SurfaceFormat::Vector4:
            case SurfaceFormat::ByteEXT:
            case SurfaceFormat::UShortEXT:
                return 1;
            default:
                throw std::out_of_range("Texture::GetBlockSizeSquaredEXT: format is not a valid SurfaceFormat value");
        }
    }

    int Texture::GetFormatSizeEXT(SurfaceFormat format)
    {
        switch (format)
        {
            case SurfaceFormat::Dxt1:
                return 8;
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
            case SurfaceFormat::Dxt5SrgbEXT:
            case SurfaceFormat::Bc7EXT:
            case SurfaceFormat::Bc7SrgbEXT:
                return 16;
            case SurfaceFormat::Alpha8:
            case SurfaceFormat::ByteEXT:
                return 1;
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::UShortEXT:
                return 2;
            case SurfaceFormat::Color:
            case SurfaceFormat::Single:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::NormalizedByte4:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::ColorBgraEXT:
            case SurfaceFormat::ColorSrgbEXT:
                return 4;
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::HdrBlendable:
                return 8;
            case SurfaceFormat::Vector4:
                return 16;
            default:
                throw std::out_of_range("Texture::GetFormatSizeEXT: format is not a valid SurfaceFormat value");
        }
    }

    void Texture::ValidateFormat(SurfaceFormat fmt)
    {
        if (fmt == SurfaceFormat::Color)
            return;
        throw std::runtime_error(
            "Texture: SurfaceFormat " + std::to_string(static_cast<int>(fmt)) +
            " is not implemented; only SurfaceFormat::Color is currently supported");
    }

    SurfaceFormat Texture::getFormatProperty() const
    {
        return format_;
    }

    int Texture::getLevelCountProperty() const
    {
        return levelCount_;
    }

    void Texture::Dispose(bool disposing)
    {
        if (!isDisposed_ && graphicsDevice_ != nullptr)
        {
            graphicsDevice_->getTexturesProperty().RemoveDisposedTexture(this);
            graphicsDevice_->getVertexTexturesProperty().RemoveDisposedTexture(this);
        }
        GraphicsResource::Dispose(disposing);
    }
}
