// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-A6: the `.fx` route's edges, and the process runner's.
//
// EffectSourceTests.cpp covers the route's ordinary shape and EffectSourceCommandLineTests.cpp
// covers it as a product. This file covers what is left: the places a scanner, a ceiling, or a
// child process behaves in a way a reader would have to guess at. Two of these are behaviours
// rather than bugs -- the scanner is a deliberately small preprocessor subset, and it must stay
// that way -- so the tests exist to pin the policy, not to demand a different one.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/EffectCompilerService.hpp"
#include "CNA/Content/Pipeline/EffectSourceContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbOutputContentPipeline.hpp"
#include "CNA/Internal/HostProcess.hpp"

namespace Pipeline = CNA::Content::Pipeline;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_fxhard_" + tag + "_" +
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

    /** @brief A compiler that returns a fixed container, so a test can be about something else. */
    class ConstantEffectCompiler final : public Pipeline::EffectCompilerService
    {
    public:
        [[nodiscard]] Pipeline::EffectCompilerIdentity Identity() const override
        {
            return {"constant", "1.0", "fx_2_0"};
        }

        [[nodiscard]] bool Available() const override { return true; }

        [[nodiscard]] std::string UnavailableReason() const override { return {}; }

        [[nodiscard]] Pipeline::EffectCompileResult Compile(
            const Pipeline::EffectCompileRequest&) const override
        {
            Pipeline::EffectCompileResult result;
            result.succeeded = true;
            result.bytecode.assign(64u, 0x22u);
            result.bytecode[0] = 0x01u;
            result.bytecode[1] = 0x09u;
            result.bytecode[2] = 0xFFu;
            result.bytecode[3] = 0xFEu;
            return result;
        }
    };

    /** @brief Imports one `.fx` and returns everything the importer recorded. */
    Pipeline::ContentBuildResult Build(const ScratchDirectory& scratch, const std::string& source)
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterEffectSourceContentPipeline(
            *registry, std::make_shared<const ConstantEffectCompiler>());
        Pipeline::RegisterXnbOutputContentPipeline(*registry, {});
        const Pipeline::ContentPipeline pipeline(registry);
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = scratch.Path();
        request.source = source;
        request.logicalName = "Effects/shader";
        request.outputFormat = Pipeline::ContentOutputFormat::Xnb;
        return pipeline.Build(request);
    }

    /** @brief The source-file dependencies the importer recorded, by identity, sorted. */
    [[nodiscard]] std::vector<std::string> Dependencies(const Pipeline::ContentBuildResult& result)
    {
        std::vector<std::string> identities;
        for (const Pipeline::ContentDependency& dependency : result.dependencies)
        {
            if (dependency.kind != Pipeline::ContentDependencyKind::SourceFile) { continue; }
            identities.push_back(dependency.identity);
        }
        std::sort(identities.begin(), identities.end());
        return identities;
    }
} // namespace

// -- Scanner policy: what is and is not a dependency --------------------------------------------

TEST(EffectSourceScannerTest, WhitespaceBetweenTheHashAndTheKeywordIsStillADirective)
{
    const auto includes = Pipeline::ScanEffectSourceIncludes(
        "#   include \"spaced.fxh\"\n"
        "\t#\tinclude\t<tabbed.fxh>\n");

    ASSERT_EQ(includes.size(), 2u);
    EXPECT_EQ(includes[0].first, "spaced.fxh");
    EXPECT_EQ(includes[1].first, "tabbed.fxh");
}

TEST(EffectSourceScannerTest, CarriageReturnsDoNotHideADirectiveOrMiscountLines)
{
    const auto includes = Pipeline::ScanEffectSourceIncludes(
        "float4 Tint;\r\n"
        "#include \"windows.fxh\"\r\n"
        "\r\n"
        "#include <second.fxh>\r\n");

    ASSERT_EQ(includes.size(), 2u);
    EXPECT_EQ(includes[0].first, "windows.fxh");
    EXPECT_EQ(includes[0].second, 2);
    EXPECT_EQ(includes[1].first, "second.fxh");
    EXPECT_EQ(includes[1].second, 4);
}

