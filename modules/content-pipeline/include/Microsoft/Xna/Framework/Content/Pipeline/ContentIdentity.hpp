// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Provides properties that identify where a piece of content came from.
     *
     * A small value class here: XNA's `ContentIdentity` is a reference type that may be null, and
     * an identity whose three strings are all empty plays that role
     * (docs/xna-content-pipeline-compat-api.md §2).
     */
    class ContentIdentity
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity";

        /** @brief Initializes an empty identity. */
        ContentIdentity() = default;

        /**
         * @brief Initializes an identity naming a source file.
         *
         * @param sourceFilename Absolute path of the source file.
         */
        explicit ContentIdentity(std::string sourceFilename);

        /**
         * @brief Initializes an identity naming a source file and the tool that produced it.
         *
         * @param sourceFilename Absolute path of the source file.
         * @param sourceTool Name of the tool that created the source, or empty.
         */
        ContentIdentity(std::string sourceFilename, std::string sourceTool);

        /**
         * @brief Initializes an identity naming a source file, a tool and a fragment.
         *
         * @param sourceFilename Absolute path of the source file.
         * @param sourceTool Name of the tool that created the source, or empty.
         * @param fragmentIdentifier Locator of the item within the source file, or empty.
         */
        ContentIdentity(std::string sourceFilename, std::string sourceTool,
                        std::string fragmentIdentifier);

        /**
         * @brief Gets the specific location of the content item within the source file.
         *
         * @return For example a line number in an XML file; empty when unknown.
         */
        [[nodiscard]] const std::string& getFragmentIdentifierProperty() const noexcept;

        /**
         * @brief Sets the specific location of the content item within the source file.
         *
         * @param value Locator text, or empty.
         */
        void setFragmentIdentifierProperty(std::string value);

        /**
         * @brief Gets the file name of the asset source.
         *
         * @return Absolute path of the source file; empty when unknown.
         */
        [[nodiscard]] const std::string& getSourceFilenameProperty() const noexcept;

        /**
         * @brief Sets the file name of the asset source.
         *
         * @param value Absolute path of the source file.
         */
        void setSourceFilenameProperty(std::string value);

        /**
         * @brief Gets the creation tool of the asset.
         *
         * @return Tool name, or empty when unknown.
         */
        [[nodiscard]] const std::string& getSourceToolProperty() const noexcept;

        /**
         * @brief Sets the creation tool of the asset.
         *
         * @param value Tool name.
         */
        void setSourceToolProperty(std::string value);

        /**
         * @brief Returns whether this identity carries no information at all.
         *
         * @return True when all three strings are empty -- the C++ stand-in for a null identity.
         */
        CNAEXT [[nodiscard]] bool IsEmpty() const noexcept;

        /**
         * @brief Formats the identity the way XNA's build diagnostics print it.
         *
         * @return `source`, or `source#fragment` when a fragment identifier is present.
         */
        CNAEXT [[nodiscard]] std::string ToString() const;

        /** @brief Compares all three strings. */
        bool operator==(const ContentIdentity&) const = default;

    private:
        std::string sourceFilename_;
        std::string sourceTool_;
        std::string fragmentIdentifier_;
    };
}
