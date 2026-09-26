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
#include <optional>

#if defined(__EMSCRIPTEN__)
#  include <emscripten.h>
// The storage module's preRun hook sets this only after IDBFS has populated
// /cna-storage from IndexedDB. Refuse an in-memory-only save if that failed.
EM_JS(int, CnaBrowserStorageReady, (), {
    return Module.cnaStorageReady === true ? 1 : 0;
});
#endif

#if defined(_WIN32)
// Contained in this translation unit deliberately: <windows.h> in a header is what produced the
// ERROR / min / max macro collisions recorded as WINNATIVE-F5 and F11.
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

#include "CNA/Internal/PathContainment.hpp"
#include "CNA/Internal/PathUtf8.hpp"
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

    namespace
    {
        // std::getenv on Windows reads the ANSI environment block, which substitutes '?' for every
        // character the code page cannot spell -- so %LOCALAPPDATA% for a user named with anything
        // outside it is already destroyed before std::filesystem sees it, and no downstream
        // conversion can recover the name. The wide environment is the only correct read.
        std::optional<std::string> EnvironmentVariableUtf8(const char* name)
        {
#if defined(_WIN32)
            const std::wstring wideName(name, name + std::char_traits<char>::length(name));
            const DWORD needed = ::GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
            if (needed == 0) return std::nullopt;
            std::wstring value(needed, L'\0');
            const DWORD written = ::GetEnvironmentVariableW(wideName.c_str(), value.data(), needed);
            if (written == 0 || written >= needed) return std::nullopt;
            value.resize(written);
            if (value.empty()) return std::nullopt;
            return CNA::Internal::PathToUtf8(std::filesystem::path(value));
#else
            const char* value = std::getenv(name);
            if (value == nullptr || *value == '\0') return std::nullopt;
            return std::string(value);
#endif
        }
    }

    const std::string& StorageDevice::EnsureStorageRoot()
    {
        if (storageRootInitialized_) return storageRoot_;

        const std::string app = appName_.empty() ? "game" : appName_;

        // Storage is intentionally independent of the selected windowing platform.  Resolve a
        // conventional per-user data root directly, then ensure it exists before returning it.
        // This also makes saved games follow the same policy under SDL3, HEADLESS and TERMINAL.
        fs::path root;
#if defined(__EMSCRIPTEN__)
        // The link-time preRun hook mounts and populates this directory before main(). It is
        // separate from /save, which existing applications may manage for other file APIs.
        if (!CnaBrowserStorageReady())
        {
            throw StorageDeviceNotConnectedException(
                "Persistent browser storage is unavailable.");
        }
        root = fs::path("/cna-storage") / app;
#elif defined(__ANDROID__)
        // Android does not define HOME and its working directory is not writable. Ask the
        // System-layer storage policy for the current package's private files directory, then
        // retain StorageDevice's normal per-game identity beneath it. Clear the host override
        // first so changing SetAppNameEXT from one game identity to another cannot nest the new
        // root below the old game.
        SharpRuntime::Storage::StoragePaths::SetIsolatedStorageRootOverride({});
        root = SharpRuntime::Storage::StoragePaths::GetIsolatedStorageRoot().parent_path() / app;
#else
        const fs::path appComponent = CNA::Internal::PathFromUtf8(app);
        if (const std::optional<std::string> xdg = EnvironmentVariableUtf8("XDG_DATA_HOME"); xdg)
        {
            root = CNA::Internal::PathFromUtf8(*xdg) / appComponent;
        }
        else if (const std::optional<std::string> localAppData =
                     EnvironmentVariableUtf8("LOCALAPPDATA"); localAppData)
        {
            root = CNA::Internal::PathFromUtf8(*localAppData) / appComponent;
        }
        else if (const std::optional<std::string> home = EnvironmentVariableUtf8("HOME"); home)
        {
#if defined(__APPLE__)
            root = CNA::Internal::PathFromUtf8(*home) / "Library" / "Application Support" / appComponent;
#else
            root = CNA::Internal::PathFromUtf8(*home) / ".local" / "share" / appComponent;
#endif
        }
        else
        {
            root = fs::current_path() / appComponent;
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
        storageRoot_ = CNA::Internal::PathToGenericUtf8(root);
        storageRootInitialized_ = true;
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
        // resolvedPath is UTF-8; the narrow overload would remove nothing at all -- remove_all on
        // a path that does not exist returns 0 without an error, so this failed silently.
        fs::remove_all(CNA::Internal::PathFromUtf8(contained.resolvedPath));
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
            CNA::Internal::PathFromUtf8(EnsureStorageRoot()) / ".cna_isolated_storage");
    }

    std::string StorageDevice::GetStorageRootEXT()
    {
        return EnsureStorageRoot();
    }

} // namespace Microsoft::Xna::Framework::Storage