TEST(EffectSourceScannerTest, AnEscapedQuoteDoesNotEndAStringAndSwallowTheNextDirective)
{
    // If the escape were not honoured, the scanner would think the string ended at the escaped
    // quote, treat the rest as code, and then read the commented path as a dependency.
    const auto includes = Pipeline::ScanEffectSourceIncludes(
        "string Semantic = \"a \\\" quote // #include \\\"ghost.fxh\\\"\";\n"
        "#include \"real.fxh\"\n");

    ASSERT_EQ(includes.size(), 1u);
    EXPECT_EQ(includes[0].first, "real.fxh");
}

TEST(EffectSourceScannerTest, AnUnterminatedBlockCommentSwallowsTheRestAndTerminates)
{
    // Deliberate: everything after an unclosed `/*` is comment, exactly as a compiler would see
    // it. The point of the test is that the scanner ends rather than reading past the buffer.
    const auto includes = Pipeline::ScanEffectSourceIncludes(
        "#include \"before.fxh\"\n"
        "/* an unclosed comment\n"
        "#include \"after.fxh\"\n");

    ASSERT_EQ(includes.size(), 1u);
    EXPECT_EQ(includes[0].first, "before.fxh");
}

TEST(EffectSourceScannerTest, AnUnterminatedIncludeIsNotADependency)
{
    // No closing delimiter and no closing delimiter before the newline: there is no path here to
    // record, and inventing one would make the incremental build depend on a file nobody named.
    const auto includes = Pipeline::ScanEffectSourceIncludes(
        "#include \"unterminated.fxh\n"
        "#include <also-unterminated.fxh\n"
        "#include \"\"\n"
        "#include \"closed.fxh\"\n");

    ASSERT_EQ(includes.size(), 1u);
    EXPECT_EQ(includes[0].first, "closed.fxh");
}

TEST(EffectSourceScannerTest, ADirectiveThatIsNotAtTheStartOfALineIsNotADirective)
{
    const auto includes = Pipeline::ScanEffectSourceIncludes(
        "float x; #include \"trailing.fxh\"\n"
        "#include \"leading.fxh\"\n");

    ASSERT_EQ(includes.size(), 1u);
    EXPECT_EQ(includes[0].first, "leading.fxh");
}

TEST(EffectSourceScannerTest, ConditionalsAreNotEvaluatedAndThatIsThePolicy)
{
    // The safe direction, and the reason this is a scanner rather than a preprocessor: recording
    // an include that a compile will not read costs a spurious rebuild, while *missing* one costs
    // a stale artifact. CNA will not implement `#if` evaluation to trade the first for the second.
    const auto includes = Pipeline::ScanEffectSourceIncludes(
        "#if 0\n"
        "#include \"disabled.fxh\"\n"
        "#endif\n"
        "#ifdef NEVER_DEFINED\n"
        "#include \"also-disabled.fxh\"\n"
        "#endif\n");

    ASSERT_EQ(includes.size(), 2u);
    EXPECT_EQ(includes[0].first, "disabled.fxh");
    EXPECT_EQ(includes[1].first, "also-disabled.fxh");
}

// -- Include resolution ------------------------------------------------------------------------

TEST(EffectSourceImporterHardeningTest, TwoSpellingsOfOneFileAreRecordedAsOneDependency)
{
    ScratchDirectory scratch("aliased");
    WriteText(scratch.Path() / "shared.fxh", "float4 Shared;\n");
    WriteText(scratch.Path() / "nested" / "middle.fxh", "#include \"../shared.fxh\"\n");
    WriteText(scratch.Path() / "shader.fx",
              "#include \"shared.fxh\"\n"
              "#include \"nested/middle.fxh\"\n"
              "#include \"./shared.fxh\"\n"
              "float4 PS() : COLOR0 { return Shared; }\n");

    const Pipeline::ContentBuildResult result = Build(scratch, "shader.fx");

    // Three authored spellings, two files. Keyed by resolved path, so a rebuild is not triggered
    // three times over by one edit, and the dependency set does not grow with the spelling count.
    const std::vector<std::string> recorded = Dependencies(result);
    ASSERT_EQ(recorded.size(), 2u) << "one per file, not one per spelling";
    EXPECT_NE(recorded[0].find("middle.fxh"), std::string::npos) << recorded[0];
    EXPECT_NE(recorded[1].find("shared.fxh"), std::string::npos) << recorded[1];
}

