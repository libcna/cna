// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterAttribute.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    ContentImporterAttribute::ContentImporterAttribute(std::string fileExtension)
        : fileExtensions_{std::move(fileExtension)}
    {
    }

    ContentImporterAttribute::ContentImporterAttribute(std::vector<std::string> fileExtensions)
        : fileExtensions_(std::move(fileExtensions))
    {
    }

    ContentImporterAttribute::ContentImporterAttribute(std::initializer_list<std::string> fileExtensions)
        : fileExtensions_(fileExtensions)
    {
    }

    bool ContentImporterAttribute::getCacheImportedDataProperty() const noexcept
    {
        return cacheImportedData_;
    }

    void ContentImporterAttribute::setCacheImportedDataProperty(bool value) noexcept
    {
        cacheImportedData_ = value;
    }

    const std::string& ContentImporterAttribute::getDefaultProcessorProperty() const noexcept
    {
        return defaultProcessor_;
    }

    void ContentImporterAttribute::setDefaultProcessorProperty(std::string value)
    {
        defaultProcessor_ = std::move(value);
    }

    const std::string& ContentImporterAttribute::getDisplayNameProperty() const noexcept
    {
        return displayName_;
    }

    void ContentImporterAttribute::setDisplayNameProperty(std::string value)
    {
        displayName_ = std::move(value);
    }

    const std::vector<std::string>& ContentImporterAttribute::getFileExtensionsProperty() const noexcept
    {
        return fileExtensions_;
    }
}
