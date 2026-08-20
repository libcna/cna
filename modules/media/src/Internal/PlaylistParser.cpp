// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/PlaylistParser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "CNA/Internal/PathContainment.hpp"

namespace CNA::Internal::Media
{
    namespace
    {
        std::string Trim(const std::string& s)
        {
            std::size_t start = s.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return {};
            std::size_t end = s.find_last_not_of(" \t\r\n");
            return s.substr(start, end - start + 1);
        }

        bool HasPlaylistExtension(const std::filesystem::path& p)
        {
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext == ".m3u" || ext == ".m3u8";
        }
    }

    ParsedPlaylist PlaylistParser::Parse(const std::string& playlistPath)
    {
        ParsedPlaylist result;
        result.sourcePath = playlistPath;
        result.name = std::filesystem::path(playlistPath).stem().string();

        std::ifstream file(playlistPath, std::ios::binary);
        if (!file.is_open())
        {
            return result;
        }

        std::filesystem::path baseDir = std::filesystem::path(playlistPath).parent_path();
        std::string line;
        while (std::getline(file, line))
        {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#')
            {
                continue; // blank line or a comment/#EXTINF/#EXTM3U directive
            }

            // REMED-CONTENT-002: standard M3U allows absolute entries and would let one playlist
            // reference songs anywhere on disk -- CNA deliberately tightens this (a security-over-
            // compatibility choice, not FNA/XNA-faithful behavior to preserve, since M3U parsing
            // has no XNA equivalent at all): an untrusted/hostile playlist file must not be able to
            // make the engine open and decode an arbitrary readable file. Every entry is contained
            // to the playlist's own directory, matching the pattern already used for
            // ContentReader::ReadExternalReference and StorageDevice::DeleteContainer.
            const auto contained = CNA::Internal::ResolveContainedPath(baseDir.string(), trimmed);
            if (!contained.ok)
            {
                continue; // absolute or escaping entry -- skipped, same as a missing entry below
            }

            std::error_code ec;
            if (std::filesystem::exists(contained.resolvedPath, ec) && !ec)
            {
                result.songPaths.push_back(contained.resolvedPath);
            }
            // A missing entry is skipped, not fatal (plans/plan_media.md MEDIA-57 acceptance).
        }

        return result;
    }

    std::vector<ParsedPlaylist> PlaylistParser::ScanDirectory(const std::string& musicRoot)
    {
        std::vector<ParsedPlaylist> playlists;
        std::error_code ec;
        if (musicRoot.empty() || !std::filesystem::exists(musicRoot, ec) || ec)
        {
            return playlists;
        }

        std::filesystem::directory_iterator it(
            musicRoot, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            return playlists;
        }
        // directory_iterator's enumeration order is filesystem-dependent -- sort for
        // deterministic, reproducible results (matching MediaLibraryIndex/PictureLibraryIndex).
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
            if (entry.is_regular_file(entryEc) && !entryEc && HasPlaylistExtension(entry.path()))
            {
                playlists.push_back(Parse(entry.path().string()));
            }
        }
        return playlists;
    }
}
