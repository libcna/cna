// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-80: a raw LZ4 block encoder.
//
// Written from the published LZ4 block format, which is a short and completely specified
// grammar: a sequence is a token byte whose high nibble is a literal length and whose low nibble
// is a match length minus four, followed by any length-extension bytes, the literals themselves,
// a two-byte little-endian match offset, and any match-length extension bytes. The final
// sequence carries literals and stops.
//
// Two rules in that format exist for the decoder's benefit rather than the encoder's, and both
// are honoured here because a block that breaks them is not decodable by a conforming
// implementation: the last five bytes of the payload are always literals, and a match never
// begins within the last twelve bytes.

#include "CNA/Internal/Xnb/XnbCompressionWriter.hpp"

#include <algorithm>

namespace CNA::Internal::Xnb
{
    namespace
    {
        constexpr std::size_t kMinMatch = 4u;
        constexpr std::size_t kLastLiterals = 5u;
        constexpr std::size_t kMatchFindMargin = 12u;
        constexpr std::size_t kMaxOffset = 65535u;
        constexpr unsigned kHashBits = 16u;

        [[nodiscard]] std::uint32_t Read32(const std::span<const std::uint8_t> payload,
                                           const std::size_t position)
        {
            return static_cast<std::uint32_t>(payload[position]) |
                   (static_cast<std::uint32_t>(payload[position + 1u]) << 8) |
                   (static_cast<std::uint32_t>(payload[position + 2u]) << 16) |
                   (static_cast<std::uint32_t>(payload[position + 3u]) << 24);
        }

        [[nodiscard]] std::size_t Hash(const std::uint32_t sequence)
        {
            // Knuth's multiplicative constant. Any dispersing function works; this one is fixed
            // so the encoder's output is reproducible.
            return static_cast<std::size_t>((sequence * 2654435761u) >> (32u - kHashBits));
        }

        /** @brief Appends a length above the nibble's capacity as 255-terminated extension bytes. */
        void AppendLengthExtension(std::vector<std::uint8_t>& out, std::size_t length)
        {
            if (length < 15u) { return; }
            std::size_t remaining = length - 15u;
            while (remaining >= 255u)
            {
                out.push_back(255u);
                remaining -= 255u;
            }
            out.push_back(static_cast<std::uint8_t>(remaining));
        }

        void AppendSequence(std::vector<std::uint8_t>& out,
                            const std::span<const std::uint8_t> payload,
                            const std::size_t literalStart, const std::size_t literalLength,
                            const std::size_t offset, const std::size_t matchCode)
        {
            out.push_back(static_cast<std::uint8_t>(
                (std::min<std::size_t>(literalLength, 15u) << 4) |
                std::min<std::size_t>(matchCode, 15u)));
            AppendLengthExtension(out, literalLength);
            out.insert(out.end(), payload.begin() + static_cast<std::ptrdiff_t>(literalStart),
                       payload.begin() +
                           static_cast<std::ptrdiff_t>(literalStart + literalLength));
            out.push_back(static_cast<std::uint8_t>(offset & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((offset >> 8) & 0xFFu));
            AppendLengthExtension(out, matchCode);
        }

        void AppendFinalLiterals(std::vector<std::uint8_t>& out,
                                 const std::span<const std::uint8_t> payload,
                                 const std::size_t literalStart)
        {
            const std::size_t literalLength = payload.size() - literalStart;
            out.push_back(
                static_cast<std::uint8_t>(std::min<std::size_t>(literalLength, 15u) << 4));
            AppendLengthExtension(out, literalLength);
            out.insert(out.end(), payload.begin() + static_cast<std::ptrdiff_t>(literalStart),
                       payload.end());
        }
    }

    std::vector<std::uint8_t> CompressXnbLz4Block(const std::span<const std::uint8_t> payload)
    {
        std::vector<std::uint8_t> out;
        out.reserve(payload.size() + payload.size() / 255u + 16u);

        // A payload this short cannot contain a legal match at all: every byte is a literal.
        if (payload.size() <= kMatchFindMargin)
        {
            AppendFinalLiterals(out, payload, 0u);
            return out;
        }

        std::vector<std::int64_t> table(static_cast<std::size_t>(1) << kHashBits, -1);
        const std::size_t searchLimit = payload.size() - kMatchFindMargin;
        const std::size_t matchLimit = payload.size() - kLastLiterals;

        std::size_t anchor = 0u;
        std::size_t position = 0u;
        while (position < searchLimit)
        {
            const std::uint32_t sequence = Read32(payload, position);
            const std::size_t slot = Hash(sequence);
            const std::int64_t candidate = table[slot];
            table[slot] = static_cast<std::int64_t>(position);

            if (candidate < 0 ||
                position - static_cast<std::size_t>(candidate) > kMaxOffset ||
                Read32(payload, static_cast<std::size_t>(candidate)) != sequence)
            {
                ++position;
                continue;
            }

            const std::size_t matchStart = static_cast<std::size_t>(candidate);
            std::size_t matchLength = kMinMatch;
            while (position + matchLength < matchLimit &&
                   payload[matchStart + matchLength] == payload[position + matchLength])
            {
                ++matchLength;
            }

            AppendSequence(out, payload, anchor, position - anchor, position - matchStart,
                           matchLength - kMinMatch);
            position += matchLength;
            anchor = position;
        }

        AppendFinalLiterals(out, payload, anchor);
        return out;
    }
}
