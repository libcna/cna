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
     * @brief Reads a `.wav` file into an `AudioContent`, keeping the encoding the file names.
     */
    class WavImporter final : public ContentImporter<Audio::AudioContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.WavImporter";

        /** @brief Initializes a new importer. */
        WavImporter() = default;

        /**
         * @brief Reads the file.
         *
         * @param filename Path to the `.wav` source.
         * @param context The importer context; nothing is added to it, as XNA's adds nothing.
         * @return The audio, in the file's own encoding.
         * @throws System::IO::FileNotFoundException when the file does not exist.
         * @throws InvalidContentException when the file is not readable audio.
         */
        [[nodiscard]] std::shared_ptr<Audio::AudioContent> Import(const std::string& filename,
                                                                  ContentImporterContext& context) override;

        /**
         * @brief The descriptor XNA declares on this importer: `.wav`, processed by
         *        `SoundEffectProcessor`.
         *
         * @return The attribute.
         */
        CNAEXT [[nodiscard]] static ContentImporterAttribute Attribute();

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;
    };
}
