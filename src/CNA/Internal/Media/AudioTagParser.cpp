// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/AudioTagParser.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace CNA::Internal::Media
{
    namespace
    {
        // ---------------------------------------------------------------------------------
        // Shared helpers
        // ---------------------------------------------------------------------------------

        std::vector<uint8_t> ReadFileBytes(const std::string& path)
        {
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (!f.is_open())
            {
                return {};
            }
            auto size = f.tellg();
            if (size <= 0)
            {
                return {};
            }
            f.seekg(0);
            std::vector<uint8_t> data(static_cast<std::size_t>(size));
            f.read(reinterpret_cast<char*>(data.data()), size);
            return data;
        }

        std::string ToLowerAscii(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        void ApplyFieldIfKnown(AudioTags& out, const std::string& key, const std::string& value)
        {
            std::string k = ToLowerAscii(key);
            if (k == "title") out.title = value;
            else if (k == "artist") out.artist = value;
            else if (k == "album") out.album = value;
            else if (k == "genre") out.genre = value;
            else if (k == "tracknumber" || k == "track")
            {
                // FNA-irrelevant, own parsing: take the leading integer (handles "3" or "3/12").
                int n = 0;
                for (char c : value)
                {
                    if (c < '0' || c > '9') break;
                    n = n * 10 + (c - '0');
                }
                out.trackNumber = n;
            }
        }

        // ---------------------------------------------------------------------------------
        // Ogg page framing (plan_media.md MEDIA-47)
        // ---------------------------------------------------------------------------------

        struct OggPage
        {
            std::vector<uint8_t> payload;
        };

        // Reads up to maxPages Ogg pages from the start of `data`. Deliberately does not track
        // multi-page packet continuation precisely (real-world short Vorbis comment headers
        // always fit within the first couple of pages) -- payloads are simply concatenated in
        // page order, which is sufficient to locate and parse the comment header packet.
        std::vector<OggPage> ReadOggPages(const std::vector<uint8_t>& data, std::size_t maxPages)
        {
            std::vector<OggPage> pages;
            std::size_t pos = 0;
            while (pos + 27 <= data.size() && pages.size() < maxPages)
            {
                if (std::memcmp(&data[pos], "OggS", 4) != 0)
                {
                    break;
                }
                uint8_t segCount = data[pos + 26];
                std::size_t segTableStart = pos + 27;
                if (segTableStart + segCount > data.size())
                {
                    break;
                }
                std::size_t payloadSize = 0;
                for (std::size_t i = 0; i < segCount; ++i)
                {
                    payloadSize += data[segTableStart + i];
                }
                std::size_t payloadStart = segTableStart + segCount;
                if (payloadStart + payloadSize > data.size())
                {
                    break;
                }

                OggPage page;
                page.payload.assign(data.begin() + static_cast<std::ptrdiff_t>(payloadStart),
                                     data.begin() + static_cast<std::ptrdiff_t>(payloadStart + payloadSize));
                pages.push_back(std::move(page));
                pos = payloadStart + payloadSize;
            }
            return pages;
        }

        uint32_t ReadU32LE(const uint8_t* p)
        {
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }

        // ---------------------------------------------------------------------------------
        // ID3v2 text-frame character-encoding decoding (plan_media.md MEDIA-50/D11)
        // ---------------------------------------------------------------------------------

        std::string Utf16ToUtf8(const uint8_t* data, std::size_t byteLen, bool bigEndian)
        {
            std::string out;
            std::size_t i = 0;
            auto readUnit = [&](std::size_t idx) -> uint16_t
            {
                return bigEndian
                    ? static_cast<uint16_t>((data[idx] << 8) | data[idx + 1])
                    : static_cast<uint16_t>(data[idx] | (data[idx + 1] << 8));
            };
            while (i + 1 < byteLen)
            {
                uint16_t unit = readUnit(i);
                i += 2;
                uint32_t codepoint = unit;
                if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < byteLen)
                {
                    uint16_t low = readUnit(i);
                    if (low >= 0xDC00 && low <= 0xDFFF)
                    {
                        codepoint = 0x10000 + ((static_cast<uint32_t>(unit) - 0xD800) << 10) +
                                    (static_cast<uint32_t>(low) - 0xDC00);
                        i += 2;
                    }
                }
                if (codepoint == 0)
                {
                    break; // NUL terminator
                }
                // Encode codepoint as UTF-8.
                if (codepoint < 0x80)
                {
                    out += static_cast<char>(codepoint);
                }
                else if (codepoint < 0x800)
                {
                    out += static_cast<char>(0xC0 | (codepoint >> 6));
                    out += static_cast<char>(0x80 | (codepoint & 0x3F));
                }
                else if (codepoint < 0x10000)
                {
                    out += static_cast<char>(0xE0 | (codepoint >> 12));
                    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (codepoint & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (codepoint >> 18));
                    out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (codepoint & 0x3F));
                }
            }
            return out;
        }

        std::string Latin1ToUtf8(const uint8_t* data, std::size_t len)
        {
            std::string out;
            for (std::size_t i = 0; i < len; ++i)
            {
                uint8_t c = data[i];
                if (c == 0) break; // NUL terminator
                if (c < 0x80)
                {
                    out += static_cast<char>(c);
                }
                else
                {
                    out += static_cast<char>(0xC0 | (c >> 6));
                    out += static_cast<char>(0x80 | (c & 0x3F));
                }
            }
            return out;
        }

        // ID3v2 text frames start with a 1-byte encoding selector (plan_media.md D11):
        // 0x00=Latin-1, 0x01=UTF-16+BOM, 0x02=UTF-16BE (2.4 only), 0x03=UTF-8 (2.4 only).
        std::string DecodeId3TextFrame(const uint8_t* data, std::size_t size)
        {
            if (size == 0) return {};
            uint8_t encoding = data[0];
            const uint8_t* text = data + 1;
            std::size_t textLen = size - 1;

            switch (encoding)
            {
                case 0x00:
                    return Latin1ToUtf8(text, textLen);
                case 0x01:
                {
                    bool bigEndian = false;
                    if (textLen >= 2 && text[0] == 0xFE && text[1] == 0xFF)
                    {
                        bigEndian = true;
                        text += 2; textLen -= 2;
                    }
                    else if (textLen >= 2 && text[0] == 0xFF && text[1] == 0xFE)
                    {
                        bigEndian = false;
                        text += 2; textLen -= 2;
                    }
                    return Utf16ToUtf8(text, textLen, bigEndian);
                }
                case 0x02:
                    return Utf16ToUtf8(text, textLen, /*bigEndian=*/true);
                case 0x03:
                default:
                {
                    std::size_t end = 0;
                    while (end < textLen && text[end] != 0) ++end;
                    return std::string(reinterpret_cast<const char*>(text), end);
                }
            }
        }
    }

    bool AudioTagParser::TryReadVorbisComments(const std::vector<uint8_t>& fileBytes, AudioTags& out)
    {
        // The identification + comment header packets are always near the start of a real Ogg
        // Vorbis file; a handful of pages is always enough to contain them.
        std::vector<OggPage> pages = ReadOggPages(fileBytes, 8);
        if (pages.empty())
        {
            return false;
        }

        std::vector<uint8_t> concatenated;
        for (const auto& page : pages)
        {
            concatenated.insert(concatenated.end(), page.payload.begin(), page.payload.end());
        }

        static const uint8_t kCommentMagic[7] = {0x03, 'v', 'o', 'r', 'b', 'i', 's'};
        auto it = std::search(concatenated.begin(), concatenated.end(),
                               std::begin(kCommentMagic), std::end(kCommentMagic));
        if (it == concatenated.end())
        {
            return false;
        }

        std::size_t pos = static_cast<std::size_t>(it - concatenated.begin()) + 7;
        if (pos + 4 > concatenated.size()) return false;
        uint32_t vendorLen = ReadU32LE(&concatenated[pos]);
        pos += 4;
        if (pos + vendorLen > concatenated.size()) return false;
        pos += vendorLen;

        if (pos + 4 > concatenated.size()) return false;
        uint32_t commentCount = ReadU32LE(&concatenated[pos]);
        pos += 4;

        return ParseVorbisCommentList(concatenated, pos, commentCount, out);
    }

    // Walks a Vorbis-comment list (count already read) of `commentCount` length-prefixed
    // "KEY=value" entries starting at `pos`. Shared by the Ogg-Vorbis, Ogg-Opus and FLAC readers:
    // all three embed the identical comment-list layout, differing only in how it is located
    // (plan_media.md MEDIA-200/202). Every length is bounds-checked against `data` so a truncated
    // or hostile file cannot read past the buffer.
    bool AudioTagParser::ParseVorbisCommentList(const std::vector<uint8_t>& data,
                                                 std::size_t pos,
                                                 uint32_t commentCount,
                                                 AudioTags& out)
    {
        bool any = false;
        for (uint32_t i = 0; i < commentCount; ++i)
        {
            if (pos + 4 > data.size()) break;
            uint32_t len = ReadU32LE(&data[pos]);
            pos += 4;
            if (pos + len > data.size()) break;

            std::string comment(reinterpret_cast<const char*>(&data[pos]), len);
            pos += len;

            auto eq = comment.find('=');
            if (eq != std::string::npos)
            {
                ApplyFieldIfKnown(out, comment.substr(0, eq), comment.substr(eq + 1));
                any = true;
            }
        }

        if (any)
        {
            out.fromRealTags = true;
        }
        return any;
    }

    bool AudioTagParser::TryReadOpusTags(const std::vector<uint8_t>& fileBytes, AudioTags& out)
    {
        // Ogg-Opus stores the same Vorbis-comment list as Ogg-Vorbis, but behind an "OpusTags"
        // magic instead of "\x03vorbis", and with no trailing framing bit. The existing Vorbis
        // reader searches specifically for the \x03vorbis magic, so it finds nothing in an Opus
        // file -- verified empirically before writing this, not assumed (plan_media.md MEDIA-202).
        std::vector<OggPage> pages = ReadOggPages(fileBytes, 8);
        if (pages.empty())
        {
            return false;
        }

        std::vector<uint8_t> concatenated;
        for (const auto& page : pages)
        {
            concatenated.insert(concatenated.end(), page.payload.begin(), page.payload.end());
        }

        static const uint8_t kOpusTagsMagic[8] = {'O', 'p', 'u', 's', 'T', 'a', 'g', 's'};
        auto it = std::search(concatenated.begin(), concatenated.end(),
                               std::begin(kOpusTagsMagic), std::end(kOpusTagsMagic));
        if (it == concatenated.end())
        {
            return false;
        }

        std::size_t pos = static_cast<std::size_t>(it - concatenated.begin()) + 8;
        if (pos + 4 > concatenated.size()) return false;
        uint32_t vendorLen = ReadU32LE(&concatenated[pos]);
        pos += 4;
        if (pos + vendorLen > concatenated.size()) return false;
        pos += vendorLen;

        if (pos + 4 > concatenated.size()) return false;
        uint32_t commentCount = ReadU32LE(&concatenated[pos]);
        pos += 4;

        return ParseVorbisCommentList(concatenated, pos, commentCount, out);
    }

    bool AudioTagParser::TryReadFlacComments(const std::vector<uint8_t>& fileBytes, AudioTags& out)
    {
        // Native FLAC is NOT an Ogg container: after the "fLaC" marker comes a chain of metadata
        // blocks, each with a 4-byte header (1 bit last-block flag, 7 bits type, 24-bit big-endian
        // length). Block type 4 is VORBIS_COMMENT and holds the same comment list as Ogg
        // (plan_media.md MEDIA-200).
        if (fileBytes.size() < 8 || std::memcmp(fileBytes.data(), "fLaC", 4) != 0)
        {
            return false;
        }

        std::size_t pos = 4;
        while (pos + 4 <= fileBytes.size())
        {
            const uint8_t header = fileBytes[pos];
            const bool isLast = (header & 0x80) != 0;
            const uint8_t blockType = header & 0x7F;
            const uint32_t blockLen = (static_cast<uint32_t>(fileBytes[pos + 1]) << 16) |
                                       (static_cast<uint32_t>(fileBytes[pos + 2]) << 8) |
                                        static_cast<uint32_t>(fileBytes[pos + 3]);
            pos += 4;
            if (pos + blockLen > fileBytes.size()) return false; // truncated/hostile file

            if (blockType == 4) // VORBIS_COMMENT
            {
                std::size_t p = pos;
                if (p + 4 > fileBytes.size()) return false;
                uint32_t vendorLen = ReadU32LE(&fileBytes[p]);
                p += 4;
                if (p + vendorLen > fileBytes.size()) return false;
                p += vendorLen;

                if (p + 4 > fileBytes.size()) return false;
                uint32_t commentCount = ReadU32LE(&fileBytes[p]);
                p += 4;

                return ParseVorbisCommentList(fileBytes, p, commentCount, out);
            }

            pos += blockLen;
            if (isLast) break;
        }
        return false;
    }

    bool AudioTagParser::TryReadId3v2(const std::vector<uint8_t>& fileBytes, AudioTags& out)
    {
        if (fileBytes.size() < 10 || std::memcmp(fileBytes.data(), "ID3", 3) != 0)
        {
            return false;
        }

        uint8_t majorVersion = fileBytes[3];
        // Synchsafe integer: 4 bytes, 7 significant bits each (high bit always 0).
        uint32_t tagSize = (static_cast<uint32_t>(fileBytes[6] & 0x7F) << 21) |
                            (static_cast<uint32_t>(fileBytes[7] & 0x7F) << 14) |
                            (static_cast<uint32_t>(fileBytes[8] & 0x7F) << 7) |
                            static_cast<uint32_t>(fileBytes[9] & 0x7F);

        std::size_t tagEnd = std::min(fileBytes.size(), static_cast<std::size_t>(10) + tagSize);
        std::size_t pos = 10;
        bool any = false;

        while (pos + 10 <= tagEnd)
        {
            if (fileBytes[pos] == 0)
            {
                break; // padding -- no more real frames
            }
            std::string frameId(reinterpret_cast<const char*>(&fileBytes[pos]), 4);

            uint32_t frameSize;
            if (majorVersion >= 4)
            {
                // ID3v2.4: frame sizes are synchsafe.
                frameSize = (static_cast<uint32_t>(fileBytes[pos + 4] & 0x7F) << 21) |
                            (static_cast<uint32_t>(fileBytes[pos + 5] & 0x7F) << 14) |
                            (static_cast<uint32_t>(fileBytes[pos + 6] & 0x7F) << 7) |
                            static_cast<uint32_t>(fileBytes[pos + 7] & 0x7F);
            }
            else
            {
                // ID3v2.3 and earlier: frame sizes are plain big-endian.
                frameSize = (static_cast<uint32_t>(fileBytes[pos + 4]) << 24) |
                            (static_cast<uint32_t>(fileBytes[pos + 5]) << 16) |
                            (static_cast<uint32_t>(fileBytes[pos + 6]) << 8) |
                            static_cast<uint32_t>(fileBytes[pos + 7]);
            }
            pos += 10; // frame header: id[4] + size[4] + flags[2]

            if (frameSize == 0 || pos + frameSize > tagEnd)
            {
                break; // malformed -- stop gracefully rather than reading out of bounds
            }

            if (frameId == "TIT2" || frameId == "TPE1" || frameId == "TALB" ||
                frameId == "TCON" || frameId == "TRCK")
            {
                std::string text = DecodeId3TextFrame(&fileBytes[pos], frameSize);
                std::string key = (frameId == "TIT2") ? "title"
                                 : (frameId == "TPE1") ? "artist"
                                 : (frameId == "TALB") ? "album"
                                 : (frameId == "TCON") ? "genre"
                                 : "tracknumber";
                ApplyFieldIfKnown(out, key, text);
                any = true;
            }

            pos += frameSize;
        }

        if (any)
        {
            out.fromRealTags = true;
        }
        return any;
    }

    void AudioTagParser::ApplyFilenameFallback(const std::string& path, AudioTags& out)
    {
        std::filesystem::path p(path);
        if (out.title.empty())
        {
            out.title = p.stem().string();
        }
        std::filesystem::path parent = p.parent_path();
        if (out.album.empty() && !parent.empty())
        {
            out.album = parent.filename().string();
        }
        std::filesystem::path grandparent = parent.parent_path();
        if (out.artist.empty() && !grandparent.empty())
        {
            out.artist = grandparent.filename().string();
        }
    }

    AudioTags AudioTagParser::ReadTags(const std::string& path)
    {
        AudioTags out;
        std::string ext = ToLowerAscii(std::filesystem::path(path).extension().string());

        std::vector<uint8_t> bytes = ReadFileBytes(path);
        if (!bytes.empty())
        {
            if (ext == ".ogg" || ext == ".oga")
            {
                TryReadVorbisComments(bytes, out);
            }
            else if (ext == ".opus")
            {
                TryReadOpusTags(bytes, out);
            }
            else if (ext == ".flac")
            {
                TryReadFlacComments(bytes, out);
            }
            else if (ext == ".mp3")
            {
                TryReadId3v2(bytes, out);
            }
        }

        ApplyFilenameFallback(path, out);
        return out;
    }
}
