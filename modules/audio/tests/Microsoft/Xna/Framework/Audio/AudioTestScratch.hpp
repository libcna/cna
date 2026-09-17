// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <random>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

// plans/plan_graphics_shared_cleanup.md GSC-0008: the audio suites wrote their XACT fixtures into fixed
// directories directly under the system temporary directory (%TEMP%\cna_audio_engine_test and siblings).
// Test processes that run at the same time -- parallel gtest shards on Windows, a parallel ctest of
// discovered tests on Linux -- then rewrote one another's files while they were being read: Windows
// reported "Could not find file ...\Temp\cna_audio_engine_test\fixture.xgs" for AudioEngineTest.
// Each process now keeps its fixtures in a directory of its own, removed when the process exits.

namespace CnaAudioTest
{
    namespace Detail
    {
        /** @brief Owns this process's fixture root for the lifetime of the process. */
        class ProcessScratch
        {
        public:
            ProcessScratch()
            {
                std::random_device entropy;
#if defined(_WIN32)
                const long long processId = ::_getpid();
#else
                const long long processId = ::getpid();
#endif
                // The process id names the owner when a crashed run leaves a directory behind; the random
                // part keeps a reused id from ever landing in a stale directory.
                path_ = std::filesystem::temp_directory_path() /
                        ("cna_audio_tests_" + std::to_string(processId) + "_" +
                         std::to_string(entropy()) + std::to_string(entropy()));
                std::filesystem::create_directories(path_);
            }

            ~ProcessScratch()
            {
                std::error_code ignored;
                std::filesystem::remove_all(path_, ignored);
            }

            ProcessScratch(const ProcessScratch&) = delete;
            ProcessScratch& operator=(const ProcessScratch&) = delete;

            [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

        private:
            std::filesystem::path path_;
        };
    }

    /**
     * @brief The directory this test process writes its audio fixtures under.
     *
     * One per process, shared by every audio test translation unit, created on first use and removed at
     * process exit. Never shared with another process, so no two concurrently running test processes can
     * see each other's partially written files.
     *
     * @return The process's fixture root.
     */
    [[nodiscard]] inline const std::filesystem::path& FixtureRoot()
    {
        static const Detail::ProcessScratch scratch;
        return scratch.Path();
    }
}
