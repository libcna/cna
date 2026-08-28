// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace CNA::Content::Cnb
{
    /**
     * @brief The pixel format identifiers a `.cnb` texture stores (plans/plan_cnb.md `CNBF-101A-fmt`).
     *
     * This enumeration exists **instead of** serializing
     * `Microsoft::Xna::Framework::Graphics::SurfaceFormat` directly, and the reason is worth
     * stating because the shortcut is tempting: `SurfaceFormat`'s enumerators carry no explicit
     * values, so they are numbered by position. Inserting one enumerator — a perfectly ordinary
     * thing to do to an XNA-shaped enum — would renumber every enumerator after it and silently
     * change the meaning of every `.cnb` already written. A file format cannot be hostage to the
     * declaration order of a runtime enum.
     *
     * So these values are **frozen** exactly like the container's own constants, they are written
     * out explicitly, and the mapping between the two enumerations is a function that has to be
     * edited deliberately rather than an implicit cast that follows along quietly.
     *
     * Every `SurfaceFormat` CNA defines has an identifier here, so the numbering never has to be
     * extended for a format that already exists. That is separate from what CNB schema 1 will
     * actually *encode*, which is `Rgba8` alone — see `CnbTextureCodec.hpp`.
     */
    enum class CnbTextureFormat : std::uint32_t
    {
        /** @brief Not a valid format; a file declaring it is rejected. */
        Unknown = 0u,

        /** @brief 8 bits per channel RGBA, the portable baseline. Maps to `SurfaceFormat::Color`. */
        Rgba8 = 1u,
        /** @brief 8 bits per channel with BGRA transfer order. Maps to `SurfaceFormat::ColorBgraEXT`. */
        Bgra8 = 2u,
        /** @brief 8 bits per channel RGBA, sRGB-encoded. Maps to `SurfaceFormat::ColorSrgbEXT`. */
        Rgba8Srgb = 3u,
        /** @brief 16-bit 5:6:5 BGR. Maps to `SurfaceFormat::Bgr565`. */
        Bgr565 = 4u,
        /** @brief 16-bit 5:5:5:1 BGRA. Maps to `SurfaceFormat::Bgra5551`. */
        Bgra5551 = 5u,
        /** @brief 16-bit 4:4:4:4 BGRA. Maps to `SurfaceFormat::Bgra4444`. */
        Bgra4444 = 6u,
        /** @brief 8-bit alpha only. Maps to `SurfaceFormat::Alpha8`. */
        Alpha8 = 7u,
        /** @brief 8-bit single channel. Maps to `SurfaceFormat::ByteEXT`. */
        R8 = 8u,
        /** @brief 16-bit unsigned single channel. Maps to `SurfaceFormat::UShortEXT`. */
        R16 = 9u,
        /** @brief 16 bits per channel RG. Maps to `SurfaceFormat::Rg32`. */
        Rg16 = 10u,
        /** @brief 16 bits per channel RGBA. Maps to `SurfaceFormat::Rgba64`. */
        Rgba16 = 11u,
        /** @brief Signed 8 bits per channel RG. Maps to `SurfaceFormat::NormalizedByte2`. */
        Rg8Snorm = 12u,
        /** @brief Signed 8 bits per channel RGBA. Maps to `SurfaceFormat::NormalizedByte4`. */
        Rgba8Snorm = 13u,
        /** @brief 10:10:10:2 RGBA. Maps to `SurfaceFormat::Rgba1010102`. */
        Rgb10A2 = 14u,
        /** @brief 32-bit float, one channel. Maps to `SurfaceFormat::Single`. */
        R32Float = 15u,
        /** @brief 32-bit float per channel, RG. Maps to `SurfaceFormat::Vector2`. */
        Rg32Float = 16u,
        /** @brief 32-bit float per channel, RGBA. Maps to `SurfaceFormat::Vector4`. */
        Rgba32Float = 17u,
        /** @brief 16-bit float, one channel. Maps to `SurfaceFormat::HalfSingle`. */
        R16Float = 18u,
        /** @brief 16-bit float per channel, RG. Maps to `SurfaceFormat::HalfVector2`. */
        Rg16Float = 19u,
        /** @brief 16-bit float per channel, RGBA. Maps to `SurfaceFormat::HalfVector4`. */
        Rgba16Float = 20u,
        /** @brief The renderer's preferred HDR-blendable format. Maps to `SurfaceFormat::HdrBlendable`. */
        HdrBlendable = 21u,

        /** @brief BC1 / DXT1 block compression. Maps to `SurfaceFormat::Dxt1`. */
        Bc1 = 22u,
        /** @brief BC2 / DXT3 block compression. Maps to `SurfaceFormat::Dxt3`. */
        Bc2 = 23u,
        /** @brief BC3 / DXT5 block compression. Maps to `SurfaceFormat::Dxt5`. */
        Bc3 = 24u,
        /** @brief BC3 / DXT5 block compression, sRGB. Maps to `SurfaceFormat::Dxt5SrgbEXT`. */
        Bc3Srgb = 25u,
        /** @brief BC7 block compression. Maps to `SurfaceFormat::Bc7EXT`. */
        Bc7 = 26u,
        /** @brief BC7 block compression, sRGB. Maps to `SurfaceFormat::Bc7SrgbEXT`. */
        Bc7Srgb = 27u,
    };

    /** @brief Highest identifier this build assigns; a larger value in a file is rejected. */
    inline constexpr std::uint32_t CnbTextureFormatMax = 27u;

    /**
     * @brief Whether @p value names a format identifier this build knows.
     *
     * @param value The raw identifier read from a file.
     * @return True when the value is in `[1, CnbTextureFormatMax]`.
     */
    [[nodiscard]] constexpr bool IsKnownCnbTextureFormat(std::uint32_t value)
    {
        return value != 0u && value <= CnbTextureFormatMax;
    }

    /**
     * @brief Renders a format identifier for diagnostics.
     *
     * @param format The format to render.
     * @return The format's name, or a hexadecimal rendering when it is not a known identifier.
     */
    [[nodiscard]] std::string CnbTextureFormatToString(CnbTextureFormat format);

    /**
     * @brief Whether @p format stores 4x4 texel blocks rather than individual texels.
     *
     * @param format The format to classify.
     * @return True for the BC formats, false for every uncompressed format.
     */
    [[nodiscard]] bool IsBlockCompressedCnbTextureFormat(CnbTextureFormat format);

    /**
     * @brief Bytes one texel occupies, for an uncompressed format, or one 4x4 block, for a
     *        block-compressed one.
     *
     * @param format The format to measure.
     * @return The unit size in bytes; 0 when @p format is not a known identifier.
     */
    [[nodiscard]] std::uint32_t CnbTextureFormatUnitBytes(CnbTextureFormat format);

    /**
     * @brief Bytes one mip level of the given dimensions occupies in @p format.
     *
     * A block-compressed level rounds each dimension up to a whole 4-texel block, which is what
     * makes a 1x1 BC7 level 16 bytes rather than a fraction of one.
     *
     * @param format The storage format.
     * @param width  Level width in texels; must be at least 1.
     * @param height Level height in texels; must be at least 1.
     * @param depth  Level depth in texels; 1 for a 2D or cube texture.
     * @return The level's exact byte size.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if @p format is unknown, a
     *         dimension is 0, or the product overflows 64 bits.
     */
    [[nodiscard]] std::uint64_t CnbTextureLevelByteSize(CnbTextureFormat format,
                                                        std::uint32_t width,
                                                        std::uint32_t height,
                                                        std::uint32_t depth);

    /**
     * @brief Maps a CNB format identifier onto the runtime surface format.
     *
     * @param format The identifier read from a file.
     * @return The equivalent `SurfaceFormat`.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if @p format is unknown.
     */
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::SurfaceFormat
    CnbTextureFormatToSurfaceFormat(CnbTextureFormat format);

    /**
     * @brief Maps a runtime surface format onto its CNB format identifier.
     *
     * @param format The runtime format.
     * @return The equivalent identifier.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if @p format has no CNB
     *         identifier, which can only happen if a `SurfaceFormat` enumerator is added without
     *         extending this mapping.
     */
    [[nodiscard]] CnbTextureFormat SurfaceFormatToCnbTextureFormat(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format);
}
