// SPDX-License-Identifier: MS-PL
//
// TQ-038: what XNA's EffectProcessor says when the compiler refuses an effect -- the text and the
// ContentIdentity the InvalidContentException carries -- and what it does with a compile that
// succeeded. The oracle is the genuine processor run in process under Wine (TQ-038 lane oracle,
// cases effect/debug/* and effect/release/*, the same compiler errors the committed graphics oracle
// and the genuine BuildContent's shader/compile_error record) and its IL:
//   DoSomethingAwesomeWithErrorsAndWarnings IL_00f4-0169 -- "Errors compiling {0}:{1}{2}" with the
//     identity's SourceFilename, under CreateContentIdentityFromErrorString's identity, help link
//     EffectProcessor_WarningsOrErrorsWhenCompilingEffect;
//   CreateContentIdentityFromErrorString IL_0000-00d0 -- the first `file(line,column): ` line gives a
//     new identity: the input's SourceTool, the named file or the input's own, the location as the
//     fragment; no such line keeps the input's identity;
//   IL_004f-00f3 -- a successful compile logs each line as a warning under that line's identity;
//   Process IL_01ac-01b7 -- the compiled effect carries the input's identity;
//   EnableEffectDebugging IL_0000-0035 -- Auto is debugging only for a Windows "Debug" build.
// The compiler is scripted, as every other EffectProcessor test here does, so what is measured is
// the processor rather than fxc; "<source>" in a scripted diagnostic is the file the processor
// handed the compiler, which is how fxc names a line of the effect itself.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "CNA/Content/Pipeline/EffectCompilerService.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/EffectContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/EffectProcessor.hpp"
#include "System/NotSupportedException.hpp"
#include "XnaProcessorTestContext.hpp"

namespace
{
    namespace Pipeline = Microsoft::Xna::Framework::Content::Pipeline;
    namespace Canonical = CNA::Content::Pipeline;
    using Pipeline::ContentIdentity;
    using Pipeline::Processors::EffectProcessor;
    using Pipeline::Processors::EffectProcessorDebugMode;
    using CNA::Tests::XnaProcessors::Spell;

    /** @brief A compiler that answers what the test scripted, naming "<source>" as the file it was handed. */
    class ScriptedCompiler final : public Canonical::EffectCompilerService
    {
    public:
        /** @brief What the next compile answers. */
        Canonical::EffectCompileResult result;

        /** @brief What the last compile was asked for. */
        mutable Canonical::EffectCompileRequest request;

        [[nodiscard]] Canonical::EffectCompilerIdentity Identity() const override { return {}; }
        [[nodiscard]] bool Available() const override { return true; }
        [[nodiscard]] std::string UnavailableReason() const override { return {}; }
        [[nodiscard]] Canonical::EffectCompileResult Compile(const Canonical::EffectCompileRequest& compileRequest) const override
        {
            request = compileRequest;
            Canonical::EffectCompileResult answered = result;
            for (Canonical::EffectCompilerDiagnostic& diagnostic : answered.diagnostics)
            {
                if (diagnostic.file == "<source>")
                {
                    diagnostic.file = CNA::Internal::PathToUtf8(compileRequest.source);
                }
            }
            return answered;
        }
    };

    /** @brief One scripted line of compiler output. */
    [[nodiscard]] Canonical::EffectCompilerDiagnostic Line(std::string file, const int line, const int column,
                                                          std::string code, std::string message, const bool isError = true)
    {
        Canonical::EffectCompilerDiagnostic diagnostic;
        diagnostic.file = std::move(file);
        diagnostic.line = line;
        diagnostic.column = column;
        diagnostic.code = std::move(code);
        diagnostic.message = std::move(message);
        diagnostic.isError = isError;
        return diagnostic;
    }

    /** @brief Records every warning: help link, identity and text. */
    class RecordingLogger final : public Pipeline::ContentBuildLogger
    {
    public:
        /** @brief `help|{identity}|message`, one per warning. */
        std::vector<std::string> warnings;

    protected:
        void LogMessage(const std::string&) override {}
        void LogImportantMessage(const std::string&) override {}
        void LogWarning(const std::string& helpLink, const ContentIdentity& identity, const std::string& message) override
        {
            warnings.push_back(helpLink + "|" + Spell(identity) + "|" + message);
        }
    };

