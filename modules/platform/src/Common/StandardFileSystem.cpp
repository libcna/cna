// SPDX-License-Identifier: MS-PL

#include "StandardFileSystem.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <filesystem>
#include <fstream>
#include <utility>

namespace CNA::Platform::Common {

    StandardFileSystem::StandardFileSystem(std::string preferencesRootName)
        : preferencesRootName_(std::move(preferencesRootName))
    {
    }

    std::string StandardFileSystem::GetBasePath() const
    {
        return std::filesystem::current_path().string() + "/";
    }

    std::string StandardFileSystem::GetPreferencesPath(const std::string& organization,
                                                       const std::string& application) const
    {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / preferencesRootName_ / organization / application;
        std::error_code code;
        std::filesystem::create_directories(path, code);
        if (code)
        {
            throw PlatformException("FileSystem::GetPreferencesPath", code.message());
        }
        return path.string() + "/";
    }

    bool StandardFileSystem::TryLoadFile(const std::string& path, std::vector<std::uint8_t>& data) const
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input.good())
        {
            return false;
        }
        const std::streamsize size = input.tellg();
        input.seekg(0);

        std::vector<std::uint8_t> contents(static_cast<std::size_t>(size));
        if (size > 0 && !input.read(reinterpret_cast<char*>(contents.data()), size))
        {
            return false;
        }
        data = std::move(contents);
        return true;
    }

    void StandardFileSystem::CreateDirectory(const std::string& path)
    {
        std::error_code code;
        std::filesystem::create_directories(path, code);
        if (code)
        {
            throw PlatformException("FileSystem::CreateDirectory(" + path + ")", code.message());
        }
    }

} // namespace CNA::Platform::Common
