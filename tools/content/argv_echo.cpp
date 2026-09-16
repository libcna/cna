// SPDX-License-Identifier: MS-PL
//
// plans/plan_windows_portability_closeout.md WINCLOSE-0003: the program the Windows half of
// RunHostProcess could not be tested without.
//
// RunHostProcess takes a vector of arguments and, on Windows, has to turn it back into the single
// command-line string CreateProcessW accepts -- quoting each argument by the rules
// CommandLineToArgvW documents. Nothing proves that round trip except a child that reports the
// argument vector it actually received. Routing through cmd.exe would have measured cmd's quoting
// instead, and /bin/echo -- which the inherited tests used -- does not exist on Windows at all.
//
// So this program reports its own argv, verbatim, and the test compares it with what was asked
// for. Two details are what make it trustworthy:
//
//   * On Windows the arguments are taken from CommandLineToArgvW(GetCommandLineW()) rather than
//     from main's `char**`. The CRT's narrow argv is converted through the ANSI code page, which
//     cannot spell the very characters the Unicode cases exist to test, and CommandLineToArgvW is
//     the exact contract RunHostProcess's quoting names.
//   * stdout is put in binary mode. Text mode would expand every '\n' to CRLF and the length
//     prefixes below would disagree with the bytes -- the WINNATIVE-F27 defect, in the one place
//     that would have made the argument tests lie about what they measured.
//
// Output is length-prefixed so an argument may contain anything at all, newlines included:
//
//     argc=<n>\n
//     arg=<byteLength>:<raw UTF-8 bytes>\n
//
// Deliberately links nothing, like cna_fake_effect_compiler next to it: it must behave like a
// third-party tool, and sharing CNA code would let one bug cancel another across the boundary.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  include <fcntl.h>
#  include <io.h>
#endif

namespace
{
    /** @brief Marks stdout and stderr as byte streams, so no '\n' is rewritten on the way out. */
    void UseBinaryOutput()
    {
#if defined(_WIN32)
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
#endif
    }

    /** @brief This process's real argument vector, as UTF-8. */
    std::vector<std::string> RealArguments(int argc, char** argv)
    {
        std::vector<std::string> arguments;
#if defined(_WIN32)
        static_cast<void>(argc);
        static_cast<void>(argv);
        int wideCount = 0;
        wchar_t** const wide = CommandLineToArgvW(GetCommandLineW(), &wideCount);
        if (wide == nullptr) { return arguments; }
        for (int index = 0; index < wideCount; ++index)
        {
            const int needed = WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, nullptr, 0,
                                                   nullptr, nullptr);
            if (needed > 1)
            {
                std::string utf8(static_cast<std::size_t>(needed - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, utf8.data(), needed - 1,
                                    nullptr, nullptr);
                arguments.push_back(std::move(utf8));
            }
            else
            {
                arguments.emplace_back();
            }
        }
        LocalFree(wide);
#else
        for (int index = 0; index < argc; ++index) { arguments.emplace_back(argv[index]); }
#endif
        return arguments;
    }

    void Emit(std::FILE* stream, const std::string& bytes)
    {
        std::fwrite(bytes.data(), 1u, bytes.size(), stream);
    }
}

int main(int argc, char** argv)
{
    UseBinaryOutput();
    const std::vector<std::string> arguments = RealArguments(argc, argv);

    Emit(stdout, "argc=" + std::to_string(arguments.size()) + "\n");
    for (const std::string& argument : arguments)
    {
        Emit(stdout, "arg=" + std::to_string(argument.size()) + ":");
        Emit(stdout, argument);
        Emit(stdout, "\n");
    }

    // Three directives, recognised anywhere in the vector and still echoed above like any other
    // argument, so that asking for them does not change what the argv report says.
    int exitCode = 0;
    for (const std::string& argument : arguments)
    {
        constexpr const char* kExit = "--cna-exit=";
        constexpr const char* kStdErr = "--cna-stderr=";
        constexpr const char* kBulk = "--cna-bulk=";
        if (argument.rfind(kExit, 0) == 0)
        {
            exitCode = std::atoi(argument.c_str() + std::strlen(kExit));
        }
        else if (argument.rfind(kStdErr, 0) == 0)
        {
            Emit(stderr, argument.substr(std::strlen(kStdErr)));
        }
        else if (argument.rfind(kBulk, 0) == 0)
        {
            // Writes the same count to BOTH streams, which is what catches a parent that drains
            // one to end-of-file before starting the other: a pipe holds a bounded amount, so the
            // child blocks on the stream nobody is reading while the parent blocks on the other.
            const std::size_t count = static_cast<std::size_t>(
                std::strtoul(argument.c_str() + std::strlen(kBulk), nullptr, 10));
            const std::string outBlock(count, 'o');
            const std::string errBlock(count, 'e');
            Emit(stderr, errBlock);
            Emit(stdout, outBlock);
        }
    }
    std::fflush(stdout);
    std::fflush(stderr);
    return exitCode;
}
