// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Pipeline/XnaPipelineBridge.hpp"

#include <cmath>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#include "CNA/Content/Pipeline/XnbOutputContentPipeline.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ProcessorEnums.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/PipelineException.hpp"
#include "System/NotSupportedException.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        /// A canonical writer that compiles one façade-typed processed value through the
        /// ContentCompiler. Platform and profile are the build's; the container options are the
        /// registration's.
        class XnaObjectXnbWriter final : public ContentTypeWriter
        {
        public:
            XnaObjectXnbWriter(std::string typeName,
                               std::shared_ptr<const Xna::Serialization::Compiler::ContentCompiler> compiler,
                               CNA::Internal::Xnb::XnbFileOptions options)
                : typeName_(std::move(typeName)), compiler_(std::move(compiler)), options_(std::move(options))
            {
            }

            [[nodiscard]] ContentComponentIdentity Identity() const override
            {
                return ContentComponentIdentity{"CNA.XnaObjectXnbWriter[" + typeName_ + "]",
                                                "1;" + XnbOutputOptionsDigest(options_)};
            }

            [[nodiscard]] ContentOutputFormat OutputFormat() const override { return ContentOutputFormat::Xnb; }

            [[nodiscard]] std::vector<ContentWriterSchemaIdentity> OutputSchemaIdentities() const override
            {
                ContentWriterSchemaIdentity identity;
                identity.assetTypeId = static_cast<std::uint32_t>(XnbOutputAssetId::XnaObject);
                identity.assetSchemaVersion = 1u;
                identity.assetTypeName = typeName_;
                identity.codec = ContentComponentIdentity{"CNA.XnaObjectXnb", "1"};
                return {identity};
            }

            [[nodiscard]] std::string InputType() const override { return typeName_; }

            [[nodiscard]] ContentWriteResult Write(const ContentValue& input, const std::string& logicalName) const override
            {
                return Write(input, logicalName, ContentBuildEnvironment{});
            }

            [[nodiscard]] ContentWriteResult Write(const ContentValue& input, const std::string& logicalName,
                                                   const ContentBuildEnvironment& environment) const override
            {
                Xna::Serialization::Compiler::CompileOptions compile;
                compile.container = options_;
                compile.assetName = logicalName;
                compile.outputDirectory = environment.outputDirectory;
                compile.compressContent = options_.compression == CNA::Internal::Xnb::XnbOutputCompression::Lzx;
                switch (options_.platform)
                {
                    case CNA::Internal::Xnb::XnbTargetPlatform::Xbox360: compile.targetPlatform = Xna::TargetPlatform::Xbox360; break;
                    case CNA::Internal::Xnb::XnbTargetPlatform::WindowsPhone: compile.targetPlatform = Xna::TargetPlatform::WindowsPhone; break;
                    default: compile.targetPlatform = Xna::TargetPlatform::Windows; break;
                }
                compile.targetProfile = options_.graphicsProfile == CNA::Internal::Xnb::XnbGraphicsProfile::HiDef
                                            ? Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef
                                            : Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach;
                const CNA::Internal::Xnb::XnbAssetWriteResult written = compiler_->CompileObject(input, compile);
                ContentWriteResult result;
                result.bytes = written.bytes;
                result.assetTypeId = static_cast<std::uint32_t>(XnbOutputAssetId::XnaObject);
                result.assetSchemaVersion = 1u;
                result.assetTypeName = typeName_;
                result.rootReaderName = written.rootReaderName;
                return result;
            }

        private:
            std::string typeName_;
            std::shared_ptr<const Xna::Serialization::Compiler::ContentCompiler> compiler_;
            CNA::Internal::Xnb::XnbFileOptions options_;
        };
    }

    void RegisterXnaXnbOutput(ContentPipelineRegistry& registry,
                              std::shared_ptr<const Xna::Serialization::Compiler::ContentCompiler> compiler,
                              const CNA::Internal::Xnb::XnbFileOptions& options)
    {
        if (compiler == nullptr) { throw std::invalid_argument("RegisterXnaXnbOutput(): compiler must not be null."); }
        for (const std::string& typeName : compiler->KnownTypeNames())
        {
            bool taken = false;
            try
            {
                (void)registry.ResolveWriter(typeName, {}, ContentOutputFormat::Xnb);
                taken = true;
            }
            catch (const std::logic_error&) {}
            if (taken) { continue; }
            registry.RegisterWriter(std::make_shared<const XnaObjectXnbWriter>(typeName, compiler, options));
        }
    }

    namespace
    {

        /**
         * @brief The declared spelling of a boxed enumeration, for the enumerations XNA's own
         *        processors forward between one another.
         *
         * Every enumeration here declares its spellings once, with `CNA_XNA_CONTENT_ENUM`; this
         * only says which of them can appear in a forwarded parameter. An enumeration missing from
         * the list is reported by name rather than guessed at.
         */
        template<typename... TEnums>
        [[nodiscard]] std::optional<std::string> EnumSpellingOf(const Xna::ContentObject& boxed)
        {
            std::optional<std::string> spelling;
            const auto tryOne = [&]<typename TEnum>()
            {
                if (spelling.has_value() || !Xna::Holds<TEnum>(boxed)) { return; }
                const TEnum value = Xna::Unbox<TEnum>(boxed);
                for (const auto& entry :
                     Xna::Serialization::Intermediate::ContentEnumNames<TEnum>::Names)
                {
                    if (entry.first == value) { spelling = std::string(entry.second); return; }
                }
            };
            (tryOne.template operator()<TEnums>(), ...);
            return spelling;
        }

        /** @brief The spelling of any enumeration an XNA processor forwards. */
        [[nodiscard]] std::optional<std::string> EnumSpelling(const Xna::ContentObject& boxed)
        {
            return EnumSpellingOf<Microsoft::Xna::Framework::Content::Pipeline::Processors::MaterialProcessorDefaultEffect,
                                  Microsoft::Xna::Framework::Content::Pipeline::Processors::TextureProcessorOutputFormat,
                                  Microsoft::Xna::Framework::Content::Pipeline::Processors::EffectProcessorDebugMode>(boxed);
        }

        /**
         * @brief The text spelling of a boxed value the canonical parameter set cannot carry.
         *
         * The canonical parameter set carries five primitives; XNA's own processors forward more
         * than that -- `ModelProcessor` hands `MaterialProcessor` a `Color` and two enumerations,
         * and `MaterialProcessor` hands the texture processor the same. Refusing them made every
         * model that reaches a material fail halfway through processing.
         *
         * Text is the right carrier rather than a widened variant: every parameter binding already
         * accepts its property's text spelling (`ParseProcessorParameterText`), a `.contentproj`
         * writes these values as text, and the spelling this produces is the one that parser
         * reads. Nothing about how the XNA processors *box* their forwarded values changes, so
         * what the measured oracle recorded is still what they forward.
         *
         * @param boxed The value.
         * @return Its text, or an empty optional when no spelling is defined for its type.
         */
        [[nodiscard]] std::optional<std::string> ParameterText(const Xna::ContentObject& boxed)
        {
            using Microsoft::Xna::Framework::Color;
            using Microsoft::Xna::Framework::Vector2;
            using Microsoft::Xna::Framework::Vector3;
            using Microsoft::Xna::Framework::Vector4;
            const auto number = [](const float value)
            {
                std::ostringstream text;
                text.imbue(std::locale::classic());
                text << value;
                return text.str();
            };
            if (Xna::Holds<Color>(boxed))
            {
                const Color color = Xna::Unbox<Color>(boxed);
                return std::to_string(static_cast<int>(color.getRProperty())) + "," +
                       std::to_string(static_cast<int>(color.getGProperty())) + "," +
                       std::to_string(static_cast<int>(color.getBProperty())) + "," +
                       std::to_string(static_cast<int>(color.getAProperty()));
            }
            if (Xna::Holds<Vector2>(boxed))
            {
                const Vector2 v = Xna::Unbox<Vector2>(boxed);
                return number(v.X) + "," + number(v.Y);
            }
            if (Xna::Holds<Vector3>(boxed))
            {
                const Vector3 v = Xna::Unbox<Vector3>(boxed);
                return number(v.X) + "," + number(v.Y) + "," + number(v.Z);
            }
            if (Xna::Holds<Vector4>(boxed))
            {
                const Vector4 v = Xna::Unbox<Vector4>(boxed);
                return number(v.X) + "," + number(v.Y) + "," + number(v.Z) + "," + number(v.W);
            }
            if (const std::optional<std::string> name = EnumSpelling(boxed); name.has_value())
            {
                return name;
            }
            return std::nullopt;
        }
    }

    Xna::TargetPlatform ToXnaTargetPlatform(const ContentTargetPlatform platform) noexcept
    {
        switch (platform)
        {
            case ContentTargetPlatform::Windows: return Xna::TargetPlatform::Windows;
            case ContentTargetPlatform::Xbox360: return Xna::TargetPlatform::Xbox360;
            case ContentTargetPlatform::WindowsPhone: return Xna::TargetPlatform::WindowsPhone;
        }
        return Xna::TargetPlatform::Windows;
    }

    ContentTargetPlatform FromXnaTargetPlatform(const Xna::TargetPlatform platform) noexcept
    {
        switch (platform)
        {
            case Xna::TargetPlatform::Windows: return ContentTargetPlatform::Windows;
            case Xna::TargetPlatform::Xbox360: return ContentTargetPlatform::Xbox360;
            case Xna::TargetPlatform::WindowsPhone: return ContentTargetPlatform::WindowsPhone;
        }
        return ContentTargetPlatform::Windows;
    }

    Xna::OpaqueDataDictionary ToOpaqueData(const ContentProcessorParameters& parameters)
    {
        Xna::OpaqueDataDictionary data;
        for (const auto& [name, value] : parameters.Values())
        {
            std::visit(
                [&](const auto& v) {
                    using V = std::decay_t<decltype(v)>;
                    data.Set(name, Xna::Box<V>(v));
                },
                value);
        }
        return data;
    }

    ContentProcessorParameters ToProcessorParameters(const Xna::OpaqueDataDictionary& data)
    {
        ContentProcessorParameters parameters;
        for (const auto& [name, boxed] : data)
        {
            if (Xna::Holds<bool>(boxed)) { parameters.Set(name, Xna::Unbox<bool>(boxed)); }
            else if (Xna::Holds<std::int64_t>(boxed)) { parameters.Set(name, Xna::Unbox<std::int64_t>(boxed)); }
            else if (Xna::Holds<std::int32_t>(boxed)) { parameters.Set(name, static_cast<std::int64_t>(Xna::Unbox<std::int32_t>(boxed))); }
            else if (Xna::Holds<std::uint64_t>(boxed)) { parameters.Set(name, Xna::Unbox<std::uint64_t>(boxed)); }
            else if (Xna::Holds<std::uint32_t>(boxed)) { parameters.Set(name, static_cast<std::uint64_t>(Xna::Unbox<std::uint32_t>(boxed))); }
            else if (Xna::Holds<double>(boxed)) { parameters.Set(name, Xna::Unbox<double>(boxed)); }
            else if (Xna::Holds<float>(boxed)) { parameters.Set(name, static_cast<double>(Xna::Unbox<float>(boxed))); }
            else if (Xna::Holds<std::string>(boxed)) { parameters.Set(name, Xna::Unbox<std::string>(boxed)); }
            else if (const std::optional<std::string> text = ParameterText(boxed); text.has_value())
            {
                parameters.Set(name, *text);
            }
            else
            {
                throw std::invalid_argument("processor parameter '" + name + "' of type '" +
                                            boxed.StableType() +
                                            "' cannot be carried by the canonical parameter set; spell it as a string.");
            }
        }
        return parameters;
    }

    XnaBridgeLogger::XnaBridgeLogger(CanonicalImporterContext& context) : importer_(&context) {}
    XnaBridgeLogger::XnaBridgeLogger(CanonicalProcessorContext& context) : processor_(&context) {}

    void XnaBridgeLogger::LogImportantMessage(const std::string& message)
    {
        // Not the same level as LogMessage: XNA documents this one as reaching the user even at
        // low verbosity, which is the only reason the method exists (XNAPP-260).
        if (importer_ != nullptr) { importer_->LogImportant(message); }
        else { processor_->LogImportant(message); }
    }

    void XnaBridgeLogger::LogMessage(const std::string& message)
    {
        if (importer_ != nullptr) { importer_->LogInfo(message); }
        else { processor_->LogInfo(message); }
    }

    void XnaBridgeLogger::LogWarning(const std::string& helpLink, const Xna::ContentIdentity& contentIdentity,
                                     const std::string& message)
    {
        std::string text = message;
        const std::string file = GetCurrentFilename(contentIdentity);
        if (!file.empty()) { text = file + ": " + text; }
        if (!helpLink.empty()) { text += " (" + helpLink + ")"; }
        if (importer_ != nullptr) { importer_->LogWarning(text); }
        else { processor_->LogWarning(text); }
    }

    XnaBridgeImporterContext::XnaBridgeImporterContext(CanonicalImporterContext& context)
        : context_(&context), logger_(context)
    {
    }

    std::string XnaBridgeImporterContext::getIntermediateDirectoryProperty() const
    {
        return context_->Environment().intermediateDirectory.string();
    }

    Xna::ContentBuildLogger& XnaBridgeImporterContext::getLoggerProperty() const
    {
        return logger_;
    }

    std::string XnaBridgeImporterContext::getOutputDirectoryProperty() const
    {
        return context_->Environment().outputDirectory.string();
    }

    void XnaBridgeImporterContext::AddDependency(const std::string& filename)
    {
        std::filesystem::path authored(filename);
        if (authored.is_absolute())
        {
            // The canonical context resolves relative to the primary source; an absolute path is
            // handed through as such and still has to be contained by a source root.
            std::error_code error;
            const std::filesystem::path relative =
                std::filesystem::relative(authored, context_->SourcePath().parent_path(), error);
            if (!error && !relative.empty()) { authored = relative; }
        }
        (void)context_->ResolveSourceDependency(authored);
    }

    XnaBridgeProcessorContext::XnaBridgeProcessorContext(CanonicalProcessorContext& context)
        : context_(&context), logger_(context), parameters_(ToOpaqueData(context.Parameters()))
    {
    }

    std::string XnaBridgeProcessorContext::getBuildConfigurationProperty() const
    {
        return context_->Environment().buildConfiguration;
    }

    std::string XnaBridgeProcessorContext::getIntermediateDirectoryProperty() const
    {
        return context_->Environment().intermediateDirectory.string();
    }

    Xna::ContentBuildLogger& XnaBridgeProcessorContext::getLoggerProperty() const
    {
        return logger_;
    }

    std::string XnaBridgeProcessorContext::getOutputDirectoryProperty() const
    {
        return context_->Environment().outputDirectory.string();
    }

    std::string XnaBridgeProcessorContext::getOutputFilenameProperty() const
    {
        const std::filesystem::path relative =
            std::filesystem::path(context_->LogicalName() + ContentOutputFormatExtension(context_->OutputFormat()));
        const std::filesystem::path& root = context_->Environment().outputDirectory;
        return root.empty() ? relative.generic_string() : (root / relative).generic_string();
    }

    const Xna::OpaqueDataDictionary& XnaBridgeProcessorContext::getParametersProperty() const
    {
        return parameters_;
    }

    Xna::TargetPlatform XnaBridgeProcessorContext::getTargetPlatformProperty() const
    {
        return ToXnaTargetPlatform(context_->Environment().targetPlatform);
    }

    Microsoft::Xna::Framework::Graphics::GraphicsProfile XnaBridgeProcessorContext::getTargetProfileProperty() const
    {
        return context_->Environment().targetProfile;
    }

    void XnaBridgeProcessorContext::AddDependency(const std::string& filename)
    {
        std::filesystem::path authored(filename);
        if (authored.is_absolute())
        {
            std::error_code error;
            const std::filesystem::path relative =
                std::filesystem::relative(authored, context_->SourcePath().parent_path(), error);
            if (!error && !relative.empty()) { authored = relative; }
        }
        (void)context_->ResolveSourceDependency(authored);
    }

    void XnaBridgeProcessorContext::AddOutputFile(const std::string& filename)
    {
        // XNA's AddOutputFile names a file the processor wrote beside the compiled asset so the
        // host deploys and cleans it. The canonical equivalent is a deployment file: the source
        // is the file the processor produced, the destination is its name below the output root.
        const std::filesystem::path path(filename);
        context_->AddDeploymentFile(path, path.filename().generic_string());
    }

    namespace
    {
        /// XNA derives a nested asset's name from its source path relative to the content root,
        /// without the extension; a source outside the root has no derivable name.
        std::string DeriveAssetName(const std::filesystem::path& source, const std::filesystem::path& root)
        {
            std::error_code error;
            const std::filesystem::path relative = std::filesystem::relative(source, root, error);
            if (error || relative.empty() || relative.native().starts_with(".."))
            {
                throw Xna::PipelineException(
                    "BuildAsset: '{0}' is outside the content root '{1}', so no asset name can be derived; pass assetName.",
                    source.generic_string(), root.generic_string());
            }
            std::filesystem::path stem = relative;
            stem.replace_extension();
            return stem.generic_string();
        }

        ContentBuildRequest NestedRequest(const CanonicalProcessorContext& context, const std::string& sourceFilename,
                                          const std::string& logicalName, const std::string& processorName,
                                          const Xna::OpaqueDataDictionary& processorParameters,
                                          const std::string& importerName)
        {
            ContentBuildRequest request;
            request.sourceRoot = context.SourceRoot();
            request.source = sourceFilename;
            request.externalSourceRoots = context.ExternalSourceRoots();
            request.logicalName = logicalName;
            request.importer = importerName;
            request.processor = processorName;
            request.outputFormat = context.OutputFormat();
            request.parameters = ToProcessorParameters(processorParameters);
            request.environment = context.Environment();
            request.logger = &context.Logger();
            return request;
        }

        /// Identifies one nested build so a repeat under the same name is recognized as the same asset.
        std::string NestedAssetKey(const std::string& sourceFilename, const std::string& importerName,
                                   const std::string& processorName, const Xna::OpaqueDataDictionary& parameters)
        {
            std::string key = sourceFilename + "|" + importerName + "|" + processorName;
            // Bound first: Values() returns a reference into the converted set, which must
            // outlive the loop.
            const ContentProcessorParameters converted = ToProcessorParameters(parameters);
            for (const auto& [name, value] : converted.Values())
            {
                key += "|" + name + "=";
                std::visit([&key](const auto& v) {
                    using V = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<V, std::string>) { key += v; }
                    else if constexpr (std::is_same_v<V, bool>) { key += v ? "true" : "false"; }
                    else { key += std::to_string(v); }
                }, value);
            }
            return key;
        }

        [[noreturn]] void RethrowNested(const char* operation, const std::string& sourceFilename,
                                        const Xna::ContentIdentity& sourceIdentity)
        {
            try { throw; }
            catch (const Xna::InvalidContentException&) { throw; }
            catch (const Xna::PipelineException&) { throw; }
            catch (const std::exception& error)
            {
                throw Xna::InvalidContentException(std::string(operation) + " of '" + sourceFilename +
                                                       "' failed: " + error.what(),
                                                   sourceIdentity, std::current_exception());
            }
        }
    }

    Xna::ContentObject XnaBridgeProcessorContext::BuildAndLoadAssetCore(
        const std::string& sourceFilename, const Xna::ContentIdentity& sourceIdentity,
        const std::string& processorName, const Xna::OpaqueDataDictionary& processorParameters,
        const std::string& importerName, const std::string& inputTypeName,
        const std::string& outputTypeName)
    {
        const ContentPipeline* pipeline = context_->Pipeline();
        if (pipeline == nullptr)
        {
            throw Xna::PipelineException(
                "ContentProcessorContext::BuildAndLoadAsset needs a running pipeline; this context was "
                "created outside a coordinator.");
        }
        const std::string logicalName = DeriveAssetName(std::filesystem::path(sourceFilename), context_->SourceRoot());
        ContentProcessResult nested;
        try
        {
            nested = pipeline->ImportAndProcess(
                NestedRequest(*context_, sourceFilename, logicalName, processorName, processorParameters, importerName),
                context_->Dependencies());
        }
        catch (...)
        {
            RethrowNested("BuildAndLoadAsset", sourceFilename, sourceIdentity);
        }
        if (nested.processed.StableType() != outputTypeName)
        {
            throw Xna::PipelineException("BuildAndLoadAsset: '{0}' processed to '{1}' where '{2}' was expected.",
                                         sourceFilename, nested.processed.StableType(), outputTypeName);
        }
        (void)inputTypeName; // the importer's declared output type is checked by the pipeline itself
        for (ContentAdditionalWriteOutput& output : nested.nestedOutputs)
        {
            context_->AddNestedOutput(std::move(output));
        }
        return nested.processed;
    }

    std::string XnaBridgeProcessorContext::BuildAssetCore(
        const std::string& sourceFilename, const Xna::ContentIdentity& sourceIdentity,
        const std::string& processorName, const Xna::OpaqueDataDictionary& processorParameters,
        const std::string& importerName, const std::string& assetName,
        const std::string& inputTypeName, const std::string& outputTypeName)
    {
        const ContentPipeline* pipeline = context_->Pipeline();
        if (pipeline == nullptr)
        {
            throw Xna::PipelineException(
                "ContentProcessorContext::BuildAsset needs a running pipeline; this context was created "
                "outside a coordinator.");
        }
        const std::string logicalName =
            assetName.empty() ? DeriveAssetName(std::filesystem::path(sourceFilename), context_->SourceRoot()) : assetName;
        if (logicalName == context_->LogicalName())
        {
            throw Xna::PipelineException("BuildAsset: nested asset name '{0}' is the current asset's own name.", logicalName);
        }
        const std::string extension = ContentOutputFormatExtension(context_->OutputFormat());
        const std::filesystem::path& outputRoot = context_->Environment().outputDirectory;
        const std::string filename = outputRoot.empty()
                                         ? logicalName + extension
                                         : (outputRoot / (logicalName + extension)).generic_string();

        // The same source built the same way twice is one asset; a different source (or a
        // different processing) under one name is a collision, as it is in XNA.
        const std::string key = NestedAssetKey(sourceFilename, importerName, processorName, processorParameters);
        const auto known = nestedAssets_.find(logicalName);
        if (known != nestedAssets_.end())
        {
            if (known->second != key)
            {
                throw Xna::PipelineException(
                    "BuildAsset: asset name '{0}' was already built from a different source or processing in this node.",
                    logicalName);
            }
            return filename;
        }

        ContentBuildResult nested;
        try
        {
            nested = pipeline->Build(
                NestedRequest(*context_, sourceFilename, logicalName, processorName, processorParameters, importerName));
        }
        catch (...)
        {
            RethrowNested("BuildAsset", sourceFilename, sourceIdentity);
        }
        (void)inputTypeName;
        if (nested.output.rootReaderName.empty() && nested.output.assetTypeName != outputTypeName &&
            !outputTypeName.empty())
        {
            // A CNB output names its asset type; an XNB output names its root reader. Neither has to
            // equal the C# type name, so this is a diagnostic aid rather than a refusal.
            context_->LogInfo("BuildAsset: '" + logicalName + "' compiled as '" + nested.output.assetTypeName +
                              "' for a reference typed '" + outputTypeName + "'.");
        }

        // The outer node depends on everything the nested node depended on, refers to what it
        // produced, and deploys what it deploys.
        for (const ContentDependency& dependency : nested.dependencies)
        {
            ContentDependency copy = dependency;
            if (copy.kind == ContentDependencyKind::PrimarySource) { copy.kind = ContentDependencyKind::SourceFile; }
            context_->Dependencies().Add(std::move(copy));
        }
        for (const RuntimeContentReference& reference : nested.runtimeReferences)
        {
            context_->Dependencies().AddRuntimeReference(reference);
        }
        for (const ContentDeploymentFile& deployment : nested.deploymentFiles)
        {
            context_->Dependencies().AddDeploymentFile(deployment);
        }
        context_->AddRuntimeReference(logicalName);

        ContentAdditionalWriteOutput primary;
        primary.logicalName = logicalName;
        primary.bytes = std::move(nested.output.bytes);
        primary.assetTypeId = nested.output.assetTypeId;
        primary.assetTypeName = nested.output.assetTypeName;
        primary.assetSchemaVersion = nested.output.assetSchemaVersion;
        primary.rootReaderName = nested.output.rootReaderName;
        context_->AddNestedOutput(std::move(primary));
        for (ContentAdditionalWriteOutput& additional : nested.output.additionalOutputs)
        {
            context_->AddNestedOutput(std::move(additional));
        }
        context_->AddNestedWriterSchemas(nested.writerSchemas);
        nestedAssets_.emplace(logicalName, key);
        return filename;
    }

    Xna::ContentObject XnaBridgeProcessorContext::ConvertCore(
        const Xna::ContentObject& input, const std::string& processorName,
        const Xna::OpaqueDataDictionary& processorParameters, const std::string& inputTypeName,
        const std::string& outputTypeName)
    {
        (void)inputTypeName;
        const ContentPipeline* pipeline = context_->Pipeline();
        if (pipeline == nullptr)
        {
            throw Xna::PipelineException(
                "ContentProcessorContext::Convert needs a running pipeline; this context was "
                "created outside a coordinator.");
        }
        if (processorName.empty())
        {
            throw Xna::PipelineException("ContentProcessorContext::Convert needs a processor name.");
        }
        std::shared_ptr<const ContentProcessor> processor;
        try
        {
            processor = pipeline->Registry().ResolveProcessor(input.StableType(), processorName);
        }
        catch (const std::exception& error)
        {
            throw Xna::PipelineException(std::string("Convert: ") + error.what());
        }
        const ContentProcessorParameters parameters = ToProcessorParameters(processorParameters);
        processor->ValidateParameters(parameters);
        CanonicalProcessorContext nested(context_->SourceRoot(), context_->SourcePath(), context_->LogicalName(),
                                       processor->Identity().name, parameters, context_->ExternalSourceRoots(),
                                       context_->Dependencies(), context_->Logger(), context_->OutputFormat(),
                                       context_->Environment(), pipeline);
        Xna::ContentObject result = processor->Process(input, nested);
        if (result.StableType() != outputTypeName)
        {
            throw Xna::PipelineException("Convert: processor '{0}' produced '{1}' where '{2}' was expected.",
                                         processorName, result.StableType(), outputTypeName);
        }
        // A converted processor may itself have started nested builds -- XNA's `MaterialProcessor`
        // builds every texture its material names, and it is reached only through `Convert`. Those
        // outputs are recorded on the context the conversion ran under, which ends here, so they
        // have to be carried up or the assets are compiled and then silently dropped: the model
        // referred to a texture that no build ever wrote. Dependencies and runtime references
        // survive on their own because both contexts share one collector.
        for (const ContentAdditionalWriteOutput& produced : nested.NestedOutputs())
        {
            context_->AddNestedOutput(produced);
        }
        context_->AddNestedWriterSchemas(nested.NestedWriterSchemas());
        return result;
    }
}
