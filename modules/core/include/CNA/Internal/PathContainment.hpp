// SPDX-License-Identifier: MS-PL
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/PathUtf8.hpp"

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
    inline bool IsDisallowedAbsolutePath(std::string_view normalized)
    {
        // is_absolute() is not enough, and on Windows it is not even close. There, a path that
        // begins with a separator has a root-directory but no root-name, which the standard calls
        // relative -- so `"/etc/passwd"`, the canonical shape of this attack, answers false. Worse,
        // `operator/` still treats such a path as rooted and discards everything but the base's
        // drive, so the join lands wherever the string says. This check is therefore made on the
        // string, which has already had its backslashes normalized to '/' by every caller: anything
        // starting with a separator is rooted on every platform, whatever is_absolute() thinks.
        //
        // The string checks subsume is_absolute() on both platforms, so no path is constructed
        // here at all: on POSIX is_absolute() means exactly "starts with '/'", and on Windows every
        // absolute spelling either starts with a separator (UNC) or with a drive letter. Not
        // building one matters because this function takes untrusted text, and constructing a path
        // from text that is not valid UTF-8 throws on Windows -- a lookup that answers a question
        // must not become an exception.
        const bool looksRooted = !normalized.empty() && normalized[0] == '/';
        const bool looksLikeDriveLetter =
            normalized.size() >= 2 &&
            std::isalpha(static_cast<unsigned char>(normalized[0])) != 0 &&
            normalized[1] == ':';
        return looksRooted || looksLikeDriveLetter;
    }

    /**
     * @brief Reports whether a native path is rooted, in the sense every platform agrees on.
     *
     * `path::is_absolute()` is not that test on Windows, and the difference is not cosmetic. There
     * a path is absolute only when it has **both** a root name and a root directory, so
     * `"/etc/passwd"` -- which has a root directory and no drive -- answers **false**, and so does
     * the drive-relative `"C:asset.png"`, which has a root name and no root directory. Both are
     * nonetheless rooted enough for `operator/` to discard most of the left-hand base, which is
     * exactly what a containment check is trying to prevent.
     *
     * Asking for all three catches every form **this platform** treats as rooted, which the
     * `is_absolute()` subset does not. It is deliberately *not* a cross-platform answer and cannot
     * be one: `path` parses according to the platform it was compiled for, so `"C:/Windows"` is
     * rooted on Windows and an ordinary relative name on POSIX. That is the correct answer to the
     * question this asks -- "will `operator/` discard my base here?" -- and it is why untrusted
     * *text*, which may have been authored on the other platform, is checked with
     * IsDisallowedAbsolutePath() instead, on the string, before any path is built.
     *
     * @param path The native path to inspect.
     * @return True when the path is absolute, has a root name, or has a root directory.
     */
    [[nodiscard]] inline bool IsRootedPath(const std::filesystem::path& path)
    {
        return path.is_absolute() || path.has_root_name() || path.has_root_directory();
    }

    /** @brief Result of the shared path-containment helpers. */
    struct ContainedPathResult
    {
        /** @brief True if the input resolved within the given base directory. */
        bool ok = false;
        /** @brief The resolved path, valid only when @c ok is true. */
        std::string resolvedPath;
    };

    /** @brief Native-filesystem counterpart of ContainedPathResult. */
    struct ContainedNativePathResult
    {
        /** @brief True if the input resolved within the given base directory. */
        bool ok = false;
        /** @brief The resolved native path, valid only when @c ok is true. */
        std::filesystem::path resolvedPath;
    };

    /**
     * @brief Verifies that an already-constructed native path is a child of a native root.
     *
     * @param rootDir      The authorized native root directory.
     * @param candidate    An already-constructed native path to validate.
     * @param canonicalize When true, resolve existing symlink components for the check.
     * @return The contained lexical path, or an empty failure result.
     */
    inline ContainedNativePathResult ValidateContainedNativePath(
        const std::filesystem::path& rootDir, const std::filesystem::path& candidate,
        bool canonicalize = true)
    {
        namespace fs = std::filesystem;

        if (candidate.empty()) { return {}; }
        const fs::path lexicalRoot =
            (rootDir.empty() ? fs::path(".") : rootDir).lexically_normal();
        const fs::path lexicalCandidate = candidate.lexically_normal();

        fs::path checkedRoot = lexicalRoot;
        fs::path checkedCandidate = lexicalCandidate;
        if (canonicalize)
        {
            std::error_code rootError;
            std::error_code candidateError;
            checkedRoot = fs::weakly_canonical(lexicalRoot, rootError);
            checkedCandidate = fs::weakly_canonical(lexicalCandidate, candidateError);
            if (rootError || candidateError) { return {}; }
        }

        const fs::path relative = checkedCandidate.lexically_relative(checkedRoot);
        if (relative.empty() || relative == "." || *relative.begin() == "..") { return {}; }
        return {true, lexicalCandidate};
    }

    /**
     * @brief Joins untrusted generic UTF-8 path text onto a native base and confines it to a
     *        native root.
     *
     * The native core every helper below is built from: it performs exactly one conversion, of the
     * untrusted relative text, and never narrows a path it already holds.
     *
     * @param rootDir      The native directory the resolved path must stay within.
     * @param baseDir      The native directory @p relativeUtf8 is relative to.
     * @param relativeUtf8 Untrusted relative path encoded as generic UTF-8.
     * @param canonicalize When true, resolve existing symlink components for the check.
     * @return The contained native path, or an empty failure result.
     */
    inline ContainedNativePathResult ResolveContainedNativePathFromBase(
        const std::filesystem::path& rootDir, const std::filesystem::path& baseDir,
        std::string_view relativeUtf8, bool canonicalize = true)
    {
        namespace fs = std::filesystem;

        std::string normalized(relativeUtf8);
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        if (normalized.empty() || IsDisallowedAbsolutePath(normalized)) { return {}; }

        // Untrusted text: where it cannot name a path on this platform, it is not contained --
        // rather than an exception escaping a containment check.
        const std::optional<fs::path> relative = TryPathFromUtf8(normalized);
        if (!relative) { return {}; }

        const fs::path base = baseDir.empty() ? fs::path(".") : baseDir;
        const fs::path root = rootDir.empty() ? fs::path(".") : rootDir;
        return ValidateContainedNativePath(root, (base / *relative).lexically_normal(),
                                           canonicalize);
    }

    /**
     * @brief Resolves authored generic UTF-8 path text below a native filesystem root.
     *
     * Cross-platform absolute spellings, lexical traversal, and symlink escapes are rejected
     * before the returned path can be opened.
     *
     * @param rootDir      The authorized native root directory and join base.
     * @param relativeUtf8 Untrusted relative path encoded as generic UTF-8.
     * @param canonicalize When true, resolve existing symlink components for the check.
     * @return The contained native path, or an empty failure result.
     */
    inline ContainedNativePathResult ResolveContainedUtf8Path(
        const std::filesystem::path& rootDir, std::string_view relativeUtf8,
        bool canonicalize = true)
    {
        const std::filesystem::path root =
            rootDir.empty() ? std::filesystem::path(".") : rootDir;
        return ResolveContainedNativePathFromBase(root, root, relativeUtf8, canonicalize);
    }

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

        // Both strings are UTF-8 (docs/filesystem-path-model.md rule 2), so they are widened with
        // PathFromUtf8 rather than the narrow path constructor, which on Windows would read them
        // as ANSI code page bytes. Untrusted text that cannot name a path here is not contained.
        const std::optional<fs::path> candidatePath = TryPathFromUtf8(candidate);
        if (!candidatePath) { return {}; }
        const std::optional<fs::path> rootPath =
            rootDir.empty() ? std::optional<fs::path>(fs::path(".")) : TryPathFromUtf8(rootDir);
        if (!rootPath) { return {}; }

        const ContainedNativePathResult result =
            ValidateContainedNativePath(*rootPath, *candidatePath, canonicalize);
        // PathToGenericUtf8, not string(): lexically_normal() rewrites separators to the platform's
        // preferred one, so on Windows this returned "\base\dir\a.png" where every other producer
        // of the same key spells it with '/'. The documented contract just below -- that callers
        // key data structures by this exact string and need it to match forms produced elsewhere --
        // silently held only where preferred_separator is already '/'. MediaLibrary's song lookup,
        // fed by PlaylistParser, is the case that missed every time on Windows.
        return result.ok ? ContainedPathResult{true, PathToGenericUtf8(result.resolvedPath)}
                         : ContainedPathResult{};
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

        // One conversion per string, straight into the native core. This used to narrow the joined
        // path back to text only to have ValidateContainedPath re-parse it -- four ANSI code page
        // round trips per call on Windows, each of them able to throw.
        const std::optional<fs::path> base =
            baseDir.empty() ? std::optional<fs::path>(fs::path(".")) : TryPathFromUtf8(baseDir);
        if (!base) { return {}; }
        const std::optional<fs::path> root =
            rootDir.empty() ? std::optional<fs::path>(fs::path(".")) : TryPathFromUtf8(rootDir);
        if (!root) { return {}; }

        const ContainedNativePathResult result =
            ResolveContainedNativePathFromBase(*root, *base, relativeOrAbsolute, canonicalize);
        return result.ok ? ContainedPathResult{true, PathToGenericUtf8(result.resolvedPath)}
                         : ContainedPathResult{};
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

        const std::optional<fs::path> referring = TryPathFromUtf8(referringFile);
        if (!referring) { return {}; }
        const fs::path referringPath = referring->lexically_normal();
        const fs::path baseDir = referringPath.parent_path().empty()
                                     ? fs::path(".")
                                     : referringPath.parent_path();

        const std::optional<fs::path> contentRootPath =
            contentRoot.empty() ? std::optional<fs::path>(fs::path(".")) : TryPathFromUtf8(contentRoot);
        if (!contentRootPath) { return {}; }

        // Both of these were narrowed back to text and re-parsed before; the paths are already in
        // hand, so the native core takes them directly.
        const bool referringFileIsLexicallyInRoot =
            ValidateContainedNativePath(*contentRootPath, referringPath, false).ok;
        const fs::path authorizedRoot =
            referringFileIsLexicallyInRoot ? *contentRootPath : baseDir;

        const ContainedNativePathResult result = ResolveContainedNativePathFromBase(
            authorizedRoot, baseDir, relativeOrAbsolute, canonicalize);
        return result.ok ? ContainedPathResult{true, PathToGenericUtf8(result.resolvedPath)}
                         : ContainedPathResult{};
    }
}
