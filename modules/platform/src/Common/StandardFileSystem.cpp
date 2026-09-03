// SPDX-License-Identifier: MS-PL

#include "StandardFileSystem.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <optional>
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

        std::optional<std::filesystem::path> ResolvePathIgnoringCase(
            const std::filesystem::path& requested)
        {
            namespace fs = std::filesystem;

            std::error_code code;
            if (fs::exists(requested, code) && !code)
            {
                return requested;
            }

            fs::path resolved = requested.is_absolute() ? requested.root_path() : fs::path{};
            for (const fs::path& component : requested.relative_path())
            {
                const fs::path exact = resolved / component;
                code.clear();
                if (fs::exists(exact, code) && !code)
                {
                    resolved = exact;
                    continue;
                }

                const fs::path parent = resolved.empty() ? fs::path(".") : resolved;
                code.clear();
                fs::directory_iterator entry(parent, code);
                if (code)
                {
                    return std::nullopt;
                }

                std::string wanted = component.string();
                std::transform(wanted.begin(), wanted.end(), wanted.begin(),
                    [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

                fs::path match;
                const fs::directory_iterator end;
                for (; entry != end; entry.increment(code))
                {
                    if (code)
                    {
                        return std::nullopt;
                    }
                    std::string candidate = entry->path().filename().string();
                    std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                        [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (candidate == wanted)
                    {
                        if (!match.empty())
                        {
                            return std::nullopt;
                        }
                        match = entry->path().filename();
                    }
                }
                if (match.empty())
                {
                    return std::nullopt;
                }
                resolved /= match;
            }
            return resolved;
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

    bool StandardFileSystem::TryLoadFileIgnoringCase(
        const std::string& path, std::vector<std::uint8_t>& data) const
    {
        return TryLoadStandardFileIgnoringCase(path, data);
    }

    bool TryLoadStandardFileIgnoringCase(
        const std::string& path, std::vector<std::uint8_t>& data)
    {
        const std::optional<std::filesystem::path> resolved =
            ResolvePathIgnoringCase(std::filesystem::path(path));
        if (!resolved.has_value())
        {
            return false;
        }

        std::ifstream input(*resolved, std::ios::binary | std::ios::ate);
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
