// SPDX-License-Identifier: MS-PL
#pragma once

#if !defined(_WIN32)
#error "WindowsProcessIsolation.hpp is only available on Windows targets"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>

#include <string>

namespace CNA::Examples
{
    enum class WindowsChildOutcome
    {
        Exited,
        TimedOut,
        LaunchFailed,
        WaitFailed,
    };

    struct WindowsChildResult
    {
        WindowsChildOutcome outcome = WindowsChildOutcome::LaunchFailed;
        DWORD exitCode = 0;
        DWORD systemError = 0;
    };

    inline std::string QuoteWindowsArgument(const std::string& argument)
    {
        std::string quoted = "\"";
        std::size_t backslashes = 0;
        for (const char value : argument)
        {
            if (value == '\\')
            {
                ++backslashes;
                continue;
            }
            if (value == '"')
            {
                quoted.append(backslashes * 2 + 1, '\\');
                quoted.push_back(value);
                backslashes = 0;
                continue;
            }
            quoted.append(backslashes, '\\');
            backslashes = 0;
            quoted.push_back(value);
        }
        quoted.append(backslashes * 2, '\\');
        quoted.push_back('"');
        return quoted;
    }

    inline WindowsChildResult RunWindowsChild(const char* executable, const std::string& argument,
                                               const DWORD timeoutMilliseconds)
    {
        std::string commandLine = QuoteWindowsArgument(executable) + " " +
                                  QuoteWindowsArgument(argument);
        STARTUPINFOA startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        if (!CreateProcessA(executable, commandLine.data(), nullptr, nullptr, TRUE, 0, nullptr,
                            nullptr, &startupInfo, &processInfo))
        {
            return { WindowsChildOutcome::LaunchFailed, 0, GetLastError() };
        }

        const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, timeoutMilliseconds);
        if (waitResult == WAIT_TIMEOUT)
        {
            TerminateProcess(processInfo.hProcess, 124);
            WaitForSingleObject(processInfo.hProcess, INFINITE);
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            return { WindowsChildOutcome::TimedOut, 124, 0 };
        }
        if (waitResult != WAIT_OBJECT_0)
        {
            const DWORD error = GetLastError();
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            return { WindowsChildOutcome::WaitFailed, 0, error };
        }

        DWORD exitCode = 0;
        if (!GetExitCodeProcess(processInfo.hProcess, &exitCode))
        {
            const DWORD error = GetLastError();
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            return { WindowsChildOutcome::WaitFailed, 0, error };
        }
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return { WindowsChildOutcome::Exited, exitCode, 0 };
    }

    inline bool IsWindowsAbnormalExit(const DWORD exitCode)
    {
        // MinGW's std::terminate()/abort path returns 3; SEH exception codes have the high bits set.
        return exitCode == 3 || exitCode >= 0xC0000000u;
    }
}
