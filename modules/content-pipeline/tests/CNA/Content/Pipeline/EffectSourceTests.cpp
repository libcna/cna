// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-A1/XNAP-A2/XNAP-A3: the `.fx` source route.
//
// No Microsoft `fxc` exists in this environment, and none can be vendored, so every test here
// drives the pipeline through a substituted EffectCompilerService. That is the whole reason the
// compiler is an interface with a process boundary behind it rather than a linked library: the
// routing, the include scan, the containment rules, the fingerprint contribution and the
// diagnostics are CNA's own code and are verifiable without any compiler at all. What is *not*
// verified here is that real `fxc` output loads in a real XNA runtime -- see XNAP-A4.

// Before everything else, and narrowed: RepeatedLaunchesDoNotLeakHandlesOrDescriptors asks the
// operating system how many handles this process holds, and there is no portable way to do that.
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <process.h>
#else
#  include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/EffectCompilerService.hpp"
#include "CNA/Content/Pipeline/EffectSourceContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbOutputContentPipeline.hpp"
#include "CNA/Internal/HostProcess.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Xnb = CNA::Internal::Xnb;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_fx_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;
        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

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

    /**
     * @brief A compiler that records what it was asked and returns bytes the test chose.
     *
     * Deliberately not a mock framework: the recorded request is compared field by field, so a
     * change that silently stopped forwarding the profile or the defines fails a test rather than
     * passing an interaction check.
     */
    class FakeEffectCompiler final : public Pipeline::EffectCompilerService
    {
    public:
        FakeEffectCompiler() = default;

        [[nodiscard]] Pipeline::EffectCompilerIdentity Identity() const override
        {
            return identity_;
        }

        [[nodiscard]] bool Available() const override { return available_; }

        [[nodiscard]] std::string UnavailableReason() const override { return reason_; }

        [[nodiscard]] Pipeline::EffectCompileResult Compile(
            const Pipeline::EffectCompileRequest& request) const override
        {
            lastRequest_ = request;
            ++compileCount_;
            return result_;
        }

        void MakeUnavailable(std::string reason)
        {
            available_ = false;
            reason_ = std::move(reason);
        }

        void SetIdentity(Pipeline::EffectCompilerIdentity identity)
        {
            identity_ = std::move(identity);
        }

        void SucceedWith(std::vector<std::uint8_t> bytecode)
        {
            result_.succeeded = true;
            result_.bytecode = std::move(bytecode);
        }

        void FailWith(std::vector<Pipeline::EffectCompilerDiagnostic> diagnostics)
        {
            result_.succeeded = false;
            result_.bytecode.clear();
            result_.diagnostics = std::move(diagnostics);
        }

        [[nodiscard]] const Pipeline::EffectCompileRequest& LastRequest() const
        {
            return lastRequest_;
        }

        [[nodiscard]] int CompileCount() const { return compileCount_; }

    private:
        Pipeline::EffectCompilerIdentity identity_{"fake", "1.0", "fx_2_0"};
        bool available_ = true;
        std::string reason_;
        Pipeline::EffectCompileResult result_;
        mutable Pipeline::EffectCompileRequest lastRequest_;
        mutable int compileCount_ = 0;
    };

    /**
     * @brief A plausible compiled effect: the Effect Framework 9.1 token, then filler.
     *
     * The processor refuses anything that is not a 9.1 container, which is the refusal that
     * matters most on this route -- so a fake has to satisfy it, or every test would be measuring
     * that one check instead of the behaviour it is about.
     */
    std::vector<std::uint8_t> FakeBytecode()
    {
        std::vector<std::uint8_t> bytes(64u, 0x11u);
        bytes[0] = 0x01u;
        bytes[1] = 0x09u;
        bytes[2] = 0xFFu;
        bytes[3] = 0xFEu;
        return bytes;
    }

    std::shared_ptr<const Pipeline::ContentPipelineRegistry> MakeRegistry(
        std::shared_ptr<const Pipeline::EffectCompilerService> compiler)
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterEffectSourceContentPipeline(*registry, std::move(compiler));
        Pipeline::RegisterXnbOutputContentPipeline(*registry, {});
        return registry;
    }

    Pipeline::ContentBuildResult Build(
        const ScratchDirectory& scratch, const std::string& source,
        const std::shared_ptr<const Pipeline::EffectCompilerService>& compiler,
        const Pipeline::ContentProcessorParameters& parameters = {})
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry(compiler));
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = scratch.Path();
        request.source = source;
        request.logicalName = "Effects/shader";
        request.outputFormat = Pipeline::ContentOutputFormat::Xnb;
        request.parameters = parameters;
        return pipeline.Build(request);
    }
} // namespace

