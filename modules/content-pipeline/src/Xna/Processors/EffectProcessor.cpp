// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/EffectProcessor.hpp"

#include <filesystem>
#include <fstream>
#include <utility>

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

        /** @brief The name the refusal blames: the effect's own source file, or a stand-in. */
        [[nodiscard]] std::string SourceName(const Graphics::EffectContent& effect)
        {
            const std::string& filename = effect.getIdentityProperty().getSourceFilenameProperty();
            if (filename.empty())
            {
                return "effect.fx";
            }
            return std::filesystem::path(filename).filename().string();
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
        request.defines = ParseDefines(defines_);
        request.profile = context.getTargetProfileProperty() ==
                                  Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef
                              ? Canonical::EffectSourceProfile::HiDef
                              : Canonical::EffectSourceProfile::Reach;
        // Auto follows the build configuration, which is what its name says and what the
        // documented behaviour is; the other two decide for themselves.
        request.debugInformation = debugMode_ == EffectProcessorDebugMode::Debug ||
                                   (debugMode_ == EffectProcessorDebugMode::Auto &&
                                    context.getBuildConfigurationProperty() == "Debug");
        const Canonical::EffectCompileResult result = compiler_->Compile(request);
        if (!result.succeeded)
        {
            std::string message = "Errors compiling " + name + ":";
            for (const Canonical::EffectCompilerDiagnostic& diagnostic : result.diagnostics)
            {
                message += "\n" + diagnostic.ToString();
            }
            throw InvalidContentException(message);
        }
        return std::make_shared<CompiledEffectContent>(result.bytecode);
    }

    const std::string& EffectProcessor::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
