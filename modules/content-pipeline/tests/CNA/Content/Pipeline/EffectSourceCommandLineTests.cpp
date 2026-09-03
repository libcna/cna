// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-A5: the `.fx` route as a *product*, not as a registry a test
// assembled for itself.
//
// EffectSourceTests.cpp verifies the route from the EffectCompilerService seam inwards, with a
// registry it builds by hand. That leaves the half a user actually meets unproven: whether
// `cna-content build shader.fx --format xnb` reaches the route at all, whether the options the
// service's own diagnostics advertise exist, and whether a compiler on the far side of a real
// process boundary is driven correctly. It did not: before XNAP-A5, RegisterBuiltInContentPipeline
// registered only the compiled `.fxb` route, so a `.fx` in a source tree was silently not an asset
// -- "Built: 0  Skipped: 0  Failed: 0" -- and the CLI had no --fx-compiler to give it one.
//
// Every test here runs the real coordinator, RunContentCompiler(), with the real built-in
// registration, and every compile crosses a real process boundary into cna_fake_effect_compiler.
// That program is not an HLSL compiler: these tests prove CNA's product integration and say
// nothing about Microsoft `fxc` compatibility, which is XNAP-A4 and remains blocked.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/ContentCompiler.hpp"
#include "CNA/Content/Pipeline/EffectCompilerService.hpp"
#include "CNA/Content/Pipeline/EffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/EffectSourceContentPipeline.hpp"
#include "System/Environment.hpp"

namespace Pipeline = CNA::Content::Pipeline;

namespace
{
#if !defined(CNA_FAKE_EFFECT_COMPILER_PATH)
#error "CNA_FAKE_EFFECT_COMPILER_PATH must be baked in; see cmake/UnitTests.cmake (XNAP-A5)."
#endif

