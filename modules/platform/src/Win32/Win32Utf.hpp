// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

namespace CNA::Platform::Win32 {

    /**
     * @brief Converts UTF-8 to the UTF-16 the wide Win32 API takes.
     *
     * @param utf8 The text to convert.
     * @return The converted text; empty for empty input and for input that is not valid UTF-8.
     */
    [[nodiscard]] std::wstring ToWide(const std::string& utf8);

    /**
     * @brief Converts UTF-16 from the wide Win32 API to the UTF-8 the contract uses.
     *
     * @param utf16 The text to convert.
     * @return The converted text; empty for empty input.
     */
    [[nodiscard]] std::string ToUtf8(const std::wstring& utf16);

    /**
     * @brief Converts a null-terminated UTF-16 buffer to UTF-8.
     *
     * @param utf16 The buffer to convert; null is treated as empty.
     * @return The converted text.
     */
    [[nodiscard]] std::string ToUtf8(const wchar_t* utf16);

    /**
     * @brief Assembles committed text from the UTF-16 code units `WM_CHAR` delivers.
     *
     * `WM_CHAR` carries one UTF-16 *code unit*, not one character: a character outside the basic
     * multilingual plane arrives as two consecutive messages carrying a high surrogate and then a
     * low surrogate. Encoding either half on its own produces invalid UTF-8, so this holds a
     * pending high surrogate until its partner arrives.
     *
     * Deliberately a small state machine rather than a free function: the pending half is state
     * that must survive between two messages, and losing it is exactly the bug this prevents.
     */
    class SurrogateAssembler
    {
    public:
        /**
         * @brief Feeds one UTF-16 code unit.
         *
         * @param unit The code unit from `WM_CHAR`'s `wParam`.
         * @param text Receives the completed character's UTF-8 encoding; left untouched when this
         *        returns false.
         * @return True when a complete character was produced. False when the unit was a high
         *         surrogate awaiting its partner, or an unpaired surrogate that was discarded.
         */
        bool Feed(char16_t unit, std::string& text);

        /** @brief Discards any pending high surrogate, e.g. on focus loss. */
        void Reset();

        /**
         * @brief Gets whether a high surrogate is waiting for its partner.
         *
         * @return True while the assembler holds an incomplete pair.
         */
        [[nodiscard]] bool HasPending() const { return pendingHighSurrogate_ != 0; }

    private:
        char16_t pendingHighSurrogate_ = 0;
    };

    /**
     * @brief Encodes one Unicode scalar value as UTF-8.
     *
     * @param codePoint The scalar value; surrogates and values above U+10FFFF produce nothing.
     * @return The UTF-8 encoding, or an empty string when @p codePoint is not a scalar value.
     */
    [[nodiscard]] std::string EncodeUtf8(char32_t codePoint);

} // namespace CNA::Platform::Win32
