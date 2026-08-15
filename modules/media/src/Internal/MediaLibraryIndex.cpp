// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/MediaLibraryIndex.hpp"

#include <algorithm>
#include <cctype>

#include "CNA/Internal/Media/AudioTagParser.hpp"

namespace CNA::Internal::Media
{
    namespace
    {
        std::string CaseFold(const std::string& s)
        {
            std::string out = s;
            std::transform(out.begin(), out.end(), out.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            // Trim leading/trailing whitespace so "Artist A " and "Artist A" fold identically.
            std::size_t start = out.find_first_not_of(" \t");
            std::size_t end = out.find_last_not_of(" \t");
            if (start == std::string::npos) return {};
            return out.substr(start, end - start + 1);
        }

        bool HasSupportedAudioExtension(const std::filesystem::path& p)
        {
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            // Only formats this project's selected mixer decoder can genuinely PLAY are indexed --
            // advertising a song MediaPlayer::Play() would fail on is worse than not listing it
            // (plan_media.md MEDIA-199). Verified against the bundled decoder set:
            // decoder_vorbis/stb_vorbis (.ogg/.oga), decoder_drmp3/mpg123 (.mp3), decoder_wav
            // (.wav), decoder_flac/drflac (.flac), decoder_opus (.opus).
            //
            // Deliberately NOT indexed: .m4a/.aac. The bundled mixer ships no AAC decoder or AAC
            // feature switch, so those files are unplayable here
            // -- indexing them would create library entries that can never be played.
            return ext == ".ogg" || ext == ".oga" || ext == ".mp3" || ext == ".wav" ||
                    ext == ".flac" || ext == ".opus";
        }
    }

    MediaLibraryIndex::MediaLibraryIndex(const std::string& musicRoot)
    {
        std::error_code ec;
        if (musicRoot.empty() || !std::filesystem::exists(musicRoot, ec) || ec)
        {
            return;
        }
        std::set<std::filesystem::path> visited;
        ScanDirectory(musicRoot, visited);
    }

    void MediaLibraryIndex::ScanDirectory(const std::filesystem::path& dir,
                                           std::set<std::filesystem::path>& visited)
    {
        // plan_media.md MEDIA-53: guard against symlink cycles by tracking canonical (symlink-
        // resolved) paths already visited, and skip (rather than crash on) unreadable/broken
        // entries -- a real filesystem scan of a real user directory can hit both.
        std::error_code ec;
        std::filesystem::path canonicalDir = std::filesystem::weakly_canonical(dir, ec);
        if (ec)
        {
            return;
        }
        if (!visited.insert(canonicalDir).second)
        {
            return; // already scanned this real directory -- cycle guard
        }

        std::filesystem::directory_iterator it(
            dir, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            return;
        }
        // directory_iterator's enumeration order is filesystem-dependent, not alphabetical --
        // sort entries first so scan results (including which casing "wins" a case-insensitive
        // Artist/Genre normalization, D10) are deterministic and reproducible across platforms/
        // filesystems, not an accident of directory-entry ordering.
        std::vector<std::filesystem::directory_entry> entries;
        std::filesystem::directory_iterator end;
        for (; it != end; it.increment(ec))
        {
            if (ec) break;
            entries.push_back(*it);
        }
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.path() < b.path(); });

        for (const auto& entry : entries)
        {
            std::error_code entryEc;
            if (entry.is_directory(entryEc) && !entryEc)
            {
                ScanDirectory(entry.path(), visited);
            }
            else if (entry.is_regular_file(entryEc) && !entryEc)
            {
                if (HasSupportedAudioExtension(entry.path()))
                {
                    AddSong(entry.path().string());
                }
            }
        }
    }

    void MediaLibraryIndex::AddSong(const std::string& path)
    {
        AudioTags tags = AudioTagParser::ReadTags(path);

        IndexedSong song;
        song.path = path;
        song.title = tags.title;
        song.artist = NormalizeDisplayValue(tags.artist, artistCanonical_);
        song.album = tags.album;
        song.genre = tags.genre.empty() ? std::string{} : NormalizeDisplayValue(tags.genre, genreCanonical_);
        song.trackNumber = tags.trackNumber;
        song.rating      = tags.rating;
        song.hasRating   = tags.hasRating;

        songs_.push_back(std::move(song));
    }

    std::string MediaLibraryIndex::NormalizeDisplayValue(
        const std::string& raw, std::unordered_map<std::string, std::string>& canonical)
    {
        if (raw.empty())
        {
            return raw;
        }
        std::string key = CaseFold(raw);
        auto it = canonical.find(key);
        if (it != canonical.end())
        {
            return it->second; // first-seen casing wins
        }
        canonical.emplace(key, raw);
        return raw;
    }
}
