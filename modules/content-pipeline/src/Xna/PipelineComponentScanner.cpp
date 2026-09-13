// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/PipelineComponentScanner.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnaPipelineBridge.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    PipelineComponentScanner::PipelineComponentScanner(
        std::shared_ptr<const CNA::Content::Pipeline::ContentPipelineRegistry> registry)
        : registry_(std::move(registry))
    {
    }

    const std::vector<std::string>& PipelineComponentScanner::getErrorsProperty() const noexcept { return errors_; }

    const std::map<std::string, ContentImporterAttribute>&
    PipelineComponentScanner::getImporterAttributesProperty() const noexcept { return importerAttributes_; }

    std::vector<std::string> PipelineComponentScanner::getImporterNamesProperty() const
    {
        std::vector<std::string> names;
        for (const auto& entry : importerAttributes_) { names.push_back(entry.first); }
        return names;
    }

    const std::map<std::string, std::string>&
    PipelineComponentScanner::getImporterOutputTypesProperty() const noexcept { return importerOutputTypes_; }

    const std::map<std::string, ContentProcessorAttribute>&
    PipelineComponentScanner::getProcessorAttributesProperty() const noexcept { return processorAttributes_; }

    const std::map<std::string, std::string>&
    PipelineComponentScanner::getProcessorInputTypesProperty() const noexcept { return processorInputTypes_; }

    std::vector<std::string> PipelineComponentScanner::getProcessorNamesProperty() const
    {
        std::vector<std::string> names;
        for (const auto& entry : processorInputTypes_) { names.push_back(entry.first); }
        return names;
    }

    const std::map<std::string, std::string>&
    PipelineComponentScanner::getProcessorOutputTypesProperty() const noexcept { return processorOutputTypes_; }

    const std::map<std::string, ProcessorParameterCollection>&
    PipelineComponentScanner::getProcessorParametersProperty() const noexcept { return processorParameters_; }

    bool PipelineComponentScanner::Update(const std::vector<std::string>& pipelineAssemblies)
    {
        return Update(pipelineAssemblies, {});
    }

    bool PipelineComponentScanner::Update(const std::vector<std::string>& pipelineAssemblies,
                                          const std::vector<std::string>& pipelineAssemblyDependencies)
    {
        const std::set<std::string> previousImporters = [this] {
            std::set<std::string> names;
            for (const auto& entry : importerAttributes_) { names.insert(entry.first); }
            return names;
        }();
        const std::set<std::string> previousProcessors = [this] {
            std::set<std::string> names;
            for (const auto& entry : processorInputTypes_) { names.insert(entry.first); }
            return names;
        }();

        errors_.clear();
        importerAttributes_.clear();
        importerOutputTypes_.clear();
        processorAttributes_.clear();
        processorInputTypes_.clear();
        processorOutputTypes_.clear();
        processorParameters_.clear();

        std::set<std::string> knownCatalogs;
        if (registry_ != nullptr)
        {
            const std::set<std::string> wanted(pipelineAssemblies.begin(), pipelineAssemblies.end());
            const auto included = [&wanted](const std::string& catalog) {
                return wanted.empty() || wanted.count(catalog) > 0;
            };
            for (const auto& importer : registry_->Importers())
            {
                const auto* metadata = dynamic_cast<const CNA::Content::Pipeline::XnaImporterMetadata*>(importer.get());
                if (metadata == nullptr) { continue; }
                knownCatalogs.insert(metadata->Catalog());
                if (!included(metadata->Catalog())) { continue; }
                importerAttributes_.insert_or_assign(metadata->XnaClassName(), metadata->Attribute());
                importerOutputTypes_[metadata->XnaClassName()] = metadata->OutputTypeName();
            }
            for (const auto& processor : registry_->Processors())
            {
                const auto* metadata = dynamic_cast<const CNA::Content::Pipeline::XnaProcessorMetadata*>(processor.get());
                if (metadata == nullptr) { continue; }
                knownCatalogs.insert(metadata->Catalog());
                if (!included(metadata->Catalog())) { continue; }
                if (metadata->HasAttribute())
                {
                    processorAttributes_.insert_or_assign(metadata->XnaClassName(), metadata->Attribute());
                }
                processorInputTypes_[metadata->XnaClassName()] = metadata->InputTypeName();
                processorOutputTypes_[metadata->XnaClassName()] = metadata->OutputTypeName();
                processorParameters_.insert_or_assign(metadata->XnaClassName(), metadata->Parameters());
            }
        }
        for (const std::string& name : pipelineAssemblies)
        {
            if (knownCatalogs.count(name) == 0)
            {
                errors_.push_back("No registered pipeline component catalog is named '" + name + "'.");
            }
        }
        for (const std::string& name : pipelineAssemblyDependencies)
        {
            if (knownCatalogs.count(name) == 0)
            {
                errors_.push_back("No registered pipeline component catalog is named '" + name +
                                  "' (listed as a dependency).");
            }
        }

        std::set<std::string> currentImporters;
        for (const auto& entry : importerAttributes_) { currentImporters.insert(entry.first); }
        std::set<std::string> currentProcessors;
        for (const auto& entry : processorInputTypes_) { currentProcessors.insert(entry.first); }
        return currentImporters != previousImporters || currentProcessors != previousProcessors;
    }
}
