// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Graphics/DdsCubeDecoder.hpp"

#include <algorithm>

#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "System/FormatException.hpp"
#include "System/NotSupportedException.hpp"

namespace CNA::Internal::Graphics
{
    namespace
    {
        // FNA's Texture.ParseDDS constants. Moved here verbatim from TextureCube.cpp along with
        // the parsing that uses them (plans/plan_cnb.md CNBF-113); not one value was changed, because
        // the point of the move was to share the existing decoder, not to write a new one.
        constexpr std::uint32_t kDdsMagic        = 0x20534444;
        constexpr std::uint32_t kDdsHeaderSize   = 124;
        constexpr std::uint32_t kDdsPixfmtSize   = 32;
        constexpr std::uint32_t kDdsdHeight      = 0x2;
        constexpr std::uint32_t kDdsdWidth       = 0x4;
        constexpr std::uint32_t kDdscapsMipmap   = 0x400000;
        constexpr std::uint32_t kDdscapsTexture  = 0x1000;
        constexpr std::uint32_t kDdscaps2Cubemap = 0x200;
        constexpr std::uint32_t kDdpfFourCC      = 0x4;
        constexpr std::uint32_t kFourCcDxt1      = 0x31545844;
        constexpr std::uint32_t kFourCcDxt3      = 0x33545844;
        constexpr std::uint32_t kFourCcDxt5      = 0x35545844;

        std::uint32_t ReadU32LE(const std::uint8_t* p)
        {
            return static_cast<std::uint32_t>(p[0])
                 | (static_cast<std::uint32_t>(p[1]) << 8)
                 | (static_cast<std::uint32_t>(p[2]) << 16)
                 | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        // Compressed block size in bytes for one mip level -- mirrors FNA's
        // Texture.CalculateDDSLevelSize (Dxt1/3/5-only subset; CNA doesn't support the
        // uncompressed/HDR DDS variants FNA also handles, matching Texture2D::FromStream's own
        // established DXT1/3/5-only scope for this exact class of problem).
        int CalculateDDSLevelSize(int width, int height, std::uint32_t fourCC)
        {
            const int blockSize = (fourCC == kFourCcDxt1) ? 8 : 16;
            width  = std::max(width, 1);
            height = std::max(height, 1);
            return ((width + 3) / 4) * ((height + 3) / 4) * blockSize;
        }
    }

    DecodedDdsCube DecodeDdsCube(const std::uint8_t* data, std::size_t size,
                                  const std::string& diagnosticPrefix)
    {
        const std::string where = diagnosticPrefix + ": ";

        if (data == nullptr || size < 128u || ReadU32LE(data) != kDdsMagic)
        {
            throw System::NotSupportedException(where + "not a DDS stream");
        }
        if (ReadU32LE(data + 4) != kDdsHeaderSize)
        {
            throw System::NotSupportedException(where + "invalid DDS header");
        }

        const std::uint32_t flags = ReadU32LE(data + 8);
        if ((flags & (kDdsdHeight | kDdsdWidth)) != (kDdsdHeight | kDdsdWidth))
        {
            throw System::NotSupportedException(where + "invalid DDS flags");
        }

        const int height = static_cast<int>(ReadU32LE(data + 12));
        const int width  = static_cast<int>(ReadU32LE(data + 16));
        int levels        = static_cast<int>(ReadU32LE(data + 28));

        const std::uint32_t formatSize = ReadU32LE(data + 76);
        if (formatSize != kDdsPixfmtSize)
        {
            throw System::NotSupportedException(where + "bogus DDS pixel format size");
        }
        const std::uint32_t formatFlags  = ReadU32LE(data + 80);
        const std::uint32_t formatFourCC = ReadU32LE(data + 84);

        const std::uint32_t caps = ReadU32LE(data + 108);
        if ((caps & kDdscapsTexture) == 0)
        {
            throw System::NotSupportedException(where + "not a texture");
        }
        const std::uint32_t caps2 = ReadU32LE(data + 112);
        const bool isCube = (caps2 & kDdscaps2Cubemap) == kDdscaps2Cubemap;
        if (caps2 != 0 && !isCube)
        {
            throw System::NotSupportedException(where + "invalid DDS caps2");
        }
        if (!isCube)
        {
            throw System::FormatException("This file does not contain cube data!");
        }

        if ((caps & kDdscapsMipmap) != kDdscapsMipmap) { levels = 1; }
        if (levels < 1) { levels = 1; }

        if ((formatFlags & kDdpfFourCC) == 0 ||
            (formatFourCC != kFourCcDxt1 && formatFourCC != kFourCcDxt3 &&
             formatFourCC != kFourCcDxt5))
        {
            throw System::NotSupportedException(
                where + "unsupported DDS pixel format "
                        "(only DXT1/DXT3/DXT5-compressed cube maps are supported)");
        }
        if (width != height)
        {
            throw System::FormatException(where + "cube map faces must be square");
        }
        // A zero or negative dimension would make every level size zero and silently produce six
        // empty faces. The header check above only guarantees the fields are PRESENT.
        if (width <= 0)
        {
            throw System::FormatException(where + "cube map faces must have a positive size");
        }

        DecodedDdsCube decoded;
        decoded.width = width;
        decoded.mipCount = levels;

        std::size_t offset = 128u;
        for (int face = 0; face < 6; ++face)
        {
            decoded.faces[static_cast<std::size_t>(face)].reserve(
                static_cast<std::size_t>(levels));
            int levelSize = width;
            for (int level = 0; level < levels; ++level)
            {
                const int blockBytes = CalculateDDSLevelSize(levelSize, levelSize, formatFourCC);
                // Checked against the remaining bytes rather than by adding to `offset` first,
                // so a hostile size cannot wrap the addition past the end and look in range.
                if (blockBytes < 0 || offset > size ||
                    static_cast<std::size_t>(blockBytes) > size - offset)
                {
                    throw System::FormatException(where + "truncated DDS stream");
                }

                std::vector<std::uint8_t> rgba;
                const auto* block = data + offset;
                const auto blockLength = static_cast<std::size_t>(blockBytes);
                if (formatFourCC == kFourCcDxt1)
                {
                    rgba = DxtUtil::DecompressDxt1(block, blockLength, levelSize, levelSize);
                }
                else if (formatFourCC == kFourCcDxt3)
                {
                    rgba = DxtUtil::DecompressDxt3(block, blockLength, levelSize, levelSize);
                }
                else
                {
                    rgba = DxtUtil::DecompressDxt5(block, blockLength, levelSize, levelSize);
                }

                decoded.faces[static_cast<std::size_t>(face)].push_back(std::move(rgba));
                offset += blockLength;
                levelSize = std::max(1, levelSize / 2);
            }
        }
        return decoded;
    }
}
