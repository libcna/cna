// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbChunkCompression.hpp"

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

#if defined(CNA_CNB_HAVE_ZSTD)
#include <zstd.h>
#endif

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    namespace
    {
        [[noreturn]] void Unsupported(CnbCompression codec, const std::string& where)
        {
            throw ContentLoadException(
                where + " uses compression codec " + CnbCompressionToString(codec) +
                ", which this build does not implement. A compressed chunk needs a CNA built with "
                "that codec; see docs/cnb-format.md §8.");
        }
    }

    bool IsCnbCompressionSupported(CnbCompression codec)
    {
        switch (codec)
        {
            case CnbCompression::None: return true;
#if defined(CNA_CNB_HAVE_ZSTD)
            case CnbCompression::ReservedZstd: return true;
#endif
            default: return false;
        }
    }

    std::string CnbCompressionToString(CnbCompression codec)
    {
        switch (codec)
        {
            case CnbCompression::None: return "none";
            case CnbCompression::ReservedLz4: return "LZ4";
            case CnbCompression::ReservedZstd: return "Zstandard";
            case CnbCompression::ReservedDeflate: return "Deflate";
            default:
                return "unknown codec " + std::to_string(static_cast<std::uint32_t>(codec));
        }
    }

    std::vector<std::uint8_t> CompressCnbChunk(std::span<const std::uint8_t> raw,
                                                CnbCompression codec, int level)
    {
        if (codec == CnbCompression::None)
        {
            return std::vector<std::uint8_t>(raw.begin(), raw.end());
        }
#if defined(CNA_CNB_HAVE_ZSTD)
        if (codec == CnbCompression::ReservedZstd)
        {
            if (level < 1) { level = 1; }
            if (level > ZSTD_maxCLevel()) { level = ZSTD_maxCLevel(); }
            std::vector<std::uint8_t> out(ZSTD_compressBound(raw.size()));
            const std::size_t produced =
                ZSTD_compress(out.data(), out.size(), raw.data(), raw.size(), level);
            if (ZSTD_isError(produced))
            {
                throw ContentLoadException(std::string("CNB: Zstandard compression failed: ") +
                                           ZSTD_getErrorName(produced));
            }
            out.resize(produced);
            return out;
        }
#endif
        Unsupported(codec, "CNB chunk compression");
    }

    std::vector<std::uint8_t> DecompressCnbChunk(std::span<const std::uint8_t> stored,
                                                  CnbCompression codec,
                                                  std::uint64_t uncompressedSize,
                                                  std::uint64_t maxUncompressedSize,
                                                  const std::string& where)
    {
        if (codec == CnbCompression::None)
        {
            return std::vector<std::uint8_t>(stored.begin(), stored.end());
        }

        // The ceiling is checked BEFORE the allocation, not after the decode. uncompressedSize is
        // a number out of the file, so a few kilobytes of hostile input could otherwise ask for
        // gigabytes and get them.
        if (uncompressedSize > maxUncompressedSize)
        {
            throw ContentLoadException(
                where + " declares an unpacked size of " + std::to_string(uncompressedSize) +
                " bytes, above the configured limit of " + std::to_string(maxUncompressedSize) +
                ". Refused before allocating anything.");
        }

#if defined(CNA_CNB_HAVE_ZSTD)
        if (codec == CnbCompression::ReservedZstd)
        {
            std::vector<std::uint8_t> out(static_cast<std::size_t>(uncompressedSize));
            const std::size_t produced =
                ZSTD_decompress(out.data(), out.size(), stored.data(), stored.size());
            if (ZSTD_isError(produced))
            {
                throw ContentLoadException(where + " could not be decompressed: " +
                                           ZSTD_getErrorName(produced));
            }
            // Exactly, not at most. A stream that expands to a different length than the entry
            // declares is a corrupt file, and accepting it would leave the tail of the buffer as
            // zeroes that later code would read as data.
            if (produced != uncompressedSize)
            {
                throw ContentLoadException(
                    where + " expands to " + std::to_string(produced) +
                    " bytes, but its table-of-contents entry declares " +
                    std::to_string(uncompressedSize) + ".");
            }
            return out;
        }
#endif
        Unsupported(codec, where);
    }
}
