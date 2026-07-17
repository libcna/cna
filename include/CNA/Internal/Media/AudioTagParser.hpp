// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Media
{
    /// Tag fields read from (or inferred for) one audio file. Fields that couldn't be determined
    /// stay empty/zero -- plan_media.md MEDIA-47..51/D2.
    struct AudioTags
    {
        std::string title;
        std::string artist;
        std::string album;
        std::string genre;      // may be empty -- e.g. filename-fallback songs never get a genre
        int trackNumber = 0;
        bool fromRealTags = false; // true if a real embedded tag source was used, not the fallback
    };

    /// Minimal, from-scratch audio-tag reader: real Ogg Vorbis-comment and ID3v2.3/2.4 parsing,
    /// falling back to filename/folder heuristics for untagged or unsupported files. No
    /// third-party tag library dependency (matches this project's established from-scratch-parser
    /// precedent, e.g. XactParser/Json.hpp) -- plan_media.md D2.
    class AudioTagParser
    {
    public:
        /// Reads tags for the given audio file: tries a real embedded-tag reader appropriate to
        /// the file's extension first (Vorbis comments for .ogg, ID3v2 for .mp3), then falls back
        /// to filename/folder heuristics (Title=filename, Album=parent dir, Artist=grandparent
        /// dir) if no usable tags were found.
        static AudioTags ReadTags(const std::string& path);

        /// Real Ogg Vorbis-comment reader. Returns false (out untouched) if no comment header is
        /// found or the data is malformed.
        static bool TryReadVorbisComments(const std::vector<uint8_t>& fileBytes, AudioTags& out);

        /// Real ID3v2.3/2.4 text-frame reader (TIT2/TPE1/TALB/TCON/TRCK), including proper
        /// text-encoding-byte handling (Latin-1/UTF-16+BOM/UTF-16BE/UTF-8). Returns false (out
        /// untouched) if no ID3v2 header is found or the data is malformed.
        static bool TryReadId3v2(const std::vector<uint8_t>& fileBytes, AudioTags& out);

        /// Derives Title/Album/Artist from the file's own path (filename, parent dir, grandparent
        /// dir respectively) when no real tags are available.
        static void ApplyFilenameFallback(const std::string& path, AudioTags& out);
    };
}
