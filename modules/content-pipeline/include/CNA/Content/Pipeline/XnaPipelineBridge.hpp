// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/OpaqueDataDictionary.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ProcessorParameter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentCompiler.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/TargetPlatform.hpp"

/**
 * @file
 * @brief The bridge between the XNA-shaped public API and the canonical engine
 *        (plans/plan_xnapipeline_parity.md `XNAPP-038`–`XNAPP-044`,
 *        docs/xna-content-pipeline-compat-api.md §1, §4–§6).
 *
 * Nothing here is XNA API; it is how an importer or processor written against
 * `Microsoft::Xna::Framework::Content::Pipeline` becomes a canonical `ContentImporter` /
 * `ContentProcessor` that the one registry schedules, fingerprints, logs and publishes.
 */
namespace CNA::Content::Pipeline
{
    namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;

    /** @brief The canonical importer context, spelled so a class deriving the XNA one can name it. */
    using CanonicalImporterContext = ContentImporterContext;

    /** @brief The canonical processor context, spelled so a class deriving the XNA one can name it. */
    using CanonicalProcessorContext = ContentProcessorContext;

    namespace detail
    {
        template<typename R>
        struct XnaImportedTypeOf { using type = R; };
        template<typename R>
        struct XnaImportedTypeOf<std::shared_ptr<R>> { using type = R; };
        template<>
        struct XnaImportedTypeOf<Xna::ContentObject> { using type = System::Object; };
    }

    /**
     * @brief Metadata every XNA-shaped component adapter exposes, so `PipelineComponentScanner`
     *        can enumerate a registry the way XNA's scanner enumerates assemblies.
     */
    class XnaComponentMetadata
    {
    public:
        virtual ~XnaComponentMetadata() = default;

        /** @brief The XNA class name the component is registered under (`TextureImporter`). */
        [[nodiscard]] virtual const std::string& XnaClassName() const noexcept = 0;

        /** @brief The catalog (XNA assembly name) the component belongs to. */
        [[nodiscard]] virtual const std::string& Catalog() const noexcept = 0;
    };

    /** @brief Importer-side metadata. */
    class XnaImporterMetadata : public XnaComponentMetadata
    {
    public:
        /** @brief The registration descriptor. */
        [[nodiscard]] virtual const Xna::ContentImporterAttribute& Attribute() const noexcept = 0;

        /** @brief The .NET name of the imported type. */
        [[nodiscard]] virtual std::string OutputTypeName() const = 0;
    };

    /** @brief Processor-side metadata. */
    class XnaProcessorMetadata : public XnaComponentMetadata
    {
    public:
        /** @brief The registration descriptor; `HasAttribute()` says whether one was given. */
        [[nodiscard]] virtual const Xna::ContentProcessorAttribute& Attribute() const noexcept = 0;

        /** @brief Whether the processor was registered with a `ContentProcessorAttribute`. */
        [[nodiscard]] virtual bool HasAttribute() const noexcept = 0;

        /** @brief The .NET name of the input type. */
        [[nodiscard]] virtual std::string InputTypeName() const = 0;

        /** @brief The .NET name of the output type. */
        [[nodiscard]] virtual std::string OutputTypeName() const = 0;

        /** @brief The declared configurable parameters. */
        [[nodiscard]] virtual Xna::ProcessorParameterCollection Parameters() const = 0;
    };

    /**
     * @brief Converts a canonical platform to the XNA enum.
     * @param platform The canonical platform.
     * @return The XNA platform.
     */
    [[nodiscard]] Xna::TargetPlatform ToXnaTargetPlatform(ContentTargetPlatform platform) noexcept;

    /**
     * @brief Converts an XNA platform to the canonical enum.
     * @param platform The XNA platform.
     * @return The canonical platform.
     */
    [[nodiscard]] ContentTargetPlatform FromXnaTargetPlatform(Xna::TargetPlatform platform) noexcept;

