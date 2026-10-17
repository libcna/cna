// SPDX-License-Identifier: MS-PL

#include "CNA/C/index_resources.h"
#include "CNA/GraphicsCapability.hpp"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"
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

using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::IndexBufferResource;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::IsCanonicalBool;
using CNA::C::Detail::IsVersionedStructure;
using CNA::C::Detail::IsZero;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::NotSupported;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::OutOfRange;
using CNA::C::Detail::Overflow;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ValidateVersionedStructure;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicIndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;

[[nodiscard]] CNA_Result GetBuffer(
    const CNA_IndexBufferHandle handle,
    std::shared_ptr<IndexBufferResource>* const outBuffer)
{
    return LookupHandle(handle, ObjectKind::IndexBuffer, outBuffer);
}

[[nodiscard]] CNA_Result ValidateUsable(const IndexBufferResource& resource)
{
    if (resource.value->getIsDisposedProperty()) {
        return InvalidState("The IndexBuffer has been disposed.");
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
        return BufferTooSmall("The caller array cannot hold the complete index transfer window.");
    }
    if (capacity != 0U && data == nullptr) {
        return InvalidArgument("The index-buffer caller array is null.");
    }
    if (transfer->element_count >
        static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return Overflow("The index transfer count exceeds the native Int32 range.");
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
        return Overflow("The index source byte offset is too large.");
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
        return Overflow("The index destination byte offset is too large.");
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

[[nodiscard]] CNA_Result IndexBufferCreate(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_IndexBufferCreateInfo* const createInfo,
    CNA_IndexBufferHandle* const outIndexBuffer)
{
    if (!IsVersionedStructure(createInfo) ||
        createInfo->index_count <= 0 ||
        createInfo->index_element_size > CNA_INDEX_ELEMENT_SIZE_THIRTY_TWO_BITS ||
        createInfo->buffer_usage > CNA_BUFFER_USAGE_WRITE_ONLY ||
        !IsCanonicalBool(createInfo->dynamic) ||
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
        return NotSupported("The selected graphics renderer does not support index buffers.");
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
    if (const CNA_Result result =
            CreateHandle(ObjectKind::IndexBuffer, resource, outIndexBuffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result IndexBufferDestroy(const CNA_IndexBufferHandle indexBufferHandle)
{
    std::shared_ptr<IndexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(indexBufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (buffer->activeModelReferenceCount != 0U) {
        return InvalidState("The IndexBuffer is retained by a ModelMeshPart.");
    }
    if (const CNA_Result result = ReleaseHandle(indexBufferHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    RemoveOwnedGraphicsResourceFor(buffer->parentGame);
    return CNA_RESULT_SUCCESS;
}

/// Applies one windowed upload, in the element width the transfer selected.
template<typename TIndex>
[[nodiscard]] CNA_Result SetTypedAt(
    const IndexBufferResource& resource,
    const uint64_t bufferOffsetInBytes,
    const TransferView& transfer,
    const void* const source)
{
    if (transfer.count == 0U) {
        return CNA_RESULT_SUCCESS;
    }
    if (transfer.start >
        static_cast<uint64_t>(std::numeric_limits<std::size_t>::max() / sizeof(TIndex))) {
        return Overflow("The index source byte offset is too large.");
    }
    const auto* const bytes = static_cast<const uint8_t*>(source) +
        static_cast<std::size_t>(transfer.start) * sizeof(TIndex);
    std::vector<TIndex> native(static_cast<std::size_t>(transfer.count));
    std::memcpy(native.data(), bytes, native.size() * sizeof(TIndex));
    resource.value->SetDataAtEXT(
        static_cast<int>(bufferOffsetInBytes),
        native.data(),
        0,
        static_cast<int>(transfer.count));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result IndexBufferSetDataAt(
    const CNA_IndexBufferHandle indexBufferHandle,
    const uint64_t bufferOffsetInBytes,
    const CNA_IndexBufferTransfer* const transfer,
    const void* const data,
    const uint64_t capacity)
{
    std::shared_ptr<IndexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(indexBufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateUsable(*buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (bufferOffsetInBytes > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return InvalidArgument(
            "The IndexBuffer destination offset is outside the native range.");
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
    // A windowed upload rewrites part of the buffer's own contents, which is the one thing a
    // streaming hint cannot describe: Discard means "the old contents are gone", and that is
    // exactly what this route promises not to do.
    if (view.options != SetDataOptions::None) {
        return NotSupported(
            "A windowed upload preserves the rest of the buffer, so it accepts no "
            "SetDataOptions other than None.");
    }
    if (view.elementSize == IndexElementSize::ThirtyTwoBits) {
        return SetTypedAt<uint32_t>(*buffer, bufferOffsetInBytes, view, data);
    }
    return SetTypedAt<uint16_t>(*buffer, bufferOffsetInBytes, view, data);
}

[[nodiscard]] CNA_Result IndexBufferSetData(
    const CNA_IndexBufferHandle indexBufferHandle,
    const CNA_IndexBufferTransfer* const transfer,
    const void* const data,
    const uint64_t capacity)
{
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
        return NotSupported("SetDataOptions other than None require DynamicIndexBuffer.");
    }
    if (view.count >
        static_cast<uint64_t>(buffer->value->getIndexCountProperty())) {
        return OutOfRange("The upload exceeds the IndexBuffer's logical capacity.");
    }
    if (view.elementSize == IndexElementSize::ThirtyTwoBits) {
        return SetTyped<uint32_t>(*buffer, view, data);
    }
    return SetTyped<uint16_t>(*buffer, view, data);
}

[[nodiscard]] CNA_Result IndexBufferGetData(
    const CNA_IndexBufferHandle indexBufferHandle,
    const CNA_IndexBufferTransfer* const transfer,
    void* const destination,
    const uint64_t capacity,
    uint64_t* const outElementCount)
{
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
        if (IsVersionedStructure(transfer)) {
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
}

[[nodiscard]] CNA_Result IndexBufferSubscribeContentLost(
    const CNA_IndexBufferHandle indexBufferHandle,
    const CNA_IndexBufferContentLostCallback callback,
    void* const context,
    CNA_IndexBufferEventRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The ContentLost callback is null.");
    }
    std::shared_ptr<IndexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(indexBufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!buffer->dynamic) {
        return NotSupported("ContentLost exists only on DynamicIndexBuffer.");
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
}

[[nodiscard]] CNA_Result IndexBufferUnsubscribeContentLost(
    const CNA_IndexBufferEventRegistrationHandle registrationHandle)
{
    std::shared_ptr<ContentLostRegistration> registration;
    const CNA_Result getResult = LookupHandle(
        registrationHandle, ObjectKind::IndexBufferEventRegistration, &registration);
    if (getResult != CNA_RESULT_SUCCESS) {
        return getResult;
    }
    registration->Unsubscribe();
    return ReleaseHandle(registrationHandle);
}

} // namespace

// The routes generated from modules/c-api/spec/IndexBuffer.toml.
#include "generated/IndexBuffer.inc"
