// SPDX-License-Identifier: MS-PL
#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace CNA::Internal::GamerServices {
/**
 * @brief The per-user disk cache of immutable service assets, named by their SHA-256.
 *
 * Every read is verified against the name, so a damaged entry is a miss (and is removed), never
 * data. Writes go to a temporary file renamed into place, so an interrupted write leaves no entry;
 * temporaries older than StaleTemporaryAge are removed by later writes. The cache keeps at most its
 * capacity: a write removes the least recently used entries (a read counts as a use) to make room.
 * Several processes may share one cache; each sees whole entries or none.
 */
class AssetDiskCache {
public:
    /** @brief Default most bytes kept. */
    static constexpr std::uintmax_t DefaultCapacity = std::uintmax_t{256} << 20;
    /** @brief Largest entry kept. */
    static constexpr std::uintmax_t MaximumEntry = std::uintmax_t{16} << 20;
    /** @brief Age after which a temporary is taken to be an interrupted write. */
    static constexpr std::chrono::minutes StaleTemporaryAge{10};

    /**
     * @brief Opens a cache directory (created on the first write).
     * @param root Directory; empty for no cache (every read misses, writes store nothing).
     * @param writer Tag naming this writer's temporaries, unique per process.
     * @param capacity Most bytes kept.
     */
    AssetDiskCache(std::filesystem::path root, std::string writer, std::uintmax_t capacity = DefaultCapacity);

    /** @brief The configured location: `CNA_GAMER_SERVICES_CACHE_DIR`, else
     * `$XDG_CACHE_HOME/cna/gamer-services/assets`, else `$HOME/.cache/cna/gamer-services/assets`.
     * @return Directory, or empty when none is configured. */
    static std::filesystem::path defaultRoot();

    /** @brief Reads an entry, verified, and marks it used. @param hash Lower-case hex SHA-256.
     * @return Contents, or empty on a miss (absent, damaged, a link, or too large). */
    std::optional<std::vector<unsigned char>> read(const std::string& hash) const;

    /** @brief Stores contents under their hash. @param hash Lower-case hex SHA-256 of bytes.
     * @param bytes Verified contents. @return Whether the entry is now stored. */
    bool write(const std::string& hash, const std::vector<unsigned char>& bytes) const;

private:
    std::filesystem::path root_;
    std::string writer_;
    std::uintmax_t capacity_;
};
}
