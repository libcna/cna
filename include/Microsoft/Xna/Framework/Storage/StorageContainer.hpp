// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include <memory>
#include <string>
#include <vector>

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

    /// Provides a logical collection of files used for user-data storage.
    class StorageContainer : public System::Object, public System::IDisposable
    {
    public:
        ~StorageContainer() override;

        void Dispose() override;

        /// The name passed to BeginOpenContainer.
        [[nodiscard]] const std::string& getDisplayNameProperty() const;

        /// Whether Dispose() has been called.
        [[nodiscard]] bool getIsDisposedProperty() const;

        /// The device that owns this container.
        [[nodiscard]] const StorageDevice& getStorageDeviceProperty() const;

        /// Fired when Dispose() is called.
        System::EventHandler<System::EventArgs> Disposing;

        [[nodiscard]] const std::string& GetTypeName() const override;

        // ---- Directory operations ----

        /// Creates a subdirectory relative to this container's root.
        void CreateDirectory(const std::string& directory);

        /// Returns true if the relative directory exists.
        [[nodiscard]] bool DirectoryExists(const std::string& directory) const;

        /// Deletes a relative directory (must be empty).
        void DeleteDirectory(const std::string& directory);

        /// Returns names of all subdirectories in this container's root.
        [[nodiscard]] std::vector<std::string> GetDirectoryNames() const;

        /// Returns names of subdirectories matching a glob pattern.
        [[nodiscard]] std::vector<std::string> GetDirectoryNames(
            const std::string& searchPattern) const;

        // ---- File operations ----

        /// Creates or truncates a file and returns a write stream.
        [[nodiscard]] std::unique_ptr<System::IO::Stream> CreateFile(
            const std::string& file);

        /// Returns true if the relative file exists.
        [[nodiscard]] bool FileExists(const std::string& file) const;

        /// Deletes a relative file.
        void DeleteFile(const std::string& file);

        /// Returns names of all files in this container's root.
        [[nodiscard]] std::vector<std::string> GetFileNames() const;

        /// Returns names of files matching a glob pattern.
        [[nodiscard]] std::vector<std::string> GetFileNames(
            const std::string& searchPattern) const;

        // ---- Stream open ----

        [[nodiscard]] std::unique_ptr<System::IO::Stream> OpenFile(
            const std::string& file,
            System::IO::FileMode fileMode);

        [[nodiscard]] std::unique_ptr<System::IO::Stream> OpenFile(
            const std::string& file,
            System::IO::FileMode fileMode,
            System::IO::FileAccess fileAccess);

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