    /** @brief The stand-in compiler, built by cmake/Harnesses.cmake and never skipped. */
    [[nodiscard]] std::filesystem::path FakeCompiler()
    {
        return std::filesystem::path(CNA_FAKE_EFFECT_COMPILER_PATH);
    }

    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_fxcli_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_ / "src");
        }

        ~ScratchDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;
        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        [[nodiscard]] std::filesystem::path Source() const { return path_ / "src"; }
        [[nodiscard]] std::filesystem::path Output() const { return path_ / "out"; }
        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        if (path.has_parent_path()) { std::filesystem::create_directories(path.parent_path()); }
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    [[nodiscard]] std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    [[nodiscard]] std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    /** @brief Whether @p haystack contains @p needle as a byte subsequence. */
    [[nodiscard]] bool Contains(const std::vector<std::uint8_t>& haystack,
                                const std::string& needle)
    {
        if (needle.size() > haystack.size()) { return false; }
        for (std::size_t start = 0u; start + needle.size() <= haystack.size(); ++start)
        {
            bool match = true;
            for (std::size_t offset = 0u; offset < needle.size(); ++offset)
            {
                if (haystack[start + offset] != static_cast<std::uint8_t>(needle[offset]))
                {
                    match = false;
                    break;
                }
            }
            if (match) { return true; }
        }
        return false;
    }

    /** @brief One complete `cna-content` invocation: its exit status and both streams. */
    struct Invocation
    {
        int exitCode = -1;
        std::string output;

        [[nodiscard]] bool Says(const std::string& text) const
        {
            return output.find(text) != std::string::npos;
        }
    };

    /**
     * @brief Runs the real coordinator with the real built-in registration.
     *
     * The registry is created by the factory the coordinator calls *after* parsing, which is the
     * only arrangement that lets --fx-compiler reach registration; `cna-content`'s own entry point
     * does exactly this. Nothing is shared between calls, which is what two-invocations-in-one-
     * process independence rests on.
     */
    [[nodiscard]] Invocation RunCli(const std::vector<std::filesystem::path>& arguments)
    {
        std::ostringstream captured;
        std::streambuf* const previousOut = std::cout.rdbuf(captured.rdbuf());
        std::streambuf* const previousErr = std::cerr.rdbuf(captured.rdbuf());
        Invocation invocation;
        try
        {
            invocation.exitCode = Pipeline::RunContentCompiler(
                arguments, [](const Pipeline::ContentCompilerOptions& options)
            {
                auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
                Pipeline::RegisterBuiltInContentPipeline(*registry, options);
                return registry;
            });
        }
        catch (...)
        {
            std::cout.rdbuf(previousOut);
            std::cerr.rdbuf(previousErr);
            throw;
        }
        std::cout.rdbuf(previousOut);
        std::cerr.rdbuf(previousErr);
        invocation.output = captured.str();
        return invocation;
    }

    /**
     * @brief Sets one environment variable for the duration of a scope, then restores it.
     *
     * Through `System::Environment` rather than POSIX `setenv`, which MinGW-w64 does not have and
     * which ctest `CNAEXT_NoPosixSetenv` refuses for that reason. An empty value unsets.
     *
     * This is a *test* mutating its own process, to prove the environment tier of the option
     * precedence exists at all. The pipeline itself never writes an environment variable: the
     * command line reaches registration through a typed structure, which is the whole point of
     * XNAP-A5's shape.
     */
    class ScopedEnvironment
    {
    public:
        ScopedEnvironment(std::string name, const std::string& value) : name_(std::move(name))
        {
            previous_ = System::Environment::GetEnvironmentVariable(name_);
            System::Environment::SetEnvironmentVariable(name_, value);
        }

        ~ScopedEnvironment()
        {
            System::Environment::SetEnvironmentVariable(name_, previous_.value_or(std::string{}));
        }

        ScopedEnvironment(const ScopedEnvironment&) = delete;
        ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    private:
        std::string name_;
        std::optional<std::string> previous_;
    };

    /** @brief Copies the fake compiler to @p destination and makes it executable. */
    [[nodiscard]] std::filesystem::path CopyCompiler(const std::filesystem::path& destination)
    {
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::copy_file(FakeCompiler(), destination,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::permissions(destination,
                                     std::filesystem::perms::owner_exec |
                                         std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add);
        return destination;
    }

    /**
     * @brief Runs @p work with a deadline, failing the test rather than hanging past it.
     *
     * A drain regression in RunHostProcess does not make a test slow, it makes it stop: the parent
     * waits on one pipe while the child waits on the other. A test that only fails after CTest's
     * own multi-minute timeout reports "timeout", not "deadlock", and takes the whole run with it.
     * There is no safe recovery once a thread is stuck in that read, so the process exits after
     * the failure has been recorded.
     *
     * @param seconds How long the work may take.
     * @param work The callable to run.
     */
    template<typename Work>
    void WithDeadline(const int seconds, Work work)
    {
        std::future<void> pending = std::async(std::launch::async, std::move(work));
        if (pending.wait_for(std::chrono::seconds(seconds)) == std::future_status::timeout)
        {
            ADD_FAILURE() << "the build did not finish within " << seconds
                          << " seconds; a child process's output streams are not being drained "
                             "concurrently.";
            std::cerr.flush();
            std::cout.flush();
            std::quick_exit(1);
        }
        pending.get();
    }

    /** @brief A minimal `.fx` carrying the fake compiler's directives. */
    [[nodiscard]] std::string EffectSource(const std::string& directives = {})
    {
        return directives + "#include \"common.fxh\"\n"
                            "float4 PS() : COLOR0 { return Tint; }\n"
                            "technique T { pass P { PixelShader = compile ps_2_0 PS(); } }\n";
    }

    /** @brief Writes `shader.fx` plus the header it includes into @p scratch. */
    void WriteEffectProject(const ScratchDirectory& scratch, const std::string& directives = {})
    {
        WriteText(scratch.Source() / "common.fxh", "float4 Tint;\n");
        WriteText(scratch.Source() / "shader.fx", EffectSource(directives));
    }
} // namespace

// -- The route exists in the product at all ----------------------------------------------------

TEST(EffectSourceCommandLineTest, TheBuiltInRegistryHasAnImporterAndProcessorForFx)
{
    Pipeline::ContentPipelineRegistry registry;
    Pipeline::RegisterBuiltInContentPipeline(registry);

    ASSERT_TRUE(registry.HasImporterForSource("shader.fx"));
    EXPECT_EQ(registry.ResolveImporter("shader.fx")->Identity().name, "CNA.EffectSourceImporter");
    EXPECT_EQ(registry.ResolveProcessor(Pipeline::ImportedEffectSourceType)->Identity().name,
              "CNA.EffectSourceProcessor");

    // The compiled route must still be there beside it: one must not shadow the other.
    EXPECT_EQ(registry.ResolveImporter("shader.fxb")->Identity().name,
              "CNA.CompiledEffectImporter");
    EXPECT_EQ(registry.ResolveProcessor(Pipeline::ImportedCompiledEffectType)->Identity().name,
              "CNA.CompiledEffectProcessor");
}

