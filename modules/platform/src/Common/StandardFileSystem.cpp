// SPDX-License-Identifier: MS-PL

#include "StandardFileSystem.hpp"

#include "CNA/Internal/CaseInsensitivePath.hpp"
#include "CNA/Internal/PathUtf8.hpp"
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

        // The case-insensitive component walk used to be duplicated here, byte for byte, from
        // CNA/Internal/CaseInsensitivePath.hpp -- including its defect of narrowing every entry it
        // enumerated through the ANSI code page. It now delegates. The failure shapes still agree:
        // where this returned nullopt for a missing or ambiguous component, the shared walker
        // returns the requested path unchanged and the open below then fails, which is the same
        // "false" to the caller.
        std::filesystem::path ResolvePathIgnoringCase(const std::filesystem::path& requested)
        {
            return CNA::Internal::ResolveExistingNativePath(requested);
        }

    }

    StandardFileSystem::StandardFileSystem(std::string preferencesRootName)
        : preferencesRootName_(std::move(preferencesRootName))
    {
    }

    std::string StandardFileSystem::GetBasePath() const
    {
        // The single most consequential conversion in the platform layer: this is where
        // TitleLocation::Path() comes from, so a game installed under a directory the ANSI code
        // page cannot spell used to lose its entire content root before loading anything.
        return CNA::Internal::PathToGenericUtf8(std::filesystem::current_path()) + "/";
    }

    std::string StandardFileSystem::GetPreferencesPath(const std::string& organization,
                                                       const std::string& application) const
    {
        // organization and application are UTF-8 and may legitimately be non-ASCII, so they are
        // widened rather than appended as narrow strings; temp_directory_path() itself contains
        // the user's name, which is the other half of the same problem.
        const std::filesystem::path path = std::filesystem::temp_directory_path()
            / preferencesRootName_
            / CNA::Internal::PathFromUtf8(organization)
            / CNA::Internal::PathFromUtf8(application);
        std::error_code code;
        std::filesystem::create_directories(path, code);
        if (code)
        {
            throw PlatformException("FileSystem::GetPreferencesPath", code.message());
        }
        return CNA::Internal::PathToGenericUtf8(path) + "/";
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
        // The platform's primary asset-read sink. The narrow ifstream overload took the UTF-8 path
        // the interface promises and read it as ANSI code page bytes, so an asset under a
        // non-ASCII directory reported itself as "not found".
        const std::optional<std::filesystem::path> native = CNA::Internal::TryPathFromUtf8(path);
        if (!native)
        {
            return false;
        }

        std::ifstream input(*native, std::ios::binary | std::ios::ate);
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
        const std::optional<std::filesystem::path> requested = CNA::Internal::TryPathFromUtf8(path);
        if (!requested)
        {
            return false;
        }
        const std::filesystem::path resolved = ResolvePathIgnoringCase(*requested);

        std::ifstream input(resolved, std::ios::binary | std::ios::ate);
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
        const std::optional<std::filesystem::path> native = CNA::Internal::TryPathFromUtf8(path);
        if (!native)
        {
            throw PlatformException("FileSystem::CreateDirectory(" + path + ")",
                                    "the path is not valid UTF-8");
        }

        std::error_code code;
        std::filesystem::create_directories(*native, code);
        if (code)
        {
            throw PlatformException("FileSystem::CreateDirectory(" + path + ")", code.message());
        }
    }

} // namespace CNA::Platform::Common
