// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterAttribute.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Reads an intermediate XML file into whatever object it names.
     *
     * The document decides the type, so this importer has no default processor: what comes out is
     * passed on as it is.
     */
    class XmlImporter final : public ContentImporter<System::Object>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.XmlImporter";

        /** @brief Initializes a new importer. */
        XmlImporter() = default;

        /**
         * @brief Reads the file through the intermediate serializer.
         *
         * @param filename Path to the `.xml` source.
         * @param context The importer context; nothing is added to it, as XNA's adds nothing.
         * @return The object the document names.
         * @throws System::IO::FileNotFoundException when the file does not exist.
         * @throws InvalidContentException when the document is not readable intermediate XML.
         */
        [[nodiscard]] ContentObject Import(const std::string& filename,
                                           ContentImporterContext& context) override;

        /**
         * @brief The descriptor XNA declares on this importer: `.xml`, and no default processor.
         *
         * @return The attribute.
         */
        CNAEXT [[nodiscard]] static ContentImporterAttribute Attribute();

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;
    };
}
