// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/MediaLibraryPaths.hpp"

#include <SDL3/SDL.h>

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

        std::string ResolveRealFolder(SDL_Folder folder)
        {
            const char* raw = SDL_GetUserFolder(folder);
            if (!raw)
            {
                return {};
            }
            return StripTrailingSeparator(raw);
        }
    }

    std::string MediaLibraryPaths::GetMusicRoot()
    {
        if (!musicOverride_.empty())
        {
            return musicOverride_;
        }
        return ResolveRealFolder(SDL_FOLDER_MUSIC);
    }

    std::string MediaLibraryPaths::GetPictureRoot()
    {
        if (!pictureOverride_.empty())
        {
            return pictureOverride_;
        }
        return ResolveRealFolder(SDL_FOLDER_PICTURES);
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
