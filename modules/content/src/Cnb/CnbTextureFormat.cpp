// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbTextureFormat.hpp"

#include <array>
#include <limits>

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

namespace CNA::Content::Cnb
{
    namespace
    {
        struct FormatRow
        {
            CnbTextureFormat cnb;
            SurfaceFormat surface;
            std::uint32_t unitBytes;
            bool blockCompressed;
            const char* name;
        };

        // One row per identifier, in identifier order. This table is the mapping in both
        // directions and the single place a new format is added; keeping it one table is what
        // stops the two directions from drifting apart, which a pair of switch statements would
        // eventually do.
        constexpr std::array<FormatRow, CnbTextureFormatMax> kFormats{{
            {CnbTextureFormat::Rgba8,        SurfaceFormat::Color,           4u,  false, "Rgba8"},
            {CnbTextureFormat::Bgra8,        SurfaceFormat::ColorBgraEXT,    4u,  false, "Bgra8"},
            {CnbTextureFormat::Rgba8Srgb,    SurfaceFormat::ColorSrgbEXT,    4u,  false, "Rgba8Srgb"},
            {CnbTextureFormat::Bgr565,       SurfaceFormat::Bgr565,          2u,  false, "Bgr565"},
            {CnbTextureFormat::Bgra5551,     SurfaceFormat::Bgra5551,        2u,  false, "Bgra5551"},
            {CnbTextureFormat::Bgra4444,     SurfaceFormat::Bgra4444,        2u,  false, "Bgra4444"},
            {CnbTextureFormat::Alpha8,       SurfaceFormat::Alpha8,          1u,  false, "Alpha8"},
            {CnbTextureFormat::R8,           SurfaceFormat::ByteEXT,         1u,  false, "R8"},
            {CnbTextureFormat::R16,          SurfaceFormat::UShortEXT,       2u,  false, "R16"},
            {CnbTextureFormat::Rg16,         SurfaceFormat::Rg32,            4u,  false, "Rg16"},
            {CnbTextureFormat::Rgba16,       SurfaceFormat::Rgba64,          8u,  false, "Rgba16"},
            {CnbTextureFormat::Rg8Snorm,     SurfaceFormat::NormalizedByte2, 2u,  false, "Rg8Snorm"},
            {CnbTextureFormat::Rgba8Snorm,   SurfaceFormat::NormalizedByte4, 4u,  false, "Rgba8Snorm"},
            {CnbTextureFormat::Rgb10A2,      SurfaceFormat::Rgba1010102,     4u,  false, "Rgb10A2"},
            {CnbTextureFormat::R32Float,     SurfaceFormat::Single,          4u,  false, "R32Float"},
            {CnbTextureFormat::Rg32Float,    SurfaceFormat::Vector2,         8u,  false, "Rg32Float"},
            {CnbTextureFormat::Rgba32Float,  SurfaceFormat::Vector4,         16u, false, "Rgba32Float"},
            {CnbTextureFormat::R16Float,     SurfaceFormat::HalfSingle,      2u,  false, "R16Float"},
            {CnbTextureFormat::Rg16Float,    SurfaceFormat::HalfVector2,     4u,  false, "Rg16Float"},
            {CnbTextureFormat::Rgba16Float,  SurfaceFormat::HalfVector4,     8u,  false, "Rgba16Float"},
            {CnbTextureFormat::HdrBlendable, SurfaceFormat::HdrBlendable,    8u,  false, "HdrBlendable"},
            {CnbTextureFormat::Bc1,          SurfaceFormat::Dxt1,            8u,  true,  "Bc1"},
            {CnbTextureFormat::Bc2,          SurfaceFormat::Dxt3,            16u, true,  "Bc2"},
            {CnbTextureFormat::Bc3,          SurfaceFormat::Dxt5,            16u, true,  "Bc3"},
            {CnbTextureFormat::Bc3Srgb,      SurfaceFormat::Dxt5SrgbEXT,     16u, true,  "Bc3Srgb"},
            {CnbTextureFormat::Bc7,          SurfaceFormat::Bc7EXT,          16u, true,  "Bc7"},
            {CnbTextureFormat::Bc7Srgb,      SurfaceFormat::Bc7SrgbEXT,      16u, true,  "Bc7Srgb"},
        }};

