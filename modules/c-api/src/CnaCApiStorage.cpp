// SPDX-License-Identifier: MS-PL

#include "CNA/C/storage.h"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageContainer.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IO/FileAccess.hpp"
#include "System/IO/FileMode.hpp"
#include "System/IO/FileShare.hpp"
#include "System/IO/SeekOrigin.hpp"
#include "System/IO/Stream.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::Storage::StorageContainer;
using Microsoft::Xna::Framework::Storage::StorageDevice;

template<typename TEnum>
[[nodiscard]] constexpr uint32_t NativeOrdinal(const TEnum value) noexcept
{
    return static_cast<uint32_t>(static_cast<std::underlying_type_t<TEnum>>(value));
}

static_assert(NativeOrdinal(System::IO::FileMode::CreateNew) == CNA_FILE_MODE_CREATE_NEW);
static_assert(NativeOrdinal(System::IO::FileMode::Create) == CNA_FILE_MODE_CREATE);
static_assert(NativeOrdinal(System::IO::FileMode::Open) == CNA_FILE_MODE_OPEN);
static_assert(NativeOrdinal(System::IO::FileMode::OpenOrCreate) == CNA_FILE_MODE_OPEN_OR_CREATE);
static_assert(NativeOrdinal(System::IO::FileMode::Truncate) == CNA_FILE_MODE_TRUNCATE);
static_assert(NativeOrdinal(System::IO::FileMode::Append) == CNA_FILE_MODE_APPEND);

static_assert(NativeOrdinal(System::IO::FileAccess::Read) == CNA_FILE_ACCESS_READ);
static_assert(NativeOrdinal(System::IO::FileAccess::Write) == CNA_FILE_ACCESS_WRITE);
static_assert(NativeOrdinal(System::IO::FileAccess::ReadWrite) == CNA_FILE_ACCESS_READ_WRITE);

static_assert(NativeOrdinal(System::IO::FileShare::None) == CNA_FILE_SHARE_NONE);
static_assert(NativeOrdinal(System::IO::FileShare::Read) == CNA_FILE_SHARE_READ);
static_assert(NativeOrdinal(System::IO::FileShare::Write) == CNA_FILE_SHARE_WRITE);
static_assert(NativeOrdinal(System::IO::FileShare::ReadWrite) == CNA_FILE_SHARE_READ_WRITE);
static_assert(NativeOrdinal(System::IO::FileShare::Delete) == CNA_FILE_SHARE_DELETE);
static_assert(NativeOrdinal(System::IO::FileShare::Inheritable) == CNA_FILE_SHARE_INHERITABLE);

static_assert(NativeOrdinal(System::IO::SeekOrigin::Begin) == CNA_SEEK_ORIGIN_BEGIN);
static_assert(NativeOrdinal(System::IO::SeekOrigin::Current) == CNA_SEEK_ORIGIN_CURRENT);
static_assert(NativeOrdinal(System::IO::SeekOrigin::End) == CNA_SEEK_ORIGIN_END);

static_assert(NativeOrdinal(PlayerIndex::One) == CNA_PLAYER_INDEX_ONE);
static_assert(NativeOrdinal(PlayerIndex::Four) == CNA_PLAYER_INDEX_FOUR);

constexpr uint32_t FileShareMask = CNA_FILE_SHARE_READ | CNA_FILE_SHARE_WRITE |
    CNA_FILE_SHARE_DELETE | CNA_FILE_SHARE_INHERITABLE;

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result InvalidState(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, message);
}

struct StorageDeviceResource final {
    std::shared_ptr<StorageDevice> value;
    std::size_t openContainers = 0U;
};

struct StorageContainerResource final {
    std::shared_ptr<StorageContainer> value;
    std::shared_ptr<StorageDeviceResource> device;
    CNA_Handle deviceHandle = CNA_INVALID_HANDLE;
    std::size_t openStreams = 0U;
};

struct StorageStreamResource final {
    std::unique_ptr<System::IO::Stream> value;
    std::shared_ptr<StorageContainerResource> container;
};

// A subscription lives exactly as long as its registration handle. The container flavor holds a
// weak reference so releasing a registration after the container is already gone is a no-op rather
// than a use-after-free; the device flavor targets a canonical static event that outlives every
// handle, so it needs no owner reference at all.
class StorageRegistration final {
public:
    using Token = System::EventHandler<System::EventArgs>::Token;

