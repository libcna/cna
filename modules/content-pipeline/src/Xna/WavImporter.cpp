// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/WavImporter.hpp"

#include <filesystem>

#include "System/IO/FileNotFoundException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    std::shared_ptr<Audio::AudioContent> WavImporter::Import(const std::string& filename,
                                                             ContentImporterContext& context)
    {
        (void)context;
        std::error_code error;
        if (!std::filesystem::exists(filename, error) || error)
        {
            // XNA's own message keeps its unformatted placeholder; this reproduces what the
            // runtime says rather than what it meant to say (measured, wav/importer_refusals).
            throw System::IO::FileNotFoundException("Could not locate audio file \"{0}\".");
        }
        return std::make_shared<Audio::AudioContent>(filename, Audio::AudioFileType::Wav);
    }

    ContentImporterAttribute WavImporter::Attribute()
    {
        ContentImporterAttribute attribute(".wav");
        attribute.setDefaultProcessorProperty("SoundEffectProcessor");
        attribute.setDisplayNameProperty("WAV Audio File - XNA Framework");
        return attribute;
    }

    const std::string& WavImporter::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
