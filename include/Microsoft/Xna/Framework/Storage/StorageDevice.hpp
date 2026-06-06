// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
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
    /// Exposes a storage device for user data persistence.
    ///
    /// Uses the XNA 4.0 fake-async pattern: BeginXxx completes synchronously
    /// and the paired EndXxx extracts the result.
    class StorageDevice final
    {
    public:
        ~StorageDevice() = default;

        // ---- Properties ----

        /// Available free space on the underlying filesystem (bytes).
        [[nodiscard]] long long getFreeSpaceProperty() const;

        /// True if the storage location is accessible.
        [[nodiscard]] bool getIsConnectedProperty() const;

        /// Total size of the underlying filesystem (bytes).
        [[nodiscard]] long long getTotalSpaceProperty() const;

        // ---- Events ----

        static System::EventHandler<System::EventArgs> DeviceChanged;

        // ---- OpenContainer ----

        /// Begins opening a StorageContainer. Completes synchronously.
        [[nodiscard]] std::unique_ptr<System::IAsyncResult> BeginOpenContainer(
            const std::string& displayName,
            std::function<void(System::IAsyncResult*)> callback,
            void* state);

        /// Returns the StorageContainer opened by BeginOpenContainer.
        [[nodiscard]] std::unique_ptr<StorageContainer> EndOpenContainer(
            System::IAsyncResult* result);

        /// Removes the container directory tree entirely.
        void DeleteContainer(const std::string& titleName);

        // ---- ShowSelector ----

        static std::unique_ptr<System::IAsyncResult> BeginShowSelector(
            std::function<void(System::IAsyncResult*)> callback,
            void* state);

        static std::unique_ptr<System::IAsyncResult> BeginShowSelector(
            PlayerIndex player,
            std::function<void(System::IAsyncResult*)> callback,
            void* state);

        static std::unique_ptr<System::IAsyncResult> BeginShowSelector(
            int sizeInBytes, int directoryCount,
            std::function<void(System::IAsyncResult*)> callback,
            void* state);

        static std::unique_ptr<System::IAsyncResult> BeginShowSelector(
            PlayerIndex player,
            int sizeInBytes, int directoryCount,
            std::function<void(System::IAsyncResult*)> callback,
            void* state);

        static std::unique_ptr<StorageDevice> EndShowSelector(System::IAsyncResult* result);

        // ---- CNA extension ----

        /// Sets the application name used to build the storage root directory.
        /// Call once at startup (e.g. in Game constructor) before accessing storage.
        NOXNA static void SetAppNameEXT(const std::string& appName);

        /// Returns the storage root directory for the current application.
        NOXNA static std::string GetStorageRootEXT();

    private:
        explicit StorageDevice(std::optional<PlayerIndex> player);

        std::optional<PlayerIndex> devicePlayer_;

        static std::string storageRoot_;
        static std::string appName_;
        static bool storageRootInitialized_;

        static const std::string& EnsureStorageRoot();
    };

} // namespace Microsoft::Xna::Framework::Storage