// -- The include scanner (XNAP-A2) -------------------------------------------------------------

TEST(EffectSourceIncludeScanTest, BothQuotedAndAngledFormsAreFoundWithTheirLineNumbers)
{
    const std::vector<std::pair<std::string, int>> found =
        Pipeline::ScanEffectSourceIncludes("float4 tint;\n#include \"common.fxh\"\n"
                                           "#include <lighting.fxh>\n");
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0].first, "common.fxh");
    EXPECT_EQ(found[0].second, 2);
    EXPECT_EQ(found[1].first, "lighting.fxh");
    EXPECT_EQ(found[1].second, 3);
}

TEST(EffectSourceIncludeScanTest, ADirectiveInsideACommentOrAStringIsNotADependency)
{
    // The safe direction differs per construct: a commented-out or quoted directive is not a
    // dependency and must not be recorded, because resolving it would fail a build over text the
    // compiler never sees.
    const std::vector<std::pair<std::string, int>> found = Pipeline::ScanEffectSourceIncludes(
        "// #include \"line_comment.fxh\"\n"
        "/* #include \"block_comment.fxh\"\n"
        "   #include \"still_in_the_block.fxh\" */\n"
        "static const char* s = \"#include \\\"in_a_string.fxh\\\"\";\n"
        "#include \"real.fxh\"\n");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].first, "real.fxh");
    EXPECT_EQ(found[0].second, 5);
}

TEST(EffectSourceIncludeScanTest, AnIncludeInsideAFalseConditionalIsStillADependency)
{
    // Conditionals are deliberately not evaluated. A rebuild too many costs a second; a rebuild
    // too few ships a stale artifact, so the scanner errs towards over-reporting and says so.
    const std::vector<std::pair<std::string, int>> found = Pipeline::ScanEffectSourceIncludes(
        "#if 0\n#include \"never_compiled.fxh\"\n#endif\n");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].first, "never_compiled.fxh");
}

// -- The importer (XNAP-A2) --------------------------------------------------------------------

TEST(EffectSourceImporterTest, TheWholeIncludeTreeIsRecordedTransitivelyAndOnlyOnce)
{
    ScratchDirectory scratch("includes");
    WriteText(scratch.Path() / "shader.fx",
              "#include \"a.fxh\"\n#include \"b.fxh\"\ntechnique T {}\n");
    WriteText(scratch.Path() / "a.fxh", "#include \"shared.fxh\"\n");
    WriteText(scratch.Path() / "b.fxh", "#include \"shared.fxh\"\n");
    WriteText(scratch.Path() / "shared.fxh", "float4 tint;\n");

    auto compiler = std::make_shared<FakeEffectCompiler>();
    compiler->SucceedWith(FakeBytecode());
    const Pipeline::ContentBuildResult result = Build(scratch, "shader.fx", compiler);

    // Diamond include: `shared.fxh` is reached twice and recorded once.
    int sharedCount = 0;
    int sourceFileDependencies = 0;
    for (const Pipeline::ContentDependency& dependency : result.dependencies)
    {
        if (dependency.kind != Pipeline::ContentDependencyKind::SourceFile) { continue; }
        ++sourceFileDependencies;
        if (dependency.identity.find("shared.fxh") != std::string::npos) { ++sharedCount; }
    }
    EXPECT_EQ(sharedCount, 1);
    EXPECT_EQ(sourceFileDependencies, 3) << "a.fxh, b.fxh and shared.fxh, each once";
}

TEST(EffectSourceImporterTest, AnIncludeCycleTerminatesInsteadOfRecursingForever)
{
    ScratchDirectory scratch("cycle");
    WriteText(scratch.Path() / "shader.fx", "#include \"a.fxh\"\n");
    WriteText(scratch.Path() / "a.fxh", "#include \"b.fxh\"\n");
    WriteText(scratch.Path() / "b.fxh", "#include \"a.fxh\"\n");

    auto compiler = std::make_shared<FakeEffectCompiler>();
    compiler->SucceedWith(FakeBytecode());
    const Pipeline::ContentBuildResult result = Build(scratch, "shader.fx", compiler);
    EXPECT_EQ(compiler->CompileCount(), 1);
}

