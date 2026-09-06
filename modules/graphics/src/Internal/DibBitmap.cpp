// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Graphics/DibBitmap.hpp"

#include <cstddef>
#include <stdexcept>

namespace CNA::Internal::Graphics
{
    namespace
    {
        /** @brief The little-endian dword at @p at, or zero when the bytes end before it. */
        [[nodiscard]] std::uint32_t Word32(std::span<const std::uint8_t> bytes,
                                           const std::size_t at) noexcept
        {
            if (at + 4u > bytes.size()) { return 0u; }
            return static_cast<std::uint32_t>(bytes[at]) |
                   (static_cast<std::uint32_t>(bytes[at + 1u]) << 8) |
                   (static_cast<std::uint32_t>(bytes[at + 2u]) << 16) |
                   (static_cast<std::uint32_t>(bytes[at + 3u]) << 24);
        }

        /** @brief The little-endian word at @p at, or zero when the bytes end before it. */
        [[nodiscard]] std::uint16_t Word16(std::span<const std::uint8_t> bytes,
                                           const std::size_t at) noexcept
        {
            if (at + 2u > bytes.size()) { return 0u; }
            return static_cast<std::uint16_t>(static_cast<std::uint32_t>(bytes[at]) |
                                              (static_cast<std::uint32_t>(bytes[at + 1u]) << 8));
        }

        /**
         * @brief Bytes between the DIB header and the first pixel.
         *
         * Two things can sit there. A bitmap of eight bits or fewer carries a colour table, whose
         * length is `biClrUsed` entries when that field is set and the full `2^biBitCount` when it
         * is not; and a v3 header that declares `BI_BITFIELDS` is followed by the three channel
         * masks (four for `BI_ALPHABITFIELDS`), which the v4 and v5 headers instead carry inside
         * themselves. Getting this wrong does not corrupt a 32-bit bitmap -- it has neither -- but
         * it points an 8-bit one at its palette and decodes noise.
         */
        [[nodiscard]] std::uint32_t ExtraBeforePixels(std::span<const std::uint8_t> body) noexcept
        {
            const std::uint32_t headerSize = Word32(body, 0u);
            const std::uint16_t bitCount = Word16(body, 14u);
            const std::uint32_t compression = Word32(body, 16u);
            const std::uint32_t declared = Word32(body, 32u);
            std::uint64_t extra = 0u;
            if (bitCount != 0u && bitCount <= 8u)
            {
                const std::uint32_t entries =
                    declared != 0u ? declared : (1u << bitCount);
                extra += static_cast<std::uint64_t>(entries) * 4u;
            }
            if (headerSize == 40u && (compression == 3u || compression == 6u))
            {
                extra += compression == 3u ? 12u : 16u;
            }
            return extra > 0xFFFFFFFFull ? 0u : static_cast<std::uint32_t>(extra);
        }
    }

    bool IsDeviceIndependentBitmap(std::span<const std::uint8_t> bytes) noexcept
    {
        if (bytes.size() < 40u || (bytes[0] == 'B' && bytes[1] == 'M'))
        {
            return false;
        }
        // 40 for BITMAPINFOHEADER, 108 and 124 for the two versions that follow it. The older
        // BITMAPCOREHEADER (12) is not accepted: its body is not what a `.dib` written by anything
        // this decade contains, and twelve bytes of leading size are far likelier to be some other
        // format's first field than a real header.
        const std::uint32_t headerSize = Word32(bytes, 0u);
        return headerSize == 40u || headerSize == 108u || headerSize == 124u;
    }

    std::vector<std::uint8_t> WithBitmapFileHeader(std::span<const std::uint8_t> body)
    {
        if (!IsDeviceIndependentBitmap(body))
        {
            throw std::invalid_argument("WithBitmapFileHeader: the bytes are not a DIB body.");
        }
        std::vector<std::uint8_t> bytes;
        bytes.reserve(body.size() + 14u);
        const auto word32 = [&bytes](std::uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8)
            {
                bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
            }
        };
        bytes.push_back('B');
        bytes.push_back('M');
        word32(static_cast<std::uint32_t>(body.size() + 14u));
        word32(0u);
        word32(14u + Word32(body, 0u) + ExtraBeforePixels(body));
        bytes.insert(bytes.end(), body.begin(), body.end());
        return bytes;
    }
}