    /**
     * @brief Converts canonical typed parameters into the boxed dictionary XNA processors read.
     *
     * Integers become `System.Int64`/`System.UInt64` boxes, floating point `System.Double`, and
     * strings `System.String`; the processor's parameter bindings convert to the property type.
     *
     * @param parameters Canonical parameters.
     * @return The boxed dictionary, in key order.
     */
    [[nodiscard]] Xna::OpaqueDataDictionary ToOpaqueData(const ContentProcessorParameters& parameters);

    /**
     * @brief Converts a boxed dictionary into canonical typed parameters.
     *
     * Values that are not bool/integer/float/string boxes are spelled through their
     * `ToString()` when they have one, and refused otherwise.
     *
     * @param data The boxed dictionary.
     * @return Canonical parameters.
     * @throws std::invalid_argument for a value the canonical variant cannot carry.
     */
    [[nodiscard]] ContentProcessorParameters ToProcessorParameters(const Xna::OpaqueDataDictionary& data);

    /**
     * @brief An XNA `ContentBuildLogger` that reports through a canonical importer or processor
     *        context, so XNA-shaped components log into the same build log as native ones.
     */
    class XnaBridgeLogger final : public Xna::ContentBuildLogger
    {
    public:
        /**
         * @brief Creates a logger over a canonical importer context.
         * @param context The context; must outlive the logger.
         */
        explicit XnaBridgeLogger(CanonicalImporterContext& context);

        /**
         * @brief Creates a logger over a canonical processor context.
         * @param context The context; must outlive the logger.
         */
        explicit XnaBridgeLogger(CanonicalProcessorContext& context);

        using Xna::ContentBuildLogger::LogImportantMessage;
        using Xna::ContentBuildLogger::LogMessage;
        using Xna::ContentBuildLogger::LogWarning;
        void LogImportantMessage(const std::string& message) override;
        void LogMessage(const std::string& message) override;
        void LogWarning(const std::string& helpLink, const Xna::ContentIdentity& contentIdentity,
                        const std::string& message) override;

    private:
        CanonicalImporterContext* importer_ = nullptr;
        CanonicalProcessorContext* processor_ = nullptr;
    };

    /** @brief The XNA importer context the bridge hands to an XNA-shaped importer. */
    class XnaBridgeImporterContext final : public Xna::ContentImporterContext
    {
    public:
        /**
         * @brief Creates the context over a canonical one.
         * @param context The canonical context; must outlive this one.
         */
        explicit XnaBridgeImporterContext(CanonicalImporterContext& context);

        [[nodiscard]] std::string getIntermediateDirectoryProperty() const override;
        [[nodiscard]] Xna::ContentBuildLogger& getLoggerProperty() const override;
        [[nodiscard]] std::string getOutputDirectoryProperty() const override;
        void AddDependency(const std::string& filename) override;

    private:
        CanonicalImporterContext* context_;
        mutable XnaBridgeLogger logger_;
    };

    /** @brief The XNA processor context the bridge hands to an XNA-shaped processor. */
    class XnaBridgeProcessorContext final : public Xna::ContentProcessorContext
    {
    public:
        /**
         * @brief Creates the context over a canonical one.
         * @param context The canonical context; must outlive this one.
         */
        explicit XnaBridgeProcessorContext(CanonicalProcessorContext& context);

        [[nodiscard]] std::string getBuildConfigurationProperty() const override;
        [[nodiscard]] std::string getIntermediateDirectoryProperty() const override;
        [[nodiscard]] Xna::ContentBuildLogger& getLoggerProperty() const override;
        [[nodiscard]] std::string getOutputDirectoryProperty() const override;
        [[nodiscard]] std::string getOutputFilenameProperty() const override;
        [[nodiscard]] const Xna::OpaqueDataDictionary& getParametersProperty() const override;
        [[nodiscard]] Xna::TargetPlatform getTargetPlatformProperty() const override;
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::GraphicsProfile getTargetProfileProperty() const override;
        void AddDependency(const std::string& filename) override;
        void AddOutputFile(const std::string& filename) override;

