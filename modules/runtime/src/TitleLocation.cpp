// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/TitleLocation.hpp"

#include <filesystem>

#include "CNA/Internal/PathUtf8.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"

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
        const std::string basePath =
            CNA::Platform::GetCurrentPlatform().GetFileSystem()->GetBasePath();
        if (!basePath.empty())
        {
            return basePath;
        }

        // The fallback when no platform filesystem answered. UTF-8 like everything else on this
        // boundary; .string() would narrow through the ANSI code page on Windows.
        return CNA::Internal::PathToGenericUtf8(std::filesystem::current_path());
    }
}