TEST(EffectSourceImporterTest, AnIncludeEscapingTheSourceRootIsRefused)
{
    // The containment rule is the pipeline's, not this route's, but an `.fx` is the one source
    // kind that names other files by path, so it is the one that has to prove it holds.
    ScratchDirectory scratch("escape");
    WriteText(scratch.Path() / "shader.fx", "#include \"../../../etc/passwd\"\n");

    auto compiler = std::make_shared<FakeEffectCompiler>();
    compiler->SucceedWith(FakeBytecode());
    EXPECT_THROW((void)Build(scratch, "shader.fx", compiler), std::exception);
    EXPECT_EQ(compiler->CompileCount(), 0) << "nothing should reach the compiler";
}

TEST(EffectSourceImporterTest, AMissingIncludeFailsNamingTheDirectiveAndItsLine)
{
    ScratchDirectory scratch("missing");
    WriteText(scratch.Path() / "shader.fx", "technique T {}\n#include \"absent.fxh\"\n");

    auto compiler = std::make_shared<FakeEffectCompiler>();
    compiler->SucceedWith(FakeBytecode());
    try
    {
        (void)Build(scratch, "shader.fx", compiler);
        FAIL() << "a missing include must fail the build";
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("absent.fxh"), std::string::npos) << message;
    }
}

// -- The processor (XNAP-A3) -------------------------------------------------------------------

TEST(EffectSourceProcessorTest, TheProfileDefinesAndDebugFlagReachTheCompilerUnchanged)
{
    ScratchDirectory scratch("request");
    WriteText(scratch.Path() / "shader.fx", "technique T {}\n");

    auto compiler = std::make_shared<FakeEffectCompiler>();
    compiler->SucceedWith(FakeBytecode());

    Pipeline::ContentProcessorParameters parameters;
    parameters.Set(Pipeline::EffectProfileParameter, std::string("hidef"));
    parameters.Set(Pipeline::EffectDefinesParameter, std::string("LIGHTS=2;SHADOWS"));
    parameters.Set(Pipeline::EffectDebugParameter, true);
    (void)Build(scratch, "shader.fx", compiler, parameters);

    const Pipeline::EffectCompileRequest& request = compiler->LastRequest();
    EXPECT_EQ(request.profile, Pipeline::EffectSourceProfile::HiDef);
    EXPECT_TRUE(request.debugInformation);
    ASSERT_EQ(request.defines.size(), 2u);
    EXPECT_EQ(request.defines.at("LIGHTS"), "2");
    EXPECT_EQ(request.defines.at("SHADOWS"), "");
}

TEST(EffectSourceProcessorTest, AnUnavailableCompilerFailsWithItsOwnExplanationNotAShaderError)
{
    ScratchDirectory scratch("unavailable");
    WriteText(scratch.Path() / "shader.fx", "technique T {}\n");

    auto compiler = std::make_shared<FakeEffectCompiler>();
    compiler->MakeUnavailable("no fxc on PATH and CNA_FXC is unset");
    try
    {
        (void)Build(scratch, "shader.fx", compiler);
        FAIL() << "a build with no compiler must fail";
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("no fxc on PATH"), std::string::npos)
            << "the reason has to survive to the author: " << message;
    }
    EXPECT_EQ(compiler->CompileCount(), 0);
}

TEST(EffectSourceProcessorTest, ACompilerDiagnosticSurvivesToTheBuildFailure)
{
    ScratchDirectory scratch("diagnostic");
    WriteText(scratch.Path() / "shader.fx", "technique T { pass P { } }\n");

    auto compiler = std::make_shared<FakeEffectCompiler>();
    Pipeline::EffectCompilerDiagnostic diagnostic;
    diagnostic.file = "shader.fx";
    diagnostic.line = 1;
    diagnostic.isError = true;
    diagnostic.code = "X3000";
    diagnostic.message = "syntax error: unexpected token";
    compiler->FailWith({diagnostic});

    try
    {
        (void)Build(scratch, "shader.fx", compiler);
        FAIL() << "a failed compile must fail the build";
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("X3000"), std::string::npos) << message;
        EXPECT_NE(message.find("syntax error"), std::string::npos) << message;
    }
}