TEST(EffectSourceCommandLineTest, AnFxInASourceTreeIsAnAssetRatherThanBeingSilentlyIgnored)
{
    ScratchDirectory scratch("route");
    WriteEffectProject(scratch);

    // No --fx-compiler and no compiler on this host: the point is that the build *reaches* the
    // route. Reporting "Built: 0  Failed: 0" would mean the extension is not an asset at all.
    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "xnb", "--fx-compiler", "/nonexistent/fxc"});

    EXPECT_EQ(run.exitCode, 1);
    EXPECT_TRUE(run.Says("Failed: 1")) << run.output;
    EXPECT_TRUE(run.Says("CNA.EffectSourceProcessor")) << run.output;
    EXPECT_TRUE(run.Says("no usable effect compiler")) << run.output;
    // Neither of the two failures this must not be confused with.
    EXPECT_FALSE(run.Says("no importer")) << run.output;
    EXPECT_FALSE(run.Says("no writer")) << run.output;
}

TEST(EffectSourceCommandLineTest, TheCompiledFxbRouteStillReachesTheSameWriter)
{
    ScratchDirectory scratch("both");
    WriteEffectProject(scratch);

    // A minimal Effect Framework 9.1 container, written directly as a .fxb: this route needs no
    // compiler at all, and must keep working now that a second route produces the same type.
    std::vector<std::uint8_t> compiled(32u, 0u);
    compiled[0] = 0x01u;
    compiled[1] = 0x09u;
    compiled[2] = 0xFFu;
    compiled[3] = 0xFEu;
    {
        std::ofstream stream(scratch.Source() / "already.fxb", std::ios::binary);
        stream.write(reinterpret_cast<const char*>(compiled.data()),
                     static_cast<std::streamsize>(compiled.size()));
    }

    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "xnb", "--fx-compiler", FakeCompiler()});

    EXPECT_EQ(run.exitCode, 0) << run.output;
    EXPECT_TRUE(run.Says("Built: 2")) << run.output;
    EXPECT_TRUE(std::filesystem::exists(scratch.Output() / "shader.xnb"));
    EXPECT_TRUE(std::filesystem::exists(scratch.Output() / "already.xnb"));
    EXPECT_TRUE(run.Says("CNA.EffectSourceImporter")) << run.output;
    EXPECT_TRUE(run.Says("CNA.CompiledEffectImporter")) << run.output;
}

// -- The process boundary ----------------------------------------------------------------------

TEST(EffectSourceCommandLineTest, TheCompiledBytesTheCompilerReturnedReachTheXnbUnchanged)
{
    ScratchDirectory scratch("payload");
    WriteEffectProject(scratch, "//FAKE: payload=exact-bytes-from-the-compiler\n");

    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "xnb", "--fx-compiler", FakeCompiler()});

    ASSERT_EQ(run.exitCode, 0) << run.output;
    const std::vector<std::uint8_t> xnb = ReadBytes(scratch.Output() / "shader.xnb");
    ASSERT_FALSE(xnb.empty());
    EXPECT_TRUE(Contains(xnb, "Microsoft.Xna.Framework.Content.EffectReader"));
    EXPECT_TRUE(Contains(xnb, "exact-bytes-from-the-compiler"));

    // The Effect Framework 9.1 token the compiler wrote, little-endian, is in the payload too.
    const std::vector<std::uint8_t> token = {0x01u, 0x09u, 0xFFu, 0xFEu};
    bool found = false;
    for (std::size_t start = 0u; start + token.size() <= xnb.size(); ++start)
    {
        if (std::equal(token.begin(), token.end(), xnb.begin() + static_cast<long>(start)))
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "the compiled container's signature is not in the .xnb";
}

TEST(EffectSourceCommandLineTest, TheCompilerIsInvokedWithTheArgumentsTheBackendDocuments)
{
    ScratchDirectory scratch("argv");
    const std::filesystem::path record = scratch.Path() / "argv.txt";
    WriteEffectProject(scratch, "//FAKE: record=" + record.string() + "\n");

    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "xnb", "--fx-compiler", FakeCompiler()});

    ASSERT_EQ(run.exitCode, 0) << run.output;
    const std::string argv = ReadText(record);
    EXPECT_NE(argv.find("arg\t/nologo\n"), std::string::npos) << argv;
    EXPECT_NE(argv.find("arg\t/T\narg\tfx_2_0\n"), std::string::npos) << argv;
    EXPECT_NE(argv.find("arg\t/D\narg\tCNA_REACH=1\n"), std::string::npos) << argv;
    EXPECT_NE(argv.find("arg\t/Qstrip_debug\n"), std::string::npos) << argv;
    // The source directory is on the include path, and the source itself is the last argument.
    EXPECT_NE(argv.find("arg\t/I\narg\t" + scratch.Source().string() + "\n"), std::string::npos)
        << argv;
    EXPECT_NE(argv.find("arg\t" + (scratch.Source() / "shader.fx").string() + "\n"),
              std::string::npos)
        << argv;
    // No launcher was asked for, so none was used.
    EXPECT_NE(argv.find("launcher\t\n"), std::string::npos) << argv;
}

