// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace CNA::Internal
{
    /** @brief The complete outcome of one child process (plans/plan_xnapipeline.md `XNAP-A1`). */
    struct HostProcessResult
    {
        /** @brief Whether the process was started at all. False means the executable is unusable. */
        bool started = false;

        /** @brief Process exit status, valid only when @ref started is true. */
        int exitCode = -1;

        /** @brief Everything the child wrote to standard output. */
        std::string standardOutput;

        /** @brief Everything the child wrote to standard error. */
        std::string standardError;

        /** @brief Why the process could not be started, when @ref started is false. */
        std::string failure;
    };

    /**
     * @brief Runs one child process to completion and captures both output streams.
     *
     * Build-time only, and deliberately narrow: no shell, no environment manipulation, no
     * streaming, no timeout. Arguments are passed as a vector rather than a command string
     * precisely so that a path containing a space -- which is the normal case for a Windows SDK
     * install -- cannot be re-split by a shell.
     *
     * @param executable The program to run. Resolved through `PATH` when it has no directory part.
     * @param arguments Arguments after `argv[0]`.
     * @return The captured outcome. A non-zero exit code is a result, not an error.
     */
    [[nodiscard]] HostProcessResult RunHostProcess(const std::filesystem::path& executable,
                                                   const std::vector<std::string>& arguments);
}
