// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Audio/AudioContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterAttribute.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Reads a `.wma` file into an `AudioContent` carrying the decoded samples.
     *
     * The samples are decoded, not carried compressed: the genuine importer answers a format
     * describing the PCM its decoder will produce -- 16-bit at 44100 Hz whatever the source
     * carries, with only the channel count surviving -- and a duration that is the stream's own
     * length truncated to whole milliseconds. See `docs/xna-content-pipeline-media.md`.
     */
    class WmaImporter final : public ContentImporter<Audio::AudioContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.WmaImporter";

        /** @brief Initializes a new importer. */
        WmaImporter() = default;

        /**
         * @brief Reads the file.
         *
         * @param filename Path to the `.wma` source.
         * @param context The importer context; nothing is added to it, as XNA's adds nothing.
         * @return The audio, as decoded PCM.
         * @throws System::IO::FileNotFoundException when the file does not exist.
         * @throws InvalidContentException when the file is not readable audio of this format.
         */
        [[nodiscard]] std::shared_ptr<Audio::AudioContent> Import(const std::string& filename,
                                                                  ContentImporterContext& context) override;

        /**
         * @brief The descriptor XNA declares on this importer: `.wma`, processed by
         *        `SongProcessor`.
         *
         * @return The attribute.
         */
        CNAEXT [[nodiscard]] static ContentImporterAttribute Attribute();

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;
    };
}