    protected:
        [[nodiscard]] Xna::ContentObject BuildAndLoadAssetCore(
            const std::string& sourceFilename, const Xna::ContentIdentity& sourceIdentity,
            const std::string& processorName, const Xna::OpaqueDataDictionary& processorParameters,
            const std::string& importerName, const std::string& inputTypeName,
            const std::string& outputTypeName) override;
        [[nodiscard]] std::string BuildAssetCore(
            const std::string& sourceFilename, const Xna::ContentIdentity& sourceIdentity,
            const std::string& processorName, const Xna::OpaqueDataDictionary& processorParameters,
            const std::string& importerName, const std::string& assetName,
            const std::string& inputTypeName, const std::string& outputTypeName) override;
        [[nodiscard]] Xna::ContentObject ConvertCore(
            const Xna::ContentObject& input, const std::string& processorName,
            const Xna::OpaqueDataDictionary& processorParameters, const std::string& inputTypeName,
            const std::string& outputTypeName) override;

    private:
        CanonicalProcessorContext* context_;
        mutable XnaBridgeLogger logger_;
        Xna::OpaqueDataDictionary parameters_;
        std::map<std::string, std::string> nestedAssets_;
    };

    /**
     * @brief Adapts an XNA-shaped importer class into a canonical importer.
     *
     * A fresh @p TImporter is constructed per asset, which is what `BuildContent` does; the
     * class must therefore be default-constructible.
     *
     * @tparam TImporter A class deriving `Microsoft::Xna::Framework::Content::Pipeline::ContentImporter<T>`.
     */
    template<typename TImporter>
    class XnaImporterComponent final : public ContentImporter, public XnaImporterMetadata
    {
        static_assert(std::is_base_of_v<Xna::IContentImporter, TImporter>,
                      "XnaImporterComponent needs a class deriving ContentImporter<T>.");
        static_assert(std::is_default_constructible_v<TImporter>,
                      "An XNA-shaped importer is constructed per asset and must be default-constructible.");

    public:
        /**
         * @brief Creates the adapter.
         * @param className The XNA class name, which becomes the registry identity.
         * @param attribute The registration descriptor.
         * @param version Build version entering the incremental fingerprint.
         * @param catalog Catalog name for the component scanner.
         */
        XnaImporterComponent(std::string className, Xna::ContentImporterAttribute attribute,
                             std::string version, std::string catalog)
            : className_(std::move(className)), attribute_(std::move(attribute)),
              version_(std::move(version)), catalog_(std::move(catalog))
        {
        }

        [[nodiscard]] ContentComponentIdentity Identity() const override
        {
            return ContentComponentIdentity{className_, version_};
        }

        [[nodiscard]] std::vector<std::string> SourceExtensions() const override
        {
            std::vector<std::string> extensions;
            for (std::string extension : attribute_.getFileExtensionsProperty())
            {
                for (char& c : extension) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                extensions.push_back(std::move(extension));
            }
            return extensions;
        }

        [[nodiscard]] std::vector<std::string> OutputTypes() const override
        {
            return {OutputTypeName()};
        }

        [[nodiscard]] std::string DefaultProcessor() const override
        {
            return attribute_.getDefaultProcessorProperty();
        }

        [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override
        {
            TImporter importer{};
            XnaBridgeImporterContext xnaContext(context);
            Xna::IContentImporter& untyped = importer;
            return untyped.Import(context.SourcePath().string(), xnaContext);
        }

        [[nodiscard]] const std::string& XnaClassName() const noexcept override { return className_; }
        [[nodiscard]] const std::string& Catalog() const noexcept override { return catalog_; }
        [[nodiscard]] const Xna::ContentImporterAttribute& Attribute() const noexcept override { return attribute_; }
        [[nodiscard]] std::string OutputTypeName() const override
        {
            return Xna::ContentTypeName<
                typename detail::XnaImportedTypeOf<typename TImporter::ImportResult>::type>::Name();
        }

    private:
        std::string className_;
        Xna::ContentImporterAttribute attribute_;
        std::string version_;
        std::string catalog_;
    };