    explicit StorageRegistration(const Token token)
        : token_(token), isDeviceChanged_(true)
    {
    }

    StorageRegistration(std::weak_ptr<StorageContainer> container, const Token token)
        : container_(std::move(container)), token_(token)
    {
    }

    StorageRegistration(const StorageRegistration&) = delete;
    StorageRegistration& operator=(const StorageRegistration&) = delete;

    ~StorageRegistration()
    {
        Unsubscribe();
    }

    void Unsubscribe() noexcept
    {
        if (!subscribed_) {
            return;
        }
        subscribed_ = false;
        if (isDeviceChanged_) {
            StorageDevice::DeviceChanged.Remove(token_);
            return;
        }
        if (const std::shared_ptr<StorageContainer> container = container_.lock()) {
            container->Disposing.Remove(token_);
        }
    }

private:
    std::weak_ptr<StorageContainer> container_;
    Token token_ = 0U;
    bool isDeviceChanged_ = false;
    bool subscribed_ = true;
};

[[nodiscard]] CNA_Result GetDevice(
    const CNA_Handle handle,
    std::shared_ptr<StorageDeviceResource>* const outDevice)
{
    const CNA_Result result = GetRuntimeHandles().Get(handle, ObjectKind::StorageDevice, outDevice);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned StorageDevice handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetContainer(
    const CNA_Handle handle,
    std::shared_ptr<StorageContainerResource>* const outContainer)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::StorageContainer,
        outContainer);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned StorageContainer handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetStream(
    const CNA_Handle handle,
    std::shared_ptr<StorageStreamResource>* const outStream)
{
    const CNA_Result result = GetRuntimeHandles().Get(handle, ObjectKind::StorageStream, outStream);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned storage stream handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CopyText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes,
    const char* const message)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument(message);
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE, message);
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

// Every fake-async canonical entry point takes a completion delegate. CNA completes them before
// Begin returns, so the C route stays a single synchronous call and this adapter forwards nothing
// but the caller's own context; no IAsyncResult ever becomes reachable from C.
[[nodiscard]] std::function<void(System::IAsyncResult*)> CompletionDelegate(
    const CNA_StorageCompletionCallback callback,
    void* const context)
{
    if (callback == nullptr) {
        return nullptr;
    }
    return [callback, context](System::IAsyncResult*) { callback(context); };
}

[[nodiscard]] CNA_Result CreateDeviceHandle(
    std::unique_ptr<StorageDevice> device,
    CNA_StorageDeviceHandle* const outDevice)
{
    const auto resource = std::make_shared<StorageDeviceResource>();
    resource->value = std::shared_ptr<StorageDevice>(std::move(device));
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::StorageDevice,
        resource,
        outDevice);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned StorageDevice handle could not be created.");
}

[[nodiscard]] CNA_Result CreateStreamHandle(
    std::unique_ptr<System::IO::Stream> stream,
    std::shared_ptr<StorageContainerResource> container,
    CNA_StorageStreamHandle* const outStream)
{
    const auto resource = std::make_shared<StorageStreamResource>();
    resource->value = std::move(stream);
    resource->container = std::move(container);
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::StorageStream,
        resource,
        outStream);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned storage stream handle could not be created.");
    }
    resource->container->openStreams += 1U;
    return CNA_RESULT_SUCCESS;
}

