// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <string>
#include <system_error>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Internal
{
    /**
     * @brief Result of safely resolving a `.cnj` envelope's `"sourceFile"` field.
     */
    struct CnjSourceFileResult
    {
        /** @brief Root-relative logical asset name, suitable for `ContentManager::Load<T>()`. */
        std::string logicalName;

        /** @brief The canonical filesystem path `sourceFile` resolves to, for diagnostics/tests. */
        std::string resolvedNativePath;
    };

    /**
     * @brief Resolves a `.cnj` envelope's `"sourceFile"` field to a safe, root-relative logical
     *        asset name (plans/plan_cnj.md CNB-32).
     *
     * `sourceFile` is always resolved relative to the referring `.cnj` file's own directory (not
     * the content root), matching `cnj.md`'s design and `SkinnedModelTypeReader`'s pre-existing
     * manifest-relative convention. This function additionally enforces the safety invariants a
     * `.cnj` sidecar reference must never violate:
     *
     * - `sourceFile` must not be an absolute path.
     * - The resolved target must not escape @p rootDirectory, including via `..` segments or a
     *   symlink.
     * - The resolved target must not itself be a `.cnj` file (no chained sidecars), whether
     *   `sourceFile` names one explicitly (`"foo.cnj"`) or implicitly (`"foo"`, where the normal
     *   resolver would pick a sibling `"foo.cnj"` over any other candidate) -- the latter also
     *   closes a self-referential cycle where a `.cnj`'s own `sourceFile` resolves back to
     *   itself.
     *
     * @param referringCnjPath Resolved filesystem path of the `.cnj` document containing
     *                         `sourceFile` (used both to compute the relative base directory and
     *                         to name the offending file in error messages).
     * @param rootDirectory    The owning `ContentManager`'s root directory
     *                         (`getRootDirectoryProperty()`).
     * @param sourceFile       The raw `"sourceFile"` field value from the `.cnj` envelope.
     * @return The safely-resolved logical name and canonical native path.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if `sourceFile` is empty,
     *         absolute, escapes @p rootDirectory, or resolves to another `.cnj` file.
     */
    inline CnjSourceFileResult ResolveCnjSourceFileSafely(const std::string& referringCnjPath,
                                                           const std::string& rootDirectory,
                                                           const std::string& sourceFile)
    {
        namespace fs = std::filesystem;
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        if (sourceFile.empty())
        {
            throw ContentLoadException(
                "ContentManager: '" + referringCnjPath + "' has an empty 'sourceFile' field.");
        }

        const fs::path sourceFilePath(sourceFile);
        if (sourceFilePath.is_absolute())
        {
            throw ContentLoadException(
                "ContentManager: '" + referringCnjPath + "' has an absolute 'sourceFile' path ('" +
                sourceFile + "'), which is not allowed.");
        }

        const fs::path referringDir = fs::path(referringCnjPath).parent_path();
        const fs::path joined = (referringDir / sourceFilePath).lexically_normal();

        const fs::path rootBase = rootDirectory.empty() ? fs::current_path() : fs::path(rootDirectory);

        std::error_code rootEc;
        std::error_code joinedEc;
        const fs::path canonicalRoot = fs::weakly_canonical(rootBase, rootEc);
        const fs::path canonicalJoined = fs::weakly_canonical(joined, joinedEc);

        if (rootEc || joinedEc)
        {
            throw ContentLoadException(
                "ContentManager: '" + referringCnjPath + "' has an unresolvable 'sourceFile' ('" +
                sourceFile + "').");
        }

        auto rootIt = canonicalRoot.begin();
        auto joinedIt = canonicalJoined.begin();
        bool withinRoot = true;
        for (; rootIt != canonicalRoot.end(); ++rootIt, ++joinedIt)
        {
            if (joinedIt == canonicalJoined.end() || *joinedIt != *rootIt)
            {
                withinRoot = false;
                break;
            }
        }

        if (!withinRoot)
        {
            throw ContentLoadException(
                "ContentManager: '" + referringCnjPath + "' has a 'sourceFile' ('" + sourceFile +
                "') that resolves outside the content root, which is not allowed.");
        }

        if (canonicalJoined.extension() == ".cnj")
        {
            throw ContentLoadException(
                "ContentManager: '" + referringCnjPath + "' has a 'sourceFile' ('" + sourceFile +
                "') that names another .cnj file; sourceFile chaining is not allowed.");
        }

        fs::path cnjSibling = canonicalJoined;
        cnjSibling += ".cnj";
        std::error_code siblingEc;
        if (fs::exists(cnjSibling, siblingEc) && !siblingEc)
        {
            throw ContentLoadException(
                "ContentManager: '" + referringCnjPath + "' has a 'sourceFile' ('" + sourceFile +
                "') that would resolve to a .cnj file ('" + cnjSibling.string() +
                "') via the normal resolver; sourceFile chaining is not allowed.");
        }

        std::error_code relativeEc;
        fs::path relative = fs::relative(joined, rootBase, relativeEc);
        if (relativeEc || relative.empty())
        {
            relative = joined;
        }

        CnjSourceFileResult result;
        result.logicalName = relative.lexically_normal().string();
        result.resolvedNativePath = canonicalJoined.string();
        return result;
    }
}