    /** @brief A context whose configuration and platform a test chooses; HiDef. */
    class EffectContext final : public Pipeline::ContentProcessorContext
    {
    public:
        EffectContext(std::string configuration, const Pipeline::TargetPlatform platform)
            : configuration_(std::move(configuration)), platform_(platform)
        {
        }

        [[nodiscard]] std::string getBuildConfigurationProperty() const override { return configuration_; }
        [[nodiscard]] std::string getIntermediateDirectoryProperty() const override { return "obj"; }
        [[nodiscard]] Pipeline::ContentBuildLogger& getLoggerProperty() const override { return logger; }
        [[nodiscard]] std::string getOutputDirectoryProperty() const override { return "bin"; }
        [[nodiscard]] std::string getOutputFilenameProperty() const override { return "asset.xnb"; }
        [[nodiscard]] const Pipeline::OpaqueDataDictionary& getParametersProperty() const override { return parameters_; }
        [[nodiscard]] Pipeline::TargetPlatform getTargetPlatformProperty() const override { return platform_; }
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::GraphicsProfile getTargetProfileProperty() const override
        {
            return Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef;
        }
        void AddDependency(const std::string&) override {}
        void AddOutputFile(const std::string&) override {}

        /** @brief What the processor logged. */
        mutable RecordingLogger logger;

    protected:
        [[nodiscard]] Pipeline::ContentObject BuildAndLoadAssetCore(
            const std::string&, const ContentIdentity&, const std::string&, const Pipeline::OpaqueDataDictionary&,
            const std::string&, const std::string&, const std::string&) override
        {
            throw System::NotSupportedException("BuildAndLoadAsset");
        }
        [[nodiscard]] std::string BuildAssetCore(const std::string&, const ContentIdentity&, const std::string&,
                                                 const Pipeline::OpaqueDataDictionary&, const std::string&,
                                                 const std::string&, const std::string&, const std::string&) override
        {
            throw System::NotSupportedException("BuildAsset");
        }
        [[nodiscard]] Pipeline::ContentObject ConvertCore(const Pipeline::ContentObject&, const std::string&,
                                                          const Pipeline::OpaqueDataDictionary&, const std::string&,
                                                          const std::string&) override
        {
            throw System::NotSupportedException("Convert");
        }

    private:
        std::string configuration_;
        Pipeline::TargetPlatform platform_;
        Pipeline::OpaqueDataDictionary parameters_;
    };

    /** @brief What one refused compile ended in. */
    struct Refused
    {
        std::string message = "built";
        std::string identity;
        std::string help;
    };

    /** @brief Compiles @p identity's effect with @p compiler's script in @p configuration on Windows. */
    [[nodiscard]] Refused Refuse(const std::shared_ptr<ScriptedCompiler>& compiler, const ContentIdentity& identity,
                                 const std::string& configuration)
    {
        EffectProcessor processor(compiler);
        auto effect = std::make_shared<Pipeline::Graphics::EffectContent>();
        effect->setEffectCodeProperty("this is not an effect");
        effect->setIdentityProperty(identity);
        EffectContext context(configuration, Pipeline::TargetPlatform::Windows);
        Refused refused;
        try
        {
            (void)processor.Process(effect, context);
        }
        catch (const Pipeline::InvalidContentException& error)
        {
            refused.message = error.getMessageProperty();
            refused.identity = Spell(error.getContentIdentityProperty());
            refused.help = error.getHelpLinkProperty();
        }
        return refused;
    }

    [[nodiscard]] std::shared_ptr<ScriptedCompiler> Failing(std::vector<Canonical::EffectCompilerDiagnostic> lines)
    {
        auto compiler = std::make_shared<ScriptedCompiler>();
        compiler->result.succeeded = false;
        compiler->result.diagnostics = std::move(lines);
        return compiler;
    }
}

