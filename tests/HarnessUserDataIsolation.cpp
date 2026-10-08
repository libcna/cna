// SPDX-License-Identifier: MS-PL
//
// plans/plan_apple_m4.md AM4-162: give every test process its own per-user data root.
//
// StorageDevice resolves its root from $XDG_DATA_HOME, else %LOCALAPPDATA%, else $HOME (on Apple,
// ~/Library/Application Support), and the gamer-services offline store, saved games and SDL's
// preferences path all live beneath it. Without this, a test run wrote into the developer's own
// data directory, and because CTest runs every discovered case as its own process, cases running
// side by side under `ctest -j` shared one store: the leaderboard, achievement and storage-device
// tests deleted or rewrote each other's files and failed only when run in parallel.
//
// Each process now gets a fresh directory under the system temporary directory and points
// $XDG_DATA_HOME at it before any test runs. The directory is named in CNA_TEST_USER_DATA_HOME as
// well, and a process that finds that variable already set uses the directory it names instead of
// making one: a child a test launches therefore meets its parent in the same store, which the
// multi-process store tests rely on, and a developer can pin the root to inspect it. Only the
// process that created the directory removes it, at exit; a forked child shares the parent's
// static objects, so the creator is identified by process id rather than by object.
//
// Tests that exercise the resolution order itself still set the variables they need through
// their own scoped environment; this only replaces the starting point.

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "System/Environment.hpp"

namespace
{
    constexpr const char* kPinnedRootVariable = "CNA_TEST_USER_DATA_HOME";

    [[nodiscard]] long currentProcessId()
    {
#if defined(_WIN32)
        return static_cast<long>(_getpid());
#else
        return static_cast<long>(getpid());
#endif
    }

    [[nodiscard]] std::filesystem::path makeUniqueDirectory()
    {
        std::error_code code;
        const std::filesystem::path base = std::filesystem::temp_directory_path(code);
        if (code)
            return {};
        std::random_device entropy;
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const std::uint64_t token =
                (static_cast<std::uint64_t>(entropy()) << 32) ^ static_cast<std::uint64_t>(entropy());
            const std::filesystem::path candidate =
                base / ("cna-tests-" + std::to_string(currentProcessId()) + "-" + std::to_string(token));
            if (std::filesystem::create_directory(candidate, code) && !code)
                return candidate;
        }
        return {};
    }

    class IsolatedUserData
    {
    public:
        IsolatedUserData()
        {
            if (const auto pinned = System::Environment::GetEnvironmentVariable(kPinnedRootVariable);
                pinned && !pinned->empty())
            {
                System::Environment::SetEnvironmentVariable("XDG_DATA_HOME", *pinned);
                return;
            }
            const std::filesystem::path directory = makeUniqueDirectory();
            if (directory.empty())
                return;
            created_ = directory;
            creator_ = currentProcessId();
            System::Environment::SetEnvironmentVariable("XDG_DATA_HOME", directory.string());
            System::Environment::SetEnvironmentVariable(kPinnedRootVariable, directory.string());
        }

        ~IsolatedUserData()
        {
            if (created_.empty() || creator_ != currentProcessId())
                return;
            std::error_code ignored;
            std::filesystem::remove_all(created_, ignored);
        }

        IsolatedUserData(const IsolatedUserData&) = delete;
        IsolatedUserData& operator=(const IsolatedUserData&) = delete;

    private:
        std::filesystem::path created_;
        long creator_ = 0;
    };

    const IsolatedUserData kIsolatedUserData;
}
