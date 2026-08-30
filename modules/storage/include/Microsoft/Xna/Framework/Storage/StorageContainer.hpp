// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/IO/FileAccess.hpp"
#include "System/IO/FileMode.hpp"
#include "System/IO/FileShare.hpp"
#include "System/IO/Stream.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Storage
{
    class StorageDevice;

        /**
         * @brief Provides a logical collection of files used for user-data persistence.
         *
         * File and directory paths are confined to this container. Absolute paths, parent
         * traversal outside the container and symlink escapes are rejected.
         */
    class StorageContainer : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Destroys the container. */
        CNAEXT ~StorageContainer() override;

        /** @brief Releases the resources used by this container. */
        void Dispose() override;

        /**
         * @brief Gets the display name that was passed to BeginOpenContainer.
         *
         * @return Container display name.
         */
        [[nodiscard]] const std::string& getDisplayNameProperty() const;

        /**
         * @brief Gets whether Dispose() has been called on this container.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the StorageDevice that owns this container.
         *
         * @return Reference to the owning StorageDevice.
         */
        [[nodiscard]] const StorageDevice& getStorageDeviceProperty() const;

        /** @brief Raised when Dispose() is called. */
        System::EventHandler<System::EventArgs> Disposing;

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        // ---- Directory operations ----

        /**
         * @brief Creates a subdirectory relative to this container's root.
         *
         * @param directory Relative directory path to create.
         */
        void CreateDirectory(const std::string& directory);

        /**
         * @brief Returns true if the relative directory exists.
         *
         * @param directory Relative directory path to check.
         * @return true if the directory exists; otherwise false.
         */
        [[nodiscard]] bool DirectoryExists(const std::string& directory) const;

        /**
         * @brief Deletes a relative directory (must be empty).
         *
         * @param directory Relative directory path to delete.
         */
        void DeleteDirectory(const std::string& directory);

        /**
         * @brief Returns the names of all subdirectories in this container's root.
         *
         * @return Vector of directory name strings.
         */
        [[nodiscard]] std::vector<std::string> GetDirectoryNames() const;

        /**
         * @brief Returns the names of subdirectories matching a glob pattern.
         *
         * @param searchPattern Glob pattern to match directory names against.
         * @return Vector of matching directory name strings.
         */
        [[nodiscard]] std::vector<std::string> GetDirectoryNames(
            const std::string& searchPattern) const;

        // ---- File operations ----

        /**
         * @brief Creates or truncates a file and returns a write stream.
         *
         * @param file Relative file path.
         * @return Unique pointer to a writable Stream.
         */
        [[nodiscard]] std::unique_ptr<System::IO::Stream> CreateFile(
            const std::string& file);

        /**
         * @brief Returns true if the relative file exists.
         *
         * @param file Relative file path.
         * @return true if the file exists; otherwise false.
         */
        [[nodiscard]] bool FileExists(const std::string& file) const;

        /**
         * @brief Deletes a relative file.
         *
         * @param file Relative file path to delete.
         */
        void DeleteFile(const std::string& file);

        /**
         * @brief Returns the names of all files in this container's root.
         *
         * @return Vector of file name strings.
         */
        [[nodiscard]] std::vector<std::string> GetFileNames() const;

        /**
         * @brief Returns the names of files matching a glob pattern.
         *
         * @param searchPattern Glob pattern to match file names against.
         * @return Vector of matching file name strings.
         */
        [[nodiscard]] std::vector<std::string> GetFileNames(
            const std::string& searchPattern) const;

        // ---- Stream open ----

        /**
         * @brief Opens a file stream with the specified file mode.
         *
         * @param file     Relative file path.
         * @param fileMode Mode for opening the file.
         * @return Unique pointer to the opened Stream.
         */
        [[nodiscard]] std::unique_ptr<System::IO::Stream> OpenFile(
            const std::string& file,
            System::IO::FileMode fileMode);

        /**
         * @brief Opens a file stream with the specified file mode and access.
         *
         * @param file       Relative file path.
         * @param fileMode   Mode for opening the file.
         * @param fileAccess Desired read/write access.
         * @return Unique pointer to the opened Stream.
         */
        [[nodiscard]] std::unique_ptr<System::IO::Stream> OpenFile(
            const std::string& file,
            System::IO::FileMode fileMode,
            System::IO::FileAccess fileAccess);

        /**
         * @brief Opens a file stream with the specified file mode, access, and share mode.
         *
         * @param file       Relative file path.
         * @param fileMode   Mode for opening the file.
         * @param fileAccess Desired read/write access.
         * @param fileShare  Sharing mode for concurrent access.
         * @return Unique pointer to the opened Stream.
         */
        [[nodiscard]] std::unique_ptr<System::IO::Stream> OpenFile(
            const std::string& file,
            System::IO::FileMode fileMode,
            System::IO::FileAccess fileAccess,
            System::IO::FileShare fileShare);

    private:
        friend class StorageDevice;

        StorageContainer(const StorageDevice& device,
                         const std::string& displayName,
                         const std::string& rootPath,
                         int playerIndex);   // -1 = all players

        const StorageDevice* device_;
        std::string displayName_;
        std::string storagePath_;
        bool isDisposed_ = false;

        [[nodiscard]] std::string ResolvePath(const std::string& relative) const;
    };

} // namespace Microsoft::Xna::Framework::Storage
