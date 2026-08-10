// SPDX-License-Identifier: MS-PL
//
// Task PERF2-002: small, Linux-only helpers for counting this process's own open file
// descriptors and threads, shared by each Devices test file's own lifecycle leak-stress test
// (AccelerometerTests.cpp, GyroscopeTests.cpp, CompassTests.cpp, MotionTests.cpp,
// VibrateControllerTests.cpp). Not a general-purpose utility -- deliberately minimal, scoped to
// exactly what those tests need. Header-only so each test binary translation unit gets its own
// inline copy with no new build target/library required.
#pragma once

#if defined(__linux__)
#include <dirent.h>

#include <fstream>
#include <string>

namespace CnaTestSupport
{
    /**
     * @brief Counts this process's currently-open file descriptors via /proc/self/fd. Returns -1
     * if /proc/self/fd could not be opened (should not happen on a real Linux system, but a test
     * asserting on the result should still check for it rather than silently trusting a garbage
     * count).
     */
    inline int CountOpenFileDescriptors()
    {
        DIR* dir = opendir("/proc/self/fd");
        if (dir == nullptr)
        {
            return -1;
        }

        int count = 0;
        for (struct dirent* entry = readdir(dir); entry != nullptr; entry = readdir(dir))
        {
            if (entry->d_name[0] != '.') // skips "." and ".."
            {
                ++count;
            }
        }
        closedir(dir);

        // Subtracts 1 for the /proc/self/fd directory handle opendir() itself just opened --
        // that fd exists (and is counted by the readdir() loop above) only for the duration of
        // this traversal, not before or after it, so it must not be attributed to the caller's
        // own resource usage.
        return count - 1;
    }

    /** @brief Reads this process's current thread count from /proc/self/status's "Threads:" line. Returns -1 if not found/parseable. */
    inline int GetThreadCount()
    {
        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line))
        {
            constexpr const char* Prefix = "Threads:";
            if (line.rfind(Prefix, 0) == 0)
            {
                try
                {
                    return std::stoi(line.substr(std::string(Prefix).size()));
                }
                catch (...)
                {
                    return -1;
                }
            }
        }
        return -1;
    }
} // namespace CnaTestSupport
#endif // defined(__linux__)