TEST(EffectSourceProcessorTest, TheCompilerIdentityEntersTheProcessorVersion)
{
    // Two compilers can legitimately turn the same source into different bytes, so the backend has
    // to reach the build fingerprint. The processor's own component version is where it lands,
    // because the manifest already fingerprints that.
    ScratchDirectory scratch("fingerprint");
    WriteText(scratch.Path() / "shader.fx", "technique T {}\n");

    auto first = std::make_shared<FakeEffectCompiler>();
    first->SucceedWith(FakeBytecode());
    first->SetIdentity({"fxc", "9.29.952.3111", "fx_2_0"});
    const Pipeline::ContentBuildResult a = Build(scratch, "shader.fx", first);

    auto second = std::make_shared<FakeEffectCompiler>();
    second->SucceedWith(FakeBytecode());
    second->SetIdentity({"fxc", "10.0.0.0", "fx_2_0"});
    const Pipeline::ContentBuildResult b = Build(scratch, "shader.fx", second);

    EXPECT_EQ(a.processor.name, b.processor.name);
    EXPECT_NE(a.processor.version, b.processor.version)
        << "a different compiler must not look like the same build input";
}

// -- End to end (XNAP-A3) ----------------------------------------------------------------------

TEST(EffectSourceRouteTest, AnFxSourceBuildsToAnEffectXnbCarryingTheCompilerBytesVerbatim)
{
    ScratchDirectory scratch("endtoend");
    WriteText(scratch.Path() / "shader.fx", "#include \"common.fxh\"\ntechnique T {}\n");
    WriteText(scratch.Path() / "common.fxh", "float4 tint;\n");

    auto compiler = std::make_shared<FakeEffectCompiler>();
    const std::vector<std::uint8_t> bytecode = FakeBytecode();
    compiler->SucceedWith(bytecode);

    const Pipeline::ContentBuildResult result = Build(scratch, "shader.fx", compiler);
    EXPECT_EQ(result.outputFormat, Pipeline::ContentOutputFormat::Xnb);
    EXPECT_EQ(result.output.rootReaderName, "Microsoft.Xna.Framework.Content.EffectReader");

    // XnbCanonicalValue has no compiled-effect alternative, so this asserts on the file itself:
    // the reader name in the type table, and the bytecode appearing verbatim in the payload
    // preceded by its UInt32 length -- which is the whole of what EffectReader consumes.
    const std::vector<std::uint8_t>& file = result.output.bytes;
    const std::string text(file.begin(), file.end());
    EXPECT_NE(text.find("Microsoft.Xna.Framework.Content.EffectReader"), std::string::npos);

    std::vector<std::uint8_t> expected{
        static_cast<std::uint8_t>(bytecode.size() & 0xFFu),
        static_cast<std::uint8_t>((bytecode.size() >> 8) & 0xFFu),
        static_cast<std::uint8_t>((bytecode.size() >> 16) & 0xFFu),
        static_cast<std::uint8_t>((bytecode.size() >> 24) & 0xFFu)};
    expected.insert(expected.end(), bytecode.begin(), bytecode.end());
    EXPECT_NE(std::search(file.begin(), file.end(), expected.begin(), expected.end()),
              file.end())
        << "the pipeline must not transform shader bytes";
}

TEST(EffectSourceRouteTest, AnFxSourceHasNoCnbRouteAndSaysWhy)
{
    // Deliberate: the CNB container reserves an Effect identifier and has no schema for it,
    // because CNA has many renderers and a .cnb carrying one API's bytecode is useless on the
    // others. The build must refuse rather than write something unusable.
    ScratchDirectory scratch("nocnb");
    WriteText(scratch.Path() / "shader.fx", "technique T {}\n");

    auto compiler = std::make_shared<FakeEffectCompiler>();
    compiler->SucceedWith(FakeBytecode());

    const Pipeline::ContentPipeline pipeline(MakeRegistry(compiler));
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = scratch.Path();
    request.source = "shader.fx";
    request.logicalName = "Effects/shader";
    request.outputFormat = Pipeline::ContentOutputFormat::Cnb;
    EXPECT_THROW((void)pipeline.Build(request), std::exception);
}

