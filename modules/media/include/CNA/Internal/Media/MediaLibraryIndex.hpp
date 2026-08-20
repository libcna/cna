// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Media
{
    /// One scanned song, with tags already resolved (real embedded tags or the filename/folder
    /// fallback) and Artist/Genre already case-insensitively normalized to their canonical
    /// first-seen display casing (plans/plan_media.md D10) -- consumers never need to re-normalize.
    struct IndexedSong
    {
        std::string path;
        std::string title;
        std::string artist;
        std::string album;
        std::string genre; // may be empty
        int trackNumber = 0;
        int rating = 0;        // XNA 0-10 scale; meaningful only when hasRating is true
        bool hasRating = false;
    };

    /// One-shot recursive scan of a Music root, building an in-memory index of every supported
    /// audio file found (.ogg/.oga/.mp3/.wav), tagged via AudioTagParser. Hardened against
    /// symlink cycles and permission-denied subdirectories (plans/plan_media.md MEDIA-52/53).
    class MediaLibraryIndex
    {
    public:
        explicit MediaLibraryIndex(const std::string& musicRoot);

        [[nodiscard]] const std::vector<IndexedSong>& GetSongs() const { return songs_; }

    private:
        void ScanDirectory(const std::filesystem::path& dir, std::set<std::filesystem::path>& visited);
        void AddSong(const std::string& path);

        // Case-insensitive artist/genre normalization (plans/plan_media.md D10): returns the canonical
        // (first-seen-casing) display value for `raw`, registering it as canonical if this is the
        // first time this case-folded key has been seen.
        std::string NormalizeDisplayValue(const std::string& raw,
                                           std::unordered_map<std::string, std::string>& canonical);

        std::vector<IndexedSong> songs_;
        std::unordered_map<std::string, std::string> artistCanonical_; // casefolded key -> display
        std::unordered_map<std::string, std::string> genreCanonical_;
    };
}