TEST(EffectSourceCommandLineTest, ACompilerPathContainingSpacesStaysOneArgument)
{
    ScratchDirectory scratch("spaces");
    const std::filesystem::path record = scratch.Path() / "argv.txt";
    WriteEffectProject(scratch, "//FAKE: record=" + record.string() + "\n");

    // The normal shape of a Windows SDK install, reproduced here so the no-shell guarantee in
    // RunHostProcess is actually exercised rather than asserted about.
    const std::filesystem::path compiler =
        CopyCompiler(scratch.Path() / "Program Files" / "Microsoft DirectX SDK" / "fxc");

    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "xnb", "--fx-compiler", compiler});

    EXPECT_EQ(run.exitCode, 0) << run.output;
    EXPECT_TRUE(std::filesystem::exists(scratch.Output() / "shader.xnb"));
    // It ran, so it was not re-split: a shell would have looked for "…/Program".
    EXPECT_FALSE(ReadText(record).empty());
}

TEST(EffectSourceCommandLineTest, TheLauncherReachesTheProcessRunnerAsTheProgramThatIsRun)
{
    ScratchDirectory scratch("launcher");
    const std::filesystem::path record = scratch.Path() / "argv.txt";
    WriteEffectProject(scratch, "//FAKE: record=" + record.string() + "\n");

    // The fake compiler behaves as a launcher when handed a program to run, exactly as `wine
    // fxc.exe …` reaches wine, and records which program that was.
    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "xnb",
                                   "--fx-compiler", "C:\\DXSDK\\fxc.exe",
                                   "--fx-compiler-launcher", FakeCompiler()});

    ASSERT_EQ(run.exitCode, 0) << run.output;
    EXPECT_NE(ReadText(record).find("launcher\tC:\\DXSDK\\fxc.exe\n"), std::string::npos)
        << ReadText(record);
}

TEST(EffectSourceCommandLineTest, MoreThanAPipeBufferOnBothStreamsDoesNotDeadlockTheBuild)
{
    ScratchDirectory scratch("flood");
    WriteEffectProject(scratch, "//FAKE: flood\n");

    // A sequential drain of the child's two pipes blocks forever here: 200 KiB on each stream is
    // far past the usual 64 KiB buffer. The build completing at all is the assertion, so it is
    // made under a deadline -- a deadlock must fail, not hang.
    Invocation run;
    WithDeadline(60, [&] { run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                         "--format", "xnb", "--fx-compiler", FakeCompiler()}); });

    EXPECT_EQ(run.exitCode, 0) << run.output.substr(0u, 400u);
    EXPECT_TRUE(std::filesystem::exists(scratch.Output() / "shader.xnb"));
}

TEST(EffectSourceCommandLineTest, ACompilerThatFailsSurfacesItsDiagnosticsAndNoOutput)
{
    ScratchDirectory scratch("failure");
    WriteEffectProject(scratch, "//FAKE: error\n");

    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "xnb", "--fx-compiler", FakeCompiler()});

    EXPECT_EQ(run.exitCode, 1);
    EXPECT_TRUE(run.Says("X3000")) << run.output;
    EXPECT_TRUE(run.Says("fake compiler was asked to fail")) << run.output;
    EXPECT_FALSE(std::filesystem::exists(scratch.Output() / "shader.xnb"));
}

TEST(EffectSourceCommandLineTest, ACompilerThatSucceedsWithoutAContainerIsRefused)
{
    ScratchDirectory scratch("container");
    WriteEffectProject(scratch, "//FAKE: bad-container\n");

    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "xnb", "--fx-compiler", FakeCompiler()});

    EXPECT_EQ(run.exitCode, 1);
    EXPECT_TRUE(run.Says("Effect Framework 9.1")) << run.output;
    EXPECT_FALSE(std::filesystem::exists(scratch.Output() / "shader.xnb"));
}

// -- Option precedence -------------------------------------------------------------------------

TEST(EffectSourceCommandLineTest, TheCommandLineCompilerBeatsTheEnvironment)
{
    ScratchDirectory scratch("cli-over-env");
    WriteEffectProject(scratch);
    const ScopedEnvironment environment("CNA_FXC", "/nonexistent/from-the-environment");

    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "xnb", "--fx-compiler", FakeCompiler()});

    EXPECT_EQ(run.exitCode, 0) << run.output;
    EXPECT_TRUE(std::filesystem::exists(scratch.Output() / "shader.xnb"));
}

