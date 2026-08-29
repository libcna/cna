// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "System/IO/BinaryReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief Compression scheme signaled by a `.xnb` header's flags byte (plans/plan_xnb.md XNB-27).
     *
     * Both bit values are real, confirmed against MonoGame's own `ContentManager.cs`
     * (`ContentCompressedLzx = 0x80`, `ContentCompressedLz4 = 0x40`) -- FNA itself only ever
     * produces/reads `Lzx`/`None`, while MonoGame's content pipeline can emit raw-block `Lz4`.
     * Deliberately a real enum rather than a bool so the rest of the architecture
     * (`ContentManager::LoadXnbAsset<T>()`, `ScanXnbReaderNames()`) branches on the actual scheme
     * instead of assuming `compressed == true` always means LZX.
     */
    enum class XnbCompression
    {
        /** @brief Neither compression bit is set; the payload follows the header as-is. */
        None,
        /** @brief Flags bit `0x80` -- FNA's own decoder, implemented (plans/plan_xnb.md XNB-28/29). */
        Lzx,
        /** @brief Flags bit `0x40` -- MonoGame's single raw-LZ4-block XNB representation. */
        Lz4,
        /** @brief Both compression bits set simultaneously -- not a real scheme either format defines. */
        Unknown,
    };

    /**
     * @brief Parsed fields of a real `.xnb` binary container header (plans/plan_xnb.md XNB-11).
     *
     * Layout, confirmed against FNA's `ContentManager.GetContentReaderFromXnb`
     * (`src/Content/ContentManager.cs`): 3 magic bytes `'X'`, `'N'`, `'B'`; 1 platform
     * identifier byte; 1 version byte (4 or 5); 1 flags byte (compression bits, see
     * @ref XnbCompression); a 4-byte little-endian total file length. 10 bytes total.
     */
    struct XnbHeader
    {
        /** @brief The platform identifier byte (4th magic byte), e.g. `'w'` for Windows. */
        char platform = '\0';

        /** @brief The container version, always 4 or 5 -- ParseXnbHeader() rejects any other value. */
        int version = 0;

        /** @brief Compression scheme signaled by the flags byte -- see @ref XnbCompression. */
        XnbCompression compression = XnbCompression::None;

        /** @brief Total length of the `.xnb` file in bytes, as recorded in the header itself. */
        int32_t totalLength = 0;
    };

    /**
     * @brief The platform identifier bytes FNA accepts, verbatim from
     *        `ContentManager.targetPlatformIdentifiers` -- includes several deprecated
     *        identifiers (`'g'`/`'l'`) FNA itself still accepts for backward compatibility.
     */
    inline const std::array<char, 16>& XnbAcceptedPlatforms()
    {
        static const std::array<char, 16> kPlatforms{
            'w', 'x', 'm', 'i', 'a', 'd', 'X', 'W',
            'n', 'u', 'p', 'M', 'r', 'P', 'g', 'l'
        };
        return kPlatforms;
    }

    /**
     * @brief Parses and validates a `.xnb` container header from @p reader, advancing it past
     *        the header's 10 bytes.
     *
     * Unlike CnjEnvelope's parse/validate split (which exists to give JSON parsing rich
     * per-field diagnostics), a binary header has no useful partial-failure state to preserve,
     * so this function validates directly and throws on any failure -- matching FNA's own
     * `GetContentReaderFromXnb`, which likewise throws immediately on a bad version rather than
     * returning a partially-populated result. One deliberate deviation from FNA: FNA reads the
     * 4 magic/platform bytes via a single raw `Stream.Read` call and only then compares them
     * (so a short read past EOF just compares against leftover/garbage bytes and fails the
     * magic check); this reads each byte through @p reader, so a truncated file throws
     * `System::IO::EndOfStreamException` instead -- a clearer, fail-fast diagnostic for the
     * same truncated-input case, not a behavior a well-formed file could ever observe.
     *
     * @param reader Binary reader positioned at the start of the `.xnb` file.
     * @param path   File path, used only to build exception messages.
     * @return The parsed header.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the magic bytes
     *         aren't `"XNB"`, the platform byte isn't one of XnbAcceptedPlatforms(), or the
     *         version byte is neither 4 nor 5.
     * @throws System::IO::EndOfStreamException if @p reader ends before the 10-byte header is
     *         fully read.
     */
    inline XnbHeader ParseXnbHeader(System::IO::BinaryReader& reader, const std::string& path)
    {
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        const auto b0 = reader.ReadByte();
        const auto b1 = reader.ReadByte();
        const auto b2 = reader.ReadByte();
        const auto platformByte = reader.ReadByte();

        if (b0 != 'X' || b1 != 'N' || b2 != 'B')
        {
            throw ContentLoadException(
                "'" + path + "' is not a valid XNB file (bad magic bytes).");
        }

        const char platform = static_cast<char>(platformByte);
        const auto& accepted = XnbAcceptedPlatforms();
        bool platformOk = false;
        for (const char p : accepted)
        {
            if (p == platform) { platformOk = true; break; }
        }
        if (!platformOk)
        {
            throw ContentLoadException(
                "'" + path + "' has an unrecognized XNB platform identifier '" +
                std::string(1, platform) + "'.");
        }

        XnbHeader header;
        header.platform = platform;
        header.version = reader.ReadByte();
        if (header.version != 4 && header.version != 5)
        {
            throw ContentLoadException(
                "'" + path + "' has an invalid XNB version (" +
                std::to_string(header.version) + "); only 4 and 5 are supported.");
        }

        const auto flags = reader.ReadByte();
        const bool lzxBit = (flags & 0x80u) != 0;
        const bool lz4Bit = (flags & 0x40u) != 0;
        if (lzxBit && lz4Bit)
        {
            header.compression = XnbCompression::Unknown;
        }
        else if (lzxBit)
        {
            header.compression = XnbCompression::Lzx;
        }
        else if (lz4Bit)
        {
            header.compression = XnbCompression::Lz4;
        }
        else
        {
            header.compression = XnbCompression::None;
        }
        header.totalLength = reader.ReadInt32();

        return header;
    }
}