        // The table is indexed by identifier - 1, so a row landing in the wrong slot would make
        // every lookup wrong in a way no individual test would obviously catch. Checked here.
        constexpr bool TableIsInIdentifierOrder()
        {
            for (std::size_t i = 0; i < kFormats.size(); ++i)
            {
                if (static_cast<std::uint32_t>(kFormats[i].cnb) != i + 1u) { return false; }
            }
            return true;
        }
        static_assert(TableIsInIdentifierOrder(),
                      "kFormats must hold one row per identifier, in identifier order.");

        const FormatRow* FindRow(CnbTextureFormat format)
        {
            const auto value = static_cast<std::uint32_t>(format);
            if (!IsKnownCnbTextureFormat(value)) { return nullptr; }
            return &kFormats[value - 1u];
        }
    }

    std::string CnbTextureFormatToString(CnbTextureFormat format)
    {
        if (const FormatRow* row = FindRow(format)) { return row->name; }
        return "unknown texture format 0x" + [](std::uint32_t v)
        {
            static constexpr char kDigits[] = "0123456789ABCDEF";
            std::string out(8, '0');
            for (int i = 7; i >= 0; --i) { out[static_cast<std::size_t>(i)] = kDigits[v & 0xFu]; v >>= 4; }
            return out;
        }(static_cast<std::uint32_t>(format));
    }

    bool IsBlockCompressedCnbTextureFormat(CnbTextureFormat format)
    {
        const FormatRow* row = FindRow(format);
        return row != nullptr && row->blockCompressed;
    }

    std::uint32_t CnbTextureFormatUnitBytes(CnbTextureFormat format)
    {
        const FormatRow* row = FindRow(format);
        return row != nullptr ? row->unitBytes : 0u;
    }

    std::uint64_t CnbTextureLevelByteSize(CnbTextureFormat format, std::uint32_t width,
                                          std::uint32_t height, std::uint32_t depth)
    {
        const FormatRow* row = FindRow(format);
        if (row == nullptr)
        {
            throw ContentLoadException("CNB texture: " + CnbTextureFormatToString(format) +
                                       " is not a texture format this build understands.");
        }
        if (width == 0u || height == 0u || depth == 0u)
        {
            throw ContentLoadException("CNB texture: a mip level cannot have a zero dimension.");
        }

        std::uint64_t across = width;
        std::uint64_t down = height;
        if (row->blockCompressed)
        {
            // Round up to whole 4x4 blocks. A 1x1 BC7 level is one block, not a fraction of one,
            // which is why this cannot simply divide.
            across = (across + 3u) / 4u;
            down = (down + 3u) / 4u;
        }

        const std::string context = "CNB texture level";
        std::uint64_t total = CheckedMultiply(across, down, context);
        total = CheckedMultiply(total, static_cast<std::uint64_t>(depth), context);
        return CheckedMultiply(total, static_cast<std::uint64_t>(row->unitBytes), context);
    }

    SurfaceFormat CnbTextureFormatToSurfaceFormat(CnbTextureFormat format)
    {
        const FormatRow* row = FindRow(format);
        if (row == nullptr)
        {
            throw ContentLoadException("CNB texture: " + CnbTextureFormatToString(format) +
                                       " is not a texture format this build understands.");
        }
        return row->surface;
    }

    CnbTextureFormat SurfaceFormatToCnbTextureFormat(SurfaceFormat format)
    {
        for (const FormatRow& row : kFormats)
        {
            if (row.surface == format) { return row.cnb; }
        }
        throw ContentLoadException(
            "CNB texture: SurfaceFormat " + std::to_string(static_cast<int>(format)) +
            " has no CNB texture format identifier. A SurfaceFormat enumerator was added without "
            "extending CnbTextureFormat.");
    }
}
