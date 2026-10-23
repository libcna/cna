// SPDX-License-Identifier: MS-PL

#include "CNA/C/storage.h"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiStorageDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiDetail.hpp"

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
using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::CreateResourceHandle;
using CNA::C::Detail::EmbeddedNul;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::InternalError;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::InvalidStateIf;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::NativeOrdinal;
using CNA::C::Detail::NotSupported;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::OutOfRange;
using CNA::C::Detail::Overflow;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::ToCBool;
using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::Storage::StorageContainer;
using Microsoft::Xna::Framework::Storage::StorageDevice;

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
    std::size_t activeBorrowCount = 0U;
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
    return LookupHandle(handle, ObjectKind::StorageDevice, outDevice);
}

[[nodiscard]] CNA_Result GetContainer(
    const CNA_Handle handle,
    std::shared_ptr<StorageContainerResource>* const outContainer)
{
    return LookupHandle(handle, ObjectKind::StorageContainer, outContainer);
}

[[nodiscard]] CNA_Result GetStream(
    const CNA_Handle handle,
    std::shared_ptr<StorageStreamResource>* const outStream)
{
    return LookupHandle(handle, ObjectKind::StorageStream, outStream);
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
    return CreateResourceHandle<StorageDeviceResource>(
        ObjectKind::StorageDevice, std::shared_ptr<StorageDevice>(std::move(device)), outDevice);
}

