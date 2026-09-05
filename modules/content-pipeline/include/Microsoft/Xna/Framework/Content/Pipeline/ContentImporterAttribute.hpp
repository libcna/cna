// SPDX-License-Identifier: MS-PL
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "System/Attribute.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Provides properties that identify and provide metadata about the importer, such as
     *        supported file extensions and caching information.
     *
     * C++ has no CLR attributes, so this is a **descriptor object** with the attribute's name and
     * properties, handed to the registry when the importer class is registered
     * (docs/xna-content-pipeline-compat-api.md §4). Its content is what MSBuild's component
     * scanner reads off the attribute in XNA.
     */
    class ContentImporterAttribute : public System::Attribute
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.ContentImporterAttribute";

        /**
         * @brief Initializes an attribute for an importer that handles one file extension.
         *
         * @param fileExtension The extension, including the leading dot (for example `.png`).
         */
        explicit ContentImporterAttribute(std::string fileExtension);

        /**
         * @brief Initializes an attribute for an importer that handles several file extensions.
         *
         * @param fileExtensions The extensions, each including the leading dot.
         */
        explicit ContentImporterAttribute(std::vector<std::string> fileExtensions);

        /**
         * @brief Initializes an attribute from a braced list of extensions, so
         *        `ContentImporterAttribute({".bmp", ".dds"})` reads as the C# `params` call does.
         *
         * @param fileExtensions The extensions, each including the leading dot.
         */
        CNAEXT ContentImporterAttribute(std::initializer_list<std::string> fileExtensions);

        /**
         * @brief Gets whether the importer's output may be cached between builds.
         *
         * @return True when the host may cache the imported data (XNA sets this for FBX and X).
         */
        [[nodiscard]] bool getCacheImportedDataProperty() const noexcept;

        /**
         * @brief Sets whether the importer's output may be cached between builds.
         *
         * @param value The caching policy.
         */
        void setCacheImportedDataProperty(bool value) noexcept;

        /**
         * @brief Gets the name of the processor used when the asset does not name one.
         *
         * @return The processor's class name, or empty when the importer declares none.
         */
        [[nodiscard]] const std::string& getDefaultProcessorProperty() const noexcept;

        /**
         * @brief Sets the name of the default processor.
         *
         * @param value The processor's class name.
         */
        void setDefaultProcessorProperty(std::string value);

        /**
         * @brief Gets the display name shown to users.
         *
         * @return The display name, or empty when none was given.
         */
        [[nodiscard]] const std::string& getDisplayNameProperty() const noexcept;

        /**
         * @brief Sets the display name shown to users.
         *
         * @param value The display name.
         */
        void setDisplayNameProperty(std::string value);

        /**
         * @brief Gets the supported file extensions, in the order they were declared.
         *
         * @return The extensions, each including the leading dot.
         */
        [[nodiscard]] const std::vector<std::string>& getFileExtensionsProperty() const noexcept;

    private:
        std::vector<std::string> fileExtensions_;
        std::string defaultProcessor_;
        std::string displayName_;
        bool cacheImportedData_ = false;
    };
}
