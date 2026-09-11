// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/VideoImporter.hpp"

#include <filesystem>

#include "System/IO/FileNotFoundException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    std::shared_ptr<VideoContent> WmvImporter::Import(const std::string& filename,
                                                      ContentImporterContext& context)
    {
        (void)context;
        std::error_code error;
        if (!std::filesystem::exists(filename, error) || error)
        {
            // The unformatted placeholder is XNA's own, as it is on the audio side (measured,
            // tests/reference/xna40/media case wmv/missing.wmv).
            throw System::IO::FileNotFoundException("Could not locate video file \"{0}\".");
        }
        return std::make_shared<VideoContent>(filename);
    }

    ContentImporterAttribute WmvImporter::Attribute()
    {
        ContentImporterAttribute attribute(".wmv");
        attribute.setDefaultProcessorProperty("VideoProcessor");
        attribute.setDisplayNameProperty("WMV Video File - XNA Framework");
        return attribute;
    }

    const std::string& WmvImporter::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
