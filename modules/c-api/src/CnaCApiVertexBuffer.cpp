// SPDX-License-Identifier: MS-PL

#include "CNA/C/vertex_resources.h"
#include "CNA/GraphicsCapability.hpp"
#include "CnaCApiDetail.hpp"
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

using CNA::C::Detail::AddOwnedGraphicsResource;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedGraphicsResource;
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

[[nodiscard]] Vector2 ToNative(const CNA_Vector2 value) noexcept
{
    return Vector2(value.x, value.y);
}

[[nodiscard]] Vector3 ToNative(const CNA_Vector3 value) noexcept
{
    return Vector3(value.x, value.y, value.z);
}

[[nodiscard]] Vector4 ToNative(const CNA_Vector4 value) noexcept
{
    return Vector4(value.x, value.y, value.z, value.w);
}

[[nodiscard]] Color ToNative(const CNA_Color value)
{
    return Color(value.r, value.g, value.b, value.a);
}

[[nodiscard]] CNA_Vector2 ToC(const Vector2& value) noexcept
{
    return CNA_Vector2{value.X, value.Y};
}

[[nodiscard]] CNA_Vector3 ToC(const Vector3& value) noexcept
{
    return CNA_Vector3{value.X, value.Y, value.Z};
}

[[nodiscard]] CNA_Vector4 ToC(const Vector4& value) noexcept
{
    return CNA_Vector4{value.X, value.Y, value.Z, value.W};
}

[[nodiscard]] CNA_Color ToC(const Color& value) noexcept
{
    return CNA_Color{
        value.getRProperty(), value.getGProperty(),
        value.getBProperty(), value.getAProperty()};
}

[[nodiscard]] VertexPositionColor ToNative(const CNA_VertexPositionColor& value)
{
    return VertexPositionColor(ToNative(value.position), ToNative(value.color));
}

[[nodiscard]] CNA_VertexPositionColor ToC(const VertexPositionColor& value) noexcept
{
    return CNA_VertexPositionColor{ToC(value.Position), ToC(value.Color)};
}

[[nodiscard]] VertexPositionColorTexture ToNative(
    const CNA_VertexPositionColorTexture& value)
{
    return VertexPositionColorTexture(
        ToNative(value.position), ToNative(value.color), ToNative(value.texture_coordinate));
}

[[nodiscard]] CNA_VertexPositionColorTexture ToC(
    const VertexPositionColorTexture& value) noexcept
{
    return CNA_VertexPositionColorTexture{
        ToC(value.Position), ToC(value.Color), ToC(value.TextureCoordinate)};
}

[[nodiscard]] VertexPositionNormalTangentTexture ToNative(
    const CNA_VertexPositionNormalTangentTexture& value)
{
    return VertexPositionNormalTangentTexture(
        ToNative(value.position), ToNative(value.normal), ToNative(value.tangent),
        ToNative(value.texture_coordinate));
}

[[nodiscard]] CNA_VertexPositionNormalTangentTexture ToC(
    const VertexPositionNormalTangentTexture& value) noexcept
{
    return CNA_VertexPositionNormalTangentTexture{
        ToC(value.Position), ToC(value.Normal), ToC(value.Tangent),
        ToC(value.TextureCoordinate)};
}

[[nodiscard]] VertexPositionNormalTangentTextureSkinned ToNative(
    const CNA_VertexPositionNormalTangentTextureSkinned& value)
{
    return VertexPositionNormalTangentTextureSkinned(
        ToNative(value.position), ToNative(value.normal), ToNative(value.tangent),
        ToNative(value.texture_coordinate), ToNative(value.blend_weight),
        std::array<uint8_t, 4>{
            value.blend_indices[0], value.blend_indices[1],
            value.blend_indices[2], value.blend_indices[3]});
}

[[nodiscard]] CNA_VertexPositionNormalTangentTextureSkinned ToC(
    const VertexPositionNormalTangentTextureSkinned& value) noexcept
{
    return CNA_VertexPositionNormalTangentTextureSkinned{
        ToC(value.Position), ToC(value.Normal), ToC(value.Tangent),
        ToC(value.TextureCoordinate), ToC(value.BlendWeight),
        {value.BlendIndices[0], value.BlendIndices[1],
         value.BlendIndices[2], value.BlendIndices[3]}};
}

[[nodiscard]] VertexPositionNormalTexture ToNative(
    const CNA_VertexPositionNormalTexture& value)
{
    return VertexPositionNormalTexture(
        ToNative(value.position), ToNative(value.normal), ToNative(value.texture_coordinate));
}

[[nodiscard]] CNA_VertexPositionNormalTexture ToC(
    const VertexPositionNormalTexture& value) noexcept
{
    return CNA_VertexPositionNormalTexture{
        ToC(value.Position), ToC(value.Normal), ToC(value.TextureCoordinate)};
}