TEST(EffectSourceCommandLineTest, TheEnvironmentIsUsedWhenTheCommandLineNamesNoCompiler)
{
    ScratchDirectory scratch("env");
    WriteEffectProject(scratch);
    const ScopedEnvironment environment("CNA_FXC", FakeCompiler().string());

    const Invocation run =
        RunCli({"build", scratch.Source(), "-o", scratch.Output(), "--format", "xnb"});

    EXPECT_EQ(run.exitCode, 0) << run.output;
    EXPECT_TRUE(std::filesystem::exists(scratch.Output() / "shader.xnb"));
}

TEST(EffectSourceCommandLineTest, TheCommandLineLauncherBeatsTheEnvironment)
{
    ScratchDirectory scratch("cli-launcher-over-env");
    const std::filesystem::path record = scratch.Path() / "argv.txt";
    WriteEffectProject(scratch, "//FAKE: record=" + record.string() + "\n");
    const ScopedEnvironment environment("CNA_FXC_LAUNCHER", "/nonexistent/from-the-environment");

    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "xnb",
                                   "--fx-compiler", "C:\\DXSDK\\fxc.exe",
                                   "--fx-compiler-launcher", FakeCompiler()});

    EXPECT_EQ(run.exitCode, 0) << run.output;
    EXPECT_NE(ReadText(record).find("launcher\tC:\\DXSDK\\fxc.exe\n"), std::string::npos);
}

TEST(EffectSourceCommandLineTest, TwoInvocationsInOneProcessDoNotShareCompilerConfiguration)
{
    ScratchDirectory good("independent-good");
    ScratchDirectory bad("independent-bad");
    WriteEffectProject(good);
    WriteEffectProject(bad);

    const Invocation first = RunCli({"build", good.Source(), "-o", good.Output(),
                                     "--format", "xnb", "--fx-compiler", FakeCompiler()});
    ASSERT_EQ(first.exitCode, 0) << first.output;

    const Invocation second = RunCli({"build", bad.Source(), "-o", bad.Output(),
                                      "--format", "xnb",
                                      "--fx-compiler", "/nonexistent/second-run"});
    EXPECT_EQ(second.exitCode, 1) << second.output;
    EXPECT_TRUE(second.Says("no usable effect compiler")) << second.output;

    // And back again: the failed run must not have poisoned the next one either.
    ScratchDirectory again("independent-again");
    WriteEffectProject(again);
    const Invocation third = RunCli({"build", again.Source(), "-o", again.Output(),
                                     "--format", "xnb", "--fx-compiler", FakeCompiler()});
    EXPECT_EQ(third.exitCode, 0) << third.output;
}

TEST(EffectSourceCommandLineTest, APreBuiltRegistryRefusesTheOptionsItCannotApply)
{
    ScratchDirectory scratch("prebuilt");
    WriteEffectProject(scratch);

    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    Pipeline::RegisterBuiltInContentPipeline(*registry);

    std::ostringstream captured;
    std::streambuf* const previous = std::cerr.rdbuf(captured.rdbuf());
    const int exitCode = Pipeline::RunContentCompiler(
        {"build", scratch.Source(), "-o", scratch.Output(), "--format", "xnb",
         "--fx-compiler", FakeCompiler()},
        registry);
    std::cerr.rdbuf(previous);

    // Refusing beats ignoring: the registry already holds a compiler this option cannot replace.
    EXPECT_EQ(exitCode, 2);
    EXPECT_NE(captured.str().find("ContentPipelineRegistryFactory"), std::string::npos)
        << captured.str();
}

TEST(EffectSourceCommandLineTest, AnEmptyRegistryFactoryIsRejected)
{
    EXPECT_THROW(static_cast<void>(
                     Pipeline::RunContentCompiler({}, Pipeline::ContentPipelineRegistryFactory{})),
                 std::invalid_argument);
}

// -- Command-line validation -------------------------------------------------------------------

