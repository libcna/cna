// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-A1: running one build-time tool and capturing what it said.
//
// This exists because the `.fx` source route needs an external effect compiler and there is no
// portable way to run one. Both implementations below avoid a shell entirely: the arguments are a
// vector on POSIX and are quoted by the documented CommandLineToArgvW rules on Windows, so a path
// like `C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x86\fxc.exe` is
// passed through intact rather than re-split on its spaces.

#include "CNA/Internal/HostProcess.hpp"

#include <array>
#include <cstring>

#if defined(_WIN32)
#include <thread>
#include <windows.h>
#else
#include <errno.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace CNA::Internal
{
    namespace
    {
#if defined(_WIN32)
        /**
         * @brief Quotes one argument by the rules `CommandLineToArgvW` documents.
         *
         * A backslash is literal except immediately before a quote, where it must be doubled;
         * everything is wrapped in quotes when it contains whitespace or a quote of its own.
         */
        [[nodiscard]] std::wstring QuoteArgument(const std::wstring& argument)
        {
            const bool needsQuotes =
                argument.empty() ||
                argument.find_first_of(L" \t\n\v\"") != std::wstring::npos;
            if (!needsQuotes) { return argument; }

            std::wstring quoted;
            quoted.push_back(L'"');
            for (std::size_t index = 0u; index < argument.size(); ++index)
            {
                std::size_t backslashes = 0u;
                while (index < argument.size() && argument[index] == L'\\')
                {
                    ++backslashes;
                    ++index;
                }
                if (index == argument.size())
                {
                    quoted.append(backslashes * 2u, L'\\');
                    break;
                }
                if (argument[index] == L'"')
                {
                    quoted.append(backslashes * 2u + 1u, L'\\');
                }
                else
                {
                    quoted.append(backslashes, L'\\');
                }
                quoted.push_back(argument[index]);
            }
            quoted.push_back(L'"');
            return quoted;
        }

        [[nodiscard]] std::wstring Widen(const std::string& text)
        {
            if (text.empty()) { return {}; }
            const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                                   static_cast<int>(text.size()), nullptr, 0);
            std::wstring wide(static_cast<std::size_t>(needed), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                wide.data(), needed);
            return wide;
        }

        [[nodiscard]] std::string Narrow(const std::wstring& text)
        {
            if (text.empty()) { return {}; }
            const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                                   static_cast<int>(text.size()), nullptr, 0,
                                                   nullptr, nullptr);
            std::string narrow(static_cast<std::size_t>(needed), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                narrow.data(), needed, nullptr, nullptr);
            return narrow;
        }

        /** @brief Reads a pipe to end-of-file. */
        [[nodiscard]] std::string DrainPipe(HANDLE pipe)
        {
            std::string text;
            std::array<char, 4096> buffer{};
            DWORD read = 0u;
            while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                            nullptr) &&
                   read > 0u)
            {
                text.append(buffer.data(), read);
            }
            return text;
        }
#else
        /**
         * @brief Reads both descriptors to end-of-file, interleaved.
         *
         * Draining one to EOF and only then the other deadlocks: a pipe holds 64 KiB on Linux, so
         * a child that writes more than that to the stream nobody is reading blocks forever while
         * the parent blocks on the other. `poll()` takes whichever stream has bytes. `fxc` reports
         * warnings on standard error and can easily exceed a pipe buffer, so this is the ordinary
         * case rather than a pathological one.
         *
         * @param outDescriptor Read end of the child's standard output.
         * @param errDescriptor Read end of the child's standard error.
         * @param standardOutput Receives everything read from @p outDescriptor.
         * @param standardError Receives everything read from @p errDescriptor.
         */
        void DrainBothDescriptors(const int outDescriptor, const int errDescriptor,
                                  std::string& standardOutput, std::string& standardError)
        {
            std::array<char, 4096> buffer{};
            struct pollfd fds[2];
            fds[0] = {outDescriptor, POLLIN, 0};
            fds[1] = {errDescriptor, POLLIN, 0};
            std::string* const targets[2] = {&standardOutput, &standardError};
            bool open[2] = {true, true};

            while (open[0] || open[1])
            {
                for (int index = 0; index < 2; ++index)
                {
                    fds[index].fd = open[index] ? fds[index].fd : -1;
                    fds[index].revents = 0;
                }
                if (::poll(fds, 2, -1) < 0)
                {
                    if (errno == EINTR) { continue; }
                    break;
                }
                for (int index = 0; index < 2; ++index)
                {
                    if (!open[index] || fds[index].revents == 0) { continue; }
                    const ssize_t read =
                        ::read(fds[index].fd, buffer.data(), buffer.size());
                    if (read > 0)
                    {
                        targets[index]->append(buffer.data(), static_cast<std::size_t>(read));
                    }
                    else if (read == 0) { open[index] = false; }
                    else if (errno != EINTR) { open[index] = false; }
                }
            }
        }