TEST(XnaEffectProcessorRefusal, AnErrorInTheEffectNamesItsLineUnderTheEffectsIdentity)
{
    // effect/release/full and effect/debug/full: an identity that names a file, an importer and a
    // fragment. In Release the compiler's own path is gone and the line reads "(1,6): ..."; in Debug
    // the #line directive makes it the identity's file. Either way the identity is a new one --
    // the effect's file and tool, the line's location, never the effect's own fragment.
    const ContentIdentity identity("content/bad.fx", "EffectImporter", "frag");
    const auto compiler = Failing({Line("<source>", 1, 6, "X3000", "invalid target or usage string")});

    const Refused release = Refuse(compiler, identity, "Release");
    EXPECT_EQ(release.message, "Errors compiling content/bad.fx:\n(1,6): error X3000: invalid target or usage string");
    EXPECT_EQ(release.identity, "{content/bad.fx|EffectImporter|1,6}");
    EXPECT_EQ(release.help, "EffectProcessor_WarningsOrErrorsWhenCompilingEffect");

    const Refused debug = Refuse(compiler, identity, "Debug");
    EXPECT_EQ(debug.message,
              "Errors compiling content/bad.fx:\ncontent/bad.fx(1,6): error X3000: invalid target or usage string");
    EXPECT_EQ(debug.identity, "{content/bad.fx|EffectImporter|1,6}");
    EXPECT_EQ(debug.help, "EffectProcessor_WarningsOrErrorsWhenCompilingEffect");
}

TEST(XnaEffectProcessorRefusal, TheFirstLocatedLineGivesTheIdentity)
{
    // effect/release/two_errors: both lines are kept, in order, and the first one's location is the
    // fragment -- 1,31, not the second line's 1,24.
    const auto compiler = Failing({Line("<source>", 1, 31, "X3004", "undeclared identifier 'TINT'"),
                                   Line("<source>", 1, 24, "X3080", "'PS': function must return a value")});
    const Refused refused = Refuse(compiler, ContentIdentity("undefined.fx", "EffectImporter"), "Release");
    EXPECT_EQ(refused.message, "Errors compiling undefined.fx:\n(1,31): error X3004: undeclared identifier 'TINT'\n"
                               "(1,24): error X3080: 'PS': function must return a value");
    EXPECT_EQ(refused.identity, "{undefined.fx|EffectImporter|1,31}");
}

TEST(XnaEffectProcessorRefusal, AnErrorWithoutALocationKeepsTheEffectsIdentity)
{
    // effect/*/no_location (and the graphics oracle's effectprocessor/empty_code): the compiler's
    // lines carry no location, so the refusal carries the effect's identity unchanged, and each line
    // is XNA's verbatim rather than "error: ...".
    const auto compiler = Failing({Line("", 0, 0, "", "ID3DXEffectCompiler: There were no techniques"),
                                   Line("", 0, 0, "", "ID3DXEffectCompiler: Compilation failed")});
    const Refused refused = Refuse(compiler, ContentIdentity("empty.fx", "EffectImporter", "frag"), "Release");
    EXPECT_EQ(refused.message, "Errors compiling empty.fx:\nID3DXEffectCompiler: There were no techniques\n"
                               "ID3DXEffectCompiler: Compilation failed");
    EXPECT_EQ(refused.identity, "{empty.fx|EffectImporter|frag}");
}

TEST(XnaEffectProcessorRefusal, AnErrorInAnIncludedFileNamesThatFile)
{
    // effect/release/include_error: the line names the included file as the compiler spelled it, and
    // that file -- not the effect's -- becomes the identity's SourceFilename.
    const auto compiler = Failing({Line("inc_bad.fxh", 2, 26, "X3004", "undeclared identifier 'Missing'"),
                                   Line("inc_bad.fxh", 2, 19, "X3080", "'Helper': function must return a value")});
    const Refused refused = Refuse(compiler, ContentIdentity("content/inc_main.fx", "EffectImporter"), "Release");
    EXPECT_EQ(refused.message, "Errors compiling content/inc_main.fx:\ninc_bad.fxh(2,26): error X3004: undeclared "
                               "identifier 'Missing'\ninc_bad.fxh(2,19): error X3080: 'Helper': function must return a value");
    EXPECT_EQ(refused.identity, "{inc_bad.fxh|EffectImporter|2,26}");
}