TEST(EffectSourceCommandLineTest, AnOptionMissingItsArgumentFailsCommandLineValidation)
{
    ScratchDirectory scratch("syntax");
    WriteEffectProject(scratch);

    const Invocation missingCompiler =
        RunCli({"build", scratch.Source(), "-o", scratch.Output(), "--fx-compiler"});
    EXPECT_EQ(missingCompiler.exitCode, 2);
    EXPECT_TRUE(missingCompiler.Says("--fx-compiler requires a path")) << missingCompiler.output;

    const Invocation missingLauncher =
        RunCli({"build", scratch.Source(), "-o", scratch.Output(), "--fx-compiler-launcher"});
    EXPECT_EQ(missingLauncher.exitCode, 2);
    EXPECT_TRUE(missingLauncher.Says("--fx-compiler-launcher requires a program"))
        << missingLauncher.output;

    const Invocation emptyCompiler = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                             "--fx-compiler", ""});
    EXPECT_EQ(emptyCompiler.exitCode, 2);
    EXPECT_TRUE(emptyCompiler.Says("must not be empty")) << emptyCompiler.output;
}

TEST(EffectSourceCommandLineTest, TheUsageTextNamesEveryOptionTheParserImplements)
{
    // The service's unavailability diagnostic advertises both options. A usage text that did not
    // mention them, or a parser that did not implement them, is the contradiction XNAP-A5 closed.
    const Invocation usage = RunCli({"--help"});
    EXPECT_EQ(usage.exitCode, 2);
    EXPECT_TRUE(usage.Says("--fx-compiler <path>")) << usage.output;
    EXPECT_TRUE(usage.Says("--fx-compiler-launcher <program>")) << usage.output;
    EXPECT_TRUE(usage.Says("CNA_FXC / CNA_FXC_LAUNCHER")) << usage.output;
}

TEST(EffectSourceCommandLineTest, AnFxAskedForCnbIsRefusedWithTheArchitecturalReason)
{
    ScratchDirectory scratch("cnb");
    WriteEffectProject(scratch);

    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "cnb", "--fx-compiler", FakeCompiler()});

    EXPECT_EQ(run.exitCode, 1);
    EXPECT_TRUE(run.Says("no writer is registered")) << run.output;
    // The decision, not only the symptom: a reader must learn why this cannot exist.
    EXPECT_TRUE(run.Says("CNB container reserves an Effect identifier")) << run.output;
    EXPECT_TRUE(run.Says("--format xnb")) << run.output;
    EXPECT_FALSE(std::filesystem::exists(scratch.Output() / "shader.cnb"));
}

// -- Incremental builds ------------------------------------------------------------------------

TEST(EffectSourceCommandLineTest, AnUnchangedEffectIsSkippedOnTheSecondBuild)
{
    ScratchDirectory scratch("skip");
    WriteEffectProject(scratch);
    const std::vector<std::filesystem::path> arguments = {
        "build", scratch.Source(), "-o", scratch.Output(), "--format", "xnb",
        "--fx-compiler", FakeCompiler()};

    ASSERT_EQ(RunCli(arguments).exitCode, 0);
    const Invocation second = RunCli(arguments);

    EXPECT_EQ(second.exitCode, 0) << second.output;
    EXPECT_TRUE(second.Says("Built: 0  Skipped: 1")) << second.output;
}

TEST(EffectSourceCommandLineTest, ChangingAnIncludedHeaderRebuildsTheEffectThatIncludesIt)
{
    ScratchDirectory scratch("include");
    WriteEffectProject(scratch);
    WriteText(scratch.Source() / "unrelated.fxh", "float4 Unused;\n");
    const std::vector<std::filesystem::path> arguments = {
        "build", scratch.Source(), "-o", scratch.Output(), "--format", "xnb",
        "--fx-compiler", FakeCompiler()};

    ASSERT_EQ(RunCli(arguments).exitCode, 0);
    ASSERT_TRUE(RunCli(arguments).Says("Skipped: 1"));

    // Only the header the effect includes changed. The whole reason the importer scans includes
    // is that this must rebuild.
    WriteText(scratch.Source() / "common.fxh", "float4 Tint;\nfloat Extra;\n");
    const Invocation afterInclude = RunCli(arguments);
    EXPECT_TRUE(afterInclude.Says("Built: 1")) << afterInclude.output;

    // A file the effect does not include must not.
    WriteText(scratch.Source() / "unrelated.fxh", "float4 Unused;\nfloat AlsoUnused;\n");
    const Invocation afterUnrelated = RunCli(arguments);
    EXPECT_TRUE(afterUnrelated.Says("Built: 0  Skipped: 1")) << afterUnrelated.output;
}

