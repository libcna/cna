// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/MediaLibraryPaths.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"

namespace CNA::Internal::Media
{
    std::string MediaLibraryPaths::musicOverride_;
    std::string MediaLibraryPaths::pictureOverride_;

    namespace
    {
        std::string StripTrailingSeparator(std::string path)
        {
            while (!path.empty() && (path.back() == '/' || path.back() == '\\'))
            {
                path.pop_back();
            }
            return path;
        }

        std::string ResolveRealFolder(CNA::Platform::UserFolder folder)
        {
            auto* fileSystem = CNA::Platform::GetCurrentPlatform().GetFileSystem();
            return fileSystem ? StripTrailingSeparator(fileSystem->GetUserFolder(folder))
                              : std::string();
        }
    }

    std::string MediaLibraryPaths::GetMusicRoot()
    {
        if (!musicOverride_.empty())
        {
            return musicOverride_;
        }
        return ResolveRealFolder(CNA::Platform::UserFolder::Music);
    }

    std::string MediaLibraryPaths::GetPictureRoot()
    {
        if (!pictureOverride_.empty())
        {
            return pictureOverride_;
        }
        return ResolveRealFolder(CNA::Platform::UserFolder::Pictures);
    }

    void MediaLibraryPaths::SetMusicRootOverride(const std::string& path)
    {
        musicOverride_ = path;
    }

    void MediaLibraryPaths::SetPictureRootOverride(const std::string& path)
    {
        pictureOverride_ = path;
    }
}