#endif
    }

    HostProcessResult RunHostProcess(const std::filesystem::path& executable,
                                     const std::vector<std::string>& arguments)
    {
        HostProcessResult result;
        if (executable.empty())
        {
            result.failure = "no executable was named";
            return result;
        }

#if defined(_WIN32)
        std::wstring commandLine = QuoteArgument(executable.wstring());
        for (const std::string& argument : arguments)
        {
            commandLine.push_back(L' ');
            commandLine.append(QuoteArgument(Widen(argument)));
        }

        SECURITY_ATTRIBUTES inheritable{};
        inheritable.nLength = sizeof(inheritable);
        inheritable.bInheritHandle = TRUE;

        HANDLE outRead = nullptr;
        HANDLE outWrite = nullptr;
        HANDLE errRead = nullptr;
        HANDLE errWrite = nullptr;
        if (!CreatePipe(&outRead, &outWrite, &inheritable, 0) ||
            !CreatePipe(&errRead, &errWrite, &inheritable, 0))
        {
            result.failure = "could not create a pipe for the child's output";
            return result;
        }
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = outWrite;
        startup.hStdError = errWrite;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION process{};
        std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back(L'\0');
        if (!CreateProcessW(nullptr, mutableCommandLine.data(), nullptr, nullptr, TRUE, 0, nullptr,
                            nullptr, &startup, &process))
        {
            result.failure = "CreateProcess failed with error " +
                             std::to_string(static_cast<unsigned long>(GetLastError()));
            CloseHandle(outRead);
            CloseHandle(outWrite);
            CloseHandle(errRead);
            CloseHandle(errWrite);
            return result;
        }
        CloseHandle(outWrite);
        CloseHandle(errWrite);

        // Both pipes are drained at once, for the reason DrainBothDescriptors gives on the POSIX
        // side: reading one to end-of-file first deadlocks as soon as the child fills the other.
        // A thread rather than overlapped I/O keeps the two implementations comparable.
        std::string errorText;
        std::thread errorDrain([&] { errorText = DrainPipe(errRead); });
        result.standardOutput = DrainPipe(outRead);
        errorDrain.join();
        result.standardError = std::move(errorText);
        CloseHandle(outRead);
        CloseHandle(errRead);

        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exitCode = 0u;
        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
        result.started = true;
        result.exitCode = static_cast<int>(exitCode);
        static_cast<void>(Narrow);
        return result;
#else
        int outPipe[2] = {-1, -1};
        int errPipe[2] = {-1, -1};
        if (::pipe(outPipe) != 0)
        {
            result.failure = "could not create a pipe for the child's output";
            return result;
        }
        if (::pipe(errPipe) != 0)
        {
            ::close(outPipe[0]);
            ::close(outPipe[1]);
            result.failure = "could not create a pipe for the child's error output";
            return result;
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addclose(&actions, outPipe[0]);
        posix_spawn_file_actions_addclose(&actions, errPipe[0]);
        posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, errPipe[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, outPipe[1]);
        posix_spawn_file_actions_addclose(&actions, errPipe[1]);

        const std::string program = executable.string();
        std::vector<std::string> owned;
        owned.reserve(arguments.size() + 1u);
        owned.push_back(program);
        for (const std::string& argument : arguments) { owned.push_back(argument); }
        std::vector<char*> argv;
        argv.reserve(owned.size() + 1u);
        for (std::string& argument : owned) { argv.push_back(argument.data()); }
        argv.push_back(nullptr);

        pid_t child = -1;
        // posix_spawnp resolves a bare name through PATH, which is what a caller naming `fxc`
        // rather than a full path means.
        const int spawned = executable.has_parent_path()
                                ? posix_spawn(&child, program.c_str(), &actions, nullptr,
                                              argv.data(), environ)
                                : posix_spawnp(&child, program.c_str(), &actions, nullptr,
                                               argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(outPipe[1]);
        ::close(errPipe[1]);
        if (spawned != 0)
        {
            ::close(outPipe[0]);
            ::close(errPipe[0]);
            result.failure = std::string("could not start it: ") + std::strerror(spawned);
            return result;
        }

        DrainBothDescriptors(outPipe[0], errPipe[0], result.standardOutput,
                             result.standardError);
        ::close(outPipe[0]);
        ::close(errPipe[0]);

        int status = 0;
        while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        result.started = true;
        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return result;
#endif
    }
}
