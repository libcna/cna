// SPDX-License-Identifier: MS-PL

#include "DropParsing.hpp"

#include <algorithm>
#include <cctype>

#include <unistd.h>

namespace CNA::Platform::Freedesktop {

    namespace {

        bool EqualsIgnoringCase(const std::string_view a, const std::string_view b)
        {
            return a.size() == b.size() &&
                   std::equal(a.begin(), a.end(), b.begin(), [](const char x, const char y) {
                       return std::tolower(static_cast<unsigned char>(x)) ==
                              std::tolower(static_cast<unsigned char>(y));
                   });
        }

        int HexValue(const char digit)
        {
            if (digit >= '0' && digit <= '9') { return digit - '0'; }
            if (digit >= 'a' && digit <= 'f') { return digit - 'a' + 10; }
            if (digit >= 'A' && digit <= 'F') { return digit - 'A' + 10; }
            return -1;
        }

        /// Whether a byte string is valid UTF-8 -- structurally: lead bytes, continuation bytes,
        /// no overlong two-byte forms, nothing past U+10FFFF.
        bool IsUtf8(const std::string& text)
        {
            std::size_t index = 0;
            while (index < text.size())
            {
                const auto lead = static_cast<unsigned char>(text[index]);
                std::size_t length = 0;
                if (lead < 0x80u) { length = 1; }
                else if (lead >= 0xC2u && lead <= 0xDFu) { length = 2; }
                else if (lead >= 0xE0u && lead <= 0xEFu) { length = 3; }
                else if (lead >= 0xF0u && lead <= 0xF4u) { length = 4; }
                else { return false; }
                if (index + length > text.size()) { return false; }
                for (std::size_t next = 1; next < length; ++next)
                {
                    if ((static_cast<unsigned char>(text[index + next]) & 0xC0u) != 0x80u)
                    {
                        return false;
                    }
                }
                index += length;
            }
            return true;
        }

        std::string Latin1ToUtf8(const std::string& bytes)
        {
            std::string text;
            text.reserve(bytes.size());
            for (const char character : bytes)
            {
                const auto byte = static_cast<unsigned char>(character);
                if (byte < 0x80u)
                {
                    text.push_back(character);
                }
                else
                {
                    text.push_back(static_cast<char>(0xC0u | (byte >> 6)));
                    text.push_back(static_cast<char>(0x80u | (byte & 0x3Fu)));
                }
            }
            return text;
        }

    } // namespace

    std::optional<DropTarget> ChooseDropTarget(const std::vector<std::string>& offered)
    {
        std::optional<DropTarget> best;
        int bestRank = 0;
        for (std::size_t index = 0; index < offered.size(); ++index)
        {
            const std::string& name = offered[index];
            int rank = 0;
            DropEncoding encoding = DropEncoding::Utf8;
            if (EqualsIgnoringCase(name, "text/uri-list"))
            {
                rank = 1;
                encoding = DropEncoding::UriList;
            }
            else if (EqualsIgnoringCase(name, "text/plain;charset=utf-8") || name == "UTF8_STRING")
            {
                rank = 2;
            }
            else if (EqualsIgnoringCase(name, "text/plain"))
            {
                rank = 3;
            }
            else if (name == "TEXT")
            {
                rank = 4;
            }
            else if (name == "STRING")
            {
                rank = 5;
                encoding = DropEncoding::Latin1;
            }
            else
            {
                continue;
            }
            // The first offered of the best kind: a source lists its types best first.
            if (!best || rank < bestRank)
            {
                best = DropTarget{index, encoding};
                bestRank = rank;
            }
        }
        return best;
    }

    std::optional<std::string> FileUriToPath(const std::string_view uri,
                                             const std::string_view hostname)
    {
        if (uri.size() < 5 || !EqualsIgnoringCase(uri.substr(0, 5), "file:"))
        {
            return std::nullopt;
        }
        std::string_view rest = uri.substr(5);
        if (rest.starts_with("//"))
        {
            rest.remove_prefix(2);
            const std::size_t slash = rest.find('/');
            if (slash == std::string_view::npos)
            {
                return std::nullopt;
            }
            const std::string_view host = rest.substr(0, slash);
            // A file on another host is not a file this process can open.
            if (!host.empty() && !EqualsIgnoringCase(host, "localhost") &&
                (hostname.empty() || !EqualsIgnoringCase(host, hostname)))
            {
                return std::nullopt;
            }
            rest.remove_prefix(slash);
        }
        else if (!rest.starts_with("/"))
        {
            return std::nullopt;
        }

        std::string path;
        path.reserve(rest.size());
        for (std::size_t index = 0; index < rest.size(); ++index)
        {
            if (rest[index] != '%')
            {
                path.push_back(rest[index]);
                continue;
            }
            if (index + 2 >= rest.size())
            {
                return std::nullopt;
            }
            const int high = HexValue(rest[index + 1]);
            const int low = HexValue(rest[index + 2]);
            if (high < 0 || low < 0 || (high == 0 && low == 0))
            {
                // A malformed escape, or a NUL no path can contain.
                return std::nullopt;
            }
            path.push_back(static_cast<char>((high << 4) | low));
            index += 2;
        }
        return path;
    }

    std::vector<DroppedItem> ParseUriList(const std::string_view list,
                                             const std::string_view hostname)
    {
        std::vector<DroppedItem> items;
        std::size_t start = 0;
        while (start < list.size())
        {
            std::size_t end = list.find('\n', start);
            if (end == std::string_view::npos)
            {
                end = list.size();
            }
            std::string_view line = list.substr(start, end - start);
            start = end + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == '\0'))
            {
                line.remove_suffix(1);
            }
            if (line.empty() || line.front() == '#')
            {
                continue;
            }
            if (std::optional<std::string> path = FileUriToPath(line, hostname))
            {
                items.push_back(DroppedItem{true, std::move(*path)});
            }
            else
            {
                items.push_back(DroppedItem{false, std::string(line)});
            }
        }
        return items;
    }

    std::string DecodeDropText(const std::vector<unsigned char>& data, const DropEncoding encoding)
    {
        std::string bytes(data.begin(), data.end());
        while (!bytes.empty() && bytes.back() == '\0')
        {
            bytes.pop_back();
        }
        if (encoding == DropEncoding::Latin1 || !IsUtf8(bytes))
        {
            return Latin1ToUtf8(bytes);
        }
        return bytes;
    }

    std::string LocalHostName()
    {
        char name[256] = {};
        if (gethostname(name, sizeof(name) - 1) != 0)
        {
            return {};
        }
        return name;
    }

} // namespace CNA::Platform::Freedesktop
