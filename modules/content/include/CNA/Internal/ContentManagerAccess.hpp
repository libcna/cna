// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

namespace Microsoft::Xna::Framework::Content
{
    class ContentManager;
}

namespace CNA::Internal
{
    /** @brief Internal path access for the C adapter and built-in media loaders. */
    struct ContentManagerAccess
    {
        /**
         * @brief Builds an asset path using the manager's content root.
         * @param manager Content manager whose path policy applies.
         * @param assetName Logical asset name.
         * @return The manager's asset path.
         */
        [[nodiscard]] static std::string BuildAssetPath(
            const Microsoft::Xna::Framework::Content::ContentManager& manager, const std::string& assetName);

        /**
         * @brief Applies the manager's existing-file path resolution.
         * @param manager Content manager whose path policy applies.
         * @param path Candidate asset path.
         * @return The resolved path.
         */
        [[nodiscard]] static std::string ResolveExistingAssetPath(
            const Microsoft::Xna::Framework::Content::ContentManager& manager, const std::string& path);

        /**
         * @brief Normalizes a logical asset name using the manager's cache-key policy.
         * @param manager Content manager whose key policy applies.
         * @param assetName Logical asset name.
         * @return The normalized cache key.
         */
        [[nodiscard]] static std::string NormalizeKey(
            const Microsoft::Xna::Framework::Content::ContentManager& manager, const std::string& assetName);
    };
} // namespace CNA::Internal
