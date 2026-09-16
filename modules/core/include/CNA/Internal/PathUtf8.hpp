// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

/**
 * @file PathUtf8.hpp
 * @brief The one conversion between a native filesystem path and CNA's UTF-8 path text.
 *
 * CNA's filesystem path model, in three rules:
 *
 * 1. `std::filesystem::path` is the representation for anything that touches the filesystem, and
 *    it is carried **native** all the way to the call. On Windows that is UTF-16; on POSIX it is
 *    the raw byte sequence the kernel stores. Nothing is narrowed in order to open, stat, create,
 *    enumerate or delete.
 * 2. A narrow `std::string` holding a path in CNA means **UTF-8** — in a public API, a log line, a
 *    manifest, a serialized reference, or a map key. It is converted back into a
 *    `std::filesystem::path` before it touches the filesystem again.
 * 3. `path::string()` and `path::generic_string()` are **not** UTF-8 on Windows. They convert
 *    through the process ANSI code page and throw when a character has no mapping there, so they
 *    are never the right way to obtain path text. Use the functions in this header instead.
 *
 * The measurements these rules come from are in `spikes/windows-unicode-path-spike/`. Two of them
 * are worth restating, because they are the reasons this is not a search-and-replace:
 *
 * - `generic_string()` fails on exactly the same inputs as `string()`. Separator style and text
 *   encoding are different problems, and normalising separators fixes nothing about encoding.
 * - Handing UTF-8 bytes to a narrow C API is **worse** than the status quo for some inputs.
 *   `fopen(p.string())` opens `café` on a CP1252 machine; `fopen(p.u8string())` does not, because
 *   the CRT reads UTF-8's `C3 A9` as two CP1252 characters. UTF-8 here is CNA's text encoding,
 *   not a way to call a narrow API.
 */
namespace CNA::Internal
{
    namespace Detail
    {
        /**
         * @brief Reinterprets UTF-8 code units as bytes.
         *
         * `char8_t` and `char` have the same size, representation and alignment, and `char` may
         * alias any object, so this is a reinterpretation rather than a conversion.
         *
         * @param value UTF-8 code units.
         * @return The same bytes as a std::string.
         */
        [[nodiscard]] inline std::string Utf8ToBytes(const std::u8string& value)
        {
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }
    }

    /**
     * @brief Converts a native filesystem path to UTF-8 text, preserving its separators.
     *
     * This is the form to use for anything a human reads — a log line, an exception message, a
     * name shown in a game — because it is the spelling the path actually has. For an identity,
     * a map key or a persisted reference use PathToGenericUtf8() instead, which is stable across
     * platforms.
     *
     * @param path Native filesystem path.
     * @return The path as UTF-8, with the separators the path holds.
     */
    [[nodiscard]] inline std::string PathToUtf8(const std::filesystem::path& path)
    {
        return Detail::Utf8ToBytes(path.u8string());
    }

    /**
     * @brief Converts a native filesystem path to generic-form UTF-8 text.
     *
     * Generic form spells every separator `/`, so the same logical path produces the same text on
     * Windows and on POSIX. That makes this the form for identities, map and cache keys, content
     * manifests and anything written to a file that another platform will read.
     *
     * Note that this normalises **separators only**. It does not resolve `.` or `..`, does not
     * canonicalise, does not touch case, and does not apply Unicode normalisation; those are
     * separate decisions and are deliberately not bundled into a conversion.
     *
     * @param path Native filesystem path.
     * @return The path as UTF-8 with `/` separators.
     */
    [[nodiscard]] inline std::string PathToGenericUtf8(const std::filesystem::path& path)
    {
        return Detail::Utf8ToBytes(path.generic_u8string());
    }

    /**
     * @brief Reconstructs a native filesystem path from UTF-8 path text.
     *
     * The exact inverse of PathToUtf8() and PathToGenericUtf8(): a path converted to UTF-8 and
     * back names the same file on every platform.
     *
     * **Malformed input differs by platform, because the platforms differ.** On Windows the text
     * must be well-formed UTF-8; the standard library converts it to UTF-16 and throws
     * `std::filesystem::filesystem_error` if it cannot. On POSIX a filename is an arbitrary byte
     * sequence that need not be valid UTF-8 at all, and rejecting such bytes here would make
     * files that genuinely exist unopenable — so they are preserved unchanged. Call
     * IsWellFormedUtf8() first where a deterministic, identical answer on both platforms is
     * wanted, such as at a public API that accepts text from a game.
     *
     * @param value Path text encoded as UTF-8.
     * @return Native filesystem path.
     */
    [[nodiscard]] inline std::filesystem::path PathFromUtf8(std::string_view value)
    {
        return std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
    }

    /**
     * @brief Reconstructs a native filesystem path from UTF-8 path text, without throwing.
     *
     * The total counterpart of PathFromUtf8(), for text that arrived from somewhere untrusted — a
     * playlist entry, an external reference inside an XNB, a name a game passed in. Where
     * PathFromUtf8() would throw because the text cannot name a path on this platform, this
     * returns an empty optional, so a caller can refuse the input as invalid rather than
     * propagating an exception out of what reads like a lookup.
     *
     * @param value Path text encoded as UTF-8.
     * @return The native path, or an empty optional when the text cannot name one here.
     */
    [[nodiscard]] inline std::optional<std::filesystem::path> TryPathFromUtf8(std::string_view value)
    {
        try
        {
            return PathFromUtf8(value);
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
    }

    /**
     * @brief Reports whether a byte sequence is well-formed UTF-8.
     *
     * Rejects what the Unicode standard rejects: truncated sequences, stray continuation bytes,
     * overlong encodings, the surrogate range `U+D800`–`U+DFFF`, and anything above `U+10FFFF`.
     * An embedded NUL is a valid code point and is accepted; a caller that cannot carry one
     * should check for it separately.
     *
     * @param value Bytes to inspect.
     * @return True when the bytes are well-formed UTF-8.
     */
    [[nodiscard]] inline bool IsWellFormedUtf8(std::string_view value)
    {
        std::size_t i = 0;
        const std::size_t size = value.size();
        while (i < size)
        {
            const auto lead = static_cast<unsigned char>(value[i]);
            std::size_t extra = 0;
            std::uint32_t code = 0;

            if (lead < 0x80u)
            {
                ++i;
                continue;
            }
            if ((lead & 0xE0u) == 0xC0u) { extra = 1; code = lead & 0x1Fu; }
            else if ((lead & 0xF0u) == 0xE0u) { extra = 2; code = lead & 0x0Fu; }
            else if ((lead & 0xF8u) == 0xF0u) { extra = 3; code = lead & 0x07u; }
            else { return false; }   // a continuation byte in lead position, or 0xF8..0xFF

            if (i + extra >= size) { return false; }

            for (std::size_t n = 1; n <= extra; ++n)
            {
                const auto byte = static_cast<unsigned char>(value[i + n]);
                if ((byte & 0xC0u) != 0x80u) { return false; }
                code = (code << 6) | (byte & 0x3Fu);
            }

            // Overlong: the code point would have fitted in a shorter sequence.
            if (extra == 1 && code < 0x80u) { return false; }
            if (extra == 2 && code < 0x800u) { return false; }
            if (extra == 3 && code < 0x10000u) { return false; }

            if (code > 0x10FFFFu) { return false; }
            if (code >= 0xD800u && code <= 0xDFFFu) { return false; }

            i += extra + 1;
        }
        return true;
    }
}
