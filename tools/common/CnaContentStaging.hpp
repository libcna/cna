// SPDX-License-Identifier: MS-PL
#pragma once

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "CnaToolAtomicWrite.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace CNA::Tools
{
    inline constexpr const char* ContentStagingDirectoryPrefix =
        "cna_content_stage_v1_";
    inline constexpr const char* ContentStagingMetadataFile = "owner.cna-stage";
    inline constexpr const char* ContentStagingLeaseFile = "lease.cna-stage";
    /** @brief Persistent per-output-root lease file shared by build and clean operations. */
    inline constexpr const char* ContentOutputLeaseFile = ".cna-content.lock";
    inline constexpr std::int64_t ContentStagingMinimumAgeSeconds = 24 * 60 * 60;
    inline constexpr std::size_t ContentStagingMaximumScannedEntries = 4096u;
    inline constexpr std::size_t ContentStagingMaximumCandidates = 256u;

    struct ContentStagingScavengeResult
    {
        std::size_t scannedEntries = 0u;
        std::size_t matchingCandidates = 0u;
        std::size_t removedDirectories = 0u;
        std::size_t activeDirectories = 0u;
        std::size_t recentDirectories = 0u;
        std::size_t conservativeSkips = 0u;
        bool scanLimitReached = false;
        std::vector<std::string> diagnostics;
    };

    namespace ContentStagingDetail
    {
        struct CandidateIdentity
        {
            std::string pid;
            std::string token;
            std::string attempt;
        };

        inline bool IsDecimal(const std::string& value)
        {
            return !value.empty() && std::all_of(
                value.begin(), value.end(),
                [](const unsigned char character) { return character >= '0' && character <= '9'; });
        }

        inline bool IsLowerHex(const std::string& value)
        {
            return value.size() == 16u && std::all_of(
                value.begin(), value.end(), [](const unsigned char character)
                {
                    return (character >= '0' && character <= '9') ||
                           (character >= 'a' && character <= 'f');
                });
        }

        inline bool ParseCandidateName(const std::string& name, CandidateIdentity& identity)
        {
            const std::string prefix = ContentStagingDirectoryPrefix;
            if (!name.starts_with(prefix)) { return false; }
            const std::string fields = name.substr(prefix.size());
            const std::size_t first = fields.find('_');
            const std::size_t second = first == std::string::npos
                                           ? std::string::npos
                                           : fields.find('_', first + 1u);
            if (first == std::string::npos || second == std::string::npos ||
                fields.find('_', second + 1u) != std::string::npos)
            {
                return false;
            }
            identity.pid = fields.substr(0u, first);
            identity.token = fields.substr(first + 1u, second - first - 1u);
            identity.attempt = fields.substr(second + 1u);
            return IsDecimal(identity.pid) && IsLowerHex(identity.token) &&
                   IsDecimal(identity.attempt);
        }

        inline std::int64_t CurrentUnixSeconds()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        inline std::string MakeToken(const void* owner)
        {
            const std::uint64_t systemTicks = static_cast<std::uint64_t>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
            const std::uint64_t steadyTicks = static_cast<std::uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count());
            const std::uint64_t address = static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(owner));
            std::ostringstream token;
            token << std::hex << std::setfill('0') << std::setw(16)
                  << (systemTicks ^ (steadyTicks << 1u) ^ address);
            return token.str();
        }

        inline std::string MetadataText(
            const std::string& directory, const CandidateIdentity& identity,
            const std::int64_t createdUnixSeconds)
        {
            return "format=CNA.ContentPipeline.Staging\nversion=1\ndirectory=" + directory +
                   "\npid=" + identity.pid + "\ntoken=" + identity.token +
                   "\ncreatedUnixSeconds=" + std::to_string(createdUnixSeconds) + "\n";
        }

        inline bool ReadMetadata(
            const std::filesystem::path& candidate, const std::string& directory,
            const CandidateIdentity& identity, std::int64_t& createdUnixSeconds,
            std::string& reason)
        {
            std::error_code error;
            const std::filesystem::path path = candidate / ContentStagingMetadataFile;
            const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
            if (error || !std::filesystem::is_regular_file(status) ||
                std::filesystem::is_symlink(status))
            {
                reason = "owner metadata is absent or is not a regular file";
                return false;
            }
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if (error || size > 1024u)
            {
                reason = "owner metadata is unreadable or oversized";
                return false;
            }
            std::ifstream input(path, std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>()};
            if (!input || text.size() != size)
            {
                reason = "owner metadata cannot be read completely";
                return false;
            }
            const std::string marker =
                "format=CNA.ContentPipeline.Staging\nversion=1\ndirectory=" + directory +
                "\npid=" + identity.pid + "\ntoken=" + identity.token +
                "\ncreatedUnixSeconds=";
            if (!text.starts_with(marker) || text.empty() || text.back() != '\n')
            {
                reason = "owner metadata does not match the staging directory identity";
                return false;
            }
            const std::string seconds =
                text.substr(marker.size(), text.size() - marker.size() - 1u);
            const auto [end, conversion] = std::from_chars(
                seconds.data(), seconds.data() + seconds.size(), createdUnixSeconds, 10);
            if (seconds.empty() || conversion != std::errc{} ||
                end != seconds.data() + seconds.size() || createdUnixSeconds < 0)
            {
                reason = "owner metadata has an invalid creation timestamp";
                return false;
            }
            return true;
        }

        inline bool OwnedByCurrentUser(const std::filesystem::path& path)
        {
#if defined(_WIN32)
            (void)path;
            // Windows temporary directories are per-user and ACL-protected. The marker, exact
            // direct-child name, age threshold and lease remain mandatory there.
            return true;
#else
            struct stat info{};
            return ::lstat(path.c_str(), &info) == 0 && info.st_uid == ::geteuid();
#endif
        }

        class LeaseHandle
        {
        public:
            LeaseHandle() = default;
            ~LeaseHandle() { Close(); }
            LeaseHandle(const LeaseHandle&) = delete;
            LeaseHandle& operator=(const LeaseHandle&) = delete;

            bool CreateAndHold(const std::filesystem::path& path, std::string& reason)
            {
#if defined(_WIN32)
                handle_ = ::CreateFileW(
                    path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_HIDDEN, nullptr);
                if (handle_ == INVALID_HANDLE_VALUE)
                {
                    reason = "cannot create the staging lease";
                    return false;
                }
                return true;
#else
                int flags = O_RDWR | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
                flags |= O_NOFOLLOW;
#endif
                handle_ = ::open(path.c_str(), flags, 0600);
                if (handle_ < 0 || ::flock(handle_, LOCK_EX | LOCK_NB) != 0)
                {
                    reason = "cannot create and lock the staging lease";
                    Close();
                    return false;
                }
                return true;
#endif
            }

            enum class ClaimResult
            {
                Claimed,
                Active,
                Invalid,
            };

            ClaimResult ClaimExisting(const std::filesystem::path& path, std::string& reason)
            {
                std::error_code error;
                const std::filesystem::file_status status =
                    std::filesystem::symlink_status(path, error);
                if (error || !std::filesystem::is_regular_file(status) ||
                    std::filesystem::is_symlink(status))
                {
                    reason = "lease is absent or is not a regular file";
                    return ClaimResult::Invalid;
                }
#if defined(_WIN32)
                handle_ = ::CreateFileW(
                    path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (handle_ == INVALID_HANDLE_VALUE)
                {
                    const DWORD code = ::GetLastError();
                    if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION)
                    {
                        return ClaimResult::Active;
                    }
                    reason = "lease cannot be opened safely";
                    return ClaimResult::Invalid;
                }
                return ClaimResult::Claimed;
#else
                int flags = O_RDWR;
#ifdef O_NOFOLLOW
                flags |= O_NOFOLLOW;
#endif
                handle_ = ::open(path.c_str(), flags);
                if (handle_ < 0)
                {
                    reason = "lease cannot be opened safely";
                    return ClaimResult::Invalid;
                }
                if (::flock(handle_, LOCK_EX | LOCK_NB) == 0)
                {
                    return ClaimResult::Claimed;
                }
                if (errno == EWOULDBLOCK || errno == EAGAIN)
                {
                    Close();
                    return ClaimResult::Active;
                }
                reason = "lease lock state cannot be determined";
                Close();
                return ClaimResult::Invalid;
#endif
            }

            void Close() noexcept
            {
#if defined(_WIN32)
                if (handle_ != INVALID_HANDLE_VALUE)
                {
                    ::CloseHandle(handle_);
                    handle_ = INVALID_HANDLE_VALUE;
                }
#else
                if (handle_ >= 0)
                {
                    ::close(handle_);
                    handle_ = -1;
                }
#endif
            }

        private:
#if defined(_WIN32)
            HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
            int handle_ = -1;
#endif
        };
    }

    inline ContentStagingScavengeResult ScavengeContentStagingDirectories(
        const std::filesystem::path& parent,
        const std::int64_t nowUnixSeconds = ContentStagingDetail::CurrentUnixSeconds(),
        const std::int64_t minimumAgeSeconds = ContentStagingMinimumAgeSeconds,
        const std::size_t maximumScannedEntries = ContentStagingMaximumScannedEntries,
        const std::size_t maximumCandidates = ContentStagingMaximumCandidates)
    {
        ContentStagingScavengeResult result;
        std::error_code error;
        std::vector<std::filesystem::path> candidates;
        std::filesystem::directory_iterator iterator(parent, error);
        if (error)
        {
            result.diagnostics.push_back(
                "cannot inspect the staging parent: " + error.message());
            return result;
        }
        const std::filesystem::directory_iterator end;
        while (iterator != end)
        {
            if (result.scannedEntries == maximumScannedEntries)
            {
                result.scanLimitReached = true;
                break;
            }
            const std::filesystem::directory_entry entry = *iterator;
            iterator.increment(error);
            if (error)
            {
                result.diagnostics.push_back(
                    "staging parent scan stopped safely: " + error.message());
                result.scanLimitReached = true;
                break;
            }
            ++result.scannedEntries;
            if (!entry.path().filename().string().starts_with(ContentStagingDirectoryPrefix))
            {
                continue;
            }
            if (candidates.size() == maximumCandidates)
            {
                result.scanLimitReached = true;
                continue;
            }
            candidates.push_back(entry.path());
        }
        std::sort(candidates.begin(), candidates.end());

        for (const std::filesystem::path& candidate : candidates)
        {
            ++result.matchingCandidates;
            const std::string name = candidate.filename().string();
            ContentStagingDetail::CandidateIdentity identity;
            std::error_code statusError;
            const std::filesystem::file_status status =
                std::filesystem::symlink_status(candidate, statusError);
            if (statusError || !std::filesystem::is_directory(status) ||
                std::filesystem::is_symlink(status) ||
                !ContentStagingDetail::ParseCandidateName(name, identity) ||
                !ContentStagingDetail::OwnedByCurrentUser(candidate))
            {
                ++result.conservativeSkips;
                result.diagnostics.push_back(
                    name + ": not a validated current-user staging directory");
                continue;
            }

            std::int64_t createdUnixSeconds = 0;
            std::string reason;
            if (!ContentStagingDetail::ReadMetadata(
                    candidate, name, identity, createdUnixSeconds, reason))
            {
                ++result.conservativeSkips;
                result.diagnostics.push_back(name + ": " + reason);
                continue;
            }
            if (createdUnixSeconds > nowUnixSeconds ||
                nowUnixSeconds - createdUnixSeconds < minimumAgeSeconds)
            {
                ++result.recentDirectories;
                continue;
            }

            ContentStagingDetail::LeaseHandle claim;
            const auto claimResult = claim.ClaimExisting(
                candidate / ContentStagingLeaseFile, reason);
            if (claimResult == ContentStagingDetail::LeaseHandle::ClaimResult::Active)
            {
                ++result.activeDirectories;
                continue;
            }
            if (claimResult == ContentStagingDetail::LeaseHandle::ClaimResult::Invalid)
            {
                ++result.conservativeSkips;
                result.diagnostics.push_back(name + ": " + reason);
                continue;
            }
#if defined(_WIN32)
            // Windows refuses to remove the lease while our exclusive handle is open. No build
            // can adopt this old unique session directory, so closing after the exclusive claim
            // does not create an active-owner ambiguity.
            claim.Close();
#endif
            std::filesystem::remove_all(candidate, error);
            if (error)
            {
                ++result.conservativeSkips;
                result.diagnostics.push_back(
                    name + ": removal failed: " + error.message());
                error.clear();
                continue;
            }
            ++result.removedDirectories;
        }
        if (result.scanLimitReached)
        {
            result.diagnostics.push_back("staging scan limit reached; remaining entries were left untouched");
        }
        std::sort(result.diagnostics.begin(), result.diagnostics.end());
        return result;
    }

    class ContentBuildStagingDirectory final
    {
    public:
        explicit ContentBuildStagingDirectory(
            std::filesystem::path parent = {},
            const std::int64_t createdUnixSeconds =
                ContentStagingDetail::CurrentUnixSeconds())
        {
            std::error_code error;
            if (parent.empty())
            {
                parent = std::filesystem::temp_directory_path(error) /
                         "cna_content_staging_v1";
                if (!error)
                {
                    std::filesystem::create_directory(parent, error);
                    if (error == std::errc::file_exists) { error.clear(); }
                }
                if (!error)
                {
                    const std::filesystem::file_status status =
                        std::filesystem::symlink_status(parent, error);
                    if (!error && (!std::filesystem::is_directory(status) ||
                                   std::filesystem::is_symlink(status) ||
                                   !ContentStagingDetail::OwnedByCurrentUser(parent)))
                    {
                        throw std::runtime_error(
                            "the content staging parent is not a private current-user directory.");
                    }
                }
                if (!error)
                {
                    std::filesystem::permissions(
                        parent, std::filesystem::perms::owner_all,
                        std::filesystem::perm_options::replace, error);
                }
            }
            if (error)
            {
                throw std::runtime_error(
                    "no content staging directory is available: " + error.message() + ".");
            }
            scavengeResult_ = ScavengeContentStagingDirectories(parent, createdUnixSeconds);
            const std::string token = ContentStagingDetail::MakeToken(this);
            for (std::size_t attempt = 0u; attempt < 1024u; ++attempt)
            {
                ContentStagingDetail::CandidateIdentity identity{
                    CNA::Tools::Detail::ProcessTag(), token, std::to_string(attempt)};
                const std::string name = std::string(ContentStagingDirectoryPrefix) +
                                         identity.pid + "_" + identity.token + "_" +
                                         identity.attempt;
                error.clear();
                const std::filesystem::path candidate = parent / name;
                if (!std::filesystem::create_directory(candidate, error))
                {
                    if (error && error != std::errc::file_exists)
                    {
                        throw std::runtime_error(
                            "cannot reserve content staging directory: " + error.message() + ".");
                    }
                    continue;
                }
                std::filesystem::permissions(
                    candidate, std::filesystem::perms::owner_all,
                    std::filesystem::perm_options::replace, error);
                if (error)
                {
                    std::error_code ignored;
                    std::filesystem::remove(candidate, ignored);
                    throw std::runtime_error(
                        "cannot secure content staging directory: " + error.message() + ".");
                }
                std::string reason;
                if (!lease_.CreateAndHold(candidate / ContentStagingLeaseFile, reason))
                {
                    std::error_code ignored;
                    std::filesystem::remove_all(candidate, ignored);
                    throw std::runtime_error(reason + ".");
                }
                try
                {
                    const std::string metadata = ContentStagingDetail::MetadataText(
                        name, identity, createdUnixSeconds);
                    WriteFileAtomically(
                        candidate / ContentStagingMetadataFile,
                        std::vector<std::uint8_t>(metadata.begin(), metadata.end()));
                }
                catch (...)
                {
                    lease_.Close();
                    std::error_code ignored;
                    std::filesystem::remove_all(candidate, ignored);
                    throw;
                }
                path_ = candidate;
                return;
            }
            throw std::runtime_error("cannot reserve a unique content staging directory.");
        }

        ~ContentBuildStagingDirectory()
        {
            lease_.Close();
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        ContentBuildStagingDirectory(const ContentBuildStagingDirectory&) = delete;
        ContentBuildStagingDirectory& operator=(const ContentBuildStagingDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }
        [[nodiscard]] const ContentStagingScavengeResult& ScavengeResult() const noexcept
        {
            return scavengeResult_;
        }

    private:
        std::filesystem::path path_;
        ContentStagingDetail::LeaseHandle lease_;
        ContentStagingScavengeResult scavengeResult_;
    };
}
