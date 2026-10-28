// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Storage/StorageContainer.hpp"

#include <filesystem>
#include <stdexcept>
#include <vector>

#include "CNA/Internal/PathContainment.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "System/IO/FileStream.hpp"
#include "System/IO/File.hpp"
#include "System/IO/Directory.hpp"
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

    [[noreturn]] void ThrowDirectoryDeletionError(const char* operation,
                                                  const std::filesystem::path& path,
                                                  const std::error_code& error)
    {
        const std::string message = "Unable to delete directory '" +
            CNA::Internal::PathToUtf8(path) + "': " + error.message();
        const auto cause = std::make_exception_ptr(
            std::filesystem::filesystem_error(operation, path, error));
        if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory)
            throw System::IO::DirectoryNotFoundException(message, cause);
        if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted)
            throw System::UnauthorizedAccessException(message, cause);
        throw System::IO::IOException(message, cause);
    }

    // Directory.Delete(recursive) reports the first entry it cannot delete. filesystem::remove_all
    // does not say which error it keeps: Apple's libc++ goes on past an undeletable file and
    // reports its parent as not empty. Links are removed, never followed.
    void RemoveTree(const std::filesystem::path& path, std::error_code& error)
    {
        namespace fs = std::filesystem;
        const auto status = fs::symlink_status(path, error);
        if (error) return;
        if (fs::is_directory(status))
        {
            std::vector<fs::path> children;
            for (fs::directory_iterator entry(path, error), end; !error && entry != end;
                 entry.increment(error))
                children.push_back(entry->path());
            if (error) return;
            for (const auto& child : children)
            {
                RemoveTree(child, error);
                if (error) return;
            }
        }
        fs::remove(path, error);
    }

    // weakly_canonical cannot follow a symlink whose target does not exist yet, so the containment
    // check accepts it -- and a file opened for writing through it is created at the target. Follow
    // such a final link to where the filesystem would create it. A loop or unreadable link is left
    // to fail the operation itself.
    bool DanglingLinkStaysBelow(const std::filesystem::path& root, std::filesystem::path path)
    {
        namespace fs = std::filesystem;
        std::error_code error;
        if (!fs::is_symlink(fs::symlink_status(path, error))) return true;
        for (int hop = 0; hop < 40; ++hop)
        {
            const fs::path target = fs::read_symlink(path, error);
            if (error) return true;
            path = target.is_absolute() ? target : path.parent_path() / target;
            // A missing target reports not_found with an error set; that is the case to check.
            if (!fs::is_symlink(fs::symlink_status(path, error)))
            {
                // Canonical before any lexical step, so ".." after a linked directory is the
                // kernel's ".." and not the text's.
                const fs::path resolved = fs::weakly_canonical(path, error);
                return error || CNA::Internal::ValidateContainedNativePath(root, resolved).ok;
            }
        }
        return true;
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

        // Intentional security deviation from FNA: Path.Combine permits a rooted display name to
        // discard the storage root and permits parent traversal. A StorageContainer is an
        // authority boundary in CNA, so construction must remain below the device storage root.
        storagePath_ = CNA::Internal::PathToGenericUtf8(ResolveBelow(
            CNA::Internal::PathFromUtf8(rootPath), displayName + "/" + PlayerFolder(playerIndex),
            "The storage container name must resolve within the storage root."));

        // XNA's FinishCreation: DirectoryInfo.Create builds the whole chain, including the storage
        // root, and its System.IO failure escapes. A file occupying any level is an IOException
        // and is kept, not opened as the container. Directory takes the UTF-8 path and widens it
        // itself, so the directory created is the one every other member resolves.
        System::IO::Directory::CreateDirectory(storagePath_);
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
        return ResolveBelow(CNA::Internal::PathFromUtf8(storagePath_), relative,
                            "The storage path must be relative and remain within the container.");
    }

    std::filesystem::path StorageContainer::ResolveBelow(const std::filesystem::path& root,
                                                         const std::string& relative,
                                                         const char* escapeMessage)
    {
        const auto contained = CNA::Internal::ResolveContainedUtf8Path(root, relative);
        if (contained.ok)
        {
            if (!DanglingLinkStaysBelow(root, contained.resolvedPath))
                throw System::ArgumentException(escapeMessage);
            return contained.resolvedPath;
        }
        // A component the filesystem cannot inspect (no search permission, a symlink loop, an
        // overlong name) is not an escape: the operation cannot traverse it either, so it fails
        // with the filesystem's own error, as XNA's does (StorageResidualPathsOracle). A lexical
        // or symlink escape is still refused.
        const auto lexical = CNA::Internal::ResolveContainedUtf8Path(root, relative, false);
        std::error_code error;
        if (lexical.ok) (void)fs::weakly_canonical(lexical.resolvedPath, error);
        if (!lexical.ok || !error) throw System::ArgumentException(escapeMessage);
        return lexical.resolvedPath;
    }

    std::string StorageContainer::PlayerFolder(int playerIndex)
    {
        return playerIndex >= 0 ? "Player" + std::to_string(playerIndex + 1) : "AllPlayers";
    }

    void StorageContainer::DeleteContainer(const std::string& rootPath,
                                           const std::string& displayName, int playerIndex)
    {
        // XNA removes this device's player folder of the container, and only when it is a
        // directory: another player's folder, the named folder and a file of that name stay, and
        // nothing is created (StorageResidualPathsOracle). Path.Combine(root, "") is the root.
        const std::string relative = displayName.empty()
            ? PlayerFolder(playerIndex) : displayName + "/" + PlayerFolder(playerIndex);
        const fs::path path = ResolveBelow(CNA::Internal::PathFromUtf8(rootPath), relative,
            "titleName must be a simple name within the storage root, not an absolute path "
            "or one that escapes it.");
        std::error_code error;
        if (!fs::is_directory(path, error)) return;
        RemoveTree(path, error);
        if (error && error != std::errc::no_such_file_or_directory)
            ThrowDirectoryDeletionError("DeleteContainer", path, error);
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
        System::IO::Directory::CreateDirectory(ResolvePath(directory));
    }

    bool StorageContainer::DirectoryExists(const std::string& directory) const
    {
        VerifyNotDisposed(*this);
        if (directory.empty())
            throw System::ArgumentNullException("directory");
        // Directory.Exists answers false for any failure.
        std::error_code error;
        return fs::is_directory(ResolveNativePath(directory), error) && !error;
    }

    void StorageContainer::DeleteDirectory(const std::string& directory)
    {
        VerifyNotDisposed(*this);
        if (directory.empty())
            throw System::ArgumentNullException("directory");
        const fs::path path = ResolveNativePath(directory);
        std::error_code error;
        const auto status = fs::status(path, error);
        if (error) ThrowDirectoryDeletionError("DeleteDirectory", path, error);
        if (!fs::exists(status))
            ThrowDirectoryDeletionError("DeleteDirectory", path,
                                        std::make_error_code(std::errc::no_such_file_or_directory));
        // filesystem::remove also deletes regular files; Directory.Delete must not.
        if (!fs::is_directory(status))
            throw System::IO::IOException("The path names a file, not a directory.");
        if (!fs::remove(path, error))
        {
            if (!error) error = std::make_error_code(std::errc::no_such_file_or_directory);
            ThrowDirectoryDeletionError("DeleteDirectory", path, error);
        }
    }

    std::vector<std::string> StorageContainer::GetDirectoryNames() const
    {
        VerifyNotDisposed(*this);
        std::vector<std::string> result;
        for (const auto& path : System::IO::Directory::GetDirectories(storagePath_))
            result.push_back(CNA::Internal::PathToUtf8(
                CNA::Internal::PathFromUtf8(path).filename()));
        return result;
    }

    std::vector<std::string> StorageContainer::GetDirectoryNames(
        const std::string& searchPattern) const
    {
        VerifyNotDisposed(*this);
        if (searchPattern.empty())
            return {};
        auto result = GetDirectoryNames();
        std::erase_if(result, [&](const std::string& name) { return !GlobMatch(searchPattern, name); });
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
        // File.Exists answers false for any failure.
        std::error_code error;
        return fs::is_regular_file(ResolveNativePath(file), error) && !error;
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
        for (const auto& path : System::IO::Directory::GetFiles(storagePath_))
            result.push_back(CNA::Internal::PathToUtf8(
                CNA::Internal::PathFromUtf8(path).filename()));
        return result;
    }

    std::vector<std::string> StorageContainer::GetFileNames(
        const std::string& searchPattern) const
    {
        VerifyNotDisposed(*this);
        if (searchPattern.empty())
            return {};
        auto result = GetFileNames();
        std::erase_if(result, [&](const std::string& name) { return !GlobMatch(searchPattern, name); });
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
