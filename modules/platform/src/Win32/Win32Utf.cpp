// SPDX-License-Identifier: MS-PL

#include "Win32Utf.hpp"

#include "Win32Common.hpp"

namespace CNA::Platform::Win32 {

    namespace {

        constexpr char16_t kHighSurrogateFirst = 0xD800;
        constexpr char16_t kHighSurrogateLast = 0xDBFF;
        constexpr char16_t kLowSurrogateFirst = 0xDC00;
        constexpr char16_t kLowSurrogateLast = 0xDFFF;

        bool IsHighSurrogate(const char16_t unit)
        {
            return unit >= kHighSurrogateFirst && unit <= kHighSurrogateLast;
        }

        bool IsLowSurrogate(const char16_t unit)
        {
            return unit >= kLowSurrogateFirst && unit <= kLowSurrogateLast;
        }

    } // namespace

    std::wstring ToWide(const std::string& utf8)
    {
        if (utf8.empty())
            return {};

        const int required = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (required <= 0)
            return {};

        std::wstring wide(static_cast<std::size_t>(required), L'\0');
        const int written = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), wide.data(),
            required);
        if (written <= 0)
            return {};
        wide.resize(static_cast<std::size_t>(written));
        return wide;
    }

    std::string ToUtf8(const std::wstring& utf16)
    {
        if (utf16.empty())
            return {};

        const int required = WideCharToMultiByte(
            CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), nullptr, 0, nullptr, nullptr);
        if (required <= 0)
            return {};

        std::string utf8(static_cast<std::size_t>(required), '\0');
        const int written = WideCharToMultiByte(
            CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), utf8.data(), required,
            nullptr, nullptr);
        if (written <= 0)
            return {};
        utf8.resize(static_cast<std::size_t>(written));
        return utf8;
    }

    std::string ToUtf8(const wchar_t* const utf16)
    {
        if (utf16 == nullptr)
            return {};
        return ToUtf8(std::wstring(utf16));
    }

    std::string EncodeUtf8(const char32_t codePoint)
    {
        // Surrogates are not scalar values. Encoding one produces CESU-8, which is not UTF-8 and
        // which a consumer validating its input will reject.
        if (codePoint > 0x10FFFF ||
            (codePoint >= kHighSurrogateFirst && codePoint <= kLowSurrogateLast))
        {
            return {};
        }

        std::string text;
        if (codePoint < 0x80)
        {
            text.push_back(static_cast<char>(codePoint));
        }
        else if (codePoint < 0x800)
        {
            text.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            text.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else if (codePoint < 0x10000)
        {
            text.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            text.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else
        {
            text.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            text.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        return text;
    }

    bool SurrogateAssembler::Feed(const char16_t unit, std::string& text)
    {
        if (IsHighSurrogate(unit))
        {
            // A second high surrogate means the first one never got its partner. Drop it: an
            // unpaired surrogate has no UTF-8 encoding, and keeping it would corrupt the next
            // character too.
            pendingHighSurrogate_ = unit;
            return false;
        }

        if (IsLowSurrogate(unit))
        {
            if (pendingHighSurrogate_ == 0)
                return false;

            const char32_t codePoint =
                0x10000u +
                ((static_cast<char32_t>(pendingHighSurrogate_) - kHighSurrogateFirst) << 10) +
                (static_cast<char32_t>(unit) - kLowSurrogateFirst);
            pendingHighSurrogate_ = 0;
            std::string encoded = EncodeUtf8(codePoint);
            if (encoded.empty())
                return false;
            text = std::move(encoded);
            return true;
        }

        pendingHighSurrogate_ = 0;
        std::string encoded = EncodeUtf8(static_cast<char32_t>(unit));
        if (encoded.empty())
            return false;
        text = std::move(encoded);
        return true;
    }

    void SurrogateAssembler::Reset()
    {
        pendingHighSurrogate_ = 0;
    }

} // namespace CNA::Platform::Win32
