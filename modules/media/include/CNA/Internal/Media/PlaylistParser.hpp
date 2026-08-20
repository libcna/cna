// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

namespace CNA::Internal::Media
{
    /// One parsed M3U/M3U8 playlist -- entries pointing at a file that doesn't exist are skipped
    /// (not fatal), matching FNA's own general "missing content degrades gracefully" posture.
    struct ParsedPlaylist
    {
        std::string name; // derived from the playlist's own filename (without extension)
        std::string sourcePath;
        std::vector<std::string> songPaths; // resolved, existing paths only
    };

    /// Minimal M3U/M3U8 reader (plans/plan_media.md MEDIA-57/58/D5). Both extensions parse identically:
    /// path-per-line, with optional "#EXTINF:" comment lines tolerated and ignored, entries
    /// resolved relative to the playlist's own directory. On this platform std::string is already
    /// byte-transparent UTF-8 throughout the codebase, so there is no separate "legacy encoding"
    /// concept to apply to plain .m3u files distinctly from .m3u8 -- both are read as raw UTF-8
    /// bytes, which correctly round-trips non-ASCII entries either way (verified against the
    /// International.m3u8 fixture's non-ASCII "Étoile" filename).
    ///
    /// REMED-CONTENT-002: deliberately diverges from standard M3U here -- an absolute entry, or a
    /// relative entry that escapes the playlist's own containing directory via ".." (or a
    /// symlink), is skipped rather than honored. Standard M3U allows both; this is a conscious
    /// security-over-compatibility choice (an untrusted/hostile playlist file must not be able to
    /// make the engine open and decode an arbitrary readable file elsewhere on disk), not an XNA/
    /// FNA-faithful behavior this class has to preserve.
    class PlaylistParser
    {
    public:
        /// Parses a single .m3u/.m3u8 file. Relative entries are resolved against the playlist
        /// file's own containing directory; absolute or escaping entries are skipped (see class
        /// docs).
        static ParsedPlaylist Parse(const std::string& playlistPath);

        /// Non-recursively scans `musicRoot` for *.m3u/*.m3u8 files and parses each one.
        static std::vector<ParsedPlaylist> ScanDirectory(const std::string& musicRoot);
    };
}
