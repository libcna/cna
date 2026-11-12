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
     * SpriteFont's glyph map is keyed by a single UTF-16 code unit (charcs), and XNA's
     * SpriteFont walks a .NET string one UTF-16 code unit at a time (StringProxy.get_Item
     * IL_0008-001c): a character above U+FFFF is its two surrogates, each looked up on its own.
     * Given @p lowSurrogate, such a character decodes to its high surrogate and stores its low one
     * there for the caller to process next; without it, it decodes to '?'. Invalid or truncated
     * sequences decode to '?' and always advance the index by at least one byte so callers can't
     * spin forever on malformed input.
     *
     * @param text  The UTF-8 encoded string being scanned.
     * @param index In/out byte offset into @p text; advanced past the decoded
     *              sequence.
     * @param lowSurrogate Receives the low surrogate of a supplementary character, else unchanged.
     * @return The decoded UTF-16 code unit.
     */
    inline SharpRuntime::charcs DecodeUtf8CodePoint(const std::string& text, std::size_t& index,
                                                    SharpRuntime::charcs* lowSurrogate = nullptr)
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
        if (decoded <= 0xFFFF) return static_cast<SharpRuntime::charcs>(decoded);
        if (decoded > 0x10FFFF || lowSurrogate == nullptr) return u'?';
        const char32_t offset = decoded - 0x10000;
        *lowSurrogate = static_cast<SharpRuntime::charcs>(0xDC00 + (offset & 0x3FF));
        return static_cast<SharpRuntime::charcs>(0xD800 + (offset >> 10));
    }
}
