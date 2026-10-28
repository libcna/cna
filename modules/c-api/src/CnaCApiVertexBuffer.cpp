// SPDX-License-Identifier: MS-PL

#include "CNA/C/vertex_resources.h"
#include "CNA/GraphicsCapability.hpp"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
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
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using CNA::C::Detail::ValidateVersionedStructure;
using CNA::C::Detail::VertexBufferResource;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTextureSkinned;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTextureSkinned;
using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;

[[nodiscard]] CNA_Result GetBuffer(
    const CNA_VertexBufferHandle handle,
    std::shared_ptr<VertexBufferResource>* const outBuffer)
{
    return LookupHandle(handle, ObjectKind::VertexBuffer, outBuffer);
}

[[nodiscard]] CNA_Result ValidateUsable(const VertexBufferResource& resource)
{
    if (resource.value->getIsDisposedProperty()) {
        return InvalidState("The VertexBuffer has been disposed.");
    }
    return CNA_RESULT_SUCCESS;
}

struct TransferView final {
    CNA_VertexType type;
    SetDataOptions options;
    uint64_t start;
    uint64_t count;
};

[[nodiscard]] CNA_Result ValidateTransfer(
    const CNA_VertexBufferTransfer* const transfer,
    const uint64_t capacity,
    const void* const data,
    const bool readback,
    TransferView* const outView)
{
    if (transfer == nullptr || outView == nullptr ||
        transfer->struct_size < sizeof(CNA_VertexBufferTransfer) ||
        transfer->struct_version != StructureVersion ||
        transfer->vertex_type > CNA_VERTEX_TYPE_POSITION_TEXTURE ||
        transfer->options > CNA_SET_DATA_NO_OVERWRITE) {
        return InvalidArgument("The vertex-buffer transfer descriptor is invalid.");
    }
    if (readback && transfer->options != CNA_SET_DATA_NONE) {
        return InvalidArgument("Vertex-buffer readback requires CNA_SET_DATA_NONE.");
    }
    if (transfer->start_index > capacity ||
        transfer->element_count > capacity - transfer->start_index) {
        return BufferTooSmall("The caller array cannot hold the complete vertex transfer window.");
    }
    if (capacity != 0U && data == nullptr) {
        return InvalidArgument("The vertex-buffer caller array is null.");
    }
    if (transfer->element_count >
        static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return Overflow("The vertex transfer count exceeds the native Int32 range.");
    }
    *outView = TransferView{
        transfer->vertex_type,
        static_cast<SetDataOptions>(transfer->options),
        transfer->start_index,
        transfer->element_count};
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] bool SupportsDynamicOptions(const CNA_VertexType type) noexcept
{
    return type == CNA_VERTEX_TYPE_POSITION_COLOR ||
        type == CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE ||
        type == CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE ||
        type == CNA_VERTEX_TYPE_POSITION_TEXTURE;
}