TEST(EffectSourceImporterHardeningTest, AnIncludeTreeAtTheCeilingBuildsAndOneOverItIsRefused)
{
    // The ceiling is 256 *included* files. At the boundary the build must succeed: a limit that
    // rejects the case it is documented to allow is a different limit.
    const auto writeChain = [](const ScratchDirectory& scratch, const int count)
    {
        std::string root;
        for (int index = 0; index < count; ++index)
        {
            root += "#include \"part" + std::to_string(index) + ".fxh\"\n";
            WriteText(scratch.Path() / ("part" + std::to_string(index) + ".fxh"),
                      "float Part" + std::to_string(index) + ";\n");
        }
        WriteText(scratch.Path() / "shader.fx", root + "float4 PS() : COLOR0 { return 0; }\n");
    };

    {
        ScratchDirectory atLimit("at-limit");
        writeChain(atLimit, 256);
        EXPECT_NO_THROW(static_cast<void>(Build(atLimit, "shader.fx")));
    }
    {
        ScratchDirectory overLimit("over-limit");
        writeChain(overLimit, 257);
        try
        {
            static_cast<void>(Build(overLimit, "shader.fx"));
            FAIL() << "an include tree over the ceiling must be refused";
        }
        catch (const Pipeline::ContentPipelineError& error)
        {
            EXPECT_NE(std::string(error.what()).find("includes more than 256 files"),
                      std::string::npos)
                << error.what();
        }
    }
}

TEST(EffectSourceImporterHardeningTest, AFileAtTheSizeCeilingIsReadAndOneOverItIsRefused)
{
    constexpr std::size_t ceiling = 8u * 1024u * 1024u;
    const std::string tail = "\nfloat4 PS() : COLOR0 { return 0; }\n";

    {
        ScratchDirectory atLimit("size-at-limit");
        std::string source(ceiling - tail.size(), ' ');
        source += tail;
        ASSERT_EQ(source.size(), ceiling);
        WriteText(atLimit.Path() / "shader.fx", source);
        EXPECT_NO_THROW(static_cast<void>(Build(atLimit, "shader.fx")));
    }
    {
        ScratchDirectory overLimit("size-over-limit");
        WriteText(overLimit.Path() / "shader.fx", std::string(ceiling + 1u, ' '));
        try
        {
            static_cast<void>(Build(overLimit, "shader.fx"));
            FAIL() << "a source over the size ceiling must be refused";
        }
        catch (const Pipeline::ContentPipelineError& error)
        {
            EXPECT_NE(std::string(error.what()).find("-byte ceiling"), std::string::npos)
                << error.what();
        }
    }
}

#if !defined(_WIN32)
TEST(EffectSourceImporterHardeningTest, AnIncludeThroughASymlinkOutOfTheSourceRootIsRefused)
{
    ScratchDirectory scratch("symlink");
    ScratchDirectory outside("symlink-target");
    WriteText(outside.Path() / "secret.fxh", "float4 Secret;\n");

    std::error_code error;
    std::filesystem::create_directory_symlink(outside.Path(), scratch.Path() / "elsewhere", error);
    if (error) { GTEST_SKIP() << "this filesystem does not allow symlinks: " << error.message(); }

    WriteText(scratch.Path() / "shader.fx",
              "#include \"elsewhere/secret.fxh\"\n"
              "float4 PS() : COLOR0 { return Secret; }\n");

    // Containment is decided on the *resolved* path, so a symlink is not a way around it.
    EXPECT_THROW(static_cast<void>(Build(scratch, "shader.fx")), Pipeline::ContentPipelineError);
}
#endif