// The canonical listing is unordered and rebuilt per call, so a C caller addresses one name by the
// index of the same immediately preceding count call rather than by a retained collection handle.
[[nodiscard]] CNA_Result ListNames(
    const std::shared_ptr<StorageContainerResource>& container,
    const CNA_StringView searchPattern,
    const bool directories,
    std::vector<std::string>* const outNames)
{
    std::string pattern;
    if (const CNA_Result result = CopyStringView(searchPattern, true, &pattern);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The storage search pattern is not valid UTF-8.");
    }
    if (pattern.empty()) {
        *outNames = directories
            ? container->value->GetDirectoryNames()
            : container->value->GetFileNames();
        return CNA_RESULT_SUCCESS;
    }
    *outNames = directories
        ? container->value->GetDirectoryNames(pattern)
        : container->value->GetFileNames(pattern);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CountNames(
    const CNA_Handle containerHandle,
    const CNA_StringView searchPattern,
    const bool directories,
    uint64_t* const outCount)
{
    if (outCount == nullptr) {
        return InvalidArgument("The storage name-count output is null.");
    }
    std::shared_ptr<StorageContainerResource> container;
    if (const CNA_Result result = GetContainer(containerHandle, &container);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::vector<std::string> names;
    if (const CNA_Result result = ListNames(container, searchPattern, directories, &names);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outCount = names.size();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyName(
    const CNA_Handle containerHandle,
    const CNA_StringView searchPattern,
    const uint64_t index,
    const bool directories,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The storage name output buffer is invalid.");
    }
    std::shared_ptr<StorageContainerResource> container;
    if (const CNA_Result result = GetContainer(containerHandle, &container);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::vector<std::string> names;
    if (const CNA_Result result = ListNames(container, searchPattern, directories, &names);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (index >= names.size()) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "The storage name index is outside the matching names.");
    }
    return CopyText(
        names[static_cast<std::size_t>(index)],
        destination,
        capacity,
        outBytes,
        "The storage name output buffer is too small.");
}

[[nodiscard]] CNA_Result RelativePath(
    const CNA_StringView value,
    std::string* const outPath)
{
    if (const CNA_Result result = CopyStringView(value, true, outPath);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The storage path is not valid UTF-8.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateFileMode(const CNA_FileMode fileMode)
{
    if (fileMode < CNA_FILE_MODE_CREATE_NEW || fileMode > CNA_FILE_MODE_APPEND) {
        return InvalidArgument("The requested file mode is not a canonical FileMode identity.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateFileAccess(const CNA_FileAccess fileAccess)
{
    if (fileAccess < CNA_FILE_ACCESS_READ || fileAccess > CNA_FILE_ACCESS_READ_WRITE) {
        return InvalidArgument("The requested file access is not a canonical FileAccess identity.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateFileShare(const CNA_FileShare fileShare)
{
    if ((fileShare & ~FileShareMask) != 0U) {
        return InvalidArgument("The requested file sharing contains unknown FileShare bits.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result OpenFileHandle(
    const CNA_Handle containerHandle,
    const CNA_StringView file,
    const CNA_FileMode fileMode,
    const CNA_FileAccess fileAccess,
    const CNA_FileShare fileShare,
    const int selections,
    CNA_StorageStreamHandle* const outStream)
{
    if (outStream == nullptr) {
        return InvalidArgument("The storage stream output handle is null.");
    }
    *outStream = CNA_INVALID_HANDLE;

    std::string path;
    if (const CNA_Result result = RelativePath(file, &path); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateFileMode(fileMode); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (selections >= 2) {
        if (const CNA_Result result = ValidateFileAccess(fileAccess);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }
    if (selections >= 3) {
        if (const CNA_Result result = ValidateFileShare(fileShare);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }

    std::shared_ptr<StorageContainerResource> container;
    if (const CNA_Result result = GetContainer(containerHandle, &container);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    const auto nativeMode = static_cast<System::IO::FileMode>(fileMode);
    std::unique_ptr<System::IO::Stream> stream;
    if (selections == 1) {
        stream = container->value->OpenFile(path, nativeMode);
    } else if (selections == 2) {
        stream = container->value->OpenFile(
            path,
            nativeMode,
            static_cast<System::IO::FileAccess>(fileAccess));
    } else {
        stream = container->value->OpenFile(
            path,
            nativeMode,
            static_cast<System::IO::FileAccess>(fileAccess),
            static_cast<System::IO::FileShare>(fileShare));
    }
    if (stream == nullptr) {
        return Fail(
            CNA_RESULT_INTERNAL,
            CNA_ERROR_CATEGORY_INTERNAL,
            "The canonical container returned no stream.");
    }
    return CreateStreamHandle(std::move(stream), std::move(container), outStream);
}

[[nodiscard]] CNA_Result ShowSelector(
    const bool hasPlayer,
    const CNA_PlayerIndex player,
    const bool hasSpace,
    const int32_t sizeInBytes,
    const int32_t directoryCount,
    const CNA_StorageCompletionCallback callback,
    void* const context,
    CNA_StorageDeviceHandle* const outDevice)
{
    if (outDevice == nullptr) {
        return InvalidArgument("The StorageDevice output handle is null.");
    }
    *outDevice = CNA_INVALID_HANDLE;
    if (hasPlayer && player > CNA_PLAYER_INDEX_FOUR) {
        return InvalidArgument("The requested player is not a canonical PlayerIndex identity.");
    }
    if (hasSpace && (sizeInBytes < 0 || directoryCount < 0)) {
        return InvalidArgument("The requested storage space and directory count must not be "
                               "negative.");
    }

    auto delegate = CompletionDelegate(callback, context);
    const auto nativePlayer = static_cast<PlayerIndex>(player);
    std::unique_ptr<System::IAsyncResult> asyncResult;
    if (hasPlayer && hasSpace) {
        asyncResult = StorageDevice::BeginShowSelector(
            nativePlayer,
            sizeInBytes,
            directoryCount,
            std::move(delegate),
            context);
    } else if (hasPlayer) {
        asyncResult = StorageDevice::BeginShowSelector(nativePlayer, std::move(delegate), context);
    } else if (hasSpace) {
        asyncResult = StorageDevice::BeginShowSelector(
            sizeInBytes,
            directoryCount,
            std::move(delegate),
            context);
    } else {
        asyncResult = StorageDevice::BeginShowSelector(std::move(delegate), context);
    }

    std::unique_ptr<StorageDevice> device = StorageDevice::EndShowSelector(asyncResult.get());
    if (device == nullptr) {
        return Fail(
            CNA_RESULT_INTERNAL,
            CNA_ERROR_CATEGORY_INTERNAL,
            "The canonical selector returned no storage device.");
    }
    return CreateDeviceHandle(std::move(device), outDevice);
}

// The canonical stream contract is expressed in .NET's Int32 offsets and lengths, so a wider C
// value is rejected up front instead of being silently truncated into the native call.
[[nodiscard]] CNA_Result CheckedInt32(
    const int64_t value,
    const bool allowNegative,
    const char* const message,
    System::IO::intcs* const outValue)
{
    if (!allowNegative && value < 0) {
        return InvalidArgument(message);
    }
    if (value < static_cast<int64_t>(std::numeric_limits<System::IO::intcs>::min()) ||
        value > static_cast<int64_t>(std::numeric_limits<System::IO::intcs>::max())) {
        return Fail(CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE, message);
    }
    *outValue = static_cast<System::IO::intcs>(value);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CheckedCount(
    const uint64_t value,
    const char* const message,
    System::IO::intcs* const outValue)
{
    if (value > static_cast<uint64_t>(std::numeric_limits<System::IO::intcs>::max())) {
        return Fail(CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE, message);
    }
    *outValue = static_cast<System::IO::intcs>(value);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result StreamQuery(
    const CNA_Handle streamHandle,
    void* const output,
    const char* const message,
    const std::function<void(System::IO::Stream&)>& query)
{
    if (output == nullptr) {
        return InvalidArgument(message);
    }
    std::shared_ptr<StorageStreamResource> stream;
    if (const CNA_Result result = GetStream(streamHandle, &stream);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    query(*stream->value);
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_storage_set_app_name_ext(const CNA_StringView appName)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string appNameCopy;
        if (const CNA_Result result = CopyStringView(appName, true, &appNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The storage application name is not valid UTF-8.");
        }
        StorageDevice::SetAppNameEXT(appNameCopy);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_get_root_size_ext(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The storage root size output is null.");
        }
        *outBytes = StorageDevice::GetStorageRootEXT().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_copy_root_ext(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyText(
            StorageDevice::GetStorageRootEXT(),
            destination,
            capacity,
            outBytes,
            "The storage root output buffer is invalid or too small.");
    });
}

CNA_Result cna_storage_device_show_selector(
    const CNA_StorageCompletionCallback callback,
    void* const context,
    CNA_StorageDeviceHandle* const outDevice)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ShowSelector(false, 0U, false, 0, 0, callback, context, outDevice);
    });
}

CNA_Result cna_storage_device_show_selector_for_player(
    const CNA_PlayerIndex player,
    const CNA_StorageCompletionCallback callback,
    void* const context,
    CNA_StorageDeviceHandle* const outDevice)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ShowSelector(true, player, false, 0, 0, callback, context, outDevice);
    });
}

CNA_Result cna_storage_device_show_selector_with_space(
    const int32_t sizeInBytes,
    const int32_t directoryCount,
    const CNA_StorageCompletionCallback callback,
    void* const context,
    CNA_StorageDeviceHandle* const outDevice)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ShowSelector(
            false,
            0U,
            true,
            sizeInBytes,
            directoryCount,
            callback,
            context,
            outDevice);
    });
}

CNA_Result cna_storage_device_show_selector_for_player_with_space(
    const CNA_PlayerIndex player,
    const int32_t sizeInBytes,
    const int32_t directoryCount,
    const CNA_StorageCompletionCallback callback,
    void* const context,
    CNA_StorageDeviceHandle* const outDevice)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ShowSelector(
            true,
            player,
            true,
            sizeInBytes,
            directoryCount,
            callback,
            context,
            outDevice);
    });
}

CNA_Result cna_storage_device_get_free_space(
    const CNA_StorageDeviceHandle deviceHandle,
    int64_t* const outFreeSpace)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFreeSpace == nullptr) {
            return InvalidArgument("The storage free-space output is null.");
        }
        std::shared_ptr<StorageDeviceResource> device;
        if (const CNA_Result result = GetDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outFreeSpace = static_cast<int64_t>(device->value->getFreeSpaceProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_device_get_is_connected(
    const CNA_StorageDeviceHandle deviceHandle,
    CNA_Bool* const outIsConnected)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsConnected == nullptr) {
            return InvalidArgument("The storage connection output is null.");
        }
        std::shared_ptr<StorageDeviceResource> device;
        if (const CNA_Result result = GetDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsConnected = device->value->getIsConnectedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_device_get_total_space(
    const CNA_StorageDeviceHandle deviceHandle,
    int64_t* const outTotalSpace)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTotalSpace == nullptr) {
            return InvalidArgument("The storage total-space output is null.");
        }
        std::shared_ptr<StorageDeviceResource> device;
        if (const CNA_Result result = GetDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTotalSpace = static_cast<int64_t>(device->value->getTotalSpaceProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_device_delete_container(
    const CNA_StorageDeviceHandle deviceHandle,
    const CNA_StringView titleName)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string titleNameCopy;
        if (const CNA_Result result = RelativePath(titleName, &titleNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageDeviceResource> device;
        if (const CNA_Result result = GetDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        device->value->DeleteContainer(titleNameCopy);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_device_subscribe_device_changed(
    const CNA_StorageCompletionCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidArgument("The DeviceChanged registration output handle is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidArgument("The DeviceChanged callback is null.");
        }
        const auto token = StorageDevice::DeviceChanged.Add(
            [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
        const auto registration = std::make_shared<StorageRegistration>(token);
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::StorageDeviceEventRegistration,
            registration,
            outRegistration);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        registration->Unsubscribe();
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The DeviceChanged registration handle could not be created.");
    });
}

CNA_Result cna_storage_device_unsubscribe_device_changed(const CNA_Handle registrationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<StorageRegistration> registration;
        const CNA_Result getResult = GetRuntimeHandles().Get(
            registrationHandle,
            ObjectKind::StorageDeviceEventRegistration,
            &registration);
        if (getResult != CNA_RESULT_SUCCESS) {
            return Fail(
                getResult,
                ErrorCategoryForResult(getResult),
                "The DeviceChanged registration handle is invalid.");
        }
        registration->Unsubscribe();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(registrationHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The DeviceChanged registration handle could not be released.");
    });
}

CNA_Result cna_storage_device_destroy(const CNA_StorageDeviceHandle deviceHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<StorageDeviceResource> device;
        if (const CNA_Result result = GetDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (device->openContainers != 0U) {
            return InvalidState(
                "Every container opened from this StorageDevice must be destroyed first.");
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(deviceHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The owned StorageDevice handle could not be released.");
    });
}

CNA_Result cna_storage_container_open(
    const CNA_StorageDeviceHandle deviceHandle,
    const CNA_StringView displayName,
    const CNA_StorageCompletionCallback callback,
    void* const context,
    CNA_StorageContainerHandle* const outContainer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContainer == nullptr) {
            return InvalidArgument("The StorageContainer output handle is null.");
        }
        *outContainer = CNA_INVALID_HANDLE;

        std::string displayNameCopy;
        if (const CNA_Result result = RelativePath(displayName, &displayNameCopy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageDeviceResource> device;
        if (const CNA_Result result = GetDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        const std::unique_ptr<System::IAsyncResult> asyncResult =
            device->value->BeginOpenContainer(
                displayNameCopy,
                CompletionDelegate(callback, context),
                context);
        std::unique_ptr<StorageContainer> nativeContainer =
            device->value->EndOpenContainer(asyncResult.get());
        if (nativeContainer == nullptr) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The canonical device returned no storage container.");
        }

        const auto resource = std::make_shared<StorageContainerResource>();
        resource->value = std::shared_ptr<StorageContainer>(std::move(nativeContainer));
        resource->device = device;
        resource->deviceHandle = deviceHandle;
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::StorageContainer,
            resource,
            outContainer);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned StorageContainer handle could not be created.");
        }
        device->openContainers += 1U;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_container_get_display_name_size(
    const CNA_StorageContainerHandle containerHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The container display-name size output is null.");
        }
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = container->value->getDisplayNameProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_container_copy_display_name(
    const CNA_StorageContainerHandle containerHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            container->value->getDisplayNameProperty(),
            destination,
            capacity,
            outBytes,
            "The container display-name output buffer is invalid or too small.");
    });
}

CNA_Result cna_storage_container_get_type_name_size(
    const CNA_StorageContainerHandle containerHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The container type-name size output is null.");
        }
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = container->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_container_copy_type_name(
    const CNA_StorageContainerHandle containerHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            container->value->GetTypeName(),
            destination,
            capacity,
            outBytes,
            "The container type-name output buffer is invalid or too small.");
    });
}

CNA_Result cna_storage_container_get_is_disposed(
    const CNA_StorageContainerHandle containerHandle,
    CNA_Bool* const outIsDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsDisposed == nullptr) {
            return InvalidArgument("The container disposal output is null.");
        }
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsDisposed = container->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_container_get_storage_device(
    const CNA_StorageContainerHandle containerHandle,
    CNA_StorageDeviceHandle* const outDevice)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDevice == nullptr) {
            return InvalidArgument("The owning StorageDevice output handle is null.");
        }
        *outDevice = CNA_INVALID_HANDLE;
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical property returns a reference to the very device the container was opened
        // from, so the C route answers with that handle instead of minting a second owner for one
        // native object.
        if (&container->value->getStorageDeviceProperty() != container->device->value.get()) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The canonical container reported a different owning storage device.");
        }
        *outDevice = container->deviceHandle;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_container_dispose(const CNA_StorageContainerHandle containerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        container->value->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_container_subscribe_disposing(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StorageCompletionCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidArgument("The Disposing registration output handle is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidArgument("The Disposing callback is null.");
        }
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto token = container->value->Disposing.Add(
            [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
        const auto registration = std::make_shared<StorageRegistration>(container->value, token);
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::StorageContainerEventRegistration,
            registration,
            outRegistration);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        registration->Unsubscribe();
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The Disposing registration handle could not be created.");
    });
}

CNA_Result cna_storage_container_unsubscribe_disposing(const CNA_Handle registrationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<StorageRegistration> registration;
        const CNA_Result getResult = GetRuntimeHandles().Get(
            registrationHandle,
            ObjectKind::StorageContainerEventRegistration,
            &registration);
        if (getResult != CNA_RESULT_SUCCESS) {
            return Fail(
                getResult,
                ErrorCategoryForResult(getResult),
                "The Disposing registration handle is invalid.");
        }
        registration->Unsubscribe();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(registrationHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The Disposing registration handle could not be released.");
    });
}

CNA_Result cna_storage_container_create_directory(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView directory)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string path;
        if (const CNA_Result result = RelativePath(directory, &path);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        container->value->CreateDirectory(path);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_container_directory_exists(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView directory,
    CNA_Bool* const outExists)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outExists == nullptr) {
            return InvalidArgument("The directory-existence output is null.");
        }
        std::string path;
        if (const CNA_Result result = RelativePath(directory, &path);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outExists = container->value->DirectoryExists(path) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_container_delete_directory(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView directory)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string path;
        if (const CNA_Result result = RelativePath(directory, &path);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        container->value->DeleteDirectory(path);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_container_file_exists(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView file,
    CNA_Bool* const outExists)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outExists == nullptr) {
            return InvalidArgument("The file-existence output is null.");
        }
        std::string path;
        if (const CNA_Result result = RelativePath(file, &path); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outExists = container->value->FileExists(path) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_container_delete_file(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView file)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string path;
        if (const CNA_Result result = RelativePath(file, &path); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        container->value->DeleteFile(path);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_container_get_directory_name_count(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView searchPattern,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CountNames(containerHandle, searchPattern, true, outCount);
    });
}

CNA_Result cna_storage_container_copy_directory_name(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView searchPattern,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyName(
            containerHandle,
            searchPattern,
            index,
            true,
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_storage_container_get_file_name_count(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView searchPattern,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CountNames(containerHandle, searchPattern, false, outCount);
    });
}

CNA_Result cna_storage_container_copy_file_name(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView searchPattern,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyName(
            containerHandle,
            searchPattern,
            index,
            false,
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_storage_container_create_file(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView file,
    CNA_StorageStreamHandle* const outStream)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outStream == nullptr) {
            return InvalidArgument("The storage stream output handle is null.");
        }
        *outStream = CNA_INVALID_HANDLE;
        std::string path;
        if (const CNA_Result result = RelativePath(file, &path); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::unique_ptr<System::IO::Stream> stream = container->value->CreateFile(path);
        if (stream == nullptr) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The canonical container returned no stream.");
        }
        return CreateStreamHandle(std::move(stream), std::move(container), outStream);
    });
}

CNA_Result cna_storage_container_open_file(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView file,
    const CNA_FileMode fileMode,
    CNA_StorageStreamHandle* const outStream)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return OpenFileHandle(
            containerHandle,
            file,
            fileMode,
            CNA_FILE_ACCESS_READ_WRITE,
            CNA_FILE_SHARE_READ_WRITE,
            1,
            outStream);
    });
}