template<typename TC, typename TNative>
[[nodiscard]] CNA_Result SetTyped(
    const VertexBufferResource& resource,
    const TransferView& transfer,
    const void* const source)
{
    if (transfer.count == 0U) {
        return CNA_RESULT_SUCCESS;
    }
    if (transfer.start >
        static_cast<uint64_t>(std::numeric_limits<std::size_t>::max() / sizeof(TC))) {
        return Overflow("The vertex source byte offset is too large.");
    }
    const auto* const bytes = static_cast<const uint8_t*>(source) +
        static_cast<std::size_t>(transfer.start) * sizeof(TC);
    std::vector<TNative> converted;
    converted.reserve(static_cast<std::size_t>(transfer.count));
    for (uint64_t index = 0U; index < transfer.count; ++index) {
        TC value{};
        std::memcpy(&value, bytes + static_cast<std::size_t>(index) * sizeof(TC), sizeof(TC));
        converted.emplace_back(ToNative(value));
    }

    const int count = static_cast<int>(transfer.count);
    if constexpr (
        std::is_same_v<TNative, VertexPositionColor> ||
        std::is_same_v<TNative, VertexPositionColorTexture> ||
        std::is_same_v<TNative, VertexPositionNormalTexture> ||
        std::is_same_v<TNative, VertexPositionTexture>) {
        if (resource.dynamic) {
            auto dynamic = std::static_pointer_cast<DynamicVertexBuffer>(resource.value);
            dynamic->SetData(converted.data(), 0, count, transfer.options);
        } else {
            resource.value->SetData(converted.data(), count);
        }
    } else {
        resource.value->SetData(converted.data(), count);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result DispatchSet(
    const VertexBufferResource& resource,
    const TransferView& transfer,
    const void* const source)
{
    switch (transfer.type) {
        case CNA_VERTEX_TYPE_POSITION_COLOR:
            return SetTyped<CNA_VertexPositionColor, VertexPositionColor>(
                resource, transfer, source);
        case CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE:
            return SetTyped<CNA_VertexPositionColorTexture, VertexPositionColorTexture>(
                resource, transfer, source);
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE:
            return SetTyped<
                CNA_VertexPositionNormalTangentTexture,
                VertexPositionNormalTangentTexture>(resource, transfer, source);
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED:
            return SetTyped<
                CNA_VertexPositionNormalTangentTextureSkinned,
                VertexPositionNormalTangentTextureSkinned>(resource, transfer, source);
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE:
            return SetTyped<CNA_VertexPositionNormalTexture, VertexPositionNormalTexture>(
                resource, transfer, source);
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED:
            return SetTyped<
                CNA_VertexPositionNormalTextureSkinned,
                VertexPositionNormalTextureSkinned>(resource, transfer, source);
        default:
            return SetTyped<CNA_VertexPositionTexture, VertexPositionTexture>(
                resource, transfer, source);
    }
}

template<typename TC, typename TNative>
[[nodiscard]] CNA_Result GetTyped(
    const VertexBufferResource& resource,
    const TransferView& transfer,
    void* const destination)
{
    if (transfer.count == 0U) {
        return CNA_RESULT_SUCCESS;
    }
    if (transfer.start >
        static_cast<uint64_t>(std::numeric_limits<std::size_t>::max() / sizeof(TC))) {
        return Overflow("The vertex destination byte offset is too large.");
    }
    std::vector<TNative> native;
    native.reserve(static_cast<std::size_t>(transfer.count));
    for (uint64_t index = 0U; index < transfer.count; ++index) {
        static_cast<void>(index);
        native.emplace_back(ToNative(TC{}));
    }
    resource.value->GetData(native.data(), static_cast<int>(transfer.count));
    std::vector<TC> converted;
    converted.reserve(native.size());
    for (const TNative& value : native) {
        converted.emplace_back(ToC(value));
    }
    auto* const bytes = static_cast<uint8_t*>(destination) +
        static_cast<std::size_t>(transfer.start) * sizeof(TC);
    std::memcpy(bytes, converted.data(), converted.size() * sizeof(TC));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result DispatchGet(
    const VertexBufferResource& resource,
    const TransferView& transfer,
    void* const destination)
{
    switch (transfer.type) {
        case CNA_VERTEX_TYPE_POSITION_COLOR:
            return GetTyped<CNA_VertexPositionColor, VertexPositionColor>(
                resource, transfer, destination);
        case CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE:
            return GetTyped<CNA_VertexPositionColorTexture, VertexPositionColorTexture>(
                resource, transfer, destination);
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE:
            return GetTyped<
                CNA_VertexPositionNormalTangentTexture,
                VertexPositionNormalTangentTexture>(resource, transfer, destination);
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED:
            return GetTyped<
                CNA_VertexPositionNormalTangentTextureSkinned,
                VertexPositionNormalTangentTextureSkinned>(resource, transfer, destination);
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE:
            return GetTyped<CNA_VertexPositionNormalTexture, VertexPositionNormalTexture>(
                resource, transfer, destination);
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED:
            return GetTyped<
                CNA_VertexPositionNormalTextureSkinned,
                VertexPositionNormalTextureSkinned>(resource, transfer, destination);
        default:
            return GetTyped<CNA_VertexPositionTexture, VertexPositionTexture>(
                resource, transfer, destination);
    }
}

class ContentLostRegistration final {
public:
    using Token = System::EventHandler<System::EventArgs>::Token;

    ContentLostRegistration(
        std::weak_ptr<DynamicVertexBuffer> buffer,
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
        if (const std::shared_ptr<DynamicVertexBuffer> buffer = buffer_.lock()) {
            buffer->ContentLost.Remove(token_);
        }
    }

private:
    std::weak_ptr<DynamicVertexBuffer> buffer_;
    Token token_;
    bool subscribed_ = true;
};

[[nodiscard]] CNA_Result VertexBufferCreate(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_VertexBufferCreateInfo* const createInfo,
    CNA_VertexBufferHandle* const outVertexBuffer)
{
    if (!IsVersionedStructure(createInfo) ||
        createInfo->vertex_count <= 0 ||
        createInfo->buffer_usage > CNA_BUFFER_USAGE_WRITE_ONLY ||
        !IsCanonicalBool(createInfo->dynamic) ||
        !IsZero(createInfo->reserved, 7U)) {
        return InvalidArgument("The VertexBuffer creation configuration is invalid.");
    }
    const bool dynamic = createInfo->dynamic == CNA_TRUE;
    if (createInfo->vertex_declaration == CNA_INVALID_HANDLE &&
        (dynamic || createInfo->buffer_usage != CNA_BUFFER_USAGE_NONE)) {
        return InvalidArgument(
            "The empty-declaration VertexBuffer extension must be static with BufferUsage.None.");
    }

    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result = GetBorrowedGraphicsDevice(
            graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!graphicsDevice->value->SupportsCapability(CNA::GraphicsCapability::ThreeD)) {
        return NotSupported("The selected graphics renderer does not support vertex buffers.");
    }

    std::shared_ptr<VertexBuffer> native;
    if (createInfo->vertex_declaration == CNA_INVALID_HANDLE) {
        native = std::make_shared<VertexBuffer>(
            *graphicsDevice->value, createInfo->vertex_count);
    } else {
        std::shared_ptr<VertexDeclaration> declaration;
        const CNA_Result declarationResult = LookupHandle(
            createInfo->vertex_declaration, ObjectKind::VertexDeclaration, &declaration);
        if (declarationResult != CNA_RESULT_SUCCESS) {
            return declarationResult;
        }
        const auto usage = static_cast<BufferUsage>(createInfo->buffer_usage);
        if (dynamic) {
            native = std::make_shared<DynamicVertexBuffer>(
                *graphicsDevice->value,
                *declaration,
                createInfo->vertex_count,
                usage);
        } else {
            native = std::make_shared<VertexBuffer>(
                *graphicsDevice->value,
                *declaration,
                createInfo->vertex_count,
                usage);
        }
    }

    const auto resource = std::make_shared<VertexBufferResource>(
        VertexBufferResource{std::move(native), graphicsDevice->parentGame, dynamic});
    if (const CNA_Result result =
            CreateHandle(ObjectKind::VertexBuffer, resource, outVertexBuffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result VertexBufferDestroy(const CNA_VertexBufferHandle vertexBufferHandle)
{
    std::shared_ptr<VertexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (buffer->activeModelReferenceCount != 0U) {
        return InvalidState("The VertexBuffer is retained by a ModelMeshPart.");
    }
    if (const CNA_Result result = ReleaseHandle(vertexBufferHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    RemoveOwnedGraphicsResourceFor(buffer->parentGame);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result VertexBufferGetInfo(
    const CNA_VertexBufferHandle vertexBufferHandle,
    CNA_VertexBufferInfo* const outInfo)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outInfo, "The VertexBuffer info output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<VertexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& declaration = buffer->value->getVertexDeclarationProperty();
    *outInfo = CNA_VertexBufferInfo{
        sizeof(CNA_VertexBufferInfo),
        StructureVersion,
        buffer->value->getVertexCountProperty(),
        static_cast<CNA_BufferUsage>(buffer->value->getBufferUsageProperty()),
        ToCBool(buffer->dynamic),
        CNA_FALSE,
        ToCBool(buffer->value->HasRenderer()),
        0U,
        declaration.getVertexStrideProperty(),
        static_cast<uint64_t>(declaration.GetVertexElements().size())};
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result VertexBufferCopyDeclarationElements(
    const CNA_VertexBufferHandle vertexBufferHandle,
    CNA_VertexElement* const destination,
    const uint64_t capacity,
    uint64_t* const outElementCount)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The VertexBuffer declaration output is invalid.");
    }
    std::shared_ptr<VertexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& elements =
        buffer->value->getVertexDeclarationProperty().GetVertexElements();
    *outElementCount = static_cast<uint64_t>(elements.size());
    if (capacity < elements.size()) {
        return BufferTooSmall("The destination cannot hold the complete VertexDeclaration.");
    }
    for (std::size_t index = 0U; index < elements.size(); ++index) {
        destination[index] = ToC(elements[index]);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result VertexBufferSetData(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const CNA_VertexBufferTransfer* const transfer,
    const void* const data,
    const uint64_t capacity)
{
    std::shared_ptr<VertexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
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
    if (view.options != SetDataOptions::None &&
        (!buffer->dynamic || !SupportsDynamicOptions(view.type))) {
        return NotSupported(
            "The selected VertexBuffer kind/type has no SetDataOptions overload.");
    }
    if (view.count >
        static_cast<uint64_t>(buffer->value->getVertexCountProperty())) {
        return OutOfRange("The upload exceeds the VertexBuffer's logical capacity.");
    }
    return DispatchSet(*buffer, view, data);
}

[[nodiscard]] CNA_Result VertexBufferGetData(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const CNA_VertexBufferTransfer* const transfer,
    void* const destination,
    const uint64_t capacity,
    uint64_t* const outElementCount)
{
    std::shared_ptr<VertexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
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
    return DispatchGet(*buffer, view, destination);
}

[[nodiscard]] CNA_Result VertexBufferSetDataRaw(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const void* const data,
    const uint64_t dataByteCount,
    const uint64_t vertexCount,
    const uint32_t vertexStride)
{
    std::shared_ptr<VertexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateUsable(*buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (vertexStride == 0U ||
        vertexStride > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        vertexCount > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return InvalidArgument("The raw VertexBuffer count or stride is invalid.");
    }
    if (vertexCount > std::numeric_limits<uint64_t>::max() / vertexStride) {
        return Overflow("The raw VertexBuffer byte count overflows UInt64.");
    }
    const uint64_t requiredBytes = vertexCount * vertexStride;
    if (dataByteCount < requiredBytes ||
        (data == nullptr && (dataByteCount != 0U || vertexCount != 0U))) {
        return InvalidArgument("The raw VertexBuffer source buffer is too small or null.");
    }
    buffer->value->SetDataRaw(
        data, static_cast<int>(vertexCount), static_cast<int>(vertexStride));
    return CNA_RESULT_SUCCESS;
}

/// Validates the count/stride/byte-count triple every raw window route repeats.
[[nodiscard]] CNA_Result ValidateRawWindow(
    const uint64_t vertexCount,
    const uint32_t vertexStride,
    const uint64_t accessibleBytes,
    const void* const buffer,
    uint64_t* const outRequiredBytes)
{
    if (vertexStride == 0U ||
        vertexStride > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        vertexCount > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return InvalidArgument("The raw VertexBuffer count or stride is invalid.");
    }
    if (vertexCount > std::numeric_limits<uint64_t>::max() / vertexStride) {
        return Overflow("The raw VertexBuffer byte count overflows UInt64.");
    }
    *outRequiredBytes = vertexCount * vertexStride;
    if (accessibleBytes < *outRequiredBytes ||
        (buffer == nullptr && (accessibleBytes != 0U || vertexCount != 0U))) {
        return InvalidArgument("The raw VertexBuffer window buffer is too small or null.");
    }
    return CNA_RESULT_SUCCESS;
}

/*
 * CBIND-104: the options-carrying raw upload.
 *
 * `SetDataRawWithOptions` and `SetDataRawAtWithOptions` are protected on `VertexBuffer`; their only
 * public door is `DynamicVertexBuffer`'s `SetData<TVertex>` templates, which is also where the
 * canonical API puts them -- a streaming hint means nothing on a static buffer. So this refuses a
 * static one rather than inventing a reachable path the canonical layer does not have.
 *
 * Both routes call `DynamicVertexBuffer`'s raw CNAEXT forwarders rather than a `SetData<TVertex>`
 * template. Instantiating the windowed template with a byte used to be equivalent, and
 * SOFTWARE-249 ended that: it restored XNA's generic semantics, where the source is tightly packed
 * `sizeof(T)` elements written into buffer locations separated by `vertexStride`. With `TVertex` a
 * byte that copies `vertexCount` single bytes at `vertexStride` spacing instead of the
 * `vertexCount * vertexStride` contiguous bytes this ABI documents, and it also loses the raw
 * route's vertex-boundary check and its zero-count no-op (see
 * C API smoke stability CSS-4). The raw routes are what the header already promises.
 */
[[nodiscard]] CNA_Result RequireDynamicForOptions(
    const VertexBufferResource& resource,
    Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer** const outDynamic)
{
    if (!resource.dynamic) {
        return NotSupported("A static VertexBuffer has no SetDataOptions overload.");
    }
    auto* const dynamic =
        dynamic_cast<Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer*>(
            resource.value.get());
    if (dynamic == nullptr) {
        return InvalidState(
            "The VertexBuffer handle is marked dynamic but does not hold a DynamicVertexBuffer.");
    }
    *outDynamic = dynamic;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateSetDataOptions(const CNA_SetDataOptions options)
{
    if (options > CNA_SET_DATA_NO_OVERWRITE) {
        return InvalidArgument("The SetDataOptions value is not a defined option.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateBufferOffset(const uint64_t offsetInBytes)
{
    if (offsetInBytes > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return InvalidArgument("The VertexBuffer destination offset is outside the native range.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result VertexBufferSetDataRawAt(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const uint64_t bufferOffsetInBytes,
    const void* const data,
    const uint64_t dataByteCount,
    const uint64_t vertexCount,
    const uint32_t vertexStride)
{
    std::shared_ptr<VertexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateUsable(*buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateBufferOffset(bufferOffsetInBytes);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    uint64_t requiredBytes = 0U;
    if (const CNA_Result result = ValidateRawWindow(
            vertexCount, vertexStride, dataByteCount, data, &requiredBytes);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    buffer->value->SetDataRawAtEXT(
        static_cast<int>(bufferOffsetInBytes),
        data,
        static_cast<int>(vertexCount),
        static_cast<int>(vertexStride));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result VertexBufferGetDataRaw(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const uint64_t bufferOffsetInBytes,
    void* const destination,
    const uint64_t destinationByteCount,
    const uint64_t vertexCount,
    const uint32_t vertexStride)
{
    std::shared_ptr<VertexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateUsable(*buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateBufferOffset(bufferOffsetInBytes);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    uint64_t requiredBytes = 0U;
    if (const CNA_Result result = ValidateRawWindow(
            vertexCount, vertexStride, destinationByteCount, destination, &requiredBytes);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    buffer->value->GetDataRawEXT(
        static_cast<int>(bufferOffsetInBytes),
        destination,
        static_cast<int>(vertexCount),
        static_cast<int>(vertexStride));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result VertexBufferSubscribeContentLost(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const CNA_VertexBufferContentLostCallback callback,
    void* const context,
    CNA_VertexBufferEventRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The ContentLost callback is null.");
    }
    std::shared_ptr<VertexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!buffer->dynamic) {
        return NotSupported("ContentLost exists only on DynamicVertexBuffer.");
    }
    const auto dynamic = std::static_pointer_cast<DynamicVertexBuffer>(buffer->value);
    const auto token = dynamic->ContentLost.Add(
        [vertexBufferHandle, callback, context](System::Object*, const System::EventArgs&) {
            callback(vertexBufferHandle, context);
        });
    const auto registration = std::make_shared<ContentLostRegistration>(dynamic, token);
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::VertexBufferEventRegistration,
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

[[nodiscard]] CNA_Result VertexBufferUnsubscribeContentLost(
    const CNA_VertexBufferEventRegistrationHandle registrationHandle)
{
    std::shared_ptr<ContentLostRegistration> registration;
    const CNA_Result getResult = LookupHandle(
        registrationHandle, ObjectKind::VertexBufferEventRegistration, &registration);
    if (getResult != CNA_RESULT_SUCCESS) {
        return getResult;
    }
    registration->Unsubscribe();
    return ReleaseHandle(registrationHandle);
}

[[nodiscard]] CNA_Result VertexBufferSetDataRawWithOptions(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const void* const data,
    const uint64_t dataByteCount,
    const uint64_t vertexCount,
    const uint32_t vertexStride,
    const CNA_SetDataOptions options)
{
    std::shared_ptr<VertexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateUsable(*buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateSetDataOptions(options);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer* dynamic = nullptr;
    if (const CNA_Result result = RequireDynamicForOptions(*buffer, &dynamic);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    uint64_t requiredBytes = 0U;
    if (const CNA_Result result = ValidateRawWindow(
            vertexCount, vertexStride, dataByteCount, data, &requiredBytes);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    dynamic->SetDataRawWithOptionsEXT(
        data,
        static_cast<int>(vertexCount),
        static_cast<int>(vertexStride),
        static_cast<SetDataOptions>(options));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result VertexBufferSetDataRawAtWithOptions(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const uint64_t bufferOffsetInBytes,
    const void* const data,
    const uint64_t dataByteCount,
    const uint64_t vertexCount,
    const uint32_t vertexStride,
    const CNA_SetDataOptions options)
{
    std::shared_ptr<VertexBufferResource> buffer;
    if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateUsable(*buffer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateSetDataOptions(options);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateBufferOffset(bufferOffsetInBytes);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer* dynamic = nullptr;
    if (const CNA_Result result = RequireDynamicForOptions(*buffer, &dynamic);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    uint64_t requiredBytes = 0U;
    if (const CNA_Result result = ValidateRawWindow(
            vertexCount, vertexStride, dataByteCount, data, &requiredBytes);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    dynamic->SetDataRawAtWithOptionsEXT(
        static_cast<int>(bufferOffsetInBytes),
        data,
        static_cast<int>(vertexCount),
        static_cast<int>(vertexStride),
        static_cast<SetDataOptions>(options));
    return CNA_RESULT_SUCCESS;
}

} // namespace

// The routes generated from modules/c-api/spec/VertexBuffer.toml.
#include "generated/VertexBuffer.inc"
