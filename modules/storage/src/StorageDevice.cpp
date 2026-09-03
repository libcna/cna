// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.hpp"
#include "SharpRuntime/Storage/StoragePaths.hpp"
#include "System/Threading/EventWaitHandle.hpp"

#include <any>
#include <filesystem>
#include <stdexcept>

#include <cstdlib>

#include "CNA/Internal/PathContainment.hpp"
#include "System/Threading/EventWaitHandle.hpp"

namespace Microsoft::Xna::Framework::Storage
{
    namespace fs = std::filesystem;

    // -------------------------------------------------------------------------
    // Synchronous IAsyncResult implementations (internal, not exposed in header)
    // -------------------------------------------------------------------------

    class SelectorResult final : public System::IAsyncResult
    {
    public:
        std::optional<PlayerIndex> playerIndex;
        std::any asyncState;

        bool getIsCompletedProperty()           const override { return true; }
        bool getCompletedSynchronouslyProperty() const override { return true; }
        const std::any& getAsyncStateProperty() const override { return asyncState; }
        System::Threading::WaitHandle& getAsyncWaitHandleProperty() const override { return waitHandle_; }

    private:
        // Already-signalled: BeginShowSelector always completes synchronously.
        mutable System::Threading::EventWaitHandle waitHandle_{true, System::Threading::EventResetMode::ManualReset};
    };

    class ContainerResult final : public System::IAsyncResult
    {
    public:
        std::string displayName;
        std::any asyncState;

        bool getIsCompletedProperty()           const override { return true; }
        bool getCompletedSynchronouslyProperty() const override { return true; }
        const std::any& getAsyncStateProperty() const override { return asyncState; }
        System::Threading::WaitHandle& getAsyncWaitHandleProperty() const override { return waitHandle_; }

    private:
        // Already-signalled: BeginOpenContainer always completes synchronously.
        mutable System::Threading::EventWaitHandle waitHandle_{true, System::Threading::EventResetMode::ManualReset};
    };

    // -------------------------------------------------------------------------
    // Static member definitions
    // -------------------------------------------------------------------------

    System::EventHandler<System::EventArgs> StorageDevice::DeviceChanged;
    std::string StorageDevice::storageRoot_;
    std::string StorageDevice::appName_;
    bool StorageDevice::storageRootInitialized_ = false;

    // -------------------------------------------------------------------------
    // Storage root resolution
    // -------------------------------------------------------------------------

    const std::string& StorageDevice::EnsureStorageRoot()
    {
        if (storageRootInitialized_) return storageRoot_;
        storageRootInitialized_ = true;

        const std::string app = appName_.empty() ? "game" : appName_;

        // Storage is intentionally independent of the selected windowing platform.  Resolve a
        // conventional per-user data root directly, then ensure it exists before returning it.
        // This also makes saved games follow the same policy under SDL3, HEADLESS and TERMINAL.
        fs::path root;
#if defined(__ANDROID__)
        // Android does not define HOME and its working directory is not writable. Ask the
        // System-layer storage policy for the current package's private files directory, then
        // retain StorageDevice's normal per-game identity beneath it. Clear the host override
        // first so changing SetAppNameEXT from one game identity to another cannot nest the new
        // root below the old game.
        SharpRuntime::Storage::StoragePaths::SetIsolatedStorageRootOverride({});
        root = SharpRuntime::Storage::StoragePaths::GetIsolatedStorageRoot().parent_path() / app;
#else
        if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0')
        {
            root = fs::path(xdg) / app;
        }
        else if (const char* localAppData = std::getenv("LOCALAPPDATA");
                 localAppData != nullptr && *localAppData != '\0')
        {
            root = fs::path(localAppData) / app;
        }
        else if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
        {
#if defined(__APPLE__)
            root = fs::path(home) / "Library" / "Application Support" / app;
#else
            root = fs::path(home) / ".local" / "share" / app;
#endif
        }
        else
        {
            root = fs::current_path() / app;
        }
#endif

        std::error_code code;
        fs::create_directories(root, code);
        if (code)
        {
            throw StorageDeviceNotConnectedException(
                "Unable to create the storage directory.",
                std::make_exception_ptr(std::filesystem::filesystem_error(
                    "create_directories", root, code)));
        }
        storageRoot_ = root.string();
        return storageRoot_;
    }

    // -------------------------------------------------------------------------
    // Constructor / destructor
    // -------------------------------------------------------------------------

    StorageDevice::StorageDevice(std::optional<PlayerIndex> player)
        : devicePlayer_(player) {}

    // -------------------------------------------------------------------------
    // Properties
    // -------------------------------------------------------------------------

    long long StorageDevice::getFreeSpaceProperty() const
    {
        try
        {
            const auto& root = EnsureStorageRoot();
            if (!fs::exists(root)) return std::numeric_limits<long long>::max();
            return static_cast<long long>(fs::space(root).available);
        }
        catch (const std::exception& e)
        {
            throw StorageDeviceNotConnectedException(
                "The storage device bound to the container is not connected.",
                std::make_exception_ptr(e));
        }
    }