[[nodiscard]] VertexPositionNormalTextureSkinned ToNative(
    const CNA_VertexPositionNormalTextureSkinned& value)
{
    return VertexPositionNormalTextureSkinned(
        ToNative(value.position), ToNative(value.normal), ToNative(value.texture_coordinate),
        ToNative(value.blend_weight),
        std::array<uint8_t, 4>{
            value.blend_indices[0], value.blend_indices[1],
            value.blend_indices[2], value.blend_indices[3]});
}

[[nodiscard]] CNA_VertexPositionNormalTextureSkinned ToC(
    const VertexPositionNormalTextureSkinned& value) noexcept
{
    return CNA_VertexPositionNormalTextureSkinned{
        ToC(value.Position), ToC(value.Normal), ToC(value.TextureCoordinate),
        ToC(value.BlendWeight),
        {value.BlendIndices[0], value.BlendIndices[1],
         value.BlendIndices[2], value.BlendIndices[3]}};
}

[[nodiscard]] VertexPositionTexture ToNative(const CNA_VertexPositionTexture& value)
{
    return VertexPositionTexture(ToNative(value.position), ToNative(value.texture_coordinate));
}

[[nodiscard]] CNA_VertexPositionTexture ToC(const VertexPositionTexture& value) noexcept
{
    return CNA_VertexPositionTexture{ToC(value.Position), ToC(value.TextureCoordinate)};
}

[[nodiscard]] CNA_VertexElement ToC(const VertexElement& value) noexcept
{
    return CNA_VertexElement{
        value.getOffsetProperty(),
        static_cast<CNA_VertexElementFormat>(value.getVertexElementFormatProperty()),
        static_cast<CNA_VertexElementUsage>(value.getVertexElementUsageProperty()),
        value.getUsageIndexProperty()};
}

[[nodiscard]] CNA_Result GetBuffer(
    const CNA_VertexBufferHandle handle,
    std::shared_ptr<VertexBufferResource>* const outBuffer)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::VertexBuffer, outBuffer);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result, ErrorCategoryForResult(result),
        "The VertexBuffer handle is invalid for this call.");
}

[[nodiscard]] CNA_Result ValidateUsable(const VertexBufferResource& resource)
{
    if (resource.value->getIsDisposedProperty()) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "The VertexBuffer has been disposed.");
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
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The caller array cannot hold the complete vertex transfer window.");
    }
    if (capacity != 0U && data == nullptr) {
        return InvalidArgument("The vertex-buffer caller array is null.");
    }
    if (transfer->element_count >
        static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The vertex transfer count exceeds the native Int32 range.");
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
        return Fail(
            CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
            "The vertex source byte offset is too large.");
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
        return Fail(
            CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
            "The vertex destination byte offset is too large.");
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

template<typename TCallable>
[[nodiscard]] CNA_Result CopyString(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount,
    TCallable&& callable)
{
    if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The vertex-buffer string output is invalid.");
    }
    const std::string value = std::forward<TCallable>(callable)();
    *outByteCount = static_cast<uint64_t>(value.size());
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination cannot hold the complete vertex-buffer type name.");
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

} // namespace

CNA_Result cna_vertex_buffer_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_VertexBufferCreateInfo* const createInfo,
    CNA_VertexBufferHandle* const outVertexBuffer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVertexBuffer == nullptr) {
            return InvalidArgument("The VertexBuffer output handle is null.");
        }
        *outVertexBuffer = CNA_INVALID_HANDLE;
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_VertexBufferCreateInfo) ||
            createInfo->struct_version != StructureVersion ||
            createInfo->vertex_count < 0 ||
            createInfo->buffer_usage > CNA_BUFFER_USAGE_WRITE_ONLY ||
            !IsBool(createInfo->dynamic) ||
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
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "The selected graphics backend does not support vertex buffers.");
        }

        std::shared_ptr<VertexBuffer> native;
        if (createInfo->vertex_declaration == CNA_INVALID_HANDLE) {
            native = std::make_shared<VertexBuffer>(
                *graphicsDevice->value, createInfo->vertex_count);
        } else {
            std::shared_ptr<VertexDeclaration> declaration;
            const CNA_Result declarationResult = GetRuntimeHandles().Get(
                createInfo->vertex_declaration,
                ObjectKind::VertexDeclaration,
                &declaration);
            if (declarationResult != CNA_RESULT_SUCCESS) {
                return Fail(
                    declarationResult,
                    ErrorCategoryForResult(declarationResult),
                    "The VertexDeclaration handle is invalid for VertexBuffer creation.");
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
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::VertexBuffer, resource, outVertexBuffer);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned VertexBuffer handle could not be created.");
        }
        AddOwnedGraphicsResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_buffer_destroy(const CNA_VertexBufferHandle vertexBufferHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VertexBufferResource> buffer;
        if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (buffer->activeModelReferenceCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The VertexBuffer is retained by a ModelMeshPart.");
        }
        if (const CNA_Result result = GetRuntimeHandles().Release(vertexBufferHandle);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned VertexBuffer handle could not be released.");
        }
        RemoveOwnedGraphicsResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_buffer_get_info(
    const CNA_VertexBufferHandle vertexBufferHandle,
    CNA_VertexBufferInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr ||
            outInfo->struct_size < sizeof(CNA_VertexBufferInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidArgument("The VertexBuffer info output structure is invalid.");
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
            buffer->dynamic ? CNA_TRUE : CNA_FALSE,
            CNA_FALSE,
            buffer->value->HasRenderer() ? CNA_TRUE : CNA_FALSE,
            0U,
            declaration.getVertexStrideProperty(),
            static_cast<uint64_t>(declaration.GetVertexElements().size())};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_buffer_copy_declaration_elements(
    const CNA_VertexBufferHandle vertexBufferHandle,
    CNA_VertexElement* const destination,
    const uint64_t capacity,
    uint64_t* const outElementCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outElementCount == nullptr || (destination == nullptr && capacity != 0U)) {
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
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the complete VertexDeclaration.");
        }
        for (std::size_t index = 0U; index < elements.size(); ++index) {
            destination[index] = ToC(elements[index]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_buffer_get_type_name_byte_count(
    const CNA_VertexBufferHandle vertexBufferHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The VertexBuffer type-name byte-count output is null.");
        }
        std::shared_ptr<VertexBufferResource> buffer;
        if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(buffer->value->GetTypeName().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_buffer_copy_type_name(
    const CNA_VertexBufferHandle vertexBufferHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VertexBufferResource> buffer;
        if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyString(destination, capacity, outByteCount, [&]() {
            return buffer->value->GetTypeName();
        });
    });
}

CNA_Result cna_vertex_buffer_set_data(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const CNA_VertexBufferTransfer* const transfer,
    const void* const data,
    const uint64_t capacity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "The selected VertexBuffer kind/type has no SetDataOptions overload.");
        }
        if (view.count >
            static_cast<uint64_t>(buffer->value->getVertexCountProperty())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The upload exceeds the VertexBuffer's logical capacity.");
        }
        return DispatchSet(*buffer, view, data);
    });
}

