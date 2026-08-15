// SPDX-License-Identifier: MS-PL

#include "CNA/C/index_resources.h"
#include "CNA/GraphicsCapability.hpp"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"

#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::AddOwnedGraphicsResource;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::IndexBufferResource;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedGraphicsResource;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicIndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] bool IsBool(const CNA_Bool value) noexcept
{
    return value == CNA_FALSE || value == CNA_TRUE;
}

[[nodiscard]] bool IsZero(const uint8_t* const values, const std::size_t count) noexcept
{
    for (std::size_t index = 0U; index < count; ++index) {
        if (values[index] != 0U) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] CNA_Result GetBuffer(
    const CNA_IndexBufferHandle handle,
    std::shared_ptr<IndexBufferResource>* const outBuffer)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::IndexBuffer, outBuffer);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The IndexBuffer handle is invalid for this call.");
}

[[nodiscard]] CNA_Result ValidateUsable(const IndexBufferResource& resource)
{
    if (resource.value->getIsDisposedProperty()) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "The IndexBuffer has been disposed.");
    }
    return CNA_RESULT_SUCCESS;
}

struct TransferView final {
    IndexElementSize elementSize;
    SetDataOptions options;
    uint64_t start;
    uint64_t count;
};

[[nodiscard]] CNA_Result ValidateTransfer(
    const CNA_IndexBufferTransfer* const transfer,
    const uint64_t capacity,
    const void* const data,
    const bool readback,
    TransferView* const outView)
{
    if (transfer == nullptr || outView == nullptr ||
        transfer->struct_size < sizeof(CNA_IndexBufferTransfer) ||
        transfer->struct_version != StructureVersion ||
        transfer->index_element_size > CNA_INDEX_ELEMENT_SIZE_THIRTY_TWO_BITS ||
        transfer->options > CNA_SET_DATA_NO_OVERWRITE) {
        return InvalidArgument("The index-buffer transfer descriptor is invalid.");
    }
    if (readback && transfer->options != CNA_SET_DATA_NONE) {
        return InvalidArgument("Index-buffer readback requires CNA_SET_DATA_NONE.");
    }
    if (transfer->start_index > capacity ||
        transfer->element_count > capacity - transfer->start_index) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The caller array cannot hold the complete index transfer window.");
    }
    if (capacity != 0U && data == nullptr) {
        return InvalidArgument("The index-buffer caller array is null.");
    }
    if (transfer->element_count >
        static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The index transfer count exceeds the native Int32 range.");
    }
    *outView = TransferView{
        static_cast<IndexElementSize>(transfer->index_element_size),
        static_cast<SetDataOptions>(transfer->options),
        transfer->start_index,
        transfer->element_count};
    return CNA_RESULT_SUCCESS;
}

template<typename TIndex>
[[nodiscard]] CNA_Result SetTyped(
    const IndexBufferResource& resource,
    const TransferView& transfer,
    const void* const source)
{
    if (transfer.count == 0U) {
        return CNA_RESULT_SUCCESS;
    }
    if (transfer.start >
        static_cast<uint64_t>(std::numeric_limits<std::size_t>::max() / sizeof(TIndex))) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The index source byte offset is too large.");
    }
    const auto* const bytes = static_cast<const uint8_t*>(source) +
        static_cast<std::size_t>(transfer.start) * sizeof(TIndex);
    std::vector<TIndex> native(static_cast<std::size_t>(transfer.count));
    std::memcpy(native.data(), bytes, native.size() * sizeof(TIndex));
    const int count = static_cast<int>(transfer.count);
    if (resource.dynamic) {
        auto dynamic = std::static_pointer_cast<DynamicIndexBuffer>(resource.value);
        dynamic->SetData(native.data(), 0, count, transfer.options);
    } else if (transfer.start == 0U) {
        resource.value->SetData(native.data(), count);
    } else {
        resource.value->SetData(native.data(), 0, count);
    }
    return CNA_RESULT_SUCCESS;
}

template<typename TIndex>
[[nodiscard]] CNA_Result GetTyped(
    const IndexBufferResource& resource,
    const TransferView& transfer,
    void* const destination)
{
    if (transfer.count == 0U) {
        return CNA_RESULT_SUCCESS;
    }
    if (transfer.start >
        static_cast<uint64_t>(std::numeric_limits<std::size_t>::max() / sizeof(TIndex))) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The index destination byte offset is too large.");
    }
    std::vector<TIndex> native(static_cast<std::size_t>(transfer.count));
    if (transfer.start == 0U) {
        resource.value->GetData(native.data(), static_cast<int>(transfer.count));
    } else {
        resource.value->GetData(native.data(), 0, static_cast<int>(transfer.count));
    }
    auto* const bytes = static_cast<uint8_t*>(destination) +
        static_cast<std::size_t>(transfer.start) * sizeof(TIndex);
    std::memcpy(bytes, native.data(), native.size() * sizeof(TIndex));
    return CNA_RESULT_SUCCESS;
}