[[nodiscard]] CNA_Result CreateStreamHandle(
    std::unique_ptr<System::IO::Stream> stream,
    std::shared_ptr<StorageContainerResource> container,
    CNA_StorageStreamHandle* const outStream)
{
    const auto resource = std::make_shared<StorageStreamResource>();
    resource->value = std::move(stream);
    resource->container = std::move(container);
    if (const CNA_Result result = CreateHandle(ObjectKind::StorageStream,
        resource,
        outStream);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
    if (const CNA_Result result = ReadText(
            searchPattern, "The storage search pattern is not valid UTF-8.", &pattern,
            EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
        return OutOfRange("The storage name index is outside the matching names.");
    }
    return CopyText(
        names[static_cast<std::size_t>(index)],
        destination,
        capacity,
        outBytes);
}

[[nodiscard]] CNA_Result RelativePath(
    const CNA_StringView value,
    std::string* const outPath)
{
    if (const CNA_Result result =
            ReadText(value, "The storage path is not valid UTF-8.", outPath, EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
        return InternalError("The canonical container returned no stream.");
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
        return InternalError("The canonical selector returned no storage device.");
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
        return Overflow(message);
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
        return Overflow(message);
    }
    *outValue = static_cast<System::IO::intcs>(value);
    return CNA_RESULT_SUCCESS;
}

} // namespace

namespace CNA::C::Detail {

CNA_Result AcquireStorageStream(const CNA_Handle handle, BorrowedStorageStream* const outStream)
{
    if (outStream == nullptr) {
        return InvalidArgument("The borrowed storage stream output is null.");
    }
    *outStream = BorrowedStorageStream{};
    std::shared_ptr<StorageStreamResource> stream;
    if (const CNA_Result result = GetStream(handle, &stream); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!stream->value->getCanReadProperty()) {
        return NotSupported("The storage stream was not opened for reading.");
    }
    stream->activeBorrowCount += 1U;
    outStream->value = stream->value.get();
    outStream->owner = stream;
    return CNA_RESULT_SUCCESS;
}

void ReleaseStorageStream(const BorrowedStorageStream& stream) noexcept
{
    if (stream.owner == nullptr) {
        return;
    }
    const auto resource = std::static_pointer_cast<StorageStreamResource>(stream.owner);
    if (resource->activeBorrowCount != 0U) {
        resource->activeBorrowCount -= 1U;
    }
}

} // namespace CNA::C::Detail

namespace {

[[nodiscard]] CNA_Result StorageDeviceDeleteContainer(
    const CNA_StorageDeviceHandle deviceHandle,
    const CNA_StringView titleName)
{
    std::string titleNameCopy;
    if (const CNA_Result result = RelativePath(titleName, &titleNameCopy);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // The ABI refuses every empty relative path. The canonical call follows XNA instead, where an
    // empty name addresses the player folder at the storage root.
    if (titleNameCopy.empty()) {
        return InvalidArgument("The container title name is empty.");
    }
    std::shared_ptr<StorageDeviceResource> device;
    if (const CNA_Result result = GetDevice(deviceHandle, &device);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    device->value->DeleteContainer(titleNameCopy);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result StorageDeviceSubscribeDeviceChanged(
    const CNA_StorageCompletionCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
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
}

[[nodiscard]] CNA_Result StorageDeviceUnsubscribeDeviceChanged(const CNA_Handle registrationHandle)
{
    std::shared_ptr<StorageRegistration> registration;
    const CNA_Result getResult = LookupHandle(
        registrationHandle, ObjectKind::StorageDeviceEventRegistration, &registration);
    if (getResult != CNA_RESULT_SUCCESS) {
        return getResult;
    }
    registration->Unsubscribe();
    return ReleaseHandle(registrationHandle);
}

[[nodiscard]] CNA_Result StorageContainerOpen(
    const CNA_StorageDeviceHandle deviceHandle,
    const CNA_StringView displayName,
    const CNA_StorageCompletionCallback callback,
    void* const context,
    CNA_StorageContainerHandle* const outContainer)
{
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
        return InternalError("The canonical device returned no storage container.");
    }

    const auto resource = std::make_shared<StorageContainerResource>();
    resource->value = std::shared_ptr<StorageContainer>(std::move(nativeContainer));
    resource->device = device;
    resource->deviceHandle = deviceHandle;
    if (const CNA_Result result = CreateHandle(ObjectKind::StorageContainer,
        resource,
        outContainer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    device->openContainers += 1U;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result StorageContainerGetStorageDevice(
    const CNA_StorageContainerHandle containerHandle,
    CNA_StorageDeviceHandle* const outDevice)
{
    std::shared_ptr<StorageContainerResource> container;
    if (const CNA_Result result = GetContainer(containerHandle, &container);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // The canonical property returns a reference to the very device the container was opened
    // from, so the C route answers with that handle instead of minting a second owner for one
    // native object.
    if (&container->value->getStorageDeviceProperty() != container->device->value.get()) {
        return InternalError(
            "The canonical container reported a different owning storage device.");
    }
    *outDevice = container->deviceHandle;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result StorageContainerSubscribeDisposing(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StorageCompletionCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
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
}

[[nodiscard]] CNA_Result StorageContainerUnsubscribeDisposing(const CNA_Handle registrationHandle)
{
    std::shared_ptr<StorageRegistration> registration;
    const CNA_Result getResult = LookupHandle(
        registrationHandle, ObjectKind::StorageContainerEventRegistration, &registration);
    if (getResult != CNA_RESULT_SUCCESS) {
        return getResult;
    }
    registration->Unsubscribe();
    return ReleaseHandle(registrationHandle);
}

[[nodiscard]] CNA_Result StorageContainerCreateDirectory(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView directory)
{
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
}

[[nodiscard]] CNA_Result StorageContainerDirectoryExists(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView directory,
    CNA_Bool* const outExists)
{
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
    *outExists = ToCBool(container->value->DirectoryExists(path));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result StorageContainerDeleteDirectory(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView directory)
{
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
}

[[nodiscard]] CNA_Result StorageContainerFileExists(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView file,
    CNA_Bool* const outExists)
{
    std::string path;
    if (const CNA_Result result = RelativePath(file, &path); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<StorageContainerResource> container;
    if (const CNA_Result result = GetContainer(containerHandle, &container);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outExists = ToCBool(container->value->FileExists(path));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result StorageContainerDeleteFile(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView file)
{
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
}

[[nodiscard]] CNA_Result StorageContainerCreateFile(
    const CNA_StorageContainerHandle containerHandle,
    const CNA_StringView file,
    CNA_StorageStreamHandle* const outStream)
{
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
        return InternalError("The canonical container returned no stream.");
    }
    return CreateStreamHandle(std::move(stream), std::move(container), outStream);
}

[[nodiscard]] CNA_Result StorageContainerDestroy(const CNA_StorageContainerHandle containerHandle)
{
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
    if (const CNA_Result releaseResult = ReleaseHandle(containerHandle);
        releaseResult != CNA_RESULT_SUCCESS) {
        return releaseResult;
    }
    if (container->device->openContainers != 0U) {
        container->device->openContainers -= 1U;
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result StorageStreamRead(
    const CNA_StorageStreamHandle streamHandle,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outRead)
{
    if ((destination == nullptr && capacity != 0U)) {
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
        return InternalError("The canonical stream reported a negative read count.");
    }
    *outRead = static_cast<uint64_t>(read);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result StorageStreamWrite(
    const CNA_StorageStreamHandle streamHandle,
    const uint8_t* const data,
    const uint64_t count)
{
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
}

[[nodiscard]] CNA_Result StorageStreamSeek(
    const CNA_StorageStreamHandle streamHandle,
    const int64_t offset,
    const CNA_SeekOrigin origin,
    int64_t* const outPosition)
{
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
}

[[nodiscard]] CNA_Result StorageStreamSetLength(
    const CNA_StorageStreamHandle streamHandle,
    const int64_t length)
{
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
}

[[nodiscard]] CNA_Result StorageStreamClose(const CNA_StorageStreamHandle streamHandle)
{
    std::shared_ptr<StorageStreamResource> stream;
    if (const CNA_Result result = GetStream(streamHandle, &stream);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (stream->activeBorrowCount != 0U) {
        return InvalidState(
            "Every reader borrowing this stream must be destroyed before it is closed.");
    }
    stream->value->Close();
    if (const CNA_Result releaseResult = ReleaseHandle(streamHandle);
        releaseResult != CNA_RESULT_SUCCESS) {
        return releaseResult;
    }
    if (stream->container->openStreams != 0U) {
        stream->container->openStreams -= 1U;
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

// The routes generated from modules/c-api/spec/Storage.toml.
#include "generated/Storage.inc"
