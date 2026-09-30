// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/AssetDiskCache.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace CNA::Internal::GamerServices {
namespace {
bool isHash(const std::string& name)
{
    return name.size() == 64 && name.find_first_not_of("0123456789abcdef") == std::string::npos;
}

std::string hashOf(const std::vector<unsigned char>& bytes)
{
    return Avatars::sha256Hex(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}
}

AssetDiskCache::AssetDiskCache(std::filesystem::path root, std::string writer, std::uintmax_t capacity)
    : root_(std::move(root)), writer_(std::move(writer)), capacity_(capacity)
{
}

std::filesystem::path AssetDiskCache::defaultRoot()
{
    if (const auto* configured = std::getenv("CNA_GAMER_SERVICES_CACHE_DIR"); configured && *configured)
        return configured;
    if (const auto* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "cna/gamer-services/assets";
    if (const auto* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".cache/cna/gamer-services/assets";
    return {};
}

std::optional<std::vector<unsigned char>> AssetDiskCache::read(const std::string& hash) const
{
    if (root_.empty() || !isHash(hash))
        return std::nullopt;
    const auto path = root_ / hash;
    std::error_code error;
    if (std::filesystem::is_symlink(path, error) || !std::filesystem::is_regular_file(path, error))
        return std::nullopt;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > MaximumEntry)
        return std::nullopt;
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) || hashOf(bytes) != hash)
    {
        // Damaged (or replaced under us by something that is not the asset): drop it.
        stream.close();
        std::filesystem::remove(path, error);
        return std::nullopt;
    }
    // A read is a use: eviction removes the entries unused the longest.
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), error);
    return bytes;
}

bool AssetDiskCache::write(const std::string& hash, const std::vector<unsigned char>& bytes) const
{
    if (root_.empty() || !isHash(hash) || bytes.empty() || bytes.size() > MaximumEntry || bytes.size() > capacity_)
        return false;
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error)
        return false;

    struct Entry
    {
        std::filesystem::path path;
        std::uintmax_t size;
        std::filesystem::file_time_type used;
    };
    std::vector<Entry> entries;
    std::uintmax_t total = 0;
    const auto now = std::filesystem::file_time_type::clock::now();
    for (std::filesystem::directory_iterator entry(root_, error), end; !error && entry != end; entry.increment(error))
    {
        std::error_code itemError;
        const auto name = entry->path().filename().string();
        if (entry->is_symlink(itemError) || !entry->is_regular_file(itemError))
            continue;
        const auto modified = entry->last_write_time(itemError);
        if (itemError)
            continue;
        if (name.ends_with(".tmp") && isHash(name.substr(0, std::min<std::size_t>(64, name.size()))))
        {
            // Another writer's temporary lives for milliseconds; an old one is an interrupted write.
            if (now - modified > StaleTemporaryAge)
                std::filesystem::remove(entry->path(), itemError);
            continue;
        }
        if (!isHash(name) || name == hash)
            continue;
        const auto size = entry->file_size(itemError);
        if (itemError)
            continue;
        entries.push_back({entry->path(), size, modified});
        total += size;
    }
    if (error)
        return false;
    if (total + bytes.size() > capacity_)
    {
        std::ranges::sort(entries, {}, &Entry::used);
        for (const auto& victim : entries)
        {
            if (total + bytes.size() <= capacity_)
                break;
            std::error_code removeError;
            if (std::filesystem::remove(victim.path, removeError))
                total -= victim.size;
        }
        if (total + bytes.size() > capacity_)
            return false;
    }

    const auto temporary = root_ / (hash + "." + writer_ + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output)
        {
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, root_ / hash, error);
    if (error)
    {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}
}
