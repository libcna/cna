// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <string>

#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Internal
{
    /**
     * @brief Decodes one UTF-8 code point from a byte string starting at the given
     *        index, advancing the index past the bytes consumed.
     *
     * SpriteFont's glyph map is keyed by a single UTF-16 code unit (charcs), so
     * this only decodes code points within the Basic Multilingual Plane -- every
     * glyph a SpriteFont atlas can hold fits in one. Invalid or truncated
     * sequences, and code points outside the BMP, decode to '?' and always
     * advance the index by at least one byte so callers can't spin forever on
     * malformed input.
     *
     * @param text  The UTF-8 encoded string being scanned.
     * @param index In/out byte offset into @p text; advanced past the decoded
     *              sequence.
     * @return The decoded UTF-16 code unit.
     */
    inline SharpRuntime::charcs DecodeUtf8CodePoint(const std::string& text, std::size_t& index)
    {
        const auto lead = static_cast<unsigned char>(text[index]);

        if (lead < 0x80)
        {
            ++index;
            return static_cast<SharpRuntime::charcs>(lead);
        }

        int extraBytes;
        char32_t codepoint;
        if ((lead & 0xE0) == 0xC0)      { extraBytes = 1; codepoint = lead & 0x1Fu; }
        else if ((lead & 0xF0) == 0xE0) { extraBytes = 2; codepoint = lead & 0x0Fu; }
        else if ((lead & 0xF8) == 0xF0) { extraBytes = 3; codepoint = lead & 0x07u; }
        else
        {
            // Not a valid UTF-8 lead byte (e.g. a stray continuation byte).
            ++index;
            return u'?';
        }

        if (index + static_cast<std::size_t>(extraBytes) >= text.size())
        {
            // Truncated sequence at the end of the string.
            ++index;
            return u'?';
        }

        char32_t decoded = codepoint;
        for (int k = 1; k <= extraBytes; ++k)
        {
            const auto cont = static_cast<unsigned char>(text[index + static_cast<std::size_t>(k)]);
            if ((cont & 0xC0) != 0x80)
            {
                ++index;
                return u'?';
            }
            decoded = (decoded << 6) | (cont & 0x3Fu);
        }

        index += static_cast<std::size_t>(extraBytes) + 1;
        return decoded <= 0xFFFF ? static_cast<SharpRuntime::charcs>(decoded) : u'?';
    }
}
