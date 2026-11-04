// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/EffectProcessor.hpp"
#include "CNA/Internal/PathUtf8.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    namespace
    {
        namespace Canonical = CNA::Content::Pipeline;

        /** @brief `A=1;B` becomes the compiler's define map, with an empty value for a bare name. */
        [[nodiscard]] std::map<std::string, std::string> ParseDefines(const std::string& defines)
        {
            std::map<std::string, std::string> parsed;
            std::size_t start = 0;
            while (start <= defines.size() && !defines.empty())
            {
                const std::size_t end = defines.find(';', start);
                const std::string entry = defines.substr(start, end == std::string::npos ? std::string::npos
                                                                                        : end - start);
                if (!entry.empty())
                {
                    const std::size_t equals = entry.find('=');
                    if (equals == std::string::npos)
                    {
                        parsed[entry] = std::string();
                    }
                    else
                    {
                        parsed[entry.substr(0, equals)] = entry.substr(equals + 1);
                    }
                }
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }
            return parsed;
        }

        /** @brief The help link XNA puts on every compile refusal and warning it logs (IL_015d, IL_00d4). */
        constexpr const char* kCompileHelpLink = "EffectProcessor_WarningsOrErrorsWhenCompilingEffect";

        /**
         * @brief Whether a diagnostic names the effect source this processor wrote for the compiler.
         *
         * fxc names the file it was handed in whatever spelling it was handed -- a Windows path
         * under Wine, a relative one, or the bare name -- so the comparison is on the path with its
         * separators and any drive letter set aside, and on a bare name alone.
         */
        [[nodiscard]] bool NamesCompiledSource(const std::string& file, const std::filesystem::path& source)
        {
            if (file.empty())
            {
                return true;
            }
            const auto normal = [](std::string text)
            {
                std::replace(text.begin(), text.end(), '\\', '/');
                if (text.size() >= 2u && text[1] == ':')
                {
                    text.erase(0, 2);
                }
                return text;
            };
            const auto endsWith = [](const std::string& text, const std::string& tail)
            {
                return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
            };
            const std::string spelled = normal(file);
            const std::string written = normal(CNA::Internal::PathToUtf8(source));
            return spelled == written || spelled == CNA::Internal::PathToUtf8(source.filename()) ||
                   endsWith(spelled, "/" + written) || endsWith(written, "/" + spelled);
        }

        /** @brief `line` or `line,column`, as the compiler's `(...)` holds them. */
        [[nodiscard]] std::string Fragment(const Canonical::EffectCompilerDiagnostic& diagnostic)
        {
            return std::to_string(diagnostic.line) +
                   (diagnostic.column > 0 ? "," + std::to_string(diagnostic.column) : std::string());
        }

        /** @brief What follows a located line's `(...): `: `error X3004: message`. */
        [[nodiscard]] std::string Remainder(const Canonical::EffectCompilerDiagnostic& diagnostic)
        {
            return std::string(diagnostic.isError ? "error" : "warning") +
                   (diagnostic.code.empty() ? std::string() : " " + diagnostic.code) + ": " + diagnostic.message;
        }

        /**
         * @brief One diagnostic as XNA's refusal text reads it.
         *
         * XNA compiles the effect from memory. With debugging on it first prefixes
         * `#line 1 "<SourceFilename>"` (AddSourceFileDirective, Process IL_00ec-0117), so a line of the
         * effect itself names the identity's file; with it off the compiler names a buffer whose path
         * XNA strips (RemoveUnwantedErrorText, IL_000c-001d), and the line reads `(12,12): error ...`
         * (measured in process, TQ-038 lane oracle effect/debug/* and effect/release/*; and the
         * genuine BuildContent's shader/compile_error). A line of an included file, and anything the
         * compiler said without a location, is kept as it was said.
         */
        [[nodiscard]] std::string Spelled(const Canonical::EffectCompilerDiagnostic& diagnostic, const bool root,
                                          const std::string& rootName)
        {
            if (diagnostic.line > 0 && root)
            {
                return rootName + "(" + Fragment(diagnostic) + "): " + Remainder(diagnostic);
            }
            if (diagnostic.line == 0 && diagnostic.file.empty() && diagnostic.code.empty())
            {
                // `ID3DXEffectCompiler: There were no techniques` is XNA's whole line
                // (graphics oracle effectprocessor/empty_code), not `error: ...`.
                return diagnostic.message;
            }
            return diagnostic.ToString();
        }

        /**
         * @brief The identity XNA gives one line of compiler output (CreateContentIdentityFromErrorString).
         *
         * A line without a `(line,column)` keeps the effect's own identity (IL_0040-0041). One with
         * it takes a new identity: the effect's SourceTool (IL_0048-0052), the file the line names
         * -- the effect's own SourceFilename when that is the effect itself (IL_006f-0086) -- and the
         * location as the fragment (IL_008f-00b1).
         */
        [[nodiscard]] ContentIdentity LineIdentity(const Canonical::EffectCompilerDiagnostic& diagnostic, const bool root,
                                                   const ContentIdentity& effect)
        {
            if (diagnostic.line <= 0)
            {
                return effect;
            }
            ContentIdentity located;
            located.setSourceToolProperty(effect.getSourceToolProperty());
            located.setSourceFilenameProperty(root ? effect.getSourceFilenameProperty() : diagnostic.file);
            located.setFragmentIdentifierProperty(Fragment(diagnostic));
            return located;
        }

        /** @brief The name the temporary source is written under: the effect's own file name, or a stand-in. */
        [[nodiscard]] std::string SourceName(const Graphics::EffectContent& effect)
        {
            const std::string& filename = effect.getIdentityProperty().getSourceFilenameProperty();
            if (filename.empty())
            {
                return "effect.fx";
            }
            return CNA::Internal::PathToUtf8(CNA::Internal::PathFromUtf8(filename).filename());
        }
    }

    EffectProcessor::EffectProcessor() : compiler_(Canonical::MakeExternalEffectCompiler()) {}

    EffectProcessor::EffectProcessor(std::shared_ptr<const Canonical::EffectCompilerService> compiler)
        : compiler_(std::move(compiler))
    {
        if (compiler_ == nullptr)
        {
            throw System::ArgumentNullException("compiler");
        }
    }

    EffectProcessorDebugMode EffectProcessor::getDebugModeProperty() const noexcept { return debugMode_; }

    void EffectProcessor::setDebugModeProperty(EffectProcessorDebugMode value) noexcept { debugMode_ = value; }

    const std::string& EffectProcessor::getDefinesProperty() const noexcept { return defines_; }

    void EffectProcessor::setDefinesProperty(std::string value) { defines_ = std::move(value); }

    std::shared_ptr<CompiledEffectContent> EffectProcessor::Process(
        const std::shared_ptr<Graphics::EffectContent>& input, ContentProcessorContext& context)
    {
        if (input == nullptr)
        {
            throw System::ArgumentNullException("input");
        }
        const std::string name = SourceName(*input);
        if (!compiler_->Available())
        {
            // XNA compiles in-process with D3DX and cannot be unavailable; CNA drives a compiler
            // that may not be installed, and says so once rather than producing empty byte code.
            throw InvalidContentException("Errors compiling " + name + ":\n" + compiler_->UnavailableReason());
        }
        // The compiler takes a file, so the source is written where the build keeps its
        // intermediates -- which is what XNA's own identity carries for a source that came from
        // disk.
        std::filesystem::path directory = context.getIntermediateDirectoryProperty();
        if (directory.empty())
        {
            directory = std::filesystem::temp_directory_path();
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        const std::filesystem::path source = directory / name;
        {
            std::ofstream out(source, std::ios::binary | std::ios::trunc);
            if (input->getEffectCodeProperty().has_value())
            {
                out << *input->getEffectCodeProperty();
            }
        }
        Canonical::EffectCompileRequest request;
        request.source = source;
        const std::filesystem::path identitySource =
            input->getIdentityProperty().getSourceFilenameProperty();
        if (identitySource.has_parent_path() && !identitySource.parent_path().empty())
        {
            // EffectProcessor compiles the in-memory EffectContent, but XNA resolves quoted
            // includes beside that content's source identity. Keep the temporary source while
            // giving the external compiler the original include root.
            request.includeDirectories.push_back(identitySource.parent_path());
        }
        request.defines = ParseDefines(getDefinesProperty());
        request.profile = context.getTargetProfileProperty() ==
                                  Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef
                              ? Canonical::EffectSourceProfile::HiDef
                              : Canonical::EffectSourceProfile::Reach;
        // XNA's EnableEffectDebugging (IL_0000-0035): Debug always, Optimize never, and Auto only for
        // a Windows build whose configuration is exactly "Debug" -- an Xbox 360 Debug build is
        // optimized.
        request.debugInformation = getDebugModeProperty() == EffectProcessorDebugMode::Debug ||
                                   (getDebugModeProperty() == EffectProcessorDebugMode::Auto &&
                                    context.getBuildConfigurationProperty() == "Debug" &&
                                    context.getTargetPlatformProperty() == TargetPlatform::Windows);
        const Canonical::EffectCompileResult result = compiler_->Compile(request);
        const ContentIdentity& identity = input->getIdentityProperty();
        // Process IL_00f2-0117 adds the #line directive only for an identity that names a file.
        const std::string rootName = request.debugInformation ? identity.getSourceFilenameProperty() : std::string();
        if (!result.succeeded)
        {
            // DoSomethingAwesomeWithErrorsAndWarnings IL_00f4-0169: "Errors compiling {0}:{1}{2}" with
            // the identity's whole SourceFilename, every line of the compiler's text, and the
            // identity of the first line that names a location.
            std::string body;
            std::optional<ContentIdentity> located;
            for (const Canonical::EffectCompilerDiagnostic& diagnostic : result.diagnostics)
            {
                const bool root = NamesCompiledSource(diagnostic.file, source);
                body += (body.empty() ? "" : "\n") + Spelled(diagnostic, root, rootName);
                if (!located.has_value() && diagnostic.line > 0)
                {
                    located = LineIdentity(diagnostic, root, identity);
                }
            }
            InvalidContentException refusal("Errors compiling " + identity.getSourceFilenameProperty() + ":\n" + body,
                                            located.value_or(identity));
            refusal.setHelpLinkProperty(kCompileHelpLink);
            throw refusal;
        }
        // A compile that succeeded with something to say logs every line as a warning of its own
        // (IL_004f-00f3): under that line's identity, as "Warning compiling {0}: {1}" with what
        // follows the location. XNA's in-memory compiler says nothing else on success; fxc also
        // reports where it saved its output, and those lines are not the compiler's warnings.
        for (const Canonical::EffectCompilerDiagnostic& diagnostic : result.diagnostics)
        {
            if (diagnostic.isError)
            {
                continue;
            }
            const bool root = NamesCompiledSource(diagnostic.file, source);
            context.getLoggerProperty().LogWarning(
                kCompileHelpLink, LineIdentity(diagnostic, root, identity),
                "Warning compiling " + identity.getSourceFilenameProperty() + ": " +
                    (diagnostic.line > 0 ? Remainder(diagnostic) : Spelled(diagnostic, root, rootName)));
        }
        auto compiled = std::make_shared<CompiledEffectContent>(result.bytecode);
        // Process IL_01ac-01b7: the compiled effect is the input's identity.
        compiled->setIdentityProperty(identity);
        return compiled;
    }

    const std::string& EffectProcessor::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
