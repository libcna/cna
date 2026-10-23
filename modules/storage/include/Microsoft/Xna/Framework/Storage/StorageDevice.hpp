// SPDX-License-Identifier: MS-PL
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageContainer.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IAsyncResult.hpp"

namespace Microsoft::Xna::Framework::Storage
{
    /**
     * @brief Exposes a storage device for user-data persistence.
     *
     * Uses the XNA 4.0 fake-async pattern: BeginXxx completes synchronously
     * and the paired EndXxx extracts the result.
     */
    class StorageDevice final
    {
    public:
        /** @brief Destroys the StorageDevice. */
        CNAEXT ~StorageDevice() = default;

        // ---- Properties ----

        /**
         * @brief Gets the available free space on the underlying filesystem in bytes.
         *
         * @return Free space in bytes.
         */
        [[nodiscard]] long long getFreeSpaceProperty() const;

        /**
         * @brief Gets whether the storage location is currently accessible.
         *
         * @return true if connected; otherwise false.
         */
        [[nodiscard]] bool getIsConnectedProperty() const;

        /**
         * @brief Gets the total size of the underlying filesystem in bytes.
         *
         * @return Total space in bytes.
         */
        [[nodiscard]] long long getTotalSpaceProperty() const;

        // ---- Events ----

        /** @brief Raised when the set of available storage devices changes. */
        static System::EventHandler<System::EventArgs> DeviceChanged;

        // ---- OpenContainer ----

        /**
         * @brief Begins opening a StorageContainer for the given display name.
         *
         * Completes synchronously; pass the result once to EndOpenContainer on this
         * device. Display-name validation occurs in EndOpenContainer, not here.
         * This device must remain alive through EndOpenContainer and container use.
         *
         * @param displayName Name of the container to open.
         * @param callback    Optional callback invoked when the operation completes.
         * @param state       User-defined state object passed to the callback.
         * @return IAsyncResult representing the pending operation.
         */
        [[nodiscard]] std::unique_ptr<System::IAsyncResult> BeginOpenContainer(
            const std::string& displayName,
            std::function<void(System::IAsyncResult*)> callback,
            void* state);

        /**
         * @brief Returns the StorageContainer opened by BeginOpenContainer.
         *
         * @param result IAsyncResult returned by BeginOpenContainer.
         * @return Unique pointer to the opened StorageContainer.
         * @throws System::ArgumentNullException If result is null, belongs to another
         * operation kind, or the display name is empty.
         * @throws System::InvalidOperationException If this result has already been ended.
         * @throws System::ArgumentException If another device started the operation.
         * @throws System::IO::IOException If a file occupies any directory of the container's
         * path, including the storage root itself.
         * @throws System::UnauthorizedAccessException If a directory of that path cannot be
         * created for lack of permission.
         * A matching result is consumed even when owner/name validation or opening fails.
         * The container's directories, including the storage root, are created here and not by
         * BeginOpenContainer.
         */
        [[nodiscard]] std::unique_ptr<StorageContainer> EndOpenContainer(
            System::IAsyncResult* result);

        /**
         * @brief Removes the container directory tree for the given title name entirely.
         *
         * @param titleName Name of the container to delete.
         */
        void DeleteContainer(const std::string& titleName);

        // ---- ShowSelector ----

        /**
         * @brief Begins showing the device-selection UI. Completes synchronously.
         *
         * @param callback Optional callback invoked on completion.
         * @param state    User-defined state object.
         * @return IAsyncResult representing the pending operation.
         */
        static std::unique_ptr<System::IAsyncResult> BeginShowSelector(
            std::function<void(System::IAsyncResult*)> callback,
            void* state);

        /**
         * @brief Begins showing the device-selection UI for a specific player.
         *
         * @param player   Player One through Four, or value 255 for shared storage.
         * @param callback Optional callback invoked on completion.
         * @param state    User-defined state object.
         * @return IAsyncResult representing the pending operation.
         * @throws System::ArgumentOutOfRangeException If player is not a supported value.
         */
        static std::unique_ptr<System::IAsyncResult> BeginShowSelector(
            PlayerIndex player,
            std::function<void(System::IAsyncResult*)> callback,
            void* state);

        /**
         * @brief Begins showing the device-selection UI with space requirements.
         *
         * @param sizeInBytes    Required free space in bytes.
         * @param directoryCount Required number of directories.
         * @param callback       Optional callback invoked on completion.
         * @param state          User-defined state object.
         * @return IAsyncResult representing the pending operation.
         * @throws System::ArgumentOutOfRangeException If sizeInBytes is negative.
         * directoryCount is accepted without validation, matching XNA on desktop.
         */
        static std::unique_ptr<System::IAsyncResult> BeginShowSelector(
            int sizeInBytes, int directoryCount,
            std::function<void(System::IAsyncResult*)> callback,
            void* state);

        /**
         * @brief Begins showing the device-selection UI for a player with space requirements.
         *
         * @param player         Player One through Four, or value 255 for shared storage.
         * @param sizeInBytes    Required free space in bytes.
         * @param directoryCount Required number of directories.
         * @param callback       Optional callback invoked on completion.
         * @param state          User-defined state object.
         * @return IAsyncResult representing the pending operation.
         * @throws System::ArgumentOutOfRangeException If player is invalid or sizeInBytes
         * is negative. Player validation precedes size validation; directoryCount is not checked.
         */
        static std::unique_ptr<System::IAsyncResult> BeginShowSelector(
            PlayerIndex player,
            int sizeInBytes, int directoryCount,
            std::function<void(System::IAsyncResult*)> callback,
            void* state);

        /**
         * @brief Returns the StorageDevice selected by BeginShowSelector.
         *
         * @param result IAsyncResult returned by BeginShowSelector.
         * @return Unique pointer to the selected StorageDevice.
         * @throws System::ArgumentNullException If result is null or has another operation kind.
         * @throws System::InvalidOperationException If this result has already been ended.
         */
        static std::unique_ptr<StorageDevice> EndShowSelector(System::IAsyncResult* result);

        // ---- CNA extension ----

        /**
         * @brief Sets the application name used to build the storage root directory.
         *
         * Call once at startup (e.g. in Game constructor) before accessing storage.
         * This only configures the identity; it does not resolve or create a directory.
         * CNA and Sharp Runtime isolated storage resolve the same per-application root on
         * first access. An unavailable root is reported by that access, not by this setter.
         * Changing the name concurrently with storage access is not supported.
         *
         * @param appName Application name.
         */
        CNAEXT static void SetAppNameEXT(const std::string& appName);

        /**
         * @brief Returns the storage root directory path for the current application.
         *
         * @return Absolute path to the storage root.
         */
        CNAEXT static std::string GetStorageRootEXT();

    private:
        explicit StorageDevice(std::optional<PlayerIndex> player);

        std::optional<PlayerIndex> devicePlayer_;

        static std::string storageRoot_;
        static std::string appName_;
        static bool storageRootInitialized_;

        static std::string ResolveStorageRoot();
        static const std::string& EnsureStorageRoot();
    };

} // namespace Microsoft::Xna::Framework::Storage
