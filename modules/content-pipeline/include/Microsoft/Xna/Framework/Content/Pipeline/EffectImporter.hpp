// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/EffectContent.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Reads an `.fx` file into an `EffectContent`, source text and all.
     *
     * The importer compiles nothing and follows no `#include`: it reads the file as it is and
     * stamps the identity the processor's diagnostics name.
     */
    class EffectImporter final : public ContentImporter<Graphics::EffectContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.EffectImporter";

        /** @brief Initializes a new importer. */
        EffectImporter() = default;

        /**
         * @brief Reads the file.
         *
         * @param filename Path to the `.fx` source.
         * @param context The importer context; nothing is added to it, not even an include.
         * @return The effect, carrying the file's text verbatim.
         * @throws System::IO::FileNotFoundException when the file does not exist.
         */
        [[nodiscard]] std::shared_ptr<Graphics::EffectContent> Import(
            const std::string& filename, ContentImporterContext& context) override;

        /**
         * @brief The descriptor XNA declares on this importer: `.fx`, processed by
         *        `EffectProcessor`.
         *
         * @return The attribute.
         */
        CNAEXT [[nodiscard]] static ContentImporterAttribute Attribute();

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;
    };
}