template<typename TCallable>
[[nodiscard]] CNA_Result CopyString(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount,
    TCallable&& callable)
{
    if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The index-buffer string output is invalid.");
    }
    const std::string value = std::forward<TCallable>(callable)();
    *outByteCount = static_cast<uint64_t>(value.size());
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination cannot hold the complete index-buffer type name.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

class ContentLostRegistration final {
public:
    using Token = System::EventHandler<System::EventArgs>::Token;

    ContentLostRegistration(
        std::weak_ptr<DynamicIndexBuffer> buffer,
        const Token token)
        : buffer_(std::move(buffer)), token_(token)
    {
    }

    ~ContentLostRegistration()
    {
        Unsubscribe();
    }

    void Unsubscribe()
    {
        if (!subscribed_) {
            return;
        }
        subscribed_ = false;
        if (const std::shared_ptr<DynamicIndexBuffer> buffer = buffer_.lock()) {
            buffer->ContentLost.Remove(token_);
        }
    }

private:
    std::weak_ptr<DynamicIndexBuffer> buffer_;
    Token token_;
    bool subscribed_ = true;
};

} // namespace

CNA_Result cna_index_buffer_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_IndexBufferCreateInfo* const createInfo,
    CNA_IndexBufferHandle* const outIndexBuffer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndexBuffer == nullptr) {
            return InvalidArgument("The IndexBuffer output handle is null.");
        }
        *outIndexBuffer = CNA_INVALID_HANDLE;
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_IndexBufferCreateInfo) ||
            createInfo->struct_version != StructureVersion ||
            createInfo->index_count < 0 ||
            createInfo->index_element_size > CNA_INDEX_ELEMENT_SIZE_THIRTY_TWO_BITS ||
            createInfo->buffer_usage > CNA_BUFFER_USAGE_WRITE_ONLY ||
            !IsBool(createInfo->dynamic) ||
            !IsZero(createInfo->reserved, 3U)) {
            return InvalidArgument("The IndexBuffer creation configuration is invalid.");
        }

        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!graphicsDevice->value->SupportsCapability(CNA::GraphicsCapability::ThreeD)) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "The selected graphics backend does not support index buffers.");
        }

        const auto elementSize =
            static_cast<IndexElementSize>(createInfo->index_element_size);
        const auto usage = static_cast<BufferUsage>(createInfo->buffer_usage);
        const bool dynamic = createInfo->dynamic == CNA_TRUE;
        std::shared_ptr<IndexBuffer> native;
        if (dynamic) {
            native = std::make_shared<DynamicIndexBuffer>(
                *graphicsDevice->value,
                elementSize,
                createInfo->index_count,
                usage);
        } else if (
            elementSize == IndexElementSize::SixteenBits && usage == BufferUsage::None) {
            native = std::make_shared<IndexBuffer>(
                *graphicsDevice->value,
                createInfo->index_count);
        } else {
            native = std::make_shared<IndexBuffer>(
                *graphicsDevice->value,
                elementSize,
                createInfo->index_count,
                usage);
        }

        const auto resource = std::make_shared<IndexBufferResource>(
            IndexBufferResource{std::move(native), graphicsDevice->parentGame, dynamic});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::IndexBuffer, resource, outIndexBuffer);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned IndexBuffer handle could not be created.");
        }
        AddOwnedGraphicsResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_index_buffer_destroy(const CNA_IndexBufferHandle indexBufferHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<IndexBufferResource> buffer;
        if (const CNA_Result result = GetBuffer(indexBufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = GetRuntimeHandles().Release(indexBufferHandle);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned IndexBuffer handle could not be released.");
        }
        RemoveOwnedGraphicsResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_index_buffer_get_info(
    const CNA_IndexBufferHandle indexBufferHandle,
    CNA_IndexBufferInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr ||
            outInfo->struct_size < sizeof(CNA_IndexBufferInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidArgument("The IndexBuffer info output structure is invalid.");
        }
        std::shared_ptr<IndexBufferResource> buffer;
        if (const CNA_Result result = GetBuffer(indexBufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outInfo = CNA_IndexBufferInfo{
            sizeof(CNA_IndexBufferInfo),
            StructureVersion,
            buffer->value->getIndexCountProperty(),
            static_cast<CNA_IndexElementSize>(
                buffer->value->getIndexElementSizeProperty()),
            static_cast<CNA_BufferUsage>(buffer->value->getBufferUsageProperty()),
            buffer->dynamic ? CNA_TRUE : CNA_FALSE,
            CNA_FALSE,
            buffer->value->HasRenderer() ? CNA_TRUE : CNA_FALSE,
            0U};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_index_buffer_get_type_name_byte_count(
    const CNA_IndexBufferHandle indexBufferHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The IndexBuffer type-name byte-count output is null.");
        }
        std::shared_ptr<IndexBufferResource> buffer;
        if (const CNA_Result result = GetBuffer(indexBufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(buffer->value->GetTypeName().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_index_buffer_copy_type_name(
    const CNA_IndexBufferHandle indexBufferHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<IndexBufferResource> buffer;
        if (const CNA_Result result = GetBuffer(indexBufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyString(destination, capacity, outByteCount, [&]() {
            return buffer->value->GetTypeName();
        });
    });
}

CNA_Result cna_index_buffer_set_data(
    const CNA_IndexBufferHandle indexBufferHandle,
    const CNA_IndexBufferTransfer* const transfer,
    const void* const data,
    const uint64_t capacity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<IndexBufferResource> buffer;
        if (const CNA_Result result = GetBuffer(indexBufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateUsable(*buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        TransferView view{};
        if (const CNA_Result result = ValidateTransfer(
                transfer, capacity, data, false, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (view.elementSize != buffer->value->getIndexElementSizeProperty()) {
            return InvalidArgument(
                "The source index width does not match the IndexBuffer element size.");
        }
        if (view.options != SetDataOptions::None && !buffer->dynamic) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "SetDataOptions other than None require DynamicIndexBuffer.");
        }
        if (view.count >
            static_cast<uint64_t>(buffer->value->getIndexCountProperty())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The upload exceeds the IndexBuffer's logical capacity.");
        }
        if (view.elementSize == IndexElementSize::ThirtyTwoBits) {
            return SetTyped<uint32_t>(*buffer, view, data);
        }
        return SetTyped<uint16_t>(*buffer, view, data);
    });
}

CNA_Result cna_index_buffer_get_data(
    const CNA_IndexBufferHandle indexBufferHandle,
    const CNA_IndexBufferTransfer* const transfer,
    void* const destination,
    const uint64_t capacity,
    uint64_t* const outElementCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outElementCount == nullptr) {
            return InvalidArgument("The IndexBuffer required-element output is null.");
        }
        std::shared_ptr<IndexBufferResource> buffer;
        if (const CNA_Result result = GetBuffer(indexBufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateUsable(*buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        TransferView view{};
        if (const CNA_Result result = ValidateTransfer(
                transfer, capacity, destination, true, &view);
            result != CNA_RESULT_SUCCESS) {
            if (transfer != nullptr &&
                transfer->struct_size >= sizeof(CNA_IndexBufferTransfer) &&
                transfer->struct_version == StructureVersion) {
                *outElementCount = transfer->element_count;
            }
            return result;
        }
        *outElementCount = view.count;
        if (view.elementSize != buffer->value->getIndexElementSizeProperty()) {
            return InvalidArgument(
                "The destination index width does not match the IndexBuffer element size.");
        }
        if (view.elementSize == IndexElementSize::ThirtyTwoBits) {
            return GetTyped<uint32_t>(*buffer, view, destination);
        }
        return GetTyped<uint16_t>(*buffer, view, destination);
    });
}

CNA_Result cna_index_buffer_subscribe_content_lost(
    const CNA_IndexBufferHandle indexBufferHandle,
    const CNA_IndexBufferContentLostCallback callback,
    void* const context,
    CNA_IndexBufferEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidArgument("The ContentLost registration output handle is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidArgument("The ContentLost callback is null.");
        }
        std::shared_ptr<IndexBufferResource> buffer;
        if (const CNA_Result result = GetBuffer(indexBufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!buffer->dynamic) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "ContentLost exists only on DynamicIndexBuffer.");
        }
        const auto dynamic = std::static_pointer_cast<DynamicIndexBuffer>(buffer->value);
        const auto token = dynamic->ContentLost.Add(
            [indexBufferHandle, callback, context](System::Object*, const System::EventArgs&) {
                callback(indexBufferHandle, context);
            });
        const auto registration = std::make_shared<ContentLostRegistration>(dynamic, token);
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::IndexBufferEventRegistration,
            registration,
            outRegistration);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        registration->Unsubscribe();
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The ContentLost registration handle could not be created.");
    });
}

CNA_Result cna_index_buffer_unsubscribe_content_lost(
    const CNA_IndexBufferEventRegistrationHandle registrationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentLostRegistration> registration;
        const CNA_Result getResult = GetRuntimeHandles().Get(
            registrationHandle,
            ObjectKind::IndexBufferEventRegistration,
            &registration);
        if (getResult != CNA_RESULT_SUCCESS) {
            return Fail(
                getResult,
                ErrorCategoryForResult(getResult),
                "The ContentLost registration handle is invalid.");
        }
        registration->Unsubscribe();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(registrationHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The ContentLost registration handle could not be released.");
    });
}
