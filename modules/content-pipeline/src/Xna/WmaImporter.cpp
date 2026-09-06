// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/WmaImporter.hpp"

#include <filesystem>

#include "System/IO/FileNotFoundException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    std::shared_ptr<Audio::AudioContent> WmaImporter::Import(const std::string& filename,
                                                             ContentImporterContext& context)
    {
        (void)context;
        std::error_code error;
        if (!std::filesystem::exists(filename, error) || error)
        {
            // XNA's own message keeps its unformatted placeholder, exactly as the WAV route's
            // does (measured, tests/reference/xna40/media case wma/missing.wma).
            throw System::IO::FileNotFoundException("Could not locate audio file \"{0}\".");
        }
        return std::make_shared<Audio::AudioContent>(filename, Audio::AudioFileType::Wma);
    }

    ContentImporterAttribute WmaImporter::Attribute()
    {
        ContentImporterAttribute attribute(".wma");
        attribute.setDefaultProcessorProperty("SongProcessor");
        attribute.setDisplayNameProperty("WMA Audio File - XNA Framework");
        return attribute;
    }

    const std::string& WmaImporter::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
