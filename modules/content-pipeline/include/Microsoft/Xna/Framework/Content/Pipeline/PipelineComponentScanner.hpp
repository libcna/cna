// SPDX-License-Identifier: MS-PL
#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ProcessorParameter.hpp"

namespace CNA::Content::Pipeline
{
    class ContentPipelineRegistry;
}

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Implements a scanner object containing methods that retrieve information about the
     *        importers and processors available to the content pipeline.
     *
     * XNA scans assembly files by path. CNA loads no code dynamically, so this scanner
     * enumerates the XNA-shaped components registered in a `ContentPipelineRegistry`, and the
     * strings given to `Update` name **component catalogs** -- the value each component was
     * registered under (the built-ins use their XNA assembly names) -- rather than files. A
     * name that matches no catalog is reported in `Errors`, as an unloadable assembly is in XNA
     * (`HOST_SUBSTITUTION`, docs/xna-content-pipeline-compat-api.md §2).
     */
    class PipelineComponentScanner final
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.PipelineComponentScanner";

        /** @brief Initializes a scanner with no registry; `Update` finds nothing until one is attached. */
        PipelineComponentScanner() = default;

        /**
         * @brief Initializes a scanner over a registry.
         *
         * @param registry The registry whose XNA-shaped components are enumerated; must outlive
         *        the scanner.
         */
        CNAEXT explicit PipelineComponentScanner(
            std::shared_ptr<const CNA::Content::Pipeline::ContentPipelineRegistry> registry);

        /**
         * @brief Gets the list of error messages produced by the last `Update`.
         *
         * @return Error texts, one per catalog name that matched nothing.
         */
        [[nodiscard]] const std::vector<std::string>& getErrorsProperty() const noexcept;

        /**
         * @brief Gets a dictionary that maps available importers to their associated metadata.
         *
         * @return Importer name to descriptor.
         */
        [[nodiscard]] const std::map<std::string, ContentImporterAttribute>& getImporterAttributesProperty() const noexcept;

        /**
         * @brief Gets the list of available importers.
         *
         * @return Importer names, sorted.
         */
        [[nodiscard]] std::vector<std::string> getImporterNamesProperty() const;

        /**
         * @brief Gets a dictionary that maps available importers to the fully qualified name of
         *        their return type.
         *
         * @return Importer name to output type name.
         */
        [[nodiscard]] const std::map<std::string, std::string>& getImporterOutputTypesProperty() const noexcept;

        /**
         * @brief Gets a dictionary that maps available processors to their associated metadata.
         *
         * @return Processor name to descriptor.
         */
        [[nodiscard]] const std::map<std::string, ContentProcessorAttribute>& getProcessorAttributesProperty() const noexcept;

        /**
         * @brief Gets a dictionary that maps available processors to the fully qualified name of
         *        their input type.
         *
         * @return Processor name to input type name.
         */
        [[nodiscard]] const std::map<std::string, std::string>& getProcessorInputTypesProperty() const noexcept;

        /**
         * @brief Gets the list of available processors.
         *
         * @return Processor names, sorted.
         */
        [[nodiscard]] std::vector<std::string> getProcessorNamesProperty() const;

        /**
         * @brief Gets a dictionary that maps available processors to the fully qualified name of
         *        their output type.
         *
         * @return Processor name to output type name.
         */
        [[nodiscard]] const std::map<std::string, std::string>& getProcessorOutputTypesProperty() const noexcept;

        /**
         * @brief Gets a dictionary that maps available processors to their supported parameters.
         *
         * @return Processor name to parameter collection.
         */
        [[nodiscard]] const std::map<std::string, ProcessorParameterCollection>& getProcessorParametersProperty() const noexcept;

        /**
         * @brief Updates the scanner object to reflect the components of the named catalogs.
         *
         * @param pipelineAssemblies Catalog names to include; empty includes every catalog.
         * @return True when the set of components changed since the previous update.
         */
        bool Update(const std::vector<std::string>& pipelineAssemblies);

        /**
         * @brief Updates the scanner object to reflect the components of the named catalogs,
         *        with a second list of dependency catalogs that are loaded but not scanned.
         *
         * @param pipelineAssemblies Catalog names to include; empty includes every catalog.
         * @param pipelineAssemblyDependencies Catalog names only checked for existence.
         * @return True when the set of components changed since the previous update.
         */
        bool Update(const std::vector<std::string>& pipelineAssemblies,
                    const std::vector<std::string>& pipelineAssemblyDependencies);

    private:
        std::shared_ptr<const CNA::Content::Pipeline::ContentPipelineRegistry> registry_;
        std::vector<std::string> errors_;
        std::map<std::string, ContentImporterAttribute> importerAttributes_;
        std::map<std::string, std::string> importerOutputTypes_;
        std::map<std::string, ContentProcessorAttribute> processorAttributes_;
        std::map<std::string, std::string> processorInputTypes_;
        std::map<std::string, std::string> processorOutputTypes_;
        std::map<std::string, ProcessorParameterCollection> processorParameters_;
    };
}
