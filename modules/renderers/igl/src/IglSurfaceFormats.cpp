// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Igl/IglSurfaceFormats.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::Igl
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        using Microsoft::Xna::Framework::Graphics::Texture;

        /// Highest ordinal SurfaceFormat declares; anything past it names no format at all.
        constexpr int kLastSurfaceFormatOrdinal = static_cast<int>(SurfaceFormat::UShortEXT);

        [[nodiscard]] bool IsSurfaceFormatOrdinal(const int surfaceFormat) noexcept
        {
            return surfaceFormat >= 0 && surfaceFormat <= kLastSurfaceFormatOrdinal;
        }

        [[nodiscard]] int DivideRoundingUp(const int value, const int divisor) noexcept
        {
            return value / divisor + (value % divisor == 0 ? 0 : 1);
        }

        /// Why a format has no IGL counterpart, quoted into the refusal message.
        [[nodiscard]] const char* DescribeRefusal(const SurfaceFormat format) noexcept
        {
            switch (format)
            {
                case SurfaceFormat::Bgr565:
                    return "XNA packs this as R5G6B5 in a 16-bit word, which is the reverse of "
                           "igl::TextureFormat::B5G6R5_UNorm -- and IGL's OpenGL backend refuses "
                           "that format outright (toFormatDescGL returns false for it)";
                case SurfaceFormat::Bgra5551:
                    return "XNA packs this as A1R5G5B5; IGL offers only B5G5R5A1 and R5G5B5A1, "
                           "neither of which is that bit order";
                case SurfaceFormat::Bgra4444:
                    return "XNA packs this as A4R4G4B4; IGL's ABGR_UNorm4 is R4G4B4A4 on its "
                           "OpenGL backend and B4G4R4A4 on its Vulkan one -- neither matches, and "
                           "the two disagree with each other";
                case SurfaceFormat::Dxt1:
                case SurfaceFormat::Dxt3:
                case SurfaceFormat::Dxt5:
                case SurfaceFormat::Dxt5SrgbEXT:
                    return "IGL v1.1.1 carries no BC1/BC2/BC3 texture format (its only block "
                           "format on desktop is BC7)";
                case SurfaceFormat::Bc7EXT:
                case SurfaceFormat::Bc7SrgbEXT:
                    return "IGL v1.1.1 names this format, but this renderer has no compressed-block "
                           "upload path -- every transfer goes through ITexture::upload, which "
                           "moves linear rows -- so promoting it would claim a route that does not "
                           "exist";
                case SurfaceFormat::NormalizedByte2:
                case SurfaceFormat::NormalizedByte4:
                    return "IGL v1.1.1 has no signed-normalized texture format at all";
                case SurfaceFormat::Rgba1010102:
                    return "XNA packs this as A2B10G10R10; IGL's RGB10_A2_UNorm_Rev is that layout "
                           "on its OpenGL backend but A2R10G10B10 on its Vulkan one, so the texel "
                           "would depend on which backend the process happened to resolve";
                case SurfaceFormat::Rgba64:
                    return "this is an 8-byte R16G16B16A16 unsigned-normalized texel, and IGL "
                           "v1.1.1 has no 16-bit-per-channel RGBA format -- RGBA_UInt32 is a "
                           "16-byte integer-sampled texel, not a wider match for it";
                case SurfaceFormat::Alpha8:
                    return "IGL's A_UNorm8 maps to VK_FORMAT_UNDEFINED on its Vulkan backend, and "
                           "to the GL_ALPHA family that the OpenGL core profile this renderer asks "
                           "for no longer contains";
                default:
                    return "IGL v1.1.1 has no texture format with this one's texel layout";
            }
        }
    }

    igl::TextureFormat ToIglSurfaceFormatOrInvalid(const int surfaceFormat) noexcept
    {
        if (!IsSurfaceFormatOrdinal(surfaceFormat))
            return igl::TextureFormat::Invalid;

        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            // 32-bit RGBA-shaped: exact on both backends (GL_RGBA/GL_UNSIGNED_BYTE and
            // VK_FORMAT_R8G8B8A8_UNORM; the BGRA and sRGB siblings likewise).
            case SurfaceFormat::Color:        return igl::TextureFormat::RGBA_UNorm8;
            case SurfaceFormat::ColorBgraEXT: return igl::TextureFormat::BGRA_UNorm8;
            case SurfaceFormat::ColorSrgbEXT: return igl::TextureFormat::RGBA_SRGB;

            // Single-channel unsigned-normalized.
            case SurfaceFormat::ByteEXT:      return igl::TextureFormat::R_UNorm8;
            case SurfaceFormat::UShortEXT:    return igl::TextureFormat::R_UNorm16;

            // Two-channel 16-bit unsigned-normalized: GL_RG16 / VK_FORMAT_R16G16_UNORM.
            case SurfaceFormat::Rg32:         return igl::TextureFormat::RG_UNorm16;

            // IEEE floats, exact in both size and interpretation on both backends.
            case SurfaceFormat::Single:       return igl::TextureFormat::R_F32;
            case SurfaceFormat::Vector2:      return igl::TextureFormat::RG_F32;
            case SurfaceFormat::Vector4:      return igl::TextureFormat::RGBA_F32;
            case SurfaceFormat::HalfSingle:   return igl::TextureFormat::R_F16;
            case SurfaceFormat::HalfVector2:  return igl::TextureFormat::RG_F16;
            case SurfaceFormat::HalfVector4:  return igl::TextureFormat::RGBA_F16;
            // XNA's HdrBlendable IS HalfVector4 on every Windows profile, and CNA sizes it the
            // same way (Texture::GetFormatSizeEXT reports 8 for both).
            case SurfaceFormat::HdrBlendable: return igl::TextureFormat::RGBA_F16;

            // Everything else is refused rather than substituted -- see DescribeRefusal above for
            // the specific reason each one carries.
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
            case SurfaceFormat::Dxt5SrgbEXT:
            case SurfaceFormat::Bc7EXT:
            case SurfaceFormat::Bc7SrgbEXT:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::Alpha8:
                return igl::TextureFormat::Invalid;
        }

        return igl::TextureFormat::Invalid;
    }

    bool IsSupportedSurfaceFormat(const int surfaceFormat) noexcept
    {
        return ToIglSurfaceFormatOrInvalid(surfaceFormat) != igl::TextureFormat::Invalid;
    }

    igl::TextureFormat ToIglSurfaceFormat(const int surfaceFormat)
    {
        const igl::TextureFormat format = ToIglSurfaceFormatOrInvalid(surfaceFormat);
        if (format != igl::TextureFormat::Invalid)
            return format;

        std::string reason = IsSurfaceFormatOrdinal(surfaceFormat)
                                 ? DescribeRefusal(static_cast<SurfaceFormat>(surfaceFormat))
                                 : "it names no SurfaceFormat at all";
        throw std::runtime_error(std::string("IGL renderer: SurfaceFormat ") +
                                 GetSurfaceFormatName(surfaceFormat) + " (" +
                                 std::to_string(surfaceFormat) +
                                 ") is not supported by this renderer -- " + reason);
    }

    const char* GetSurfaceFormatName(const int surfaceFormat) noexcept
    {
        if (!IsSurfaceFormatOrdinal(surfaceFormat))
            return "<unknown>";

        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Color:           return "Color";
            case SurfaceFormat::Bgr565:          return "Bgr565";
            case SurfaceFormat::Bgra5551:        return "Bgra5551";
            case SurfaceFormat::Bgra4444:        return "Bgra4444";
            case SurfaceFormat::Dxt1:            return "Dxt1";
            case SurfaceFormat::Dxt3:            return "Dxt3";
            case SurfaceFormat::Dxt5:            return "Dxt5";
            case SurfaceFormat::NormalizedByte2: return "NormalizedByte2";
            case SurfaceFormat::NormalizedByte4: return "NormalizedByte4";
            case SurfaceFormat::Rgba1010102:     return "Rgba1010102";
            case SurfaceFormat::Rg32:            return "Rg32";
            case SurfaceFormat::Rgba64:          return "Rgba64";
            case SurfaceFormat::Alpha8:          return "Alpha8";
            case SurfaceFormat::Single:          return "Single";
            case SurfaceFormat::Vector2:         return "Vector2";
            case SurfaceFormat::Vector4:         return "Vector4";
            case SurfaceFormat::HalfSingle:      return "HalfSingle";
            case SurfaceFormat::HalfVector2:     return "HalfVector2";
            case SurfaceFormat::HalfVector4:     return "HalfVector4";
            case SurfaceFormat::HdrBlendable:    return "HdrBlendable";
            case SurfaceFormat::ColorBgraEXT:    return "ColorBgraEXT";
            case SurfaceFormat::ColorSrgbEXT:    return "ColorSrgbEXT";
            case SurfaceFormat::Dxt5SrgbEXT:     return "Dxt5SrgbEXT";
            case SurfaceFormat::Bc7EXT:          return "Bc7EXT";
            case SurfaceFormat::Bc7SrgbEXT:      return "Bc7SrgbEXT";
            case SurfaceFormat::ByteEXT:         return "ByteEXT";
            case SurfaceFormat::UShortEXT:       return "UShortEXT";
        }
        return "<unknown>";
    }

    bool IsBlockCompressedFormat(const int surfaceFormat) noexcept
    {
        if (!IsSurfaceFormatOrdinal(surfaceFormat))
            return false;
        // The shared metadata already answers this: a linear format's "block size squared" is 1,
        // a 4x4 block format's is 16.
        return Texture::GetBlockSizeSquaredEXT(static_cast<SurfaceFormat>(surfaceFormat)) > 1;
    }

    int FormatUnitByteCount(const int surfaceFormat) noexcept
    {
        if (!IsSurfaceFormatOrdinal(surfaceFormat))
            return 0;
        return Texture::GetFormatSizeEXT(static_cast<SurfaceFormat>(surfaceFormat));
    }

    int FormatRowByteCount(const int surfaceFormat, const int width) noexcept
    {
        const int unitBytes = FormatUnitByteCount(surfaceFormat);
        if (unitBytes <= 0 || width <= 0)
            return 0;
        if (!IsBlockCompressedFormat(surfaceFormat))
            return width * unitBytes;
        // One row of BLOCKS: a 5-texel-wide level still occupies two full block columns.
        return DivideRoundingUp(width, 4) * unitBytes;
    }

    int FormatRegionByteCount(const int surfaceFormat, const int width, const int height) noexcept
    {
        const int rowBytes = FormatRowByteCount(surfaceFormat, width);
        if (rowBytes <= 0 || height <= 0)
            return 0;
        if (!IsBlockCompressedFormat(surfaceFormat))
            return rowBytes * height;
        return rowBytes * DivideRoundingUp(height, 4);
    }

    int FormatBoxByteCount(const int surfaceFormat, const int width, const int height,
                           const int depth) noexcept
    {
        const int sliceBytes = FormatRegionByteCount(surfaceFormat, width, height);
        if (sliceBytes <= 0 || depth <= 0)
            return 0;
        return sliceBytes * depth;
    }

    void FlipRowsInPlace(const int surfaceFormat, const int width, const int height,
                         void* const data) noexcept
    {
        if (data == nullptr || width <= 0 || height <= 1)
            return;

        const int rowBytes = FormatRowByteCount(surfaceFormat, width);
        if (rowBytes <= 0)
            return;

        // A compressed format's "row" is a row of 4x4 blocks, so reversing rows would also have to
        // reverse each block's own four texel rows. No block-compressed format is supported on this
        // renderer (IglSurfaceFormats' own table refuses every one of them), so rather than write
        // arithmetic nothing can reach, refuse to touch the data at all.
        if (IsBlockCompressedFormat(surfaceFormat))
            return;

        auto* const bytes = static_cast<std::uint8_t*>(data);
        std::vector<std::uint8_t> scratch(static_cast<std::size_t>(rowBytes));
        for (int row = 0; row < height / 2; ++row)
        {
            std::uint8_t* const top = bytes + static_cast<std::size_t>(row) * rowBytes;
            std::uint8_t* const bottom =
                bytes + static_cast<std::size_t>(height - 1 - row) * rowBytes;
            std::memcpy(scratch.data(), top, static_cast<std::size_t>(rowBytes));
            std::memcpy(top, bottom, static_cast<std::size_t>(rowBytes));
            std::memcpy(bottom, scratch.data(), static_cast<std::size_t>(rowBytes));
        }
    }
}
