// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"

#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    const std::string& ContentBuildLogger::getLoggerRootDirectoryProperty() const noexcept
    {
        return loggerRootDirectory_;
    }

    void ContentBuildLogger::setLoggerRootDirectoryProperty(std::string value)
    {
        loggerRootDirectory_ = std::move(value);
    }

    void ContentBuildLogger::PopFile()
    {
        if (files_.empty())
        {
            throw std::logic_error("ContentBuildLogger::PopFile(): no file is being processed.");
        }
        files_.pop_back();
    }

    void ContentBuildLogger::PushFile(std::string filename)
    {
        files_.push_back(std::move(filename));
    }

    std::string ContentBuildLogger::GetCurrentFilename(const ContentIdentity& contentIdentity) const
    {
        std::string filename = contentIdentity.getSourceFilenameProperty();
        if (filename.empty())
        {
            if (files_.empty()) { return {}; }
            filename = files_.back();
        }
        if (loggerRootDirectory_.empty()) { return filename; }
        std::error_code error;
        const std::filesystem::path relative =
            std::filesystem::relative(filename, loggerRootDirectory_, error);
        if (error || relative.empty()) { return filename; }
        return relative.generic_string();
    }
}
