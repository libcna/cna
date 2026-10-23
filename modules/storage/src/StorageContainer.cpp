// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Storage/StorageContainer.hpp"

#include <filesystem>
#include <stdexcept>

#include "CNA/Internal/PathContainment.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "System/IO/FileStream.hpp"
#include "System/IO/File.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/IO/DirectoryNotFoundException.hpp"
#include "System/IO/IOException.hpp"
#include "System/UnauthorizedAccessException.hpp"

namespace
{
    // Disposal is checked before argument validation and filesystem access.
    void VerifyNotDisposed(const Microsoft::Xna::Framework::Storage::StorageContainer& container)
    {
        if (container.getIsDisposedProperty())
            throw System::ObjectDisposedException("StorageContainer");
    }

    [[noreturn]] void ThrowDirectoryDeletionError(const std::filesystem::path& path,
                                                  const std::error_code& error)
    {
        const std::string message = "Unable to delete directory '" +
            CNA::Internal::PathToUtf8(path) + "': " + error.message();
        const auto cause = std::make_exception_ptr(
            std::filesystem::filesystem_error("DeleteDirectory", path, error));
        if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory)
            throw System::IO::DirectoryNotFoundException(message, cause);
        if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted)
            throw System::UnauthorizedAccessException(message, cause);
        throw System::IO::IOException(message, cause);
    }

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
            throw System::ArgumentNullException("You must pass in a non-null title name.");

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
            throw System::ArgumentException(
                "The storage container name must resolve within the storage root.");
        }
        storagePath_ = contained.resolvedPath;

        // Widened like every other member does. Left narrow, the constructor created a
        // mojibake-named directory while ResolveNativePath, GetFileNames and FileExists all
        // operated on the correct one -- so the container was permanently empty and a junk
        // directory accumulated beside it.
        const std::filesystem::path nativeRoot = CNA::Internal::PathFromUtf8(storagePath_);
        if (!fs::exists(nativeRoot))
            fs::create_directories(nativeRoot);
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

    const std::string& StorageContainer::getDisplayNameProperty() const
    {
        VerifyNotDisposed(*this);
        return displayName_;
    }
    bool StorageContainer::getIsDisposedProperty() const { return isDisposed_; }
    const StorageDevice& StorageContainer::getStorageDeviceProperty() const
    {
        VerifyNotDisposed(*this);
        return *device_;
    }

    const std::string& StorageContainer::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Storage.StorageContainer";
        return name;
    }

    std::filesystem::path StorageContainer::ResolveNativePath(const std::string& relative) const
    {
        // Intentional security deviation from FNA: its unchecked Path.Combine calls can escape
        // the logical container through absolute paths, parent traversal or an existing symlink.
        //
        // The native form exists so the filesystem calls below never narrow: ResolvePath's string
        // was being handed straight back to fs::exists / fs::remove / directory_iterator, whose
        // narrow overloads re-read it as ANSI code page bytes on Windows.
        const auto contained = CNA::Internal::ResolveContainedNativePathFromBase(
            CNA::Internal::PathFromUtf8(storagePath_), CNA::Internal::PathFromUtf8(storagePath_),
            relative);
        if (!contained.ok)
        {
            throw System::ArgumentException(
                "The storage path must be relative and remain within the container.");
        }
        return contained.resolvedPath;
    }

    std::string StorageContainer::ResolvePath(const std::string& relative) const
    {
        return CNA::Internal::PathToGenericUtf8(ResolveNativePath(relative));
    }

    // ---- Directory ----

    void StorageContainer::CreateDirectory(const std::string& directory)
    {
        VerifyNotDisposed(*this);
        if (directory.empty())
            throw System::ArgumentNullException("directory");
        fs::path p = ResolveNativePath(directory);
        if (!fs::exists(p)) fs::create_directories(p);
    }

    bool StorageContainer::DirectoryExists(const std::string& directory) const
    {
        VerifyNotDisposed(*this);
        if (directory.empty())
            throw System::ArgumentNullException("directory");
        return fs::is_directory(ResolveNativePath(directory));
    }

    void StorageContainer::DeleteDirectory(const std::string& directory)
    {
        VerifyNotDisposed(*this);
        if (directory.empty())
            throw System::ArgumentNullException("directory");
        const fs::path path = ResolveNativePath(directory);
        std::error_code error;
        const auto status = fs::status(path, error);
        if (error) ThrowDirectoryDeletionError(path, error);
        if (!fs::exists(status))
            ThrowDirectoryDeletionError(path, std::make_error_code(std::errc::no_such_file_or_directory));
        // filesystem::remove also deletes regular files; Directory.Delete must not.
        if (!fs::is_directory(status))
            throw System::IO::IOException("The path names a file, not a directory.");
        if (!fs::remove(path, error))
        {
            if (!error) error = std::make_error_code(std::errc::no_such_file_or_directory);
            ThrowDirectoryDeletionError(path, error);
        }
    }

    std::vector<std::string> StorageContainer::GetDirectoryNames() const
    {
        VerifyNotDisposed(*this);
        std::vector<std::string> result;
        for (const auto& e : fs::directory_iterator(CNA::Internal::PathFromUtf8(storagePath_)))
            if (e.is_directory())
                result.push_back(CNA::Internal::PathToUtf8(e.path().filename()));
        return result;
    }

    std::vector<std::string> StorageContainer::GetDirectoryNames(
        const std::string& searchPattern) const
    {
        VerifyNotDisposed(*this);
        if (searchPattern.empty())
            return {};
        std::vector<std::string> result;
        for (const auto& e : fs::directory_iterator(CNA::Internal::PathFromUtf8(storagePath_)))
        {
            if (!e.is_directory()) continue;
            // Simple glob: '*' matches anything, '?' matches one char
            const std::string name = CNA::Internal::PathToUtf8(e.path().filename());
            if (GlobMatch(searchPattern, name)) result.push_back(name);
        }
        return result;
    }

    // ---- File ----

    std::unique_ptr<System::IO::Stream> StorageContainer::CreateFile(
        const std::string& file)
    {
        VerifyNotDisposed(*this);
        if (file.empty())
            throw System::ArgumentNullException("file");
        return std::make_unique<System::IO::FileStream>(
            ResolvePath(file), System::IO::FileMode::Create,
            System::IO::FileAccess::ReadWrite, System::IO::FileShare::None);
    }

    bool StorageContainer::FileExists(const std::string& file) const
    {
        VerifyNotDisposed(*this);
        if (file.empty())
            throw System::ArgumentNullException("file");
        return fs::is_regular_file(ResolveNativePath(file));
    }

    void StorageContainer::DeleteFile(const std::string& file)
    {
        VerifyNotDisposed(*this);
        if (file.empty())
            throw System::ArgumentNullException("file");
        System::IO::File::Delete(ResolvePath(file));
    }

    std::vector<std::string> StorageContainer::GetFileNames() const
    {
        VerifyNotDisposed(*this);
        std::vector<std::string> result;
        for (const auto& e : fs::directory_iterator(CNA::Internal::PathFromUtf8(storagePath_)))
            if (e.is_regular_file())
                result.push_back(CNA::Internal::PathToUtf8(e.path().filename()));
        return result;
    }

    std::vector<std::string> StorageContainer::GetFileNames(
        const std::string& searchPattern) const
    {
        VerifyNotDisposed(*this);
        if (searchPattern.empty())
            return {};
        std::vector<std::string> result;
        for (const auto& e : fs::directory_iterator(CNA::Internal::PathFromUtf8(storagePath_)))
        {
            if (!e.is_regular_file()) continue;
            const std::string name = CNA::Internal::PathToUtf8(e.path().filename());
            if (GlobMatch(searchPattern, name)) result.push_back(name);
        }
        return result;
    }

    // ---- OpenFile ----

    std::unique_ptr<System::IO::Stream> StorageContainer::OpenFile(
        const std::string& file, System::IO::FileMode fileMode)
    {
        return OpenFile(file, fileMode,
                        fileMode == System::IO::FileMode::Append
                            ? System::IO::FileAccess::Write : System::IO::FileAccess::ReadWrite,
                        System::IO::FileShare::None);
    }

    std::unique_ptr<System::IO::Stream> StorageContainer::OpenFile(
        const std::string& file, System::IO::FileMode fileMode,
        System::IO::FileAccess fileAccess)
    {
        return OpenFile(file, fileMode, fileAccess, System::IO::FileShare::None);
    }

    std::unique_ptr<System::IO::Stream> StorageContainer::OpenFile(
        const std::string& file, System::IO::FileMode fileMode,
        System::IO::FileAccess fileAccess, System::IO::FileShare fileShare)
    {
        VerifyNotDisposed(*this);
        if (file.empty())
            throw System::ArgumentNullException("file");
        return std::make_unique<System::IO::FileStream>(
            ResolvePath(file), fileMode, fileAccess, fileShare);
    }

} // namespace Microsoft::Xna::Framework::Storage
