// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/TitleLocation.hpp"

#include <filesystem>

#include <SDL3/SDL.h>

namespace Microsoft::Xna::Framework
{
    std::string TitleLocation::path_;
    bool TitleLocation::initialized_ = false;

    const std::string& TitleLocation::getPathProperty()
    {
        EnsureInitialized();
        return path_;
    }

    void TitleLocation::setPathProperty(const std::string& value)
    {
        path_ = value;
        initialized_ = true;
    }

    const std::string& TitleLocation::Path()
    {
        return getPathProperty();
    }

    void TitleLocation::EnsureInitialized()
    {
        if (!initialized_)
        {
            path_ = DetectBasePath();
            initialized_ = true;
        }
    }

    std::string TitleLocation::DetectBasePath()
    {
        const char* basePath = SDL_GetBasePath();
        if (basePath != nullptr)
        {
            std::string result(basePath);
            if (!result.empty())
            {
                return result;
            }
        }

        return std::filesystem::current_path().string();
    }
}
