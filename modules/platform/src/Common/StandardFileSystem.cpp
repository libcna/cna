// SPDX-License-Identifier: MS-PL

#include "StandardFileSystem.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <utility>

namespace CNA::Platform::Common {

    namespace {

        std::string WithTrailingSeparator(std::string path)
        {
            if (!path.empty() && path.back() != '/' && path.back() != '\\') path.push_back('/');
            return path;
        }

        std::string ReadXdgUserFolder(const char* variable)
        {
            const char* home = std::getenv("HOME");
            if (!home || *home == '\0') return {};

            const char* configuredRoot = std::getenv("XDG_CONFIG_HOME");
            const std::filesystem::path config = configuredRoot && *configuredRoot
                ? std::filesystem::path(configuredRoot) / "user-dirs.dirs"
                : std::filesystem::path(home) / ".config" / "user-dirs.dirs";
            std::ifstream input(config);
            const std::string prefix = std::string(variable) + "=\"";
            std::string line;
            while (std::getline(input, line))
            {
                if (!line.starts_with(prefix) || line.size() <= prefix.size()
                    || line.back() != '"')
                    continue;

                std::string value = line.substr(prefix.size(), line.size() - prefix.size() - 1);
                constexpr const char* homeToken = "$HOME";
                if (value.starts_with(homeToken)) value.replace(0, 5, home);
                return WithTrailingSeparator(std::move(value));
            }
            return {};
        }

    }

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

    std::string StandardFileSystem::GetUserFolder(const UserFolder folder) const
    {
        switch (folder)
        {
            case UserFolder::Music: return ReadXdgUserFolder("XDG_MUSIC_DIR");
            case UserFolder::Pictures: return ReadXdgUserFolder("XDG_PICTURES_DIR");
        }
        return {};
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
