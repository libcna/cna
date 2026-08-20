// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Media
{
    /// Tag fields read from (or inferred for) one audio file. Fields that couldn't be determined
    /// stay empty/zero -- plans/plan_media.md MEDIA-47..51/D2.
    struct AudioTags
    {
        std::string title;
        std::string artist;
        std::string album;
        std::string genre;      // may be empty -- e.g. filename-fallback songs never get a genre
        int trackNumber = 0;

        /// User rating on XNA's 0-10 scale, valid only when `hasRating` is true.
        ///
        /// Source formats disagree wildly: ID3v2 POPM is 0-255 (0 meaning "unrated"), while the
        /// Vorbis `RATING` comment has no standard at all -- different taggers write 0-100, 0-5 or
        /// 0-10. The chosen interpretations are documented in CHECKLIST.md; neither XNA nor FNA
        /// defines a conversion, so these are CNA decisions (plans/plan_media.md MEDIA-182/183).
        int rating = 0;

        /// True only if a real rating tag was present -- this is what XNA's Song.IsRated means
        /// ("the user has rated this song"), which is NOT the same as `rating != 0`.
        bool hasRating = false;
        bool fromRealTags = false; // true if a real embedded tag source was used, not the fallback
    };

    /// Minimal, from-scratch audio-tag reader: real Ogg Vorbis-comment and ID3v2.3/2.4 parsing,
    /// falling back to filename/folder heuristics for untagged or unsupported files. No
    /// third-party tag library dependency (matches this project's established from-scratch-parser
    /// precedent, e.g. XactParser/Json.hpp) -- plans/plan_media.md D2.
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

        /// Extracts embedded cover art from an audio file, if it has any.
        ///
        /// Supports ID3v2 APIC frames (MP3) and FLAC METADATA_BLOCK_PICTURE (block type 6).
        /// Front cover (picture type 3) is preferred when several images are present; otherwise
        /// the first image found is used (plans/plan_media.md MEDIA-206/207).
        ///
        /// @param path     Audio file to read.
        /// @param outImage Receives the raw encoded image bytes (JPEG/PNG as stored in the tag).
        /// @return true if embedded art was found, leaving `outImage` untouched otherwise.
        static bool ExtractEmbeddedArt(const std::string& path, std::vector<uint8_t>& outImage);


        /// Reads an Ogg-Opus "OpusTags" comment header (plans/plan_media.md MEDIA-202).
        static bool TryReadOpusTags(const std::vector<uint8_t>& fileBytes, AudioTags& out);

        /// Reads a native-FLAC VORBIS_COMMENT metadata block (plans/plan_media.md MEDIA-200).
        static bool TryReadFlacComments(const std::vector<uint8_t>& fileBytes, AudioTags& out);

        /// Walks a length-prefixed "KEY=value" Vorbis-comment list; shared by all three readers.
        static bool ParseVorbisCommentList(const std::vector<uint8_t>& data, std::size_t pos,
                                            uint32_t commentCount, AudioTags& out);

        /// Derives Title/Album/Artist from the file's own path (filename, parent dir, grandparent
        /// dir respectively) when no real tags are available.
        static void ApplyFilenameFallback(const std::string& path, AudioTags& out);

        /// Applies a Vorbis `RATING` comment, converting to XNA's 0-10 scale.
        static void ApplyRatingIfPresent(AudioTags& out, const std::string& key,
                                          const std::string& value);
    };
}
