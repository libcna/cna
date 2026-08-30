// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Storage/StorageContainer.hpp"

#include <filesystem>
#include <stdexcept>

#include "CNA/Internal/PathContainment.hpp"
#include "System/IO/FileStream.hpp"

namespace
{
    // Minimal glob: '*' = any sequence, '?' = any single char
    bool GlobMatch(const std::string& pattern, const std::string& str)
    {
        std::size_t pi = 0, si = 0;
        std::size_t starPi = std::string::npos, starSi = 0;
        while (si < str.size())
        {
            if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == str[si]))
            {
                ++pi; ++si;
            }
            else if (pi < pattern.size() && pattern[pi] == '*')
            {
                starPi = pi++; starSi = si;
            }
            else if (starPi != std::string::npos)
            {
                pi = starPi + 1; si = ++starSi;
            }
            else return false;
        }
        while (pi < pattern.size() && pattern[pi] == '*') ++pi;
        return pi == pattern.size();
    }
}

namespace Microsoft::Xna::Framework::Storage
{
    namespace fs = std::filesystem;

    StorageContainer::StorageContainer(const StorageDevice& device,
                                       const std::string& displayName,
                                       const std::string& rootPath,
                                       int playerIndex)
        : device_(&device), displayName_(displayName)
    {
        if (displayName.empty())
            throw std::invalid_argument(
                "A title name has to be provided in parameter displayName.");

        std::string playerFolder = (playerIndex >= 0)
            ? "Player" + std::to_string(playerIndex + 1)
            : "AllPlayers";

        // Intentional security deviation from FNA: Path.Combine permits a rooted display name to
        // discard the storage root and permits parent traversal. A StorageContainer is an
        // authority boundary in CNA, so construction must remain below the device storage root.
        const auto contained = CNA::Internal::ResolveContainedPath(
            rootPath, displayName + "/" + playerFolder);
        if (!contained.ok)
        {
            throw std::invalid_argument(
                "The storage container name must resolve within the storage root.");
        }
        storagePath_ = contained.resolvedPath;

        if (!fs::exists(storagePath_))
            fs::create_directories(storagePath_);
    }

    StorageContainer::~StorageContainer()
    {
        if (!isDisposed_) Dispose();
    }

    void StorageContainer::Dispose()
    {
        if (isDisposed_) return;
        isDisposed_ = true;
        Disposing.Raise(this, System::EventArgs::Empty);
    }

    const std::string& StorageContainer::getDisplayNameProperty() const { return displayName_; }
    bool StorageContainer::getIsDisposedProperty() const { return isDisposed_; }
    const StorageDevice& StorageContainer::getStorageDeviceProperty() const { return *device_; }

    const std::string& StorageContainer::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Storage.StorageContainer";
        return name;
    }

    std::string StorageContainer::ResolvePath(const std::string& relative) const
    {
        // Intentional security deviation from FNA: its unchecked Path.Combine calls can escape
        // the logical container through absolute paths, parent traversal or an existing symlink.
        const auto contained = CNA::Internal::ResolveContainedPath(storagePath_, relative);
        if (!contained.ok)
        {
            throw std::invalid_argument(
                "The storage path must be relative and remain within the container.");
        }
        return contained.resolvedPath;
    }

    // ---- Directory ----

    void StorageContainer::CreateDirectory(const std::string& directory)
    {
        if (directory.empty())
            throw std::invalid_argument("Parameter directory must contain a value.");
        fs::path p = ResolvePath(directory);
        if (!fs::exists(p)) fs::create_directories(p);
    }

    bool StorageContainer::DirectoryExists(const std::string& directory) const
    {
        if (directory.empty())
            throw std::invalid_argument("Parameter directory must contain a value.");
        return fs::is_directory(ResolvePath(directory));
    }

    void StorageContainer::DeleteDirectory(const std::string& directory)
    {
        if (directory.empty())
            throw std::invalid_argument("Parameter directory must contain a value.");
        fs::remove(ResolvePath(directory));
    }

    std::vector<std::string> StorageContainer::GetDirectoryNames() const
    {
        std::vector<std::string> result;
        for (const auto& e : fs::directory_iterator(storagePath_))
            if (e.is_directory())
                result.push_back(e.path().filename().string());
        return result;
    }

    std::vector<std::string> StorageContainer::GetDirectoryNames(
        const std::string& searchPattern) const
    {
        if (searchPattern.empty())
            throw std::invalid_argument("Parameter searchPattern must contain a value.");
        std::vector<std::string> result;
        for (const auto& e : fs::directory_iterator(storagePath_))
        {
            if (!e.is_directory()) continue;
            // Simple glob: '*' matches anything, '?' matches one char
            const std::string name = e.path().filename().string();
            if (GlobMatch(searchPattern, name)) result.push_back(name);
        }
        return result;
    }

    // ---- File ----

    std::unique_ptr<System::IO::Stream> StorageContainer::CreateFile(
        const std::string& file)
    {
        if (file.empty())
            throw std::invalid_argument("Parameter file must contain a value.");
        return std::make_unique<System::IO::FileStream>(
            ResolvePath(file), System::IO::FileMode::Create);
    }

    bool StorageContainer::FileExists(const std::string& file) const
    {
        if (file.empty())
            throw std::invalid_argument("Parameter file must contain a value.");
        return fs::is_regular_file(ResolvePath(file));
    }

    void StorageContainer::DeleteFile(const std::string& file)
    {
        if (file.empty())
            throw std::invalid_argument("Parameter file must contain a value.");
        fs::remove(ResolvePath(file));
    }

    std::vector<std::string> StorageContainer::GetFileNames() const
    {
        std::vector<std::string> result;
        for (const auto& e : fs::directory_iterator(storagePath_))
            if (e.is_regular_file())
                result.push_back(e.path().filename().string());
        return result;
    }

    std::vector<std::string> StorageContainer::GetFileNames(
        const std::string& searchPattern) const
    {
        if (searchPattern.empty())
            throw std::invalid_argument("Parameter searchPattern must contain a value.");
        std::vector<std::string> result;
        for (const auto& e : fs::directory_iterator(storagePath_))
        {
            if (!e.is_regular_file()) continue;
            const std::string name = e.path().filename().string();
            if (GlobMatch(searchPattern, name)) result.push_back(name);
        }
        return result;
    }

    // ---- OpenFile ----

    std::unique_ptr<System::IO::Stream> StorageContainer::OpenFile(
        const std::string& file, System::IO::FileMode fileMode)
    {
        return OpenFile(file, fileMode,
                        System::IO::FileAccess::ReadWrite,
                        System::IO::FileShare::ReadWrite);
    }

    std::unique_ptr<System::IO::Stream> StorageContainer::OpenFile(
        const std::string& file, System::IO::FileMode fileMode,
        System::IO::FileAccess fileAccess)
    {
        return OpenFile(file, fileMode, fileAccess, System::IO::FileShare::ReadWrite);
    }

    std::unique_ptr<System::IO::Stream> StorageContainer::OpenFile(
        const std::string& file, System::IO::FileMode fileMode,
        System::IO::FileAccess fileAccess, System::IO::FileShare /*fileShare*/)
    {
        if (file.empty())
            throw std::invalid_argument("Parameter file must contain a value.");
        return std::make_unique<System::IO::FileStream>(
            ResolvePath(file), fileMode, fileAccess);
    }

} // namespace Microsoft::Xna::Framework::Storage
