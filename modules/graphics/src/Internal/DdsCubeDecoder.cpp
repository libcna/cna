// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Graphics/DdsCubeDecoder.hpp"

#include <algorithm>
#include <limits>
#include <string>

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
        // DDSCAPS2_CUBEMAP_POSITIVEX .. _NEGATIVEZ, contiguous from bit 10. This decoder reads six
        // faces unconditionally, so a file that declares only some of them is describing a
        // different image than the one that will be read out of it (plans/plan_cnb.md CNBF-116).
        constexpr std::uint32_t kDdscaps2CubemapAllFaces = 0xFC00;
        // Every caps2 bit this decoder can account for. Anything else -- DDSCAPS2_VOLUME above
        // all -- changes what the payload after the header means.
        constexpr std::uint32_t kDdscaps2Known = kDdscaps2Cubemap | kDdscaps2CubemapAllFaces;

        /// Largest face dimension this decoder will accept, in texels.
        ///
        /// Not an arbitrary round number. `DecodedDdsCube::width` is an `int` and `DxtUtil`
        /// computes a level's RGBA output length as `width * height * 4` in `int`, so the
        /// representable ceiling for a square face is `sqrt(INT32_MAX / 4)` = 23170. 16384 is the
        /// largest power of two under that, and is also the maximum 2D texture dimension every
        /// GPU API CNA targets exposes -- so nothing real is refused, and every product below
        /// provably fits. It also bounds the mip chain at 15 levels, which is inside CNB's own
        /// `CnbMaxTextureMipLevels` of 16, so a decoded cube always has an expressible mip count.
        constexpr std::uint32_t kMaxFaceDimension = 16384u;

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
        //
        // Computed in std::uint64_t rather than int (plans/plan_cnb.md CNBF-116). The block-count
        // rounding `(w + 3) / 4` overflows a signed int for a width near INT32_MAX -- which is
        // undefined behaviour, not a large answer -- and the product with the block size overflows
        // long before that, producing a SMALL value that then passes the "is this many bytes still
        // in the file?" check. Widening removes the whole class; the dimension ceiling above
        // guarantees the result also fits a std::size_t.
        std::uint64_t CalculateDDSLevelSize(std::uint64_t width, std::uint64_t height,
                                            std::uint32_t fourCC)
        {
            const std::uint64_t blockSize = (fourCC == kFourCcDxt1) ? 8u : 16u;
            width  = std::max<std::uint64_t>(width, 1u);
            height = std::max<std::uint64_t>(height, 1u);
            return ((width + 3u) / 4u) * ((height + 3u) / 4u) * blockSize;
        }

        /// Refuses a header whose decoded size cannot even be counted (plans/plan_cnb.md
        /// `CNBF-122`). Unreachable while the dimension ceiling stands; present so that raising
        /// that ceiling produces a refusal rather than a wrapped total that passes the budget.
        [[noreturn]] void Overflow(const std::string& where)
        {
            throw System::FormatException(
                where + "cube map header describes a decoded size too large to count");
        }

        /// Number of mip levels a square face of @p width texels can physically have: each level
        /// halves and the chain ends at 1x1, so a 256-texel face has exactly nine.
        std::uint32_t PhysicalMipChainLength(std::uint32_t width)
        {
            std::uint32_t levels = 1u;
            while (width > 1u) { width /= 2u; ++levels; }
            return levels;
        }
    }

    DecodedDdsCube DecodeDdsCube(const std::uint8_t* data, std::size_t size,
                                  const std::string& diagnosticPrefix,
                                  std::uint64_t maxDecodedBytes)
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

        // Read as the unsigned fields the format defines, and narrowed only after every bound
        // below has been applied (plans/plan_cnb.md CNBF-116). Casting straight to int made
        // 0xFFFFFFFF read as -1 and 0x7FFFFFFF read as two billion, and the arithmetic that
        // followed was signed.
        const std::uint32_t height = ReadU32LE(data + 12);
        const std::uint32_t width  = ReadU32LE(data + 16);
        std::uint32_t levels       = ReadU32LE(data + 28);

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
        // A bit outside the cube set -- DDSCAPS2_VOLUME being the one that matters -- describes a
        // payload this decoder would misread as six square faces (CNBF-116).
        if ((caps2 & ~kDdscaps2Known) != 0)
        {
            throw System::NotSupportedException(where + "invalid DDS caps2");
        }
        // Six faces are read unconditionally, so all six must be declared present. A file marked
        // as a cube map that only carries, say, +X and -X has four faces' worth of bytes this
        // decoder would take from whatever follows.
        if ((caps2 & kDdscaps2CubemapAllFaces) != kDdscaps2CubemapAllFaces)
        {
            throw System::FormatException(
                where + "cube map does not declare all six faces; CNA decodes complete cube maps "
                        "only");
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
        // A zero dimension would make every level size zero and silently produce six empty faces.
        // The header check above only guarantees the fields are PRESENT.
        if (width == 0u)
        {
            throw System::FormatException(where + "cube map faces must have a positive size");
        }
        // The upper bound is the representation's, not a preference (CNBF-116): above it a level's
        // RGBA length no longer fits the `int` DecodedDdsCube and DxtUtil compute it in, so the
        // dimension has to be refused rather than decoded into a wrapped-around allocation.
        if (width > kMaxFaceDimension)
        {
            throw System::FormatException(
                where + "cube map face is " + std::to_string(width) +
                " texels; the largest this decoder can represent is " +
                std::to_string(kMaxFaceDimension));
        }
        // Validated BEFORE the reserve below, which is the whole point: a declared count of two
        // billion levels was previously reserved for before a single byte of the file had been
        // consulted. A chain longer than the dimensions physically allow is a malformed header,
        // refused rather than quietly truncated -- repairing it would produce a plausible wrong
        // answer, which is worse than a refusal.
        const std::uint32_t maxLevels = PhysicalMipChainLength(width);
        if (levels > maxLevels)
        {
            throw System::FormatException(
                where + "declares " + std::to_string(levels) + " mip levels, but a " +
                std::to_string(width) + "-texel face has at most " + std::to_string(maxLevels));
        }

        // plans/plan_cnb.md CNBF-122: the aggregate DECODED size, computed from the validated
        // header alone before a single output vector is allocated and before any block is
        // decompressed. The dimension ceiling bounds ONE level of ONE face; six faces times a
        // whole mip chain is 8 times that at the same dimension, which at 16384 texels is about
        // 8.6 GiB of retained memory -- an OOM kill rather than an exception on most machines.
        //
        // Every term is bounded above by the ceilings checked above -- width <= 16384 so
        // width*width*4*6 < 2^33, and levels <= 15 -- so the sum provably fits a std::uint64_t
        // today. Each step is nevertheless CHECKED rather than argued, because the invariant that
        // makes it safe lives fifty lines up in a constant someone may raise, and unsigned wrap is
        // well defined: it would hand this budget a SMALL number from two huge ones and let the
        // very allocation it exists to refuse through.
        std::uint64_t decodedBytes = 0u;
        {
            std::uint64_t levelExtent = width;
            for (std::uint32_t level = 0; level < levels; ++level)
            {
                const std::uint64_t texels = levelExtent * levelExtent;
                if (levelExtent != 0u && texels / levelExtent != levelExtent) { Overflow(where); }
                if (texels > std::numeric_limits<std::uint64_t>::max() / 24u) { Overflow(where); }
                const std::uint64_t levelBytes = texels * 24u; // 4 bytes RGBA x 6 faces
                if (decodedBytes > std::numeric_limits<std::uint64_t>::max() - levelBytes)
                {
                    Overflow(where);
                }
                decodedBytes += levelBytes;
                levelExtent = std::max<std::uint64_t>(1u, levelExtent / 2u);
            }
        }
        if (decodedBytes > maxDecodedBytes)
        {
            throw System::FormatException(
                where + "cube map decodes to " + std::to_string(decodedBytes) +
                " bytes of RGBA across six faces and " + std::to_string(levels) +
                " mip level(s), above the " + std::to_string(maxDecodedBytes) +
                "-byte decoded-output budget. Refused before allocating anything.");
        }

        DecodedDdsCube decoded;
        decoded.width = static_cast<int>(width);
        decoded.mipCount = static_cast<int>(levels);

        std::size_t offset = 128u;
        for (int face = 0; face < 6; ++face)
        {
            decoded.faces[static_cast<std::size_t>(face)].reserve(
                static_cast<std::size_t>(levels));
            std::uint32_t levelSize = width;
            for (std::uint32_t level = 0; level < levels; ++level)
            {
                const std::uint64_t blockBytes =
                    CalculateDDSLevelSize(levelSize, levelSize, formatFourCC);
                // Checked against the remaining bytes rather than by adding to `offset` first,
                // so a hostile size cannot wrap the addition past the end and look in range.
                if (offset > size || blockBytes > static_cast<std::uint64_t>(size - offset))
                {
                    throw System::FormatException(where + "truncated DDS stream");
                }

                std::vector<std::uint8_t> rgba;
                const auto* block = data + offset;
                const auto blockLength = static_cast<std::size_t>(blockBytes);
                const int levelExtent = static_cast<int>(levelSize);
                if (formatFourCC == kFourCcDxt1)
                {
                    rgba = DxtUtil::DecompressDxt1(block, blockLength, levelExtent, levelExtent);
                }
                else if (formatFourCC == kFourCcDxt3)
                {
                    rgba = DxtUtil::DecompressDxt3(block, blockLength, levelExtent, levelExtent);
                }
                else
                {
                    rgba = DxtUtil::DecompressDxt5(block, blockLength, levelExtent, levelExtent);
                }

                decoded.faces[static_cast<std::size_t>(face)].push_back(std::move(rgba));
                offset += blockLength;
                levelSize = std::max<std::uint32_t>(1u, levelSize / 2u);
            }
        }
        return decoded;
    }
}
