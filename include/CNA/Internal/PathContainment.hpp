// SPDX-License-Identifier: MS-PL
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>

namespace CNA::Internal
{
    /**
     * @brief Returns true if @p normalized (already forward-slash-normalized) is absolute, or is
     *        a Windows-style drive-letter (`C:/...`) or UNC (`//server/share/...`) path.
     *
     * `std::filesystem::path::is_absolute()` alone only recognizes a leading `/`, so a Windows-
     * style path in a caller-/file-supplied string would slip through unrecognized when compiled/
     * run on POSIX. Shared building block for ResolveContainedPath() and for callers (like
     * `ContentReader::ReadExternalReference`) whose own containment root is not simply "the join
     * base" -- see that function's own comments.
     *
     * @param normalized A path string that has already had `\` replaced with `/`.
     */
    inline bool IsDisallowedAbsolutePath(const std::string& normalized)
    {
        if (std::filesystem::path(normalized).is_absolute())
        {
            return true;
        }
        const bool looksLikeDriveLetter =
            normalized.size() >= 2 &&
            std::isalpha(static_cast<unsigned char>(normalized[0])) != 0 &&
            normalized[1] == ':';
        const bool looksLikeUnc = normalized.rfind("//", 0) == 0;
        return looksLikeDriveLetter || looksLikeUnc;
    }

    /** @brief Result of ResolveContainedPath(). */
    struct ContainedPathResult
    {
        /** @brief True if the input resolved within the given base directory. */
        bool ok = false;
        /** @brief The resolved path, valid only when @c ok is true. */
        std::string resolvedPath;
    };

    /**
     * @brief Joins @p relativeOrAbsolute onto @p baseDir and verifies the result stays within
     *        @p baseDir.
     *
     * Guards against the classic `std::filesystem::path::operator/` pitfall -- an absolute
     * right-hand operand silently discards the left-hand base, so a naive `fs::path(baseDir) /
     * untrustedString` can resolve to anywhere on disk -- and against any `..`-escaping relative
     * path (see IsDisallowedAbsolutePath() for the absolute/drive-letter/UNC half of the check).
     *
     * Used directly by two of the three sites sharing this root cause (plan REMED-CONTENT-002):
     * `StorageDevice::DeleteContainer` and `PlaylistParser::Parse`, where the natural containment
     * root (storage root; the playlist's own directory) is the same directory the untrusted string
     * is joined onto. The third site, `ContentReader::ReadExternalReference` (via
     * `ResolveRelativeAssetPath`), joins onto the *current asset's own directory* but must only be
     * rejected for escaping the *content root* above that -- a legitimate sibling reference like
     * `"../textures/foo"` from `"effects/myeffect"` climbs out of `effects/` by design, so it uses
     * IsDisallowedAbsolutePath() directly plus its own existing root-escape check instead of this
     * function.
     *
     * @param baseDir            The directory @p relativeOrAbsolute must resolve within.
     * @param relativeOrAbsolute Caller-/file-supplied path, untrusted.
     * @param canonicalize       When true (the default), both @p baseDir and the joined result are
     *                           additionally resolved via `std::filesystem::weakly_canonical` for
     *                           the containment *check* only, so a real, existing symlink under
     *                           @p baseDir that points outside it is also caught. When false, the
     *                           check is purely lexical (no filesystem access at all). Either way,
     *                           the returned @c resolvedPath is always the lexically-normalized
     *                           join (never the canonicalized form) -- callers that key other data
     *                           structures by this exact string (e.g. `MediaLibrary`'s song-path
     *                           lookup, fed by `PlaylistParser`) need it to match the same,
     *                           non-canonicalized form produced elsewhere, not an OS-resolved
     *                           absolute path.
     * @return `{true, resolvedPath}` if contained; `{false, {}}` if @p relativeOrAbsolute is
     *         empty, absolute, resolves to exactly @p baseDir itself (never a valid target for
     *         either caller above), or escapes @p baseDir (lexically, or -- with @p canonicalize --
     *         via symlink resolution too).
     */
    inline ContainedPathResult ResolveContainedPath(const std::string& baseDir,
                                                      const std::string& relativeOrAbsolute,
                                                      bool canonicalize = true)
    {
        namespace fs = std::filesystem;

        if (relativeOrAbsolute.empty())
        {
            return {};
        }

        std::string normalized = relativeOrAbsolute;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');

        if (IsDisallowedAbsolutePath(normalized))
        {
            return {};
        }

        const fs::path base = baseDir.empty() ? fs::path(".") : fs::path(baseDir);
        const fs::path lexicalJoined = (base / fs::path(normalized)).lexically_normal();

        fs::path checkedBase = base;
        fs::path checkedJoined = lexicalJoined;
        if (canonicalize)
        {
            std::error_code baseEc;
            std::error_code joinedEc;
            checkedBase = fs::weakly_canonical(base, baseEc);
            checkedJoined = fs::weakly_canonical(lexicalJoined, joinedEc);
            if (baseEc || joinedEc)
            {
                return {};
            }
        }

        // lexically_relative(), not relative(): the latter always canonicalizes both arguments
        // internally regardless of what this function already did above, which would silently
        // force real filesystem access even when canonicalize=false was requested. Both operands
        // are already in comparable form by this point (either both lexically normalized only,
        // or both already weakly_canonical'd above), so a pure lexical comparison is correct and
        // sufficient here.
        const fs::path rel = checkedJoined.lexically_relative(checkedBase);
        if (rel.empty() || rel == "." || *rel.begin() == "..")
        {
            return {};
        }

        return {true, lexicalJoined.string()};
    }
}
