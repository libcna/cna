// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/VideoContent.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Reads a `.wmv` file into a `VideoContent`.
     *
     * Unlike the constructor it calls, this checks that the file is there first, and refuses a
     * missing one with the runtime's own exception -- the same split the audio importers have
     * (measured, `docs/xna-content-pipeline-media.md` section 4).
     */
    class WmvImporter final : public ContentImporter<VideoContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.WmvImporter";

        /** @brief Initializes a new importer. */
        WmvImporter() = default;

        /**
         * @brief Reads the file.
         *
         * @param filename Path to the `.wmv` source.
         * @param context The importer context; nothing is added to it, as XNA's adds nothing.
         * @return The video and what it declares about itself.
         * @throws System::IO::FileNotFoundException when the file does not exist.
         * @throws InvalidContentException when the file is not a readable video.
         */
        [[nodiscard]] std::shared_ptr<VideoContent> Import(const std::string& filename,
                                                           ContentImporterContext& context) override;

        /**
         * @brief The descriptor XNA declares on this importer: `.wmv`, processed by
         *        `VideoProcessor`.
         *
         * @return The attribute.
         */
        CNAEXT [[nodiscard]] static ContentImporterAttribute Attribute();

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;
    };
}
