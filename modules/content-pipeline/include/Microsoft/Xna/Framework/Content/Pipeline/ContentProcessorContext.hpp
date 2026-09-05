// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/OpaqueDataDictionary.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/TargetPlatform.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Provides access to custom processor parameters, methods for converting member data,
     *        and triggering nested builds.
     *
     * Abstract, as in XNA. The three generic members `BuildAndLoadAsset`, `BuildAsset` and
     * `Convert` are member templates that forward to non-template virtuals carrying the type
     * names, because C++ virtuals cannot be templates; a subclass (the pipeline's bridge, or a
     * test double) overrides the `*Core` members (docs/xna-content-pipeline-compat-api.md §2, §6).
     */
    class ContentProcessorContext
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext";

        /** @brief Initializes a context. */
        ContentProcessorContext() = default;

        /** @brief Destroys the context. */
        virtual ~ContentProcessorContext() = default;

        /**
         * @brief Gets the name of the current content build configuration (`Debug`, `Release`).
         *
         * @return The configuration name.
         */
        [[nodiscard]] virtual std::string getBuildConfigurationProperty() const = 0;

        /**
         * @brief Gets the path of the directory that contains intermediate build files.
         *
         * @return The intermediate directory, or empty when the host provides none.
         */
        [[nodiscard]] virtual std::string getIntermediateDirectoryProperty() const = 0;

        /**
         * @brief Gets the logger interface used for status messages or warnings.
         *
         * @return The logger; valid for the duration of the process call.
         */
        [[nodiscard]] virtual ContentBuildLogger& getLoggerProperty() const = 0;

        /**
         * @brief Gets the output path of the content processor.
         *
         * @return The output directory, or empty when the host provides none.
         */
        [[nodiscard]] virtual std::string getOutputDirectoryProperty() const = 0;

        /**
         * @brief Gets the output file name of the content processor.
         *
         * @return The full path of the compiled asset, or the logical name with the container
         *         extension when no output directory is known.
         */
        [[nodiscard]] virtual std::string getOutputFilenameProperty() const = 0;

        /**
         * @brief Gets the collection of parameters used by the content processor.
         *
         * @return The parameters, as boxed values keyed by property name.
         */
        [[nodiscard]] virtual const OpaqueDataDictionary& getParametersProperty() const = 0;

        /**
         * @brief Gets the current content build target platform.
         *
         * @return The platform.
         */
        [[nodiscard]] virtual TargetPlatform getTargetPlatformProperty() const = 0;

        /**
         * @brief Gets the current content build target profile.
         *
         * @return `Reach` or `HiDef`.
         */
        [[nodiscard]] virtual Microsoft::Xna::Framework::Graphics::GraphicsProfile getTargetProfileProperty() const = 0;

        /**
         * @brief Adds a dependency to the specified file: the asset is rebuilt when it changes.
         *
         * @param filename Name of the asset file.
         */
        virtual void AddDependency(const std::string& filename) = 0;

        /**
         * @brief Adds a file name to the list of related output files maintained by the content
         *        processor, so it is deployed and cleaned with the asset.
         *
         * @param filename Name of the file.
         */
        virtual void AddOutputFile(const std::string& filename) = 0;

        /**
         * @brief Initiates a nested build of the specified asset and then loads the result into
         *        memory.
         *
         * @tparam TInput Type of the input.
         * @tparam TOutput Type of the converted output.
         * @param sourceAsset Reference to the source asset.
         * @param processorName Name of the processor for this content; empty selects the
         *        importer's default.
         * @param processorParameters Optional parameters for the processor.
         * @param importerName Optional name of the importer; empty selects by extension.
         * @return The loaded processed content.
         */
        template<typename TInput, typename TOutput>
        [[nodiscard]] Carrier<TOutput> BuildAndLoadAsset(const ExternalReference<TInput>& sourceAsset,
                                                         const std::string& processorName,
                                                         const OpaqueDataDictionary& processorParameters = {},
                                                         const std::string& importerName = {})
        {
            return Unbox<TOutput>(BuildAndLoadAssetCore(sourceAsset.getFilenameProperty(),
                                                        sourceAsset.getIdentityProperty(), processorName,
                                                        processorParameters, importerName,
                                                        ContentTypeName<TInput>::Name(),
                                                        ContentTypeName<TOutput>::Name()));
        }

        /**
         * @brief Initiates a nested build of the specified asset, writing it as a separate
         *        compiled asset the current one refers to.
         *
         * @tparam TInput Type of the input.
         * @tparam TOutput Type of the converted output.
         * @param sourceAsset Reference to the source asset.
         * @param processorName Name of the processor for this content; empty selects the
         *        importer's default.
         * @param processorParameters Optional parameters for the processor.
         * @param importerName Optional name of the importer; empty selects by extension.
         * @param assetName Optional name of the built asset; empty derives it from the source name.
         * @return Reference to the built asset.
         */
        template<typename TInput, typename TOutput>
        [[nodiscard]] ExternalReference<TOutput> BuildAsset(const ExternalReference<TInput>& sourceAsset,
                                                            const std::string& processorName,
                                                            const OpaqueDataDictionary& processorParameters = {},
                                                            const std::string& importerName = {},
                                                            const std::string& assetName = {})
        {
            return ExternalReference<TOutput>(BuildAssetCore(sourceAsset.getFilenameProperty(),
                                                             sourceAsset.getIdentityProperty(), processorName,
                                                             processorParameters, importerName, assetName,
                                                             ContentTypeName<TInput>::Name(),
                                                             ContentTypeName<TOutput>::Name()));
        }

        /**
         * @brief Converts a content item object using the specified content processor.
         *
         * @tparam TInput Type of the input content.
         * @tparam TOutput Type of the converted output.
         * @param input Source content object.
         * @param processorName Name of the processor to apply.
         * @param processorParameters Optional parameters for the processor.
         * @return The converted content.
         */
        template<typename TInput, typename TOutput>
        [[nodiscard]] Carrier<TOutput> Convert(const Carrier<TInput>& input, const std::string& processorName,
                                               const OpaqueDataDictionary& processorParameters = {})
        {
            return Unbox<TOutput>(ConvertCore(Box<TInput>(input), processorName, processorParameters,
                                              ContentTypeName<TOutput>::Name()));
        }

    protected:
        /**
         * @brief Runs a nested import and process in-process and returns the boxed result.
         *
         * @param sourceFilename The source file, as resolved by the external reference.
         * @param sourceIdentity Identity of the referencing content, for diagnostics.
         * @param processorName Processor name, or empty for the importer's default.
         * @param processorParameters Processor parameters.
         * @param importerName Importer name, or empty to select by extension.
         * @param inputTypeName Expected imported type name.
         * @param outputTypeName Expected processed type name.
         * @return The processed content, boxed.
         */
        CNAEXT [[nodiscard]] virtual ContentObject BuildAndLoadAssetCore(
            const std::string& sourceFilename, const ContentIdentity& sourceIdentity,
            const std::string& processorName, const OpaqueDataDictionary& processorParameters,
            const std::string& importerName, const std::string& inputTypeName,
            const std::string& outputTypeName) = 0;

        /**
         * @brief Runs a nested build that writes a separate asset and returns its filename.
         *
         * @param sourceFilename The source file.
         * @param sourceIdentity Identity of the referencing content.
         * @param processorName Processor name, or empty for the importer's default.
         * @param processorParameters Processor parameters.
         * @param importerName Importer name, or empty to select by extension.
         * @param assetName Asset name, or empty to derive one.
         * @param inputTypeName Expected imported type name.
         * @param outputTypeName Expected processed type name.
         * @return The compiled asset's filename, relative to the output directory.
         */
        CNAEXT [[nodiscard]] virtual std::string BuildAssetCore(
            const std::string& sourceFilename, const ContentIdentity& sourceIdentity,
            const std::string& processorName, const OpaqueDataDictionary& processorParameters,
            const std::string& importerName, const std::string& assetName,
            const std::string& inputTypeName, const std::string& outputTypeName) = 0;

        /**
         * @brief Runs a processor over an in-memory object and returns the boxed result.
         *
         * @param input The boxed input.
         * @param processorName Processor name; must be registered.
         * @param processorParameters Processor parameters.
         * @param outputTypeName Expected processed type name.
         * @return The processed content, boxed.
         */
        CNAEXT [[nodiscard]] virtual ContentObject ConvertCore(
            const ContentObject& input, const std::string& processorName,
            const OpaqueDataDictionary& processorParameters, const std::string& outputTypeName) = 0;
    };
}