// -- The process runner (XNAP-A1, WINCLOSE-0003) ------------------------------------------------
//
// These used to ask for /bin/echo and /bin/sh, so on Windows every one of them reported "could not
// start" and the CreateProcessW half of RunHostProcess -- command line construction, quoting,
// Unicode, the pipe drain, exit status -- had no test behind it at all (WINNATIVE-F31). They go
// through cna_argv_echo now, which reports the argument vector it actually received; see that
// program's own comment for why neither cmd.exe nor a shell could stand in for it.

namespace
{
    /** @brief The argv-reporting helper the build puts next to the test binary. */
    std::filesystem::path ArgvEcho() { return std::filesystem::path(CNA_ARGV_ECHO_PATH); }

    /** @brief This process's id, spelled the way MSVC and POSIX each require. */
    [[nodiscard]] int CurrentProcessId()
    {
#if defined(_WIN32)
        return ::_getpid();
#else
        return ::getpid();
#endif
    }

    /**
     * @brief Decodes cna_argv_echo's length-prefixed report back into a vector.
     *
     * Length-prefixed rather than line-separated precisely so an argument containing a newline --
     * which is legal on both platforms -- cannot be mistaken for two arguments here.
     */
    std::vector<std::string> DecodeArgvReport(const std::string& text)
    {
        std::vector<std::string> arguments;
        std::size_t cursor = text.find('\n');
        if (cursor == std::string::npos) { return arguments; }
        ++cursor;
        while (cursor < text.size())
        {
            const std::size_t colon = text.find(':', cursor);
            if (text.compare(cursor, 4u, "arg=") != 0 || colon == std::string::npos) { break; }
            const std::size_t length =
                static_cast<std::size_t>(std::strtoul(text.c_str() + cursor + 4u, nullptr, 10));
            if (colon + 1u + length > text.size()) { break; }
            arguments.push_back(text.substr(colon + 1u, length));
            cursor = colon + 1u + length + 1u;
        }
        return arguments;
    }

    /** @brief The arguments the child received, excluding argv[0]. */
    std::vector<std::string> EchoedArguments(const std::vector<std::string>& arguments,
                                             CNA::Internal::HostProcessResult& result)
    {
        result = CNA::Internal::RunHostProcess(ArgvEcho(), arguments);
        std::vector<std::string> received = DecodeArgvReport(result.standardOutput);
        if (!received.empty()) { received.erase(received.begin()); }
        return received;
    }
}

TEST(HostProcessTest, StandardOutputAndAZeroExitAreCapturedFromARealProcess)
{
    CNA::Internal::HostProcessResult result;
    const std::vector<std::string> received = EchoedArguments({"hello", "world"}, result);
    ASSERT_TRUE(result.started) << result.failure;
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(received, (std::vector<std::string>{"hello", "world"}));
    EXPECT_TRUE(result.standardError.empty());
}

TEST(HostProcessTest, AProcessWithNoArgumentsAtAllStartsAndReportsOnlyItsOwnName)
{
    // The command line is then just the quoted executable, with nothing after it -- a shape the
    // Windows builder reaches only when the argument vector is empty.
    CNA::Internal::HostProcessResult result;
    const std::vector<std::string> received = EchoedArguments({}, result);
    ASSERT_TRUE(result.started) << result.failure;
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_TRUE(received.empty());
}

TEST(HostProcessTest, ANonZeroExitIsAResultRatherThanAFailureToStart)
{
    CNA::Internal::HostProcessResult result;
    const std::vector<std::string> received =
        EchoedArguments({"--cna-exit=3", "--cna-stderr=err"}, result);
    ASSERT_TRUE(result.started) << result.failure;
    EXPECT_EQ(result.exitCode, 3);
    EXPECT_EQ(received, (std::vector<std::string>{"--cna-exit=3", "--cna-stderr=err"}));
    EXPECT_EQ(result.standardError, "err");
}

TEST(HostProcessTest, AMissingExecutableReportsWhyRatherThanLookingLikeAFailedCompile)
{
    const CNA::Internal::HostProcessResult result = CNA::Internal::RunHostProcess(
        "/nonexistent/definitely-not-a-compiler", {"--version"});
    EXPECT_FALSE(result.started);
    EXPECT_FALSE(result.failure.empty());
}

