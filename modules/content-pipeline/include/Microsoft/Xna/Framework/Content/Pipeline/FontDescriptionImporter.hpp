// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/FontDescription.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Reads a `.spritefont` file into a `FontDescription`.
     *
     * The file is an intermediate XML document, so it is read by the intermediate serializer and
     * follows its rules: the members are read in order, and one that is not optional must be
     * there.
     */
    class FontDescriptionImporter final : public ContentImporter<Graphics::FontDescription>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.FontDescriptionImporter";

        /** @brief Initializes a new importer. */
        FontDescriptionImporter() = default;

        /**
         * @brief Reads the file.
         *
         * @param filename Path to the `.spritefont` source.
         * @param context The importer context; nothing is added to it, as XNA's adds nothing.
         * @return The font description, with the identity of the file it came from.
         * @throws System::IO::FileNotFoundException when the file does not exist.
         * @throws InvalidContentException when the document is not a readable font description.
         */
        [[nodiscard]] std::shared_ptr<Graphics::FontDescription> Import(
            const std::string& filename, ContentImporterContext& context) override;

        /**
         * @brief The descriptor XNA declares on this importer: `.spritefont`, processed by
         *        `FontDescriptionProcessor`.
         *
         * @return The attribute.
         */
        CNAEXT [[nodiscard]] static ContentImporterAttribute Attribute();

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;
    };
}
