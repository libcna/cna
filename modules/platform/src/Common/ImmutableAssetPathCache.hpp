// SPDX-License-Identifier: MS-PL
#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Platform::Common {

    /**
     * @brief Private case-insensitive path resolver for one immutable packaged asset namespace.
     *
     * Own one cache per filesystem service. It retains only canonical directories reached through
     * the package's own listings, never arbitrary requested misses or file contents. Storage is
     * bounded by the package's names and canonical directory paths, not by the number of lookups.
     * Destruction releases everything; mutable host storage must never use this cache.
     */
    class ImmutableAssetPathCache final
    {
    public:
        using Listing = std::optional<std::vector<std::string>>;
        // List immediate child basenames; nullopt means an operational failure, not an empty dir.
        // The callback must not reenter this cache. It is invoked under the cache mutex.
        using ListDirectory = std::function<Listing(const std::string&)>;

        /**
         * @brief Resolves relative slash-separated components without ASCII case.
         * @param requested The packaged path; dot/empty components are ignored, parents refused.
         * @param listDirectory Lists a canonical directory (empty string for the root).
         * @return The uniquely matched canonical spelling, or empty on absence/ambiguity/failure.
         */
        [[nodiscard]] std::string Resolve(
            const std::string& requested, const ListDirectory& listDirectory);

    private:
        // Empty value marks an ambiguous folded basename, never a selectable entry.
        using Directory = std::unordered_map<std::string, std::string>;
        std::mutex mutex_;
        std::unordered_map<std::string, Directory> directories_;
    };

} // namespace CNA::Platform::Common