TEST(HostProcessTest, AnArgumentContainingSpacesIsNotResplit)
{
    // The reason arguments are a vector and not a command string: a Windows SDK path has spaces
    // in it, and a shell would turn one argument into three. Now checked where it matters -- on
    // Windows, where RunHostProcess really does have to rebuild a command string and quote it.
    CNA::Internal::HostProcessResult result;
    const std::vector<std::string> received =
        EchoedArguments({"C:\\Program Files\\fxc.exe"}, result);
    ASSERT_TRUE(result.started) << result.failure;
    EXPECT_EQ(received, (std::vector<std::string>{"C:\\Program Files\\fxc.exe"}));
}

TEST(HostProcessTest, EveryPathologicalArgumentSurvivesTheRoundTripUnchanged)
{
    // CreateProcessW takes one string, not a vector, so on Windows RunHostProcess has to quote by
    // the rules CommandLineToArgvW documents and the child has to get the vector back. These are
    // the shapes those rules exist for: a backslash is literal EXCEPT before a quote, where it
    // must be doubled, and a run of them before the closing quote must be doubled too or the
    // quote is escaped instead of closing.
    const std::vector<std::string> sent = {
        "plain",
        "hello world",
        "\"quoted\"",
        "back\\slash",
        "trailing\\",
        "two\\\\trailing\\\\",
        "backslash\\\"quote",
        "\"",
        "\\\"",
        "",                       // an empty argument must survive as an empty argument
        "tab\there",
        "new\nline",              // legal in an argument; the length prefix is why it decodes
        "semi;colon&amp|pipe^caret",
        "%NOT_EXPANDED%",         // no shell is involved, so this is literal
        "C:\\Program Files (x86)\\Microsoft DirectX SDK\\fxc.exe",
    };

    CNA::Internal::HostProcessResult result;
    const std::vector<std::string> received = EchoedArguments(sent, result);
    ASSERT_TRUE(result.started) << result.failure;
    ASSERT_EQ(received.size(), sent.size()) << result.standardOutput;
    for (std::size_t index = 0u; index < sent.size(); ++index)
    {
        EXPECT_EQ(received[index], sent[index]) << "argument " << index << " did not round-trip";
    }
}

TEST(HostProcessTest, NonAsciiArgumentsSurviveTheRoundTripAsUtf8)
{
    // The arguments are UTF-8 std::strings and Windows takes a UTF-16 command line, so there is a
    // conversion on this path that Linux does not have. Spelled from code points that no single
    // Windows ANSI code page can represent, so a conversion that quietly went through the ANSI
    // code page -- the WINNATIVE-F30 defect, in a new place -- fails here rather than passing on
    // a machine whose code page happens to fit.
    const std::vector<std::string> sent = {
        "\xC5\xBElu\xC5\xA5ou\xC4\x8Dk\xC3\xBD",  // zluťoučký
        "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E",   // 日本語
        "\xD0\x9A\xD0\xB8\xD1\x80\xD0\xB8\xD0\xBB\xD0\xBB\xD0\xB8\xD1\x86\xD0\xB0",
        "\xF0\x9F\x98\x80",                       // an emoji: a surrogate pair in UTF-16
        "mixed \xE6\x97\xA5 and spaces",
    };

    CNA::Internal::HostProcessResult result;
    const std::vector<std::string> received = EchoedArguments(sent, result);
    ASSERT_TRUE(result.started) << result.failure;
    ASSERT_EQ(received.size(), sent.size()) << result.standardOutput;
    for (std::size_t index = 0u; index < sent.size(); ++index)
    {
        EXPECT_EQ(received[index], sent[index]) << "argument " << index << " was not UTF-8 clean";
    }
}

