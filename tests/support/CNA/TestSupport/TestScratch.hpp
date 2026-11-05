// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/TestSupport/TestScratchConfig.hpp"
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace CNA::TestSupport
{
    namespace ScratchDetail
    {
        inline long ProcessId()
        {
#if defined(_WIN32)
            return static_cast<long>(::_getpid());
#else
            return static_cast<long>(::getpid());
#endif
        }

        inline void MakeOwnedTreeRemovable(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            if (error || std::filesystem::is_symlink(status)) return;
            if (std::filesystem::is_directory(status))
            {
                std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                    std::filesystem::perm_options::add, error);
                std::filesystem::directory_iterator at(path, error), end;
                while (!error && at != end)
                {
                    MakeOwnedTreeRemovable(at->path());
                    at.increment(error);
                }
            }
            else if (std::filesystem::is_regular_file(status))
            {
                std::filesystem::permissions(path, std::filesystem::perms::owner_write,
                    std::filesystem::perm_options::add, error);
            }
        }

        // Test-only ownership. An existing directory is never adopted, even after PID reuse.
        class ProcessScratch final
        {
        public:
            ProcessScratch() : creator_(ProcessId())
            {
                const auto base = std::filesystem::canonical(std::filesystem::temp_directory_path());
                const std::string sourceUtf8 = ScratchDetail::kSourceRoot;
                const auto source = std::filesystem::canonical(std::filesystem::path(
                    std::u8string(sourceUtf8.begin(), sourceUtf8.end())));
                const auto relative = base.lexically_relative(source);
                if (!relative.empty() && *relative.begin() != "..")
                    throw std::runtime_error("test temporary directory is inside the source tree");
                std::random_device entropy;
                for (int attempt = 0; attempt < 32; ++attempt)
                {
                    const auto candidate = base / ("cna-tests-" + std::to_string(creator_) + "-" +
                        std::to_string(entropy()) + "-" + std::to_string(entropy()));
                    std::error_code error;
                    // Atomic exclusive claim: false means another owner or a stale directory.
                    if (std::filesystem::create_directory(candidate, error))
                    {
                        path_ = candidate;
                        return;
                    }
                    if (error && error != std::errc::file_exists)
                        throw std::filesystem::filesystem_error("create test scratch", candidate, error);
                }
                throw std::runtime_error("could not claim a unique test scratch directory");
            }

            ~ProcessScratch()
            {
                // A fork may intentionally share fixture paths, but cannot delete its parent's root.
                if (creator_ != ProcessId()) return;
                std::error_code ignored;
                for (const auto& directory : nativeRoots_)
                {
                    MakeOwnedTreeRemovable(directory);
                    std::filesystem::remove_all(directory, ignored);
                }
                MakeOwnedTreeRemovable(path_);
                std::filesystem::remove_all(path_, ignored);
            }

            ProcessScratch(const ProcessScratch&) = delete;
            ProcessScratch& operator=(const ProcessScratch&) = delete;
            const std::filesystem::path& Path() const { return path_; }

            void OwnNativeRoot(const std::filesystem::path& directory)
            {
                const auto native = std::filesystem::weakly_canonical(directory);
                // Native preference APIs choose the parent (e.g. Windows Known Folders). Only
                // a leaf named with this owner's unique token can be registered for cleanup.
                if (native.filename() != path_.filename())
                    throw std::runtime_error("native test scratch must use the process token");
                nativeRoots_.push_back(native);
            }

        private:
            long creator_;
            std::filesystem::path path_;
            std::vector<std::filesystem::path> nativeRoots_;
        };
    }

    // One inline-function static across translation units. Normal exit and fatal GTest assertions
    // retain cleanup; abort, _Exit and SIGKILL cannot run C++ destructors. Never sweep other roots.
    inline ScratchDetail::ProcessScratch& ScratchOwner()
    {
        static ScratchDetail::ProcessScratch scratch;
        return scratch;
    }

    inline const std::filesystem::path& FixtureRoot()
    {
        return ScratchOwner().Path();
    }

    inline void OwnNativeScratchRoot(const std::string& utf8Directory)
    {
        ScratchOwner().OwnNativeRoot(std::filesystem::path(
            std::u8string(utf8Directory.begin(), utf8Directory.end())));
    }
}
