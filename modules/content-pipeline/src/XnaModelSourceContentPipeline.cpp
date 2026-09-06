// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Pipeline/XnaModelSourceContentPipeline.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnaModelBridge.hpp"
#include "CNA/Content/Pipeline/XnaPipelineBridge.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ModelImporters.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/MaterialProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ModelProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/TextureProcessor.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        namespace Xnb = CNA::Internal::Xnb;
        namespace Processors = Microsoft::Xna::Framework::Content::Pipeline::Processors;
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        constexpr const char* kXImporterName = "CNA.XImporter";
        constexpr const char* kFbxImporterName = "CNA.FbxImporter";
        constexpr const char* kModelProcessorName = "CNA.XnaModelProcessor";

        /**
         * @brief Runs one XNA-shaped importer over the context's source.
         *
         * The importer is constructed per asset, as XNA constructs one per asset, and reads
         * through the same bridge context every XNA-shaped component sees.
         */
        template<typename TImporter>
        [[nodiscard]] ContentValue ImportThroughXna(ContentImporterContext& context)
        {
            TImporter importer{};
            XnaBridgeImporterContext xnaContext(context);
            Xna::IContentImporter& untyped = importer;
            return untyped.Import(CNA::Internal::ContentPathToUtf8(context.SourcePath()), xnaContext);
        }

        /**
         * @brief The logical name a built texture output is known by.
         *
         * `MaterialProcessor` starts a nested build for every texture a material names and stores
         * the path that build wrote. What the model must carry is the name a `ContentManager`
         * loads, so the path is spelled relative to the output root without its extension -- the
         * same name the nested build was given.
         */
        [[nodiscard]] std::string TextureLogicalName(const std::filesystem::path& outputRoot,
                                                     const std::string& authored)
        {
            std::filesystem::path path = CNA::Internal::ContentPathFromUtf8(authored);
            if (!outputRoot.empty() && path.is_absolute())
            {
                std::error_code error;
                const std::filesystem::path relative =
                    std::filesystem::relative(path, outputRoot, error);
                if (!error && !relative.empty() && *relative.begin() != "..") { path = relative; }
            }
            path.replace_extension();
            const std::string logical =
                CNA::Internal::ContentPathToUtf8(path.lexically_normal().generic_string());
            if (logical.empty())
            {
                throw ContentLoadException(
                    "XnaModelProcessor: a material names a texture whose built output has no "
                    "loadable name.");
            }
            return logical;
        }

        /** @brief The `.x` source route. */
        class XImporterComponent final : public ContentImporter
        {
        public:
            [[nodiscard]] ContentComponentIdentity Identity() const override
            {
                return {kXImporterName, "1"};
            }

            [[nodiscard]] std::vector<std::string> SourceExtensions() const override
            {
                return {".x"};
            }

            [[nodiscard]] std::vector<std::string> OutputTypes() const override
            {
                return {ImportedXnaNodeGraphType};
            }

            [[nodiscard]] std::string DefaultProcessor() const override
            {
                return kModelProcessorName;
            }

            [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override
            {
                return ImportThroughXna<Xna::XImporter>(context);
            }
        };

        /** @brief The `.fbx` source route. */
        class FbxImporterComponent final : public ContentImporter
        {
        public:
            [[nodiscard]] ContentComponentIdentity Identity() const override
            {
                return {kFbxImporterName, "1"};
            }

            [[nodiscard]] std::vector<std::string> SourceExtensions() const override
            {
                return {".fbx"};
            }

            [[nodiscard]] std::vector<std::string> OutputTypes() const override
            {
                return {ImportedXnaNodeGraphType};
            }

            [[nodiscard]] std::string DefaultProcessor() const override
            {
                return kModelProcessorName;
            }

            [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override
            {
                return ImportThroughXna<Xna::FbxImporter>(context);
            }
        };

        /**
         * @brief `TextureProcessor` for the nested build a material's texture starts.
         *
         * XNA's `MaterialProcessor` builds every texture a material names through a processor it
         * asks for by the name `TextureProcessor`, with XNA's own property spellings. The source
         * that build imports is an ordinary image, so what the coordinator hands the processor is
         * the canonical `ImportedImage` -- not the `TextureContent` the XNA-shaped
         * `Processors::TextureProcessor` takes. This is the join: XNA's name and XNA's properties
         * (including XNA's defaults, read back off a fresh instance rather than restated here) in
         * front of the one texture processor CNA has.
         *
         * It resolves that processor out of the running registry rather than holding one, so it
         * cannot drift from it and needs no encoder of its own -- a `DxtCompressed` texture is
         * compressed exactly when the registered processor can compress it.
         */
        class XnaTextureProcessorAlias final : public ContentProcessor
        {
        public:
            [[nodiscard]] ContentComponentIdentity Identity() const override
            {
                return {"TextureProcessor", "1"};
            }

            [[nodiscard]] std::string InputType() const override { return ImportedImageType; }

            [[nodiscard]] std::string OutputType() const override { return ProcessedTexture2DType; }

            [[nodiscard]] bool SelectedByNameOnly() const override { return true; }

            void ValidateParameters(const ContentProcessorParameters& parameters) const override
            {
                Processors::TextureProcessor probe{};
                AssignXna(probe, parameters);
            }

            [[nodiscard]] ContentValue Process(const ContentValue& input,
                                               ContentProcessorContext& context) const override
            {
                Processors::TextureProcessor configured{};
                AssignXna(configured, context.Parameters());
                const ContentPipeline* pipeline = context.Pipeline();
                if (pipeline == nullptr)
                {
                    throw ContentLoadException(
                        "TextureProcessor: this context was created outside a coordinator.");
                }
                const std::shared_ptr<const ContentProcessor> canonical =
                    pipeline->Registry().ResolveProcessor(ImportedImageType, "CNA.TextureProcessor");
                const ContentProcessorParameters translated = Translate(configured);
                canonical->ValidateParameters(translated);
                ContentProcessorContext nested(
                    context.SourceRoot(), context.SourcePath(), context.LogicalName(),
                    canonical->Identity().name, translated, context.ExternalSourceRoots(),
                    context.Dependencies(), context.Logger(), context.OutputFormat(),
                    context.Environment(), pipeline);
                return canonical->Process(input, nested);
            }

        private:
            void AssignXna(Processors::TextureProcessor& processor,
                           const ContentProcessorParameters& parameters) const
            {
                for (const auto& [name, value] : parameters.Values())
                {
                    const Xna::ProcessorParameterBinding<Processors::TextureProcessor>* binding =
                        bindings_.Find(name);
                    if (binding == nullptr)
                    {
                        throw ContentParameterError(
                            ContentParameterFault::UnknownName, name,
                            "processor 'TextureProcessor' has no parameter named '" + name + "'.");
                    }
                    ContentProcessorParameters single;
                    single.Set(name, value);
                    try
                    {
                        binding->assignObject(processor, ToOpaqueData(single)[name]);
                    }
                    catch (const System::Exception& error)
                    {
                        throw ContentParameterError(
                            ContentParameterFault::UnconvertibleValue, name,
                            "processor parameter '" + name + "': " + error.getMessageProperty());
                    }
                }
            }

            /** @brief The same six settings, spelled the way the canonical processor names them. */
            [[nodiscard]] static ContentProcessorParameters Translate(
                const Processors::TextureProcessor& processor)
            {
                ContentProcessorParameters parameters;
                if (processor.getColorKeyEnabledProperty())
                {
                    const Microsoft::Xna::Framework::Color key = processor.getColorKeyColorProperty();
                    parameters.Set(TextureColorKeyParameter,
                                   std::to_string(static_cast<int>(key.getRProperty())) + "," +
                                       std::to_string(static_cast<int>(key.getGProperty())) + "," +
                                       std::to_string(static_cast<int>(key.getBProperty())));
                }
                parameters.Set(TextureGenerateMipmapsParameter, processor.getGenerateMipmapsProperty());
                parameters.Set(TexturePremultiplyAlphaParameter, processor.getPremultiplyAlphaProperty());
                parameters.Set(TextureResizeToPowerOfTwoParameter, processor.getResizeToPowerOfTwoProperty());
                switch (processor.getTextureFormatProperty())
                {
                    case Processors::TextureProcessorOutputFormat::NoChange:
                        parameters.Set(TextureFormatParameter, std::string("NoChange"));
                        break;
                    case Processors::TextureProcessorOutputFormat::Color:
                        parameters.Set(TextureFormatParameter, std::string("Color"));
                        break;
                    case Processors::TextureProcessorOutputFormat::DxtCompressed:
                        parameters.Set(TextureFormatParameter, std::string("DxtCompressed"));
                        break;
                }
                return parameters;
            }

            Xna::ProcessorParameterBindings<Processors::TextureProcessor> bindings_ =
                Xna::DescribeProcessorParameters<Processors::TextureProcessor>();
        };

        /**
         * @brief Runs XNA's `ModelProcessor` and hands its model to the canonical writers.
         *
         * The processing itself is XNA's, measured against the genuine runtime; what this adds is
         * the last step, turning the processed `ModelContent` into the one processed-model value
         * both the CNB and the XNB writer already take. It goes through the canonical XNB model
         * because that is the form `XnaModelBridge` produces and the form the frozen CNB schemas
         * are defined against -- schema 1 when every semantic fits it exactly, schema 2 otherwise,
         * which is the same choice the `.xnb` import route makes and for the same reason.
         */
        class XnaModelProcessorComponent final : public ContentProcessor
        {
        public:
            [[nodiscard]] ContentComponentIdentity Identity() const override
            {
                return {kModelProcessorName, "1"};
            }

            [[nodiscard]] std::string InputType() const override { return ImportedXnaNodeGraphType; }

            [[nodiscard]] std::string OutputType() const override { return ProcessedModelType; }

            void ValidateParameters(const ContentProcessorParameters& parameters) const override
            {
                Processors::ModelProcessor probe{};
                Assign(probe, parameters);
            }

            [[nodiscard]] ContentValue Process(const ContentValue& input,
                                               ContentProcessorContext& context) const override
            {
                Processors::ModelProcessor processor{};
                Assign(processor, context.Parameters());
                XnaBridgeProcessorContext xnaContext(context);
                Xna::IContentProcessor& untyped = processor;
                const ContentValue processed = untyped.Process(input, xnaContext);
                const auto& model =
                    processed.Get<std::shared_ptr<Processors::ModelContent>>();
                if (model == nullptr)
                {
                    throw ContentLoadException("XnaModelProcessor: the processor produced no model.");
                }
                const Xnb::XnbModelData xnb = ToCanonicalModel(*model);
                const std::filesystem::path outputRoot = context.Environment().outputDirectory;
                const auto resolve = [&outputRoot](const std::string& authored)
                {
                    return TextureLogicalName(outputRoot, authored);
                };
                ProcessedModelBundle bundle;
                try
                {
                    bundle.primary = Xnb::ConvertXnbModelToCnb(xnb, resolve);
                    context.LogInfo("selected frozen Model schema 1 because every semantic fits exactly.");
                }
                catch (const ContentLoadException& schema1Failure)
                {
                    bundle.primary = Xnb::ConvertXnbModelToCnbV2(xnb, resolve);
                    context.LogInfo(std::string("selected Model schema 2 after the schema-1 "
                                                "fidelity check: ") + schema1Failure.what());
                }
                return ContentValue::Create(ProcessedModelType, std::move(bundle));
            }

        private:
            void Assign(Processors::ModelProcessor& processor,
                        const ContentProcessorParameters& parameters) const
            {
                for (const auto& [name, value] : parameters.Values())
                {
                    const Xna::ProcessorParameterBinding<Processors::ModelProcessor>* binding =
                        bindings_.Find(name);
                    if (binding == nullptr)
                    {
                        throw ContentParameterError(
                            ContentParameterFault::UnknownName, name,
                            std::string("processor '") + kModelProcessorName +
                                "' has no parameter named '" + name + "'.");
                    }
                    ContentProcessorParameters single;
                    single.Set(name, value);
                    const Xna::OpaqueDataDictionary boxed = ToOpaqueData(single);
                    try
                    {
                        binding->assignObject(processor, boxed[name]);
                    }
                    catch (const System::Exception& error)
                    {
                        throw ContentParameterError(
                            ContentParameterFault::UnconvertibleValue, name,
                            "processor parameter '" + name + "': " + error.getMessageProperty());
                    }
                }
            }

            Xna::ProcessorParameterBindings<Processors::ModelProcessor> bindings_ =
                Xna::DescribeProcessorParameters<Processors::ModelProcessor>();
        };
    }

    void RegisterXnaModelSourceContentPipeline(ContentPipelineRegistry& registry)
    {
        registry.RegisterImporter(std::make_shared<const XImporterComponent>());
        registry.RegisterImporter(std::make_shared<const FbxImporterComponent>());
        registry.RegisterProcessor(std::make_shared<const XnaModelProcessorComponent>());
        // Registered under XNA's own names because XNA's own components reach them by name:
        // `ModelProcessor` converts every material through "MaterialProcessor", and that processor
        // builds every texture the material names through "TextureProcessor". A name these two
        // cannot resolve is a model that imports and then fails halfway through processing.
        RegisterXnaProcessorWithoutAttribute<Processors::MaterialProcessor>(registry, "MaterialProcessor");
        // A material is never an asset of its own: XNA reaches this processor only through
        // `ModelProcessor`, which converts each of a model's materials and then writes them inside
        // the model. Recorded rather than left silent, because a processed type with no writer and
        // no reason is the thing the coverage gate exists to catch.
        for (const ContentOutputFormat format : {ContentOutputFormat::Cnb, ContentOutputFormat::Xnb})
        {
            registry.DocumentAbsentWriter(
                format, "Microsoft.Xna.Framework.Content.Pipeline.Graphics.MaterialContent",
                "a material is not an asset. XNA's MaterialProcessor is reached only through "
                "ModelProcessor, which converts a model's materials and writes them inside the "
                "model; naming it on an asset of its own is refused by XNA too.");
        }
        registry.RegisterProcessor(std::make_shared<const XnaTextureProcessorAlias>());
    }
}
