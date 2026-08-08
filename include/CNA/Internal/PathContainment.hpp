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

    /** @brief Result of the shared path-containment helpers. */
    struct ContainedPathResult
    {
        /** @brief True if the input resolved within the given base directory. */
        bool ok = false;
        /** @brief The resolved path, valid only when @c ok is true. */
        std::string resolvedPath;
    };

    /**
     * @brief Verifies that an already-constructed @p candidate path is a child of @p rootDir.
     *
     * This is the component-aware check shared by ResolveContainedPath() and
     * ResolveContainedPathFromBase(). It deliberately compares path components after lexical
     * normalization instead of using a string prefix, so a sibling such as `content-evil` is not
     * considered a child of `content`.
     *
     * @param rootDir      The authorized root directory.
     * @param candidate    An already-constructed path to validate. Unlike untrusted input to the
     *                     resolving helpers, this may be absolute.
     * @param canonicalize When true (the default), additionally use `weakly_canonical` for the
     *                     check so existing symlink components cannot redirect outside the root.
     * @return `{true, normalizedCandidate}` when @p candidate is a non-root child of @p rootDir;
     *         `{false, {}}` otherwise.
     */
    inline ContainedPathResult ValidateContainedPath(const std::string& rootDir,
                                                       const std::string& candidate,
                                                       bool canonicalize = true)
    {
        namespace fs = std::filesystem;

        if (candidate.empty())
        {
            return {};
        }

        const fs::path lexicalRoot =
            (rootDir.empty() ? fs::path(".") : fs::path(rootDir)).lexically_normal();
        const fs::path lexicalCandidate = fs::path(candidate).lexically_normal();

        fs::path checkedRoot = lexicalRoot;
        fs::path checkedCandidate = lexicalCandidate;
        if (canonicalize)
        {
            std::error_code rootEc;
            std::error_code candidateEc;
            checkedRoot = fs::weakly_canonical(lexicalRoot, rootEc);
            checkedCandidate = fs::weakly_canonical(lexicalCandidate, candidateEc);
            if (rootEc || candidateEc)
            {
                return {};
            }
        }

        // lexically_relative(), not relative(): the latter always canonicalizes both arguments
        // internally regardless of what this function already did above, which would silently
        // force real filesystem access even when canonicalize=false was requested. Both operands
        // are already in comparable form by this point (either both lexically normalized only,
        // or both already weakly_canonical'd above), so a pure component comparison is correct.
        const fs::path rel = checkedCandidate.lexically_relative(checkedRoot);
        if (rel.empty() || rel == "." || *rel.begin() == "..")
        {
            return {};
        }

        return {true, lexicalCandidate.string()};
    }

    /**
     * @brief Joins @p relativeOrAbsolute onto @p baseDir and verifies the result stays within
     *        @p rootDir.
     *
     * This variant is for references whose join base is below their authorization root, such as
     * a media filename relative to an XNB file's directory while still confined to the enclosing
     * ContentManager root.
     *
     * @param rootDir            The directory the resolved path must stay within.
     * @param baseDir            The directory @p relativeOrAbsolute is relative to.
     * @param relativeOrAbsolute Caller-/file-supplied path, untrusted and required to be relative.
     * @param canonicalize       When true (the default), existing symlink components are resolved
     *                           for the containment check only.
     * @return `{true, resolvedPath}` if contained; `{false, {}}` for empty, absolute/rooted,
     *         root-equal, or escaping input.
     */
    inline ContainedPathResult ResolveContainedPathFromBase(
        const std::string& rootDir, const std::string& baseDir,
        const std::string& relativeOrAbsolute, bool canonicalize = true)
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
        return ValidateContainedPath(rootDir, lexicalJoined.string(), canonicalize);
    }

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
        return ResolveContainedPathFromBase(
            baseDir, baseDir, relativeOrAbsolute, canonicalize);
    }

    /**
     * @brief Resolves an untrusted path relative to the directory containing @p referringFile.
     *
     * A referring file lexically inside @p contentRoot is confined to that content root, allowing
     * legitimate `..` segments that merely move between directories within it. A referring file
     * loaded through an explicit outside-root API is instead confined to its own containing
     * directory, preserving supported external bundles without granting their embedded paths
     * access to arbitrary sibling directories. Canonical checking of the resolved candidate is
     * still enabled by default, so a lexically in-root symlink is not reclassified as an explicit
     * external bundle.
     *
     * @param contentRoot        The ContentManager root.
     * @param referringFile      The XNB/manifest path that contains the untrusted reference.
     * @param relativeOrAbsolute The embedded path, required to be relative and non-empty.
     * @param canonicalize       When true (the default), existing symlink components are resolved
     *                           for the containment check only.
     * @return The same result shape as ResolveContainedPathFromBase().
     */
    inline ContainedPathResult ResolveContainedPathRelativeToFile(
        const std::string& contentRoot, const std::string& referringFile,
        const std::string& relativeOrAbsolute, bool canonicalize = true)
    {
        namespace fs = std::filesystem;

        if (referringFile.empty())
        {
            return {};
        }

        const fs::path referringPath = fs::path(referringFile).lexically_normal();
        const fs::path baseDir = referringPath.parent_path().empty()
                                     ? fs::path(".")
                                     : referringPath.parent_path();
        const bool referringFileIsLexicallyInRoot =
            ValidateContainedPath(contentRoot, referringPath.string(), false).ok;
        const fs::path authorizedRoot = referringFileIsLexicallyInRoot
                                            ? (contentRoot.empty() ? fs::path(".")
                                                                   : fs::path(contentRoot))
                                            : baseDir;

        return ResolveContainedPathFromBase(
            authorizedRoot.string(), baseDir.string(), relativeOrAbsolute, canonicalize);
    }
}