CNA_Result cna_storage_container_open_file_access(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView file,
    const CNA_FileMode fileMode,
    const CNA_FileAccess fileAccess,
    CNA_StorageStreamHandle* const outStream)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return OpenFileHandle(
            containerHandle,
            file,
            fileMode,
            fileAccess,
            CNA_FILE_SHARE_READ_WRITE,
            2,
            outStream);
    });
}

CNA_Result cna_storage_container_open_file_share(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView file,
    const CNA_FileMode fileMode,
    const CNA_FileAccess fileAccess,
    const CNA_FileShare fileShare,
    CNA_StorageStreamHandle* const outStream)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return OpenFileHandle(
            containerHandle,
            file,
            fileMode,
            fileAccess,
            fileShare,
            3,
            outStream);
    });
}

CNA_Result cna_storage_container_destroy(const CNA_StorageContainerHandle containerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<StorageContainerResource> container;
        if (const CNA_Result result = GetContainer(containerHandle, &container);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (container->openStreams != 0U) {
            return InvalidState(
                "Every stream opened from this StorageContainer must be closed first.");
        }
        container->value->Dispose();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(containerHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned StorageContainer handle could not be released.");
        }
        if (container->device->openContainers != 0U) {
            container->device->openContainers -= 1U;
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_stream_read(
    const CNA_StorageStreamHandle streamHandle,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outRead)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRead == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The storage stream read buffer is invalid.");
        }
        *outRead = 0U;
        System::IO::intcs count = 0;
        if (const CNA_Result result = CheckedCount(
                capacity,
                "The storage stream read capacity exceeds the canonical range.",
                &count);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageStreamResource> stream;
        if (const CNA_Result result = GetStream(streamHandle, &stream);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (count == 0) {
            return CNA_RESULT_SUCCESS;
        }
        const System::IO::intcs read = stream->value->Read(destination, 0, count);
        if (read < 0) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The canonical stream reported a negative read count.");
        }
        *outRead = static_cast<uint64_t>(read);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_stream_write(
    const CNA_StorageStreamHandle streamHandle,
    const uint8_t* const data,
    const uint64_t count)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (data == nullptr && count != 0U) {
            return InvalidArgument("The storage stream write buffer is invalid.");
        }
        System::IO::intcs byteCount = 0;
        if (const CNA_Result result = CheckedCount(
                count,
                "The storage stream write count exceeds the canonical range.",
                &byteCount);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageStreamResource> stream;
        if (const CNA_Result result = GetStream(streamHandle, &stream);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (byteCount == 0) {
            return CNA_RESULT_SUCCESS;
        }
        stream->value->Write(data, 0, byteCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_stream_seek(
    const CNA_StorageStreamHandle streamHandle,
    const int64_t offset,
    const CNA_SeekOrigin origin,
    int64_t* const outPosition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPosition == nullptr) {
            return InvalidArgument("The storage stream position output is null.");
        }
        if (origin > CNA_SEEK_ORIGIN_END) {
            return InvalidArgument("The requested origin is not a canonical SeekOrigin identity.");
        }
        System::IO::intcs nativeOffset = 0;
        if (const CNA_Result result = CheckedInt32(
                offset,
                true,
                "The storage stream seek offset exceeds the canonical range.",
                &nativeOffset);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageStreamResource> stream;
        if (const CNA_Result result = GetStream(streamHandle, &stream);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outPosition = static_cast<int64_t>(
            stream->value->Seek(nativeOffset, static_cast<System::IO::SeekOrigin>(origin)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_stream_get_position(
    const CNA_StorageStreamHandle streamHandle,
    int64_t* const outPosition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return StreamQuery(
            streamHandle,
            outPosition,
            "The storage stream position output is null.",
            [outPosition](System::IO::Stream& stream) {
                *outPosition = static_cast<int64_t>(stream.getPositionProperty());
            });
    });
}

CNA_Result cna_storage_stream_get_length(
    const CNA_StorageStreamHandle streamHandle,
    int64_t* const outLength)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return StreamQuery(
            streamHandle,
            outLength,
            "The storage stream length output is null.",
            [outLength](System::IO::Stream& stream) {
                *outLength = static_cast<int64_t>(stream.getLengthProperty());
            });
    });
}

CNA_Result cna_storage_stream_set_length(
    const CNA_StorageStreamHandle streamHandle,
    const int64_t length)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        System::IO::intcs nativeLength = 0;
        if (const CNA_Result result = CheckedInt32(
                length,
                false,
                "The storage stream length is negative or exceeds the canonical range.",
                &nativeLength);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageStreamResource> stream;
        if (const CNA_Result result = GetStream(streamHandle, &stream);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        stream->value->SetLength(nativeLength);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_stream_get_can_read(
    const CNA_StorageStreamHandle streamHandle,
    CNA_Bool* const outCanRead)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return StreamQuery(
            streamHandle,
            outCanRead,
            "The storage stream read-capability output is null.",
            [outCanRead](System::IO::Stream& stream) {
                *outCanRead = stream.getCanReadProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_storage_stream_get_can_write(
    const CNA_StorageStreamHandle streamHandle,
    CNA_Bool* const outCanWrite)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return StreamQuery(
            streamHandle,
            outCanWrite,
            "The storage stream write-capability output is null.",
            [outCanWrite](System::IO::Stream& stream) {
                *outCanWrite = stream.getCanWriteProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_storage_stream_get_can_seek(
    const CNA_StorageStreamHandle streamHandle,
    CNA_Bool* const outCanSeek)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return StreamQuery(
            streamHandle,
            outCanSeek,
            "The storage stream seek-capability output is null.",
            [outCanSeek](System::IO::Stream& stream) {
                *outCanSeek = stream.getCanSeekProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_storage_stream_flush(const CNA_StorageStreamHandle streamHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<StorageStreamResource> stream;
        if (const CNA_Result result = GetStream(streamHandle, &stream);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        stream->value->Flush();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_stream_close(const CNA_StorageStreamHandle streamHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<StorageStreamResource> stream;
        if (const CNA_Result result = GetStream(streamHandle, &stream);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        stream->value->Close();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(streamHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned storage stream handle could not be released.");
        }
        if (stream->container->openStreams != 0U) {
            stream->container->openStreams -= 1U;
        }
        return CNA_RESULT_SUCCESS;
    });
}
