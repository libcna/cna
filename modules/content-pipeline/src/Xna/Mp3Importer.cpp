// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Mp3Importer.hpp"

#include <filesystem>

#include "System/IO/FileNotFoundException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    std::shared_ptr<Audio::AudioContent> Mp3Importer::Import(const std::string& filename,
                                                             ContentImporterContext& context)
    {
        (void)context;
        std::error_code error;
        if (!std::filesystem::exists(filename, error) || error)
        {
            // XNA's own message keeps its unformatted placeholder, exactly as the WAV route's
            // does (measured, tests/reference/xna40/media case mp3/missing.mp3).
            throw System::IO::FileNotFoundException("Could not locate audio file \"{0}\".");
        }
        return std::make_shared<Audio::AudioContent>(filename, Audio::AudioFileType::Mp3);
    }

    ContentImporterAttribute Mp3Importer::Attribute()
    {
        ContentImporterAttribute attribute(".mp3");
        attribute.setDefaultProcessorProperty("SongProcessor");
        attribute.setDisplayNameProperty("MP3 Audio File - XNA Framework");
        return attribute;
    }

    const std::string& Mp3Importer::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