TEST(XnaEffectProcessorRefusal, AnEffectWithoutAFileNameIsRefusedUnderAnEmptyOne)
{
    // effect/*/empty_filename and effect/*/no_identity: no file to name in the header, no #line even
    // in Debug, and an identity whose SourceFilename is empty.
    const auto compiler = Failing({Line("<source>", 1, 6, "X3000", "invalid target or usage string")});
    for (const char* configuration : {"Release", "Debug"})
    {
        SCOPED_TRACE(configuration);
        const Refused refused = Refuse(compiler, ContentIdentity("", "EffectImporter"), configuration);
        EXPECT_EQ(refused.message, "Errors compiling :\n(1,6): error X3000: invalid target or usage string");
        EXPECT_EQ(refused.identity, "{|EffectImporter|1,6}");
    }
}

TEST(XnaEffectProcessor, ACompiledEffectCarriesTheInputsIdentityAndLogsTheCompilersWarnings)
{
    // effect/*/warning_only: the compiled effect is the input's identity (Process IL_01ac-01b7). A
    // warning the compiler gave is logged under its own line's identity with what follows the
    // location (IL_0082-00e3); a line that is not a warning -- fxc's own report of where it saved
    // the output -- is not one of XNA's.
    auto compiler = std::make_shared<ScriptedCompiler>();
    compiler->result.succeeded = true;
    compiler->result.bytecode = {0xCF, 0x0B, 0xF0, 0xBC};
    compiler->result.diagnostics = {
        Line("<source>", 1, 50, "X3206", "implicit truncation of vector type", false),
        Line("", 0, 0, "", "compilation object save succeeded; see obj/warn.fxo")};
    EffectProcessor processor(compiler);
    auto effect = std::make_shared<Pipeline::Graphics::EffectContent>();
    effect->setEffectCodeProperty("technique T { }");
    effect->setIdentityProperty(ContentIdentity("warn.fx", "EffectImporter", "frag"));
    EffectContext context("Release", Pipeline::TargetPlatform::Windows);
    const auto compiled = processor.Process(effect, context);
    ASSERT_NE(compiled, nullptr);
    EXPECT_EQ(Spell(compiled->getIdentityProperty()), "{warn.fx|EffectImporter|frag}");
    EXPECT_EQ(context.logger.warnings,
              (std::vector<std::string>{"EffectProcessor_WarningsOrErrorsWhenCompilingEffect|{warn.fx|EffectImporter|1,50}|"
                                        "Warning compiling warn.fx: warning X3206: implicit truncation of vector type"}));
}

TEST(XnaEffectProcessor, AutoDebugsOnlyAWindowsDebugBuild)
{
    // EnableEffectDebugging IL_0018-0033: Auto is BuildConfiguration == "Debug" && TargetPlatform ==
    // Windows, so an Xbox 360 Debug build is optimized; Debug debugs on every platform.
    struct Case
    {
        EffectProcessorDebugMode mode;
        Pipeline::TargetPlatform platform;
        bool debugInformation;
    };
    for (const Case& probe : std::vector<Case>{
             {EffectProcessorDebugMode::Auto, Pipeline::TargetPlatform::Windows, true},
             {EffectProcessorDebugMode::Auto, Pipeline::TargetPlatform::Xbox360, false},
             {EffectProcessorDebugMode::Debug, Pipeline::TargetPlatform::Xbox360, true}})
    {
        auto compiler = std::make_shared<ScriptedCompiler>();
        compiler->result.succeeded = true;
        compiler->result.bytecode = {1, 2, 3, 4};
        EffectProcessor processor(compiler);
        processor.setDebugModeProperty(probe.mode);
        auto effect = std::make_shared<Pipeline::Graphics::EffectContent>();
        effect->setEffectCodeProperty("technique T { }");
        effect->setIdentityProperty(ContentIdentity("shader.fx"));
        EffectContext context("Debug", probe.platform);
        (void)processor.Process(effect, context);
        EXPECT_EQ(compiler->request.debugInformation, probe.debugInformation)
            << static_cast<int>(probe.mode) << " on platform " << static_cast<int>(probe.platform);
    }
}