TEST(HostProcessTest, AnExecutableUnderANonAsciiPathIsFoundAndRun)
{
    // The executable is a std::filesystem::path and is widened with wstring() on Windows, so it
    // has to survive a directory the ANSI code page cannot spell. Copied rather than built there,
    // because what is under test is the launch, not the toolchain.
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("cna-argv-\xC5\xBElu\xC5\xA5ou\xC4\x8Dk\xC3\xBD-\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E-" +
         std::to_string(CurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    ASSERT_FALSE(error) << "could not create a non-ASCII directory here";

    const std::filesystem::path copied = root / ArgvEcho().filename();
    std::filesystem::copy_file(ArgvEcho(), copied,
                               std::filesystem::copy_options::overwrite_existing, error);
    ASSERT_FALSE(error) << "could not copy the helper into a non-ASCII directory";

    const CNA::Internal::HostProcessResult result =
        CNA::Internal::RunHostProcess(copied, {"ran"});
    ASSERT_TRUE(result.started) << result.failure;
    EXPECT_EQ(result.exitCode, 0);
    std::vector<std::string> received = DecodeArgvReport(result.standardOutput);
    ASSERT_FALSE(received.empty()) << result.standardOutput;
    received.erase(received.begin());
    EXPECT_EQ(received, (std::vector<std::string>{"ran"}));

    std::filesystem::remove_all(root, error);
}

TEST(HostProcessTest, OutputLargerThanAPipeBufferIsNotTruncatedOrDeadlocked)
{
    // Both streams have to be drained concurrently. Writing more than a pipe buffer to each is
    // what catches a runner that reads one to completion before starting on the other. Under a
    // deadline, because the failure mode is a hang: without one this reports "timeout" minutes
    // later and takes the whole run with it.
    //
    // Now runs on Windows too, where the drain is a thread rather than poll() and had never been
    // exercised.
    constexpr std::size_t kBlock = 200000u;
    CNA::Internal::HostProcessResult result;
    std::future<void> pending = std::async(std::launch::async, [&]
    {
        result = CNA::Internal::RunHostProcess(
            ArgvEcho(), {"--cna-bulk=" + std::to_string(kBlock)});
    });
    if (pending.wait_for(std::chrono::seconds(60)) == std::future_status::timeout)
    {
        ADD_FAILURE() << "RunHostProcess did not return within 60 seconds; the two output "
                         "streams are not being drained concurrently.";
        std::cerr.flush();
        ::_Exit(1);
    }
    pending.get();
    ASSERT_TRUE(result.started) << result.failure;
    EXPECT_EQ(result.exitCode, 0);
    // The bulk block is written after the argv report, so it is the tail of standard output --
    // checked as the tail rather than by counting 'o', which the helper's own path can contain.
    ASSERT_GE(result.standardOutput.size(), kBlock);
    EXPECT_EQ(result.standardOutput.substr(result.standardOutput.size() - kBlock),
              std::string(kBlock, 'o'));
    EXPECT_EQ(result.standardError, std::string(kBlock, 'e'));
}

TEST(HostProcessTest, RepeatedLaunchesDoNotLeakHandlesOrDescriptors)
{
    // Every launch opens two pipes, and on Windows a process and a thread handle as well. A
    // runner that forgets one of them fails only after a content build has shelled out a few
    // thousand times, which is exactly the scale a real pipeline reaches and no single-launch
    // test can see.
    const auto openResources = []() -> long
    {
#if defined(_WIN32)
        DWORD handles = 0u;
        return GetProcessHandleCount(GetCurrentProcess(), &handles) ? static_cast<long>(handles)
                                                                    : -1;
#else
        std::error_code error;
        const std::filesystem::directory_iterator descriptors("/proc/self/fd", error);
        if (error) { return -1; }
        return static_cast<long>(std::distance(std::filesystem::begin(descriptors),
                                               std::filesystem::end(descriptors)));
#endif
    };

    CNA::Internal::HostProcessResult warmup = CNA::Internal::RunHostProcess(ArgvEcho(), {"warm"});
    ASSERT_TRUE(warmup.started) << warmup.failure;

    const long before = openResources();
    if (before < 0) { GTEST_SKIP() << "this host does not report an open-resource count"; }

    constexpr int kLaunches = 40;
    for (int launch = 0; launch < kLaunches; ++launch)
    {
        const CNA::Internal::HostProcessResult result =
            CNA::Internal::RunHostProcess(ArgvEcho(), {"launch", std::to_string(launch)});
        ASSERT_TRUE(result.started) << result.failure;
        ASSERT_EQ(result.exitCode, 0);
    }

    const long after = openResources();
    ASSERT_GE(after, 0);
    // A small allowance rather than equality: a C runtime may cache a handle of its own on the
    // first launch. What this must catch is growth proportional to the launch count, and leaking
    // even one handle per launch would put this at least kLaunches above the baseline.
    EXPECT_LE(after - before, 4)
        << "open resources grew from " << before << " to " << after << " across " << kLaunches
        << " launches; a per-launch handle is being leaked";
}