// -- Compiler results the processor must not turn into an asset ---------------------------------

namespace
{
    /** @brief A compiler whose outcome the test dictates. */
    class ScriptedEffectCompiler final : public Pipeline::EffectCompilerService
    {
    public:
        explicit ScriptedEffectCompiler(Pipeline::EffectCompileResult result)
            : result_(std::move(result))
        {
        }

        [[nodiscard]] Pipeline::EffectCompilerIdentity Identity() const override
        {
            return {"scripted", "1.0", "fx_2_0"};
        }

        [[nodiscard]] bool Available() const override { return true; }

        [[nodiscard]] std::string UnavailableReason() const override { return {}; }

        [[nodiscard]] Pipeline::EffectCompileResult Compile(
            const Pipeline::EffectCompileRequest&) const override
        {
            return result_;
        }

    private:
        Pipeline::EffectCompileResult result_;
    };

    [[nodiscard]] std::string BuildWithResult(const ScratchDirectory& scratch,
                                              Pipeline::EffectCompileResult result)
    {
        WriteText(scratch.Path() / "shader.fx", "float4 PS() : COLOR0 { return 0; }\n");
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterEffectSourceContentPipeline(
            *registry, std::make_shared<const ScriptedEffectCompiler>(std::move(result)));
        Pipeline::RegisterXnbOutputContentPipeline(*registry, {});
        const Pipeline::ContentPipeline pipeline(registry);
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = scratch.Path();
        request.source = "shader.fx";
        request.logicalName = "Effects/shader";
        request.outputFormat = Pipeline::ContentOutputFormat::Xnb;
        try
        {
            static_cast<void>(pipeline.Build(request));
        }
        catch (const std::exception& error)
        {
            return error.what();
        }
        return {};
    }
}

TEST(EffectSourceProcessorHardeningTest, SuccessWithNoBytesIsAFailureRatherThanAnEmptyEffect)
{
    ScratchDirectory scratch("empty-success");
    Pipeline::EffectCompileResult result;
    result.succeeded = true;

    const std::string message = BuildWithResult(scratch, std::move(result));

    EXPECT_NE(message.find("Effect Framework 9.1"), std::string::npos) << message;
}

TEST(EffectSourceProcessorHardeningTest, AFailureCarryingNoDiagnosticsStillNamesTheCompiler)
{
    ScratchDirectory scratch("silent-failure");
    Pipeline::EffectCompileResult result;
    result.succeeded = false;

    const std::string message = BuildWithResult(scratch, std::move(result));

    EXPECT_NE(message.find("scripted"), std::string::npos) << message;
    EXPECT_FALSE(message.empty());
}

TEST(EffectSourceProcessorHardeningTest, WarningsAreReportedAndDoNotFailTheBuild)
{
    ScratchDirectory scratch("warning");
    Pipeline::EffectCompileResult result;
    result.succeeded = true;
    result.bytecode.assign(64u, 0x33u);
    result.bytecode[0] = 0x01u;
    result.bytecode[1] = 0x09u;
    result.bytecode[2] = 0xFFu;
    result.bytecode[3] = 0xFEu;
    Pipeline::EffectCompilerDiagnostic warning;
    warning.isError = false;
    warning.file = "shader.fx";
    warning.line = 3;
    warning.code = "X3206";
    warning.message = "implicit truncation of vector type";
    result.diagnostics.push_back(warning);

    EXPECT_TRUE(BuildWithResult(scratch, std::move(result)).empty());
}

// -- The process runner --------------------------------------------------------------------------

#if !defined(_WIN32)
namespace
{
    /** @brief Runs a shell command as a child, under a deadline so a hang fails rather than waits. */
    [[nodiscard]] CNA::Internal::HostProcessResult RunShell(const std::string& script)
    {
        CNA::Internal::HostProcessResult result;
        std::future<void> pending = std::async(std::launch::async, [&]
        {
            result = CNA::Internal::RunHostProcess("/bin/sh", {"-c", script});
        });
        if (pending.wait_for(std::chrono::seconds(60)) == std::future_status::timeout)
        {
            ADD_FAILURE() << "RunHostProcess did not return within 60 seconds for: " << script;
            std::cerr.flush();
            std::quick_exit(1);
        }
        pending.get();
        return result;
    }
}