    /**
     * @brief Adapts an XNA-shaped processor class into a canonical processor.
     *
     * A fresh @p TProcessor is constructed per asset, its declared parameters are assigned from
     * the build's parameters, and its typed `Process` runs over the unboxed input.
     *
     * @tparam TProcessor A class deriving `Microsoft::Xna::Framework::Content::Pipeline::ContentProcessor<TIn,TOut>`.
     */
    template<typename TProcessor>
    class XnaProcessorComponent final : public ContentProcessor, public XnaProcessorMetadata
    {
        static_assert(std::is_base_of_v<Xna::IContentProcessor, TProcessor>,
                      "XnaProcessorComponent needs a class deriving ContentProcessor<TInput, TOutput>.");
        static_assert(std::is_default_constructible_v<TProcessor>,
                      "An XNA-shaped processor is constructed per asset and must be default-constructible.");

    public:
        /**
         * @brief Creates the adapter.
         * @param className The XNA class name, which becomes the registry identity.
         * @param attribute The registration descriptor, or none.
         * @param hasAttribute Whether @p attribute was given.
         * @param version Build version entering the incremental fingerprint.
         * @param catalog Catalog name for the component scanner.
         */
        XnaProcessorComponent(std::string className, Xna::ContentProcessorAttribute attribute,
                              bool hasAttribute, std::string version, std::string catalog)
            : className_(std::move(className)), attribute_(std::move(attribute)),
              hasAttribute_(hasAttribute), version_(std::move(version)), catalog_(std::move(catalog)),
              bindings_(Xna::DescribeProcessorParameters<TProcessor>())
        {
        }

        [[nodiscard]] ContentComponentIdentity Identity() const override
        {
            return ContentComponentIdentity{className_, version_};
        }

        [[nodiscard]] std::string InputType() const override { return InputTypeName(); }
        [[nodiscard]] std::string OutputType() const override { return OutputTypeName(); }

        void ValidateParameters(const ContentProcessorParameters& parameters) const override
        {
            TProcessor probe{};
            AssignParameters(probe, parameters);
        }

        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override
        {
            TProcessor processor{};
            AssignParameters(processor, context.Parameters());
            XnaBridgeProcessorContext xnaContext(context);
            Xna::IContentProcessor& untyped = processor;
            return untyped.Process(input, xnaContext);
        }

        [[nodiscard]] const std::string& XnaClassName() const noexcept override { return className_; }
        [[nodiscard]] const std::string& Catalog() const noexcept override { return catalog_; }
        [[nodiscard]] const Xna::ContentProcessorAttribute& Attribute() const noexcept override { return attribute_; }
        [[nodiscard]] bool HasAttribute() const noexcept override { return hasAttribute_; }
        [[nodiscard]] std::string InputTypeName() const override
        {
            const TProcessor probe{};
            return probe.getInputTypeNameProperty();
        }
        [[nodiscard]] std::string OutputTypeName() const override
        {
            const TProcessor probe{};
            return probe.getOutputTypeNameProperty();
        }
        [[nodiscard]] Xna::ProcessorParameterCollection Parameters() const override
        {
            return bindings_.ToCollection();
        }

    private:
        void AssignParameters(TProcessor& processor, const ContentProcessorParameters& parameters) const
        {
            for (const auto& [name, value] : parameters.Values())
            {
                const Xna::ProcessorParameterBinding<TProcessor>* binding = bindings_.Find(name);
                if (binding == nullptr)
                {
                    throw std::invalid_argument("processor '" + className_ +
                                                "' has no parameter named '" + name + "'.");
                }
                const Xna::OpaqueDataDictionary boxed = ToOpaqueData(ContentProcessorParametersOf(name, value));
                try
                {
                    binding->assignObject(processor, boxed[name]);
                }
                catch (const System::Exception& error)
                {
                    throw std::invalid_argument("processor parameter '" + name + "': " +
                                                error.getMessageProperty());
                }
            }
        }

        static ContentProcessorParameters ContentProcessorParametersOf(const std::string& name,
                                                                       const ContentProcessorParameterValue& value)
        {
            ContentProcessorParameters single;
            single.Set(name, value);
            return single;
        }

        std::string className_;
        Xna::ContentProcessorAttribute attribute_;
        bool hasAttribute_;
        std::string version_;
        std::string catalog_;
        Xna::ProcessorParameterBindings<TProcessor> bindings_;
    };