TEST(EffectSourceCommandLineTest, ChangingTheCompilerIdentityRebuilds)
{
    ScratchDirectory scratch("identity");
    WriteEffectProject(scratch);

    // Two copies of one program announcing different versions: the compiler's identity is folded
    // into the processor's component version, so a different compiler must not silently reuse the
    // artifacts the previous one produced.
    const std::filesystem::path first = CopyCompiler(scratch.Path() / "tools" / "fxc@1.0.0");
    const std::filesystem::path second = CopyCompiler(scratch.Path() / "tools" / "fxc@2.0.0");

    ASSERT_EQ(RunCli({"build", scratch.Source(), "-o", scratch.Output(), "--format", "xnb",
                      "--fx-compiler", first}).exitCode, 0);
    ASSERT_TRUE(RunCli({"build", scratch.Source(), "-o", scratch.Output(), "--format", "xnb",
                        "--fx-compiler", first}).Says("Skipped: 1"));

    const Invocation changed = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                       "--format", "xnb", "--fx-compiler", second});
    EXPECT_TRUE(changed.Says("Built: 1")) << changed.output;

    // And back to the first: still a rebuild, because the identity changed again.
    const Invocation back = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                    "--format", "xnb", "--fx-compiler", first});
    EXPECT_TRUE(back.Says("Built: 1")) << back.output;
}

// -- Processor parameters through the ordinary project file ------------------------------------

TEST(EffectSourceCommandLineTest, ProfileDefinesAndDebugAreAuthoredInTheProjectFileLikeAnyOthers)
{
    ScratchDirectory scratch("parameters");
    const std::filesystem::path record = scratch.Path() / "argv.txt";
    WriteEffectProject(scratch, "//FAKE: record=" + record.string() + "\n");

    // The `.fx` route invents no configuration syntax of its own: these are the generic processor
    // parameters every other route uses, in the file every other route reads.
    const std::filesystem::path configuration = scratch.Source() / "pipeline-config.json";
    WriteText(configuration,
              R"({"format":"CNA.ContentPipeline.Config","version":1,"assets":{)"
              R"("shader.fx":{"parameters":{)"
              R"("profile":{"type":"string","value":"hidef"},)"
              R"("defines":{"type":"string","value":"ALPHA=1;BETA"},)"
              R"("debug":{"type":"bool","value":true}}}}})");

    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "xnb", "--config", configuration,
                                   "--fx-compiler", FakeCompiler()});

    ASSERT_EQ(run.exitCode, 0) << run.output;
    const std::string argv = ReadText(record);
    EXPECT_NE(argv.find("arg\t/D\narg\tCNA_HIDEF=1\n"), std::string::npos) << argv;
    EXPECT_NE(argv.find("arg\t/D\narg\tALPHA=1\n"), std::string::npos) << argv;
    EXPECT_NE(argv.find("arg\t/D\narg\tBETA\n"), std::string::npos) << argv;
    // Debug information was asked for, so the strip-debug switch is not there.
    EXPECT_NE(argv.find("arg\t/Zi\n"), std::string::npos) << argv;
    EXPECT_EQ(argv.find("arg\t/Qstrip_debug\n"), std::string::npos) << argv;
}

TEST(EffectSourceCommandLineTest, ChangingAParameterRebuildsTheEffect)
{
    ScratchDirectory scratch("parameter-rebuild");
    WriteEffectProject(scratch);
    const std::filesystem::path configuration = scratch.Source() / "pipeline-config.json";

    const auto configure = [&configuration](const std::string& parameters)
    {
        WriteText(configuration,
                  R"({"format":"CNA.ContentPipeline.Config","version":1,"assets":{)"
                  R"("shader.fx":{"parameters":{)" + parameters + R"(}}}})");
    };
    const std::vector<std::filesystem::path> arguments = {
        "build", scratch.Source(), "-o", scratch.Output(), "--format", "xnb",
        "--config", configuration, "--fx-compiler", FakeCompiler()};

    configure(R"("profile":{"type":"string","value":"reach"})");
    ASSERT_EQ(RunCli(arguments).exitCode, 0);
    ASSERT_TRUE(RunCli(arguments).Says("Skipped: 1"));

    // Each of the three changes the bytes a real compiler would produce, so each must rebuild.
    configure(R"("profile":{"type":"string","value":"hidef"})");
    EXPECT_TRUE(RunCli(arguments).Says("Built: 1")) << "profile";
    ASSERT_TRUE(RunCli(arguments).Says("Skipped: 1"));

    configure(R"("profile":{"type":"string","value":"hidef"},)"
              R"("defines":{"type":"string","value":"GAMMA=2"})");
    EXPECT_TRUE(RunCli(arguments).Says("Built: 1")) << "defines";
    ASSERT_TRUE(RunCli(arguments).Says("Skipped: 1"));

    configure(R"("profile":{"type":"string","value":"hidef"},)"
              R"("defines":{"type":"string","value":"GAMMA=2"},)"
              R"("debug":{"type":"bool","value":true})");
    EXPECT_TRUE(RunCli(arguments).Says("Built: 1")) << "debug";
    EXPECT_TRUE(RunCli(arguments).Says("Skipped: 1"));
}