CNA_Result cna_vertex_buffer_get_data(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const CNA_VertexBufferTransfer* const transfer,
    void* const destination,
    const uint64_t capacity,
    uint64_t* const outElementCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outElementCount == nullptr) {
            return InvalidArgument("The VertexBuffer required-element output is null.");
        }
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
            if (transfer != nullptr &&
                transfer->struct_size >= sizeof(CNA_VertexBufferTransfer) &&
                transfer->struct_version == StructureVersion) {
                *outElementCount = transfer->element_count;
            }
            return result;
        }
        *outElementCount = view.count;
        return DispatchGet(*buffer, view, destination);
    });
}

CNA_Result cna_vertex_buffer_set_data_raw(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const void* const data,
    const uint64_t dataByteCount,
    const uint64_t vertexCount,
    const uint32_t vertexStride)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The raw VertexBuffer byte count overflows UInt64.");
        }
        const uint64_t requiredBytes = vertexCount * vertexStride;
        if (dataByteCount < requiredBytes ||
            (data == nullptr && (dataByteCount != 0U || vertexCount != 0U))) {
            return InvalidArgument("The raw VertexBuffer source buffer is too small or null.");
        }
        buffer->value->SetDataRaw(
            data, static_cast<int>(vertexCount), static_cast<int>(vertexStride));
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

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
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The raw VertexBuffer byte count overflows UInt64.");
    }
    *outRequiredBytes = vertexCount * vertexStride;
    if (accessibleBytes < *outRequiredBytes ||
        (buffer == nullptr && (accessibleBytes != 0U || vertexCount != 0U))) {
        return InvalidArgument("The raw VertexBuffer window buffer is too small or null.");
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

} // namespace

CNA_Result cna_vertex_buffer_set_data_raw_at(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const uint64_t bufferOffsetInBytes,
    const void* const data,
    const uint64_t dataByteCount,
    const uint64_t vertexCount,
    const uint32_t vertexStride)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_vertex_buffer_get_data_raw(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const uint64_t bufferOffsetInBytes,
    void* const destination,
    const uint64_t destinationByteCount,
    const uint64_t vertexCount,
    const uint32_t vertexStride)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_vertex_buffer_subscribe_content_lost(
    const CNA_VertexBufferHandle vertexBufferHandle,
    const CNA_VertexBufferContentLostCallback callback,
    void* const context,
    CNA_VertexBufferEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidArgument("The ContentLost registration output handle is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidArgument("The ContentLost callback is null.");
        }
        std::shared_ptr<VertexBufferResource> buffer;
        if (const CNA_Result result = GetBuffer(vertexBufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!buffer->dynamic) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "ContentLost exists only on DynamicVertexBuffer.");
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
    });
}

CNA_Result cna_vertex_buffer_unsubscribe_content_lost(
    const CNA_VertexBufferEventRegistrationHandle registrationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ContentLostRegistration> registration;
        const CNA_Result getResult = GetRuntimeHandles().Get(
            registrationHandle,
            ObjectKind::VertexBufferEventRegistration,
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