    /**
     * @brief Registers a canonical `.xnb` writer for every type a `ContentCompiler` can write, so
     *        an XNA-shaped processor's output reaches `.xnb` through the one pipeline
     *        (plans/plan_xnapipeline_parity.md `XNAPP-063`).
     *
     * One canonical writer is registered per known .NET type name (its stable processed type);
     * each compiles the boxed value with the compiler under the given platform/profile/container
     * options. A type that already has a canonical XNB writer (the built-in routes' processed
     * types are named differently, so none collides today) is skipped rather than duplicated.
     *
     * @param registry The registry to configure before builds begin.
     * @param compiler The compiler, shared with the writers; add user type writers before calling.
     * @param options Container options every registered writer emits; platform and profile come
     *        from the build environment at write time.
     */
    void RegisterXnaXnbOutput(ContentPipelineRegistry& registry,
                              std::shared_ptr<const Xna::Serialization::Compiler::ContentCompiler> compiler,
                              const CNA::Internal::Xnb::XnbFileOptions& options);

    /**
     * @brief Registers an XNA-shaped importer class with a canonical registry.
     *
     * @tparam TImporter A class deriving `ContentImporter<T>`.
     * @param registry The registry to configure.
     * @param className The XNA class name (`TextureImporter`), used as the registry identity and
     *        by `.contentproj` `<Importer>` elements.
     * @param attribute The `ContentImporterAttribute` descriptor: extensions, default processor,
     *        display name, caching.
     * @param version Build version entering the incremental fingerprint.
     * @param catalog Catalog name reported by `PipelineComponentScanner`.
     */
    template<typename TImporter>
    void RegisterXnaImporter(ContentPipelineRegistry& registry, std::string className,
                             Xna::ContentImporterAttribute attribute, std::string version = "1",
                             std::string catalog = "Microsoft.Xna.Framework.Content.Pipeline")
    {
        registry.RegisterImporter(std::make_shared<const XnaImporterComponent<TImporter>>(
            std::move(className), std::move(attribute), std::move(version), std::move(catalog)));
    }

    /**
     * @brief Registers an XNA-shaped processor class with a canonical registry.
     *
     * @tparam TProcessor A class deriving `ContentProcessor<TInput, TOutput>`.
     * @param registry The registry to configure.
     * @param className The XNA class name (`TextureProcessor`), used as the registry identity and
     *        by `.contentproj` `<Processor>` elements.
     * @param attribute The `ContentProcessorAttribute` descriptor.
     * @param version Build version entering the incremental fingerprint.
     * @param catalog Catalog name reported by `PipelineComponentScanner`.
     */
    template<typename TProcessor>
    void RegisterXnaProcessor(ContentPipelineRegistry& registry, std::string className,
                              Xna::ContentProcessorAttribute attribute, std::string version = "1",
                              std::string catalog = "Microsoft.Xna.Framework.Content.Pipeline")
    {
        registry.RegisterProcessor(std::make_shared<const XnaProcessorComponent<TProcessor>>(
            std::move(className), std::move(attribute), true, std::move(version), std::move(catalog)));
    }

    /**
     * @brief Registers an XNA-shaped processor class that carries no `ContentProcessorAttribute`
     *        (XNA's `MaterialProcessor`, `ModelTextureProcessor`, `SpriteTextureProcessor`).
     *
     * @tparam TProcessor A class deriving `ContentProcessor<TInput, TOutput>`.
     * @param registry The registry to configure.
     * @param className The XNA class name.
     * @param version Build version entering the incremental fingerprint.
     * @param catalog Catalog name reported by `PipelineComponentScanner`.
     */
    template<typename TProcessor>
    void RegisterXnaProcessorWithoutAttribute(ContentPipelineRegistry& registry, std::string className,
                                              std::string version = "1",
                                              std::string catalog = "Microsoft.Xna.Framework.Content.Pipeline")
    {
        registry.RegisterProcessor(std::make_shared<const XnaProcessorComponent<TProcessor>>(
            std::move(className), Xna::ContentProcessorAttribute{}, false, std::move(version),
            std::move(catalog)));
    }
}