// -- The parser and its own help -----------------------------------------------------------------

TEST(EffectSourceCommandLineTest, EveryOptionTheParserAcceptsAppearsInTheUsageText)
{
    // XNAP-A5 fixed a service that documented two options the parser did not implement. This is
    // the mirror image, and the reason it is worth a test rather than a proofread: `--output` was
    // accepted and undocumented, so nobody could have found it except by reading the parser. The
    // list is derived from the parser's own source rather than restated, so a new option is
    // covered the moment it is written.
    const std::filesystem::path parser =
        std::filesystem::path(CNA_TOOLS_SOURCE_DIR) / "content" / "content.cpp";
    ASSERT_TRUE(std::filesystem::is_regular_file(parser)) << parser;
    const std::string source = ReadText(parser);

    std::set<std::string> accepted;
    const std::string marker = "IsOption(argument, \"";
    for (std::size_t at = source.find(marker); at != std::string::npos;
         at = source.find(marker, at + 1u))
    {
        const std::size_t start = at + marker.size();
        const std::size_t end = source.find('"', start);
        const std::string option = source.substr(start, end - start);
        // Only long options: `-o` is documented as part of the usage line's own syntax, and the
        // two subcommands are not options at all.
        if (option.rfind("--", 0u) == 0u) { accepted.insert(option); }
    }
    ASSERT_GE(accepted.size(), 10u) << "the parser's option list could not be derived";

    const Invocation usage = RunCli({});
    ASSERT_EQ(usage.exitCode, 2);
    for (const std::string& option : accepted)
    {
        EXPECT_TRUE(usage.Says(option))
            << "the parser accepts '" << option << "' and the usage text never mentions it. An "
               "option nobody documents is the same defect as an option nobody implements, "
               "pointing the other way.";
    }
}

TEST(EffectSourceCommandLineTest, TheCompilerAndTheLauncherResolveIndependently)
{
    ScratchDirectory scratch("axes");
    const std::filesystem::path record = scratch.Path() / "argv.txt";
    WriteEffectProject(scratch, "//FAKE: record=" + record.string() + "\n");

    // Two axes, not one setting: naming a launcher on the command line must not also decide the
    // compiler, and vice versa. The failure this pins is subtle -- a build that supplies only
    // --fx-compiler on a machine whose CMake baked in a launcher still runs *through* that
    // launcher, and a reader who thought one option overrode both would be surprised by which
    // program actually failed to start.
    const ScopedEnvironment compiler("CNA_FXC", "/nonexistent/from-the-environment");
    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "xnb",
                                   "--fx-compiler", FakeCompiler(),
                                   "--fx-compiler-launcher", FakeCompiler()});

    ASSERT_EQ(run.exitCode, 0) << run.output;
    // The launcher was handed the compiler path, which is what running one *through* the other
    // means, and neither came from the environment.
    EXPECT_NE(ReadText(record).find("launcher\t" + FakeCompiler().string() + "\n"),
              std::string::npos)
        << ReadText(record);
    EXPECT_EQ(ReadText(record).find("from-the-environment"), std::string::npos);
}

TEST(EffectSourceCommandLineTest, NoConfigureTimeCompilerDefaultIsBakedIntoThisBuild)
{
    // Almost every test above supplies --fx-compiler and expects the build to succeed. A build
    // configured with -DCNA_FXC_LAUNCHER=wine defeats that: the launcher axis resolves
    // independently (see TheCompilerAndTheLauncherResolveIndependently), so the stand-in compiler
    // is correctly run *through* a launcher that is not there, and sixteen tests fail for one
    // reason none of them names. This test names it once, first, so the cause is legible rather
    // than inferred.
    ScratchDirectory scratch("no-default");
    WriteEffectProject(scratch);

    const Invocation run = RunCli({"build", scratch.Source(), "-o", scratch.Output(),
                                   "--format", "xnb", "--fx-compiler", FakeCompiler()});

    EXPECT_EQ(run.exitCode, 0)
        << "the stand-in compiler did not run, which usually means this build was configured with "
           "-DCNA_FXC_EXECUTABLE or -DCNA_FXC_LAUNCHER. Those are legitimate settings and the "
           "route honours them; they are simply incompatible with a suite that supplies its own "
           "compiler. Re-configure with both empty to run these tests.\n"
        << run.output;
}