TEST(HostProcessHardeningTest, ClosingOneStreamLongBeforeTheOtherDoesNotTruncateEither)
{
    // stdout ends immediately while stderr keeps writing past a pipe buffer. A runner that stops
    // once *a* stream reaches end-of-file loses the rest of the other.
    const CNA::Internal::HostProcessResult errLast =
        RunShell("echo first; exec 1>&-; yes klmnopqrst | head -c 200000 1>&2");
    EXPECT_TRUE(errLast.started) << errLast.failure;
    EXPECT_EQ(errLast.standardOutput, "first\n");
    EXPECT_EQ(errLast.standardError.size(), 200000u);

    // And the mirror image.
    const CNA::Internal::HostProcessResult outLast =
        RunShell("echo problem 1>&2; exec 2>&-; yes abcdefghij | head -c 200000");
    EXPECT_TRUE(outLast.started) << outLast.failure;
    EXPECT_EQ(outLast.standardError, "problem\n");
    EXPECT_EQ(outLast.standardOutput.size(), 200000u);
}

TEST(HostProcessHardeningTest, AChildThatUsesOnlyOneStreamLeavesTheOtherEmpty)
{
    const CNA::Internal::HostProcessResult onlyOut = RunShell("echo out-only");
    EXPECT_EQ(onlyOut.standardOutput, "out-only\n");
    EXPECT_TRUE(onlyOut.standardError.empty()) << onlyOut.standardError;

    const CNA::Internal::HostProcessResult onlyErr = RunShell("echo err-only 1>&2");
    EXPECT_TRUE(onlyErr.standardOutput.empty()) << onlyErr.standardOutput;
    EXPECT_EQ(onlyErr.standardError, "err-only\n");

    const CNA::Internal::HostProcessResult neither = RunShell("exit 0");
    EXPECT_TRUE(neither.standardOutput.empty());
    EXPECT_TRUE(neither.standardError.empty());
    EXPECT_EQ(neither.exitCode, 0);
}

TEST(HostProcessHardeningTest, AChildKilledBySignalIsReportedAsStartedAndNonZero)
{
    // A compiler that segfaults has run; it has not failed to start. Reporting it as "could not
    // be started" would send a user looking for a missing executable that is right there.
    const CNA::Internal::HostProcessResult result = RunShell("kill -TERM $$");

    EXPECT_TRUE(result.started) << result.failure;
    EXPECT_NE(result.exitCode, 0);
}

TEST(HostProcessHardeningTest, ExitStatusIsPreservedExactly)
{
    for (const int expected : {0, 1, 2, 42, 127})
    {
        const CNA::Internal::HostProcessResult result =
            RunShell("exit " + std::to_string(expected));
        EXPECT_TRUE(result.started) << result.failure;
        EXPECT_EQ(result.exitCode, expected);
    }
}

TEST(HostProcessHardeningTest, RepeatedRunsDoNotLeakFileDescriptors)
{
    // Every run opens two pipes. If the parent kept an end, this loop would exhaust the process's
    // descriptor table long before it finished, and the count would climb monotonically.
    const auto openDescriptors = []() -> std::size_t
    {
        std::error_code error;
        const std::filesystem::directory_iterator entries("/proc/self/fd", error);
        if (error) { return 0u; }
        return static_cast<std::size_t>(
            std::distance(std::filesystem::begin(entries), std::filesystem::end(entries)));
    };

    const std::size_t before = openDescriptors();
    if (before == 0u) { GTEST_SKIP() << "/proc/self/fd is not readable here"; }
    for (int index = 0; index < 64; ++index)
    {
        const CNA::Internal::HostProcessResult result = RunShell("echo x; echo y 1>&2");
        ASSERT_TRUE(result.started) << result.failure;
    }
    EXPECT_LE(openDescriptors(), before + 2u);
}
#endif
