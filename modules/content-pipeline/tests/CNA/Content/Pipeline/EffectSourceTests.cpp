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

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
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

// -- The process runner (XNAP-A1) --------------------------------------------------------------

TEST(HostProcessTest, StandardOutputAndAZeroExitAreCapturedFromARealProcess)
{
    const CNA::Internal::HostProcessResult result =
        CNA::Internal::RunHostProcess("/bin/echo", {"hello", "world"});
    ASSERT_TRUE(result.started) << result.failure;
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(result.standardOutput, "hello world\n");
    EXPECT_TRUE(result.standardError.empty());
}

TEST(HostProcessTest, ANonZeroExitIsAResultRatherThanAFailureToStart)
{
    const CNA::Internal::HostProcessResult result =
        CNA::Internal::RunHostProcess("/bin/sh", {"-c", "echo out; echo err 1>&2; exit 3"});
    ASSERT_TRUE(result.started) << result.failure;
    EXPECT_EQ(result.exitCode, 3);
    EXPECT_EQ(result.standardOutput, "out\n");
    EXPECT_EQ(result.standardError, "err\n");
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
    // in it, and a shell would turn one argument into three.
    const CNA::Internal::HostProcessResult result =
        CNA::Internal::RunHostProcess("/bin/echo", {"C:\\Program Files\\fxc.exe"});
    ASSERT_TRUE(result.started) << result.failure;
    EXPECT_EQ(result.standardOutput, "C:\\Program Files\\fxc.exe\n");
}

TEST(HostProcessTest, OutputLargerThanAPipeBufferIsNotTruncatedOrDeadlocked)
{
    // Both streams have to be drained concurrently. Writing more than a pipe buffer to each is
    // what catches a runner that reads one to completion before starting on the other.
    const CNA::Internal::HostProcessResult result = CNA::Internal::RunHostProcess(
        "/bin/sh", {"-c", "yes abcdefghij | head -c 200000; yes klmnopqrst | head -c 200000 1>&2"});
    ASSERT_TRUE(result.started) << result.failure;
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(result.standardOutput.size(), 200000u);
    EXPECT_EQ(result.standardError.size(), 200000u);
}
