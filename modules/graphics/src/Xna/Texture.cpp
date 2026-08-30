// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include <algorithm>
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

    int Texture::GetPixelStoreAlignment(SurfaceFormat format)
    {
        return std::min(8, GetFormatSizeEXT(format));
    }

    void Texture::ValidateGetDataFormat(SurfaceFormat format, int elementSizeInBytes)
    {
        if (GetFormatSizeEXT(format) % elementSizeInBytes != 0)
            throw std::invalid_argument(
                "Texture::ValidateGetDataFormat: the type used for the destination element is an "
                "invalid size for this resource");
    }

    bool Texture::IsFormatAllowedByProfileEXT(GraphicsProfile profile, SurfaceFormat fmt) noexcept
    {
        // HiDef refuses none of the XNA 4.0 SurfaceFormats for a Texture2D, so only Reach needs a
        // list. Anything outside the enum XNA knows (CNA's own *EXT formats) is not the profile's
        // business and is left to the renderer to accept or refuse.
        if (profile != GraphicsProfile::Reach)
            return true;
        switch (fmt)
        {
        case SurfaceFormat::Color:
        case SurfaceFormat::Bgr565:
        case SurfaceFormat::Bgra5551:
        case SurfaceFormat::Bgra4444:
        case SurfaceFormat::Dxt1:
        case SurfaceFormat::Dxt3:
        case SurfaceFormat::Dxt5:
        case SurfaceFormat::NormalizedByte2:
        case SurfaceFormat::NormalizedByte4:
            return true;
        // Measured as refused by Reach: Rgba1010102, Rg32, Rgba64, Alpha8, Single, Vector2,
        // Vector4, HalfSingle, HalfVector2, HalfVector4, HdrBlendable.
        case SurfaceFormat::Rgba1010102:
        case SurfaceFormat::Rg32:
        case SurfaceFormat::Rgba64:
        case SurfaceFormat::Alpha8:
        case SurfaceFormat::Single:
        case SurfaceFormat::Vector2:
        case SurfaceFormat::Vector4:
        case SurfaceFormat::HalfSingle:
        case SurfaceFormat::HalfVector2:
        case SurfaceFormat::HalfVector4:
        case SurfaceFormat::HdrBlendable:
            return false;
        default:
            return true;
        }
    }

    bool Texture::IsCubeFormatAllowedByProfileEXT(GraphicsProfile profile,
                                                  SurfaceFormat fmt) noexcept
    {
        (void)profile;
        // A cube never carries the signed-normalized byte formats, on EITHER profile. Measured
        // rather than assumed: HiDef refuses them on a device that carries them as a Texture2D
        // without complaint, so it is the resource kind saying no and not the hardware. Nothing in
        // CNA has ever wanted such a cube, so enforcing this costs nothing.
        //
        // Deliberately NOT the whole cube list. XNA's Reach tier also excludes the eleven
        // HiDef-only formats for a cube, and enforcing THAT would refuse a float cube on the
        // default profile -- which MOD-107 exists to guarantee, because an irradiance or
        // prefiltered-specular cube is rendered face by face into float storage and "a cube that
        // reported HdrBlendable while holding 8-bit texels would make every IBL product quietly
        // wrong". That collision is the same one MOD-115 settled for RenderTarget2D, and it is the
        // owner's to reopen, not this gate's to decide. See REMED-GFX-245.
        return fmt != SurfaceFormat::NormalizedByte2 && fmt != SurfaceFormat::NormalizedByte4;
    }

    bool Texture::IsRenderTargetFormatAllowedByProfileEXT(GraphicsProfile profile,
                                                          SurfaceFormat fmt) noexcept
    {
        // Nothing renders into a block-compressed surface, at either profile.
        if (fmt == SurfaceFormat::Dxt1 || fmt == SurfaceFormat::Dxt3 || fmt == SurfaceFormat::Dxt5)
            return false;
        return IsFormatAllowedByProfileEXT(profile, fmt);
    }

    void Texture::ValidateFormat(SurfaceFormat fmt)
    {
        if (fmt == SurfaceFormat::Color)
            return;
        throw std::runtime_error(
            "Texture: SurfaceFormat " + std::to_string(static_cast<int>(fmt)) +
            " is not implemented by the selected graphics renderer");
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