    bool StorageDevice::getIsConnectedProperty() const
    {
        try
        {
            const auto& root = EnsureStorageRoot();
            // If path doesn't exist yet, the drive is still accessible
            fs::path p(root);
            // Walk up to find an existing ancestor
            while (!p.empty() && !fs::exists(p)) p = p.parent_path();
            return !p.empty();
        }
        catch (...) { return false; }
    }

    long long StorageDevice::getTotalSpaceProperty() const
    {
        try
        {
            const auto& root = EnsureStorageRoot();
            if (!fs::exists(root)) return std::numeric_limits<long long>::max();
            return static_cast<long long>(fs::space(root).capacity);
        }
        catch (const std::exception& e)
        {
            throw StorageDeviceNotConnectedException(
                "The storage device bound to the container is not connected.",
                std::make_exception_ptr(e));
        }
    }

    // -------------------------------------------------------------------------
    // OpenContainer
    // -------------------------------------------------------------------------

    std::unique_ptr<System::IAsyncResult> StorageDevice::BeginOpenContainer(
        const std::string& displayName,
        std::function<void(System::IAsyncResult*)> callback,
        void* state)
    {
        auto result = std::make_unique<ContainerResult>();
        result->displayName = displayName;
        result->asyncState  = state;
        if (callback) callback(result.get());
        return result;
    }

    std::unique_ptr<StorageContainer> StorageDevice::EndOpenContainer(
        System::IAsyncResult* result)
    {
        auto* r = dynamic_cast<ContainerResult*>(result);
        if (!r) throw std::invalid_argument("result was not produced by BeginOpenContainer.");

        int playerIdx = devicePlayer_.has_value()
            ? static_cast<int>(devicePlayer_.value())
            : -1;

        return std::unique_ptr<StorageContainer>(
            new StorageContainer(*this, r->displayName, EnsureStorageRoot(), playerIdx));
    }

    void StorageDevice::DeleteContainer(const std::string& titleName)
    {
        if (titleName.empty())
            throw std::invalid_argument("titleName must not be empty.");

        // REMED-CONTENT-002: titleName is caller-supplied. fs::path::operator/ silently discards
        // the storage root for an absolute titleName, and does not reject ".." segments -- either
        // one previously let DeleteContainer("../../../SomeOtherAppData") (or an absolute path)
        // recursively delete anything the process can reach, not just this game's own storage.
        // CNA-introduced (FNA's own DeleteContainer always throws NotImplementedException), so no
        // FNA behavior constrains this fix.
        const auto contained = CNA::Internal::ResolveContainedPath(EnsureStorageRoot(), titleName);
        if (!contained.ok)
        {
            throw std::invalid_argument(
                "titleName must be a simple name within the storage root, not an absolute path "
                "or one that escapes it.");
        }
        fs::remove_all(contained.resolvedPath);
    }

    // -------------------------------------------------------------------------
    // ShowSelector
    // -------------------------------------------------------------------------

    std::unique_ptr<System::IAsyncResult> StorageDevice::BeginShowSelector(
        std::function<void(System::IAsyncResult*)> callback, void* state)
    {
        return BeginShowSelector(0, 0, std::move(callback), state);
    }

    std::unique_ptr<System::IAsyncResult> StorageDevice::BeginShowSelector(
        PlayerIndex player,
        std::function<void(System::IAsyncResult*)> callback, void* state)
    {
        return BeginShowSelector(player, 0, 0, std::move(callback), state);
    }

    std::unique_ptr<System::IAsyncResult> StorageDevice::BeginShowSelector(
        int /*sizeInBytes*/, int /*directoryCount*/,
        std::function<void(System::IAsyncResult*)> callback, void* state)
    {
        auto result = std::make_unique<SelectorResult>();
        result->playerIndex = std::nullopt;
        result->asyncState  = state;
        if (callback) callback(result.get());
        return result;
    }

    std::unique_ptr<System::IAsyncResult> StorageDevice::BeginShowSelector(
        PlayerIndex player, int /*sizeInBytes*/, int /*directoryCount*/,
        std::function<void(System::IAsyncResult*)> callback, void* state)
    {
        auto result = std::make_unique<SelectorResult>();
        result->playerIndex = player;
        result->asyncState  = state;
        if (callback) callback(result.get());
        return result;
    }

    std::unique_ptr<StorageDevice> StorageDevice::EndShowSelector(System::IAsyncResult* result)
    {
        auto* r = dynamic_cast<SelectorResult*>(result);
        if (!r) throw std::invalid_argument("result was not produced by BeginShowSelector.");
        return std::unique_ptr<StorageDevice>(new StorageDevice(r->playerIndex));
    }

    // -------------------------------------------------------------------------
    // CNA extensions
    // -------------------------------------------------------------------------

    void StorageDevice::SetAppNameEXT(const std::string& appName)
    {
        appName_               = appName;
        storageRootInitialized_ = false; // force re-evaluation next access
        storageRoot_.clear();

        SharpRuntime::Storage::StoragePaths::SetIsolatedStorageRootOverride(
            fs::path(EnsureStorageRoot()) / ".cna_isolated_storage");
    }

    std::string StorageDevice::GetStorageRootEXT()
    {
        return EnsureStorageRoot();
    }

} // namespace Microsoft::Xna::Framework::Storage
