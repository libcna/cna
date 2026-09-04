// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-A5: a standalone (non-GTest) program that stands in for
// Microsoft's legacy `fxc` on the *other* side of a real process boundary.
//
// This exists because a FakeEffectCompiler C++ object proves nothing about the parts of the `.fx`
// route that are not C++ calls: command-line parsing, backend discovery, argument quoting, the
// pipe drain in RunHostProcess, exit-status handling, and the output file the compiler leaves
// behind. Substituting the EffectCompilerService interface skips all of it. This program does not.
//
// It is **not** an HLSL compiler and proves nothing about real `fxc` compatibility. It reproduces
// the shape of the conversation CNA has with a compiler, and nothing about shader semantics.
//
// Two modes, chosen the way a real launcher/compiler pair is chosen rather than by a private flag,
// because a private flag would not survive CNA's fixed argument list:
//
//   * version probe -- `/?`, which is what MakeExternalEffectCompiler() uses to decide whether a
//     backend is usable at all. Prints a banner and exits 0. The version it announces comes from
//     this program's own file name when that name carries an `@<version>` suffix, so a test that
//     needs a *second*, differently identified compiler copies the binary rather than needing a
//     second one built -- which is what an incremental-rebuild-on-compiler-change test is about.
//   * compile -- `/nologo /T fx_2_0 /Fo <out> ...` followed by the source path.
//
// CNA always leads with `/?` or `/nologo`, so anything else in argv[1] is the program this process
// was asked to run *through* -- launcher mode, exactly as `wine fxc.exe ...` reaches wine -- and
// the rest of the command line is handled as if this process were that program. Options are
// recognized by name rather than by a leading slash, because on this side of the boundary a POSIX
// source path begins with one too.
//
// Behaviour is steered by directives inside the `.fx` source itself. A compiler reads its input,
// so this needs no environment variable and no extra argument, and two tests running concurrently
// with different expectations cannot interfere with each other:
//
//   //FAKE: record=<path>       append one line per argument to <path>, then a blank line
//   //FAKE: payload=<text>      put <text> verbatim after the container header
//   //FAKE: warn                write one warning diagnostic to stderr and still succeed
//   //FAKE: error               write one error diagnostic to stderr and exit 1
//   //FAKE: exit=<n>            exit with status <n> after everything else
//   //FAKE: silent-failure      exit 1 having said nothing at all
//   //FAKE: no-output           report success without writing the output file
//   //FAKE: empty-output        report success and write a zero-byte output file
//   //FAKE: bad-container       write a bare shader blob rather than an effect container
//   //FAKE: flood               write more than 64 KiB to *both* streams before finishing
//   //FAKE: only-stdout         flood standard output alone
//   //FAKE: only-stderr         flood standard error alone

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    /** @brief The version this program announces unless its file name overrides it. */
    constexpr const char* kVersion = "9.29.952.3111";

    /** @brief Returns the `@<version>` suffix of this program's own file name, or @ref kVersion. */
    [[nodiscard]] std::string AnnouncedVersion(const char* program)
    {
        const std::string stem = std::filesystem::path(program == nullptr ? "" : program).stem()
                                     .string();
        const std::size_t marker = stem.rfind('@');
        if (marker == std::string::npos || marker + 1u >= stem.size()) { return kVersion; }
        return stem.substr(marker + 1u);
    }

    /** @brief Bytes over the usual 64 KiB pipe buffer, so a sequential drain would deadlock. */
    constexpr std::size_t kFloodBytes = 200000u;

    struct Directives
    {
        std::string record;
        std::string payload = "cna-fake-effect";
        bool warn = false;
        bool error = false;
        bool silentFailure = false;
        bool noOutput = false;
        bool emptyOutput = false;
        bool badContainer = false;
        bool floodOut = false;
        bool floodErr = false;
        bool hasExitCode = false;
        int exitCode = 0;
    };

    [[nodiscard]] std::string Trim(const std::string& text)
    {
        const std::size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) { return {}; }
        return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1u);
    }

    /** @brief Reads the //FAKE: directives out of the source this invocation was handed. */
    [[nodiscard]] Directives ReadDirectives(const std::filesystem::path& source)
    {
        Directives directives;
        std::ifstream stream(source);
        std::string line;
        while (std::getline(stream, line))
        {
            const std::size_t marker = line.find("//FAKE:");
            if (marker == std::string::npos) { continue; }
            const std::string body = Trim(line.substr(marker + 7u));
            if (body.rfind("record=", 0u) == 0u) { directives.record = body.substr(7u); }
            else if (body.rfind("payload=", 0u) == 0u) { directives.payload = body.substr(8u); }
            else if (body.rfind("exit=", 0u) == 0u)
            {
                directives.hasExitCode = true;
                directives.exitCode = std::atoi(body.substr(5u).c_str());
            }
            else if (body == "warn") { directives.warn = true; }
            else if (body == "error") { directives.error = true; }
            else if (body == "silent-failure") { directives.silentFailure = true; }
            else if (body == "no-output") { directives.noOutput = true; }
            else if (body == "empty-output") { directives.emptyOutput = true; }
            else if (body == "bad-container") { directives.badContainer = true; }
            else if (body == "flood") { directives.floodOut = directives.floodErr = true; }
            else if (body == "only-stdout") { directives.floodOut = true; }
            else if (body == "only-stderr") { directives.floodErr = true; }
        }
        return directives;
    }

    void AppendUInt32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    }

    /**
     * @brief Builds a container with the Effect Framework 9.1 signature and a caller-chosen body.
     *
     * The header is padded to the 24 bytes IsCompiledEffectBinary() requires before it will look
     * at the signature at all; the payload follows verbatim so a test can assert that exactly the
     * bytes this program returned reached the `.xnb`.
     */
    [[nodiscard]] std::vector<std::uint8_t> BuildEffect(const std::string& payload,
                                                        const bool badContainer)
    {
        std::vector<std::uint8_t> bytes;
        AppendUInt32(bytes, badContainer ? 0xFFFE0300u : 0xFEFF0901u);
        AppendUInt32(bytes, 0u);
        AppendUInt32(bytes, static_cast<std::uint32_t>(payload.size()));
        AppendUInt32(bytes, 0u);
        AppendUInt32(bytes, 0u);
        AppendUInt32(bytes, 0u);
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        return bytes;
    }

    /** @brief Whether @p argument is one of the options CNA's command line is built from. */
    [[nodiscard]] bool IsKnownOption(const std::string& argument)
    {
        return argument == "/nologo" || argument == "/T" || argument == "/Fo" ||
               argument == "/Zi" || argument == "/Qstrip_debug" || argument == "/D" ||
               argument == "/I";
    }

    /** @brief Whether @p argument is one of the options that consume the argument after it. */
    [[nodiscard]] bool TakesValue(const std::string& argument)
    {
        return argument == "/T" || argument == "/D" || argument == "/I";
    }

    void Flood(std::ostream& stream)
    {
        // One long unrecognized line rather than many: ParseEffectCompilerDiagnostics keeps every
        // line it does not recognize, and a test asserting on diagnostics should not have to wade
        // through thousands of them to prove the pipe did not fill.
        stream << std::string(kFloodBytes, 'x') << "\n";
        stream.flush();
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index) { arguments.emplace_back(argv[index]); }

    // Launcher mode: the first argument is the program to run rather than the option CNA always
    // leads with.
    std::string launched;
    if (!arguments.empty() && arguments[0] != "/?" && arguments[0] != "/nologo")
    {
        launched = arguments[0];
        arguments.erase(arguments.begin());
    }

    if (arguments.size() == 1u && arguments[0] == "/?")
    {
        std::cout << "CNA fake effect compiler, version " << AnnouncedVersion(argv[0]) << "\n";
        return 0;
    }

    std::filesystem::path output;
    std::filesystem::path source;
    for (std::size_t index = 0u; index < arguments.size(); ++index)
    {
        if (arguments[index] == "/Fo" && index + 1u < arguments.size())
        {
            output = arguments[++index];
        }
        else if (TakesValue(arguments[index]))
        {
            ++index;
        }
        else if (!IsKnownOption(arguments[index]))
        {
            source = arguments[index];
        }
    }
    if (source.empty())
    {
        std::cerr << "fake effect compiler: no source file in the command line\n";
        return 2;
    }

    const Directives directives = ReadDirectives(source);
    if (!directives.record.empty())
    {
        std::ofstream record(directives.record, std::ios::app);
        record << "launcher\t" << launched << "\n";
        for (const std::string& argument : arguments) { record << "arg\t" << argument << "\n"; }
        record << "\n";
    }

    if (directives.floodOut) { Flood(std::cout); }
    if (directives.floodErr) { Flood(std::cerr); }

    if (directives.silentFailure) { return 1; }
    if (directives.error)
    {
        std::cerr << source.string() << "(3,17): error X3000: fake compiler was asked to fail\n";
        return 1;
    }
    if (directives.warn)
    {
        std::cerr << source.string() << "(2,5): warning X3206: fake compiler warning\n";
    }

    if (!directives.noOutput && !output.empty())
    {
        std::ofstream stream(output, std::ios::binary);
        if (!directives.emptyOutput)
        {
            const std::vector<std::uint8_t> bytes =
                BuildEffect(directives.payload, directives.badContainer);
            stream.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
    }

    return directives.hasExitCode ? directives.exitCode : 0;
}
