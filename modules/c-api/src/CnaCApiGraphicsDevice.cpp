// SPDX-License-Identifier: MS-PL

#include "CNA/C/graphics_device.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Unsupported3DGraphicsCallBehavior.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceStatus.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/EventArgs.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CopyFormattedText;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::EmbeddedNul;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::IsVersionedStructure;
using CNA::C::Detail::NativeOrdinal;
using CNA::C::Detail::NotSupported;
using CNA::C::Detail::OutOfRange;
using CNA::C::Detail::Overflow;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ReleaseHandle;

namespace NativeGraphics = Microsoft::Xna::Framework::Graphics;

static_assert(
    NativeOrdinal(NativeGraphics::ClearOptions::Target) == CNA_CLEAR_OPTION_TARGET &&
    NativeOrdinal(NativeGraphics::ClearOptions::DepthBuffer) == CNA_CLEAR_OPTION_DEPTH_BUFFER &&
    NativeOrdinal(NativeGraphics::ClearOptions::Stencil) == CNA_CLEAR_OPTION_STENCIL);
static_assert(
    NativeOrdinal(NativeGraphics::GraphicsDeviceStatus::Normal) ==
        CNA_GRAPHICS_DEVICE_STATUS_NORMAL &&
    NativeOrdinal(NativeGraphics::GraphicsDeviceStatus::Lost) ==
        CNA_GRAPHICS_DEVICE_STATUS_LOST &&
    NativeOrdinal(NativeGraphics::GraphicsDeviceStatus::NotReset) ==
        CNA_GRAPHICS_DEVICE_STATUS_NOT_RESET);
static_assert(
    NativeOrdinal(CNA::Unsupported3DGraphicsCallBehavior::Throw) ==
        CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_THROW &&
    NativeOrdinal(CNA::Unsupported3DGraphicsCallBehavior::WarnAndStub) ==
        CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_WARN_AND_STUB);
static_assert(
    NativeGraphics::TextureCollection::MaxTextures == CNA_TEXTURE_COLLECTION_MAX_TEXTURES);

using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::EffectResource;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::IndexBufferResource;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::OcclusionQueryResource;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::TextureResourceView;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using CNA::C::Detail::ValidateVersionedStructure;
using CNA::C::Detail::VertexBufferResource;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::OcclusionQuery;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
using Microsoft::Xna::Framework::Graphics::ResourceCreatedEventArgs;
using Microsoft::Xna::Framework::Graphics::Texture;
using Microsoft::Xna::Framework::Graphics::TextureCollection;
using Microsoft::Xna::Framework::Graphics::ResourceDestroyedEventArgs;
using Microsoft::Xna::Framework::Graphics::Viewport;

template<typename TValue, typename TCallable>
[[nodiscard]] CNA_Result DeviceQuery(
    const CNA_Handle graphicsDeviceHandle,
    TValue* const output,
    TCallable&& callable)
{
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const TValue value = static_cast<TValue>(
        std::forward<TCallable>(callable)(*graphicsDevice->value));
    *output = value;
    return CNA_RESULT_SUCCESS;
}

template<typename TCallable>
[[nodiscard]] CNA_Result DeviceCommand(
    const CNA_Handle graphicsDeviceHandle,
    TCallable&& callable)
{
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::forward<TCallable>(callable)(*graphicsDevice->value);
    return CNA_RESULT_SUCCESS;
}

// A draw route that a backend without 3D support cannot serve is reported as an explicit
// capability refusal rather than as whatever generic failure that backend's first unsupported
// call happens to raise.
[[nodiscard]] CNA_Result EnsureThreeDSupported(GraphicsDevice& device) noexcept
{
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) {
        return NotSupported("The selected graphics renderer does not support 3D draw submission.");
    }
    return CNA_RESULT_SUCCESS;
}

// Callback-scoped view over native text. The event payload never owns bytes: the canonical string
// outlives the synchronous callback, and the C consumer copies whatever it needs before returning.
[[nodiscard]] CNA_StringView ToStringView(const std::string* const value) noexcept
{
    if (value == nullptr || value->empty()) {
        return CNA_StringView{nullptr, UINT64_C(0)};
    }
    return CNA_StringView{value->data(), static_cast<uint64_t>(value->size())};
}

class DeviceRegistration {
public:
    DeviceRegistration() = default;
    DeviceRegistration(const DeviceRegistration&) = delete;
    DeviceRegistration& operator=(const DeviceRegistration&) = delete;
    virtual ~DeviceRegistration() = default;
    virtual void Unsubscribe() noexcept = 0;

    // Drops the subscription without touching the native handler, for use once the canonical
    // device is gone.
    void Invalidate() noexcept
    {
        subscribed_ = false;
    }

protected:
    bool subscribed_ = true;
};

template<typename TEventArgs>
class TypedDeviceRegistration final : public DeviceRegistration {
public:
    using Source = System::EventHandler<TEventArgs>;
    using Token = typename Source::Token;

    TypedDeviceRegistration(Source* const source, const Token token) noexcept
        : source_(source), token_(token)
    {
    }

    ~TypedDeviceRegistration() override
    {
        TypedDeviceRegistration::Unsubscribe();
    }

    void Unsubscribe() noexcept override
    {
        if (!subscribed_) {
            return;
        }
        subscribed_ = false;
        source_->Remove(token_);
    }

private:
    Source* source_;
    Token token_;
};

// The canonical collections store raw `Texture*` slots, so a C reader cannot recover the owning C
// handle from them. These records remember what the C API itself bound; the native pointer is kept
// only for identity comparison and is never dereferenced.
struct BoundTextureSlot final {
    CNA_Handle handle = CNA_INVALID_HANDLE;
    const Microsoft::Xna::Framework::Graphics::Texture* nativePointer = nullptr;
};

struct TextureSlotState final {
    std::mutex mutex;
    BoundTextureSlot pixel[CNA_TEXTURE_COLLECTION_MAX_TEXTURES];
    BoundTextureSlot vertex[CNA_TEXTURE_COLLECTION_MAX_TEXTURES];
};

[[nodiscard]] TextureSlotState& GetTextureSlotState()
{
    static TextureSlotState state;
    return state;
}

[[nodiscard]] bool IsSupportedShaderStage(const CNA_ShaderStage stage) noexcept
{
    return stage == CNA_SHADER_STAGE_PIXEL || stage == CNA_SHADER_STAGE_VERTEX;
}

[[nodiscard]] BoundTextureSlot* RecordedSlot(
    const CNA_ShaderStage stage,
    const uint32_t slot) noexcept
{
    TextureSlotState& state = GetTextureSlotState();
    return stage == CNA_SHADER_STAGE_PIXEL ? &state.pixel[slot] : &state.vertex[slot];
}

[[nodiscard]] CNA_Result ValidateTextureSlot(
    const CNA_ShaderStage stage,
    const uint32_t slot) noexcept
{
    if (!IsSupportedShaderStage(stage)) {
        return InvalidArgument("The shader stage is not recognized.");
    }
    if (slot >= CNA_TEXTURE_COLLECTION_MAX_TEXTURES) {
        return OutOfRange("The texture sampler slot is out of range.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] TextureCollection& SelectCollection(
    GraphicsDevice& device,
    const CNA_ShaderStage stage)
{
    return stage == CNA_SHADER_STAGE_PIXEL ? device.getTexturesProperty()
                                           : device.getVertexTexturesProperty();
}

// The same identity problem as the sampler slots: the device reports bound buffers as raw native
// pointers, so the C handle that bound one is remembered here for read-back.
struct BoundBufferSlot final {
    CNA_Handle handle = CNA_INVALID_HANDLE;
    const void* nativePointer = nullptr;
};

struct DeviceBindingState final {
    std::mutex mutex;
    std::vector<BoundBufferSlot> vertexBindings;
    CNA_Handle indexBuffer = CNA_INVALID_HANDLE;
    const void* indexNativePointer = nullptr;
    // The device keeps a borrowed Effect pointer, so the C API keeps the assigned effect alive for
    // as long as it can still be dereferenced by a draw call.
    std::shared_ptr<void> currentEffect;
};

[[nodiscard]] DeviceBindingState& GetDeviceBindingState()
{
    static DeviceBindingState state;
    return state;
}

[[nodiscard]] CNA_Result GetVertexBuffer(
    const CNA_VertexBufferHandle handle,
    std::shared_ptr<VertexBufferResource>* const outBuffer)
{
    return LookupHandle(handle, ObjectKind::VertexBuffer, outBuffer);
}

[[nodiscard]] CNA_Result GetIndexBuffer(
    const CNA_IndexBufferHandle handle,
    std::shared_ptr<IndexBufferResource>* const outBuffer)
{
    return LookupHandle(handle, ObjectKind::IndexBuffer, outBuffer);
}

[[nodiscard]] CNA_VertexBufferHandle RecordedVertexBufferHandle(
    const VertexBuffer* const nativeBuffer)
{
    if (nativeBuffer == nullptr) {
        return CNA_INVALID_HANDLE;
    }
    CNA_Handle candidate = CNA_INVALID_HANDLE;
    {
        DeviceBindingState& state = GetDeviceBindingState();
        std::lock_guard lock(state.mutex);
        for (const BoundBufferSlot& slot : state.vertexBindings) {
            if (slot.nativePointer == nativeBuffer) {
                candidate = slot.handle;
                break;
            }
        }
    }
    if (candidate == CNA_INVALID_HANDLE) {
        return CNA_INVALID_HANDLE;
    }
    std::shared_ptr<VertexBufferResource> buffer;
    if (GetRuntimeHandles().Get(candidate, ObjectKind::VertexBuffer, &buffer) ==
            CNA_RESULT_SUCCESS &&
        buffer->value.get() == nativeBuffer) {
        return candidate;
    }
    return CNA_INVALID_HANDLE;
}

[[nodiscard]] CNA_IndexBufferHandle RecordedIndexBufferHandle(
    const IndexBuffer* const nativeBuffer)
{
    if (nativeBuffer == nullptr) {
        return CNA_INVALID_HANDLE;
    }
    CNA_Handle candidate = CNA_INVALID_HANDLE;
    {
        DeviceBindingState& state = GetDeviceBindingState();
        std::lock_guard lock(state.mutex);
        if (state.indexNativePointer == nativeBuffer) {
            candidate = state.indexBuffer;
        }
    }
    if (candidate == CNA_INVALID_HANDLE) {
        return CNA_INVALID_HANDLE;
    }
    std::shared_ptr<IndexBufferResource> buffer;
    if (GetRuntimeHandles().Get(candidate, ObjectKind::IndexBuffer, &buffer) ==
            CNA_RESULT_SUCCESS &&
        buffer->value.get() == nativeBuffer) {
        return candidate;
    }
    return CNA_INVALID_HANDLE;
}

[[nodiscard]] uint64_t RequestedRegionPixels(
    GraphicsDevice& device,
    const CNA_BackBufferReadback& readback)
{
    if (readback.has_source_rectangle == CNA_TRUE) {
        if (readback.source_rectangle.width <= 0 || readback.source_rectangle.height <= 0) {
            return 0U;
        }
        return static_cast<uint64_t>(readback.source_rectangle.width) *
            static_cast<uint64_t>(readback.source_rectangle.height);
    }
    const auto& parameters = device.getPresentationParametersProperty();
    const int32_t width = parameters.getBackBufferWidthProperty();
    const int32_t height = parameters.getBackBufferHeightProperty();
    if (width <= 0 || height <= 0) {
        return 0U;
    }
    return static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
}

[[nodiscard]] CNA_Result ValidatePrimitiveType(const CNA_PrimitiveType primitiveType) noexcept
{
    if (primitiveType > CNA_PRIMITIVE_POINT_LIST_EXT) {
        return InvalidArgument("The primitive topology is not recognized.");
    }
    return CNA_RESULT_SUCCESS;
}

// One resolved user-primitive draw. The vertex source and the optional declaration together select
// which canonical overload runs, so the C surface needs two functions instead of twenty-nine.
struct UserPrimitiveRequest final {
    GraphicsDevice* device = nullptr;
    PrimitiveType primitiveType = PrimitiveType::TriangleList;
    CNA_UserVertexSource vertexSource = CNA_USER_VERTEX_SOURCE_RAW_STREAM;
    const void* vertexData = nullptr;
    const VertexDeclaration* declaration = nullptr;
    int32_t vertexOffset = 0;
    int32_t numVertices = 0;
    int32_t primitiveCount = 0;
    std::shared_ptr<VertexDeclaration> declarationOwner;
};

[[nodiscard]] CNA_Result ResolveUserPrimitives(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_UserPrimitives* const primitives,
    const bool indexed,
    UserPrimitiveRequest* const outRequest)
{
    if (!IsVersionedStructure(primitives) || primitives->reserved != 0U ||
        primitives->vertex_data == nullptr || primitives->vertex_offset < 0 ||
        primitives->primitive_count <= 0 ||
        primitives->vertex_source > CNA_USER_VERTEX_SOURCE_POSITION_NORMAL_TEXTURE ||
        (indexed && primitives->num_vertices <= 0)) {
        return InvalidArgument("The user vertex description is invalid.");
    }
    // The canonical declaration-less raw overloads read their bytes as an array of the native
    // VertexPositionColor object, which carries a vtable and can never be produced from C. A raw
    // stream therefore always needs its declaration; the equivalent C-safe route for that overload
    // is the POSITION_COLOR source, which converts before it reaches CNA.
    if (primitives->vertex_source == CNA_USER_VERTEX_SOURCE_RAW_STREAM &&
        primitives->vertex_declaration == CNA_INVALID_HANDLE) {
        return InvalidArgument("A raw vertex stream requires an explicit vertex declaration.");
    }
    if (const CNA_Result result = ValidatePrimitiveType(primitives->primitive_type);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    std::shared_ptr<VertexDeclaration> declaration;
    if (primitives->vertex_declaration != CNA_INVALID_HANDLE) {
        const CNA_Result result = LookupHandle(
            primitives->vertex_declaration, ObjectKind::VertexDeclaration, &declaration);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }

    if (const CNA_Result result = EnsureThreeDSupported(*graphicsDevice->value);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    outRequest->device = graphicsDevice->value;
    outRequest->primitiveType = static_cast<PrimitiveType>(primitives->primitive_type);
    outRequest->vertexSource = primitives->vertex_source;
    outRequest->vertexData = primitives->vertex_data;
    outRequest->declaration = declaration.get();
    outRequest->vertexOffset = primitives->vertex_offset;
    outRequest->numVertices = primitives->num_vertices;
    outRequest->primitiveCount = primitives->primitive_count;
    outRequest->declarationOwner = std::move(declaration);
    return CNA_RESULT_SUCCESS;
}

// The built-in vertex structures embed a polymorphic Color, so a C array of the matching POD is
// never layout-compatible with them: every typed route converts before it reaches CNA.
[[nodiscard]] VertexPositionColor ToNativeVertex(const CNA_VertexPositionColor& value)
{
    return VertexPositionColor(
        Vector3(value.position.x, value.position.y, value.position.z),
        Color(value.color.r, value.color.g, value.color.b, value.color.a));
}

[[nodiscard]] VertexPositionColorTexture ToNativeVertex(
    const CNA_VertexPositionColorTexture& value)
{
    return VertexPositionColorTexture(
        Vector3(value.position.x, value.position.y, value.position.z),
        Color(value.color.r, value.color.g, value.color.b, value.color.a),
        Vector2(value.texture_coordinate.x, value.texture_coordinate.y));
}

[[nodiscard]] VertexPositionTexture ToNativeVertex(const CNA_VertexPositionTexture& value)
{
    return VertexPositionTexture(
        Vector3(value.position.x, value.position.y, value.position.z),
        Vector2(value.texture_coordinate.x, value.texture_coordinate.y));
}

[[nodiscard]] VertexPositionNormalTexture ToNativeVertex(
    const CNA_VertexPositionNormalTexture& value)
{
    return VertexPositionNormalTexture(
        Vector3(value.position.x, value.position.y, value.position.z),
        Vector3(value.normal.x, value.normal.y, value.normal.z),
        Vector2(value.texture_coordinate.x, value.texture_coordinate.y));
}

template<typename TNative, typename TValue>
[[nodiscard]] std::vector<TNative> ToNativeVertices(
    const void* const data,
    const int32_t offset,
    const int32_t count)
{
    const TValue* const source = static_cast<const TValue*>(data);
    std::vector<TNative> native;
    native.reserve(static_cast<std::size_t>(count));
    for (int32_t index = 0; index < count; ++index) {
        native.push_back(ToNativeVertex(source[offset + index]));
    }
    return native;
}

void DrawUserPrimitivesNative(const UserPrimitiveRequest& request)
{
    GraphicsDevice& device = *request.device;
    const int32_t vertexCount =
        GraphicsDevice::PrimitiveVerts(request.primitiveType, request.primitiveCount);
    switch (request.vertexSource) {
        case CNA_USER_VERTEX_SOURCE_POSITION_COLOR: {
            const auto native = ToNativeVertices<VertexPositionColor, CNA_VertexPositionColor>(
                request.vertexData, request.vertexOffset, vertexCount);
            if (request.declaration != nullptr) {
                device.DrawUserPrimitives(
                    request.primitiveType, native.data(), 0, request.primitiveCount,
                    *request.declaration);
            } else {
                device.DrawUserPrimitives(
                    request.primitiveType, native.data(), 0, request.primitiveCount);
            }
            return;
        }
        case CNA_USER_VERTEX_SOURCE_POSITION_COLOR_TEXTURE: {
            const auto native =
                ToNativeVertices<VertexPositionColorTexture, CNA_VertexPositionColorTexture>(
                    request.vertexData, request.vertexOffset, vertexCount);
            if (request.declaration != nullptr) {
                device.DrawUserPrimitives(
                    request.primitiveType, native.data(), 0, request.primitiveCount,
                    *request.declaration);
            } else {
                device.DrawUserPrimitives(
                    request.primitiveType, native.data(), 0, request.primitiveCount);
            }
            return;
        }
        case CNA_USER_VERTEX_SOURCE_POSITION_TEXTURE: {
            const auto native = ToNativeVertices<VertexPositionTexture, CNA_VertexPositionTexture>(
                request.vertexData, request.vertexOffset, vertexCount);
            if (request.declaration != nullptr) {
                device.DrawUserPrimitives(
                    request.primitiveType, native.data(), 0, request.primitiveCount,
                    *request.declaration);
            } else {
                device.DrawUserPrimitives(
                    request.primitiveType, native.data(), 0, request.primitiveCount);
            }
            return;
        }
        case CNA_USER_VERTEX_SOURCE_POSITION_NORMAL_TEXTURE: {
            const auto native =
                ToNativeVertices<VertexPositionNormalTexture, CNA_VertexPositionNormalTexture>(
                    request.vertexData, request.vertexOffset, vertexCount);
            if (request.declaration != nullptr) {
                device.DrawUserPrimitives(
                    request.primitiveType, native.data(), 0, request.primitiveCount,
                    *request.declaration);
            } else {
                device.DrawUserPrimitives(
                    request.primitiveType, native.data(), 0, request.primitiveCount);
            }
            return;
        }
        default:
            break;
    }

    if (request.declaration != nullptr) {
        device.DrawUserPrimitives(
            request.primitiveType, request.vertexData, request.vertexOffset,
            request.primitiveCount, *request.declaration);
    } else {
        device.DrawUserPrimitives(
            request.primitiveType, request.vertexData, request.vertexOffset,
            request.primitiveCount);
    }
}

template<typename TNative, typename TValue, typename TIndex>
void DrawTypedIndexedPrimitives(
    const UserPrimitiveRequest& request,
    const TIndex* const indexData,
    const int32_t indexOffset)
{
    const auto native = ToNativeVertices<TNative, TValue>(
        request.vertexData, request.vertexOffset, request.numVertices);
    if (request.declaration != nullptr) {
        request.device->DrawUserIndexedPrimitives(
            request.primitiveType, native.data(), 0, request.numVertices, indexData, indexOffset,
            request.primitiveCount, *request.declaration);
    } else {
        request.device->DrawUserIndexedPrimitives(
            request.primitiveType, native.data(), 0, request.numVertices, indexData, indexOffset,
            request.primitiveCount);
    }
}

template<typename TIndex>
void DrawUserIndexedPrimitivesTyped(
    const UserPrimitiveRequest& request,
    const TIndex* const indexData,
    const int32_t indexOffset)
{
    switch (request.vertexSource) {
        case CNA_USER_VERTEX_SOURCE_POSITION_COLOR:
            DrawTypedIndexedPrimitives<VertexPositionColor, CNA_VertexPositionColor>(
                request, indexData, indexOffset);
            return;
        case CNA_USER_VERTEX_SOURCE_POSITION_COLOR_TEXTURE:
            DrawTypedIndexedPrimitives<VertexPositionColorTexture, CNA_VertexPositionColorTexture>(
                request, indexData, indexOffset);
            return;
        case CNA_USER_VERTEX_SOURCE_POSITION_TEXTURE:
            DrawTypedIndexedPrimitives<VertexPositionTexture, CNA_VertexPositionTexture>(
                request, indexData, indexOffset);
            return;
        case CNA_USER_VERTEX_SOURCE_POSITION_NORMAL_TEXTURE:
            DrawTypedIndexedPrimitives<
                VertexPositionNormalTexture, CNA_VertexPositionNormalTexture>(
                request, indexData, indexOffset);
            return;
        default:
            break;
    }

    if (request.declaration != nullptr) {
        request.device->DrawUserIndexedPrimitives(
            request.primitiveType, request.vertexData, request.vertexOffset, request.numVertices,
            indexData, indexOffset, request.primitiveCount, *request.declaration);
    } else {
        request.device->DrawUserIndexedPrimitives(
            request.primitiveType, request.vertexData, request.vertexOffset, request.numVertices,
            indexData, indexOffset, request.primitiveCount);
    }
}

void DrawUserIndexedPrimitivesNative(
    const UserPrimitiveRequest& request,
    const CNA_UserIndices& indices)
{
    // The raw void*/void* overload is the only one without an index width, so a 16-bit request on
    // a raw stream without a declaration takes it; everything else is width-typed.
    if (indices.index_element_size == CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS) {
        DrawUserIndexedPrimitivesTyped<std::uint16_t>(
            request,
            static_cast<const std::uint16_t*>(indices.index_data),
            indices.index_offset);
        return;
    }
    DrawUserIndexedPrimitivesTyped<std::uint32_t>(
        request,
        static_cast<const std::uint32_t*>(indices.index_data),
        indices.index_offset);
}

[[nodiscard]] CNA_Result GetOcclusionQuery(
    const CNA_Handle handle,
    std::shared_ptr<OcclusionQueryResource>* const outQuery)
{
    return LookupHandle(handle, ObjectKind::OcclusionQuery, outQuery);
}

struct LiveRegistrations final {
    std::mutex mutex;
    std::vector<std::weak_ptr<DeviceRegistration>> entries;
};

[[nodiscard]] LiveRegistrations& GetLiveRegistrations()
{
    static LiveRegistrations registrations;
    return registrations;
}

void TrackRegistration(const std::shared_ptr<DeviceRegistration>& registration)
{
    LiveRegistrations& live = GetLiveRegistrations();
    std::lock_guard lock(live.mutex);
    std::erase_if(live.entries, [](const std::weak_ptr<DeviceRegistration>& entry) {
        return entry.expired();
    });
    live.entries.push_back(registration);
}

using DeviceEventRegistration = TypedDeviceRegistration<System::EventArgs>;
using ResourceCreatedRegistration = TypedDeviceRegistration<ResourceCreatedEventArgs>;
using ResourceDestroyedRegistration = TypedDeviceRegistration<ResourceDestroyedEventArgs>;

template<typename TCallback>
[[nodiscard]] CNA_Result ValidateSubscription(
    const TCallback callback,
    CNA_GraphicsDeviceEventRegistrationHandle* const outRegistration) noexcept
{
    if (outRegistration == nullptr) {
        return InvalidArgument("The device event-registration output handle is null.");
    }
    *outRegistration = CNA_INVALID_HANDLE;
    if (callback == nullptr) {
        return InvalidArgument("The device event callback is null.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result StoreRegistration(
    std::shared_ptr<DeviceRegistration> registration,
    CNA_GraphicsDeviceEventRegistrationHandle* const outRegistration)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::GraphicsDeviceEventRegistration,
        registration,
        outRegistration);
    if (result == CNA_RESULT_SUCCESS) {
        TrackRegistration(registration);
        return CNA_RESULT_SUCCESS;
    }
    registration->Unsubscribe();
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The graphics-device event-registration handle could not be created.");
}

} // namespace

namespace CNA::C::Detail {

void ResetGraphicsDeviceAdapterState() noexcept
{
    {
        LiveRegistrations& live = GetLiveRegistrations();
        std::lock_guard lock(live.mutex);
        for (const std::weak_ptr<DeviceRegistration>& entry : live.entries) {
            if (const std::shared_ptr<DeviceRegistration> registration = entry.lock()) {
                registration->Invalidate();
            }
        }
        live.entries.clear();
    }

    {
        TextureSlotState& slots = GetTextureSlotState();
        std::lock_guard lock(slots.mutex);
        for (uint32_t slot = 0U; slot < CNA_TEXTURE_COLLECTION_MAX_TEXTURES; ++slot) {
            slots.pixel[slot] = BoundTextureSlot{};
            slots.vertex[slot] = BoundTextureSlot{};
        }
    }

    DeviceBindingState& bindings = GetDeviceBindingState();
    std::lock_guard lock(bindings.mutex);
    bindings.vertexBindings.clear();
    bindings.indexBuffer = CNA_INVALID_HANDLE;
    bindings.indexNativePointer = nullptr;
    bindings.currentEffect.reset();
}

} // namespace CNA::C::Detail

namespace {

[[nodiscard]] CNA_Result ViewportSetBounds(
    CNA_Viewport* const viewport,
    const CNA_Rectangle bounds)
{
    if (viewport == nullptr) {
        return InvalidArgument("The viewport is null.");
    }
    Viewport native = ToNative(*viewport);
    native.setBoundsProperty(ToNative(bounds));
    *viewport = ToC(native);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceGetAdapterIndex(
    const CNA_Handle graphicsDeviceHandle,
    uint32_t* const outAdapterIndex)
{
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const GraphicsAdapter* const adapter = &graphicsDevice->value->getAdapterProperty();
    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    for (std::size_t index = 0U; index < adapters.size(); ++index) {
        if (adapters[index].get() == adapter) {
            *outAdapterIndex = static_cast<uint32_t>(index);
            return CNA_RESULT_SUCCESS;
        }
    }
    return InvalidState(
        "The device's graphics adapter is no longer one of the enumerated adapters.");
}

[[nodiscard]] CNA_Result GraphicsDeviceSubscribeEvent(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_GraphicsDeviceEvent deviceEvent,
    const CNA_GraphicsDeviceEventCallback callback,
    void* const context,
    CNA_GraphicsDeviceEventRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The device event callback is null.");
    }
    if (deviceEvent > CNA_GRAPHICS_DEVICE_EVENT_DEVICE_RESETTING) {
        return InvalidArgument("The graphics-device event identity is not recognized.");
    }
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    GraphicsDevice& device = *graphicsDevice->value;
    System::EventHandler<System::EventArgs>& source =
        deviceEvent == CNA_GRAPHICS_DEVICE_EVENT_DISPOSING ? device.Disposing
        : deviceEvent == CNA_GRAPHICS_DEVICE_EVENT_DEVICE_LOST ? device.DeviceLost
        : deviceEvent == CNA_GRAPHICS_DEVICE_EVENT_DEVICE_RESET ? device.DeviceReset
                                                                : device.DeviceResetting;
    const auto token = source.Add(
        [graphicsDeviceHandle, callback, context](System::Object*, const System::EventArgs&) {
            callback(graphicsDeviceHandle, context);
        });
    return StoreRegistration(
        std::make_shared<DeviceEventRegistration>(&source, token),
        outRegistration);
}

[[nodiscard]] CNA_Result GraphicsDeviceSubscribeResourceCreated(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_GraphicsDeviceResourceCreatedCallback callback,
    void* const context,
    CNA_GraphicsDeviceEventRegistrationHandle* const outRegistration)
{
    if (const CNA_Result result = ValidateSubscription(callback, outRegistration);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    auto& source = graphicsDevice->value->ResourceCreated;
    const auto token = source.Add(
        [graphicsDeviceHandle, callback, context](
            System::Object*, const ResourceCreatedEventArgs& args) {
            // The canonical event fires from the GraphicsResource base constructor, so the
            // reported object has no concrete type yet: querying any virtual member of it
            // here would be a pure-virtual call. Only its presence is reported.
            CNA_ResourceCreatedEventInfo info = {};
            info.struct_size = static_cast<uint32_t>(sizeof(CNA_ResourceCreatedEventInfo));
            info.struct_version = StructureVersion;
            info.has_resource =
                ToCBool(args.getResourceProperty() != nullptr);
            callback(graphicsDeviceHandle, &info, context);
        });
    return StoreRegistration(
        std::make_shared<ResourceCreatedRegistration>(&source, token),
        outRegistration);
}

[[nodiscard]] CNA_Result GraphicsDeviceSubscribeResourceDestroyed(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_GraphicsDeviceResourceDestroyedCallback callback,
    void* const context,
    CNA_GraphicsDeviceEventRegistrationHandle* const outRegistration)
{
    if (const CNA_Result result = ValidateSubscription(callback, outRegistration);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    auto& source = graphicsDevice->value->ResourceDestroyed;
    const auto token = source.Add(
        [graphicsDeviceHandle, callback, context](
            System::Object*, const ResourceDestroyedEventArgs& args) {
            // The tag is caller-owned native state of unknown liveness, so its presence is
            // reported without dereferencing it.
            CNA_ResourceDestroyedEventInfo info = {};
            info.struct_size = static_cast<uint32_t>(sizeof(CNA_ResourceDestroyedEventInfo));
            info.struct_version = StructureVersion;
            info.has_tag = ToCBool(args.getTagProperty() != nullptr);
            info.name = ToStringView(&args.getNameProperty());
            callback(graphicsDeviceHandle, &info, context);
        });
    return StoreRegistration(
        std::make_shared<ResourceDestroyedRegistration>(&source, token),
        outRegistration);
}

[[nodiscard]] CNA_Result GraphicsDeviceUnsubscribe(
    const CNA_GraphicsDeviceEventRegistrationHandle registrationHandle)
{
    std::shared_ptr<DeviceRegistration> registration;
    const CNA_Result result = LookupHandle(
        registrationHandle, ObjectKind::GraphicsDeviceEventRegistration, &registration);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }
    registration->Unsubscribe();
    return ReleaseHandle(registrationHandle);
}

[[nodiscard]] CNA_Result GraphicsDeviceGetTexture(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ShaderStage stage,
    const uint32_t slot,
    CNA_TextureSlotInfo* const outInfo)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outInfo, "The texture-slot output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateTextureSlot(stage, slot);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    const TextureCollection& collection = SelectCollection(*graphicsDevice->value, stage);
    const Texture* const nativeTexture = collection[static_cast<int>(slot)];
    outInfo->struct_size = static_cast<uint32_t>(sizeof(CNA_TextureSlotInfo));
    outInfo->struct_version = StructureVersion;
    outInfo->bound = ToCBool(nativeTexture != nullptr);
    std::memset(outInfo->reserved, 0, sizeof(outInfo->reserved));
    outInfo->texture = CNA_INVALID_HANDLE;
    if (nativeTexture == nullptr) {
        return CNA_RESULT_SUCCESS;
    }

    CNA_Handle recordedHandle = CNA_INVALID_HANDLE;
    {
        TextureSlotState& slots = GetTextureSlotState();
        std::lock_guard lock(slots.mutex);
        const BoundTextureSlot& recorded = *RecordedSlot(stage, slot);
        if (recorded.nativePointer == nativeTexture) {
            recordedHandle = recorded.handle;
        }
    }
    if (recordedHandle == CNA_INVALID_HANDLE) {
        return CNA_RESULT_SUCCESS;
    }

    // The record only names a candidate; the handle must still resolve to that exact object.
    TextureResourceView texture;
    if (GetOwnedTexture(recordedHandle, &texture) == CNA_RESULT_SUCCESS &&
        texture.value.get() == nativeTexture) {
        outInfo->texture = recordedHandle;
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceSetTexture(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ShaderStage stage,
    const uint32_t slot,
    const CNA_Handle textureHandle)
{
    if (const CNA_Result result = ValidateTextureSlot(stage, slot);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    TextureResourceView texture;
    if (textureHandle != CNA_INVALID_HANDLE) {
        if (const CNA_Result result = GetOwnedTexture(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }

    TextureCollection& collection = SelectCollection(*graphicsDevice->value, stage);
    collection(static_cast<int>(slot), texture.value.get());

    TextureSlotState& slots = GetTextureSlotState();
    std::lock_guard lock(slots.mutex);
    *RecordedSlot(stage, slot) = BoundTextureSlot{textureHandle, texture.value.get()};
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceUnbindTexture(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Handle textureHandle)
{
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    TextureResourceView texture;
    if (const CNA_Result result = GetOwnedTexture(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    const Texture* const nativeTexture = texture.value.get();
    graphicsDevice->value->getTexturesProperty().RemoveDisposedTexture(nativeTexture);
    graphicsDevice->value->getVertexTexturesProperty().RemoveDisposedTexture(nativeTexture);

    TextureSlotState& slots = GetTextureSlotState();
    std::lock_guard lock(slots.mutex);
    for (uint32_t slot = 0U; slot < CNA_TEXTURE_COLLECTION_MAX_TEXTURES; ++slot) {
        if (slots.pixel[slot].nativePointer == nativeTexture) {
            slots.pixel[slot] = BoundTextureSlot{};
        }
        if (slots.vertex[slot].nativePointer == nativeTexture) {
            slots.vertex[slot] = BoundTextureSlot{};
        }
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceGetBackbufferDataWindow(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_BackBufferReadback* const readback,
    CNA_Color* const destination,
    const uint64_t capacity)
{
    if (!IsVersionedStructure(readback) ||
        (readback->has_source_rectangle != CNA_TRUE &&
         readback->has_source_rectangle != CNA_FALSE) ||
        readback->reserved[0] != 0U || readback->reserved[1] != 0U ||
        readback->reserved[2] != 0U) {
        return InvalidArgument("The back-buffer readback structure is invalid.");
    }
    if (destination == nullptr && capacity != 0U) {
        return InvalidArgument("The back-buffer destination is invalid.");
    }
    if (readback->start_index > static_cast<uint64_t>(INT32_MAX) ||
        readback->element_count > static_cast<uint64_t>(INT32_MAX)) {
        return Overflow("The back-buffer readback window exceeds the native element range.");
    }
    if (readback->start_index + readback->element_count > capacity) {
        return BufferTooSmall("The destination cannot hold the requested back-buffer window.");
    }

    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    // The canonical routine reports an undersized element count as a generic native failure.
    // Deciding it here keeps the C contract deterministic: a window smaller than its region is
    // a capacity error, reported before any native read begins.
    const uint64_t regionPixels = RequestedRegionPixels(*graphicsDevice->value, *readback);
    if (readback->element_count < regionPixels) {
        return BufferTooSmall(
            "The requested element count is smaller than the selected back-buffer region.");
    }

    // Read into scratch storage first so a native failure cannot leave the caller's array
    // partially overwritten.
    std::vector<Color> scratch;
    scratch.reserve(static_cast<std::size_t>(readback->element_count));
    for (uint64_t index = 0U; index < readback->element_count; ++index) {
        scratch.emplace_back(UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0));
    }
    const Rectangle sourceRectangle = ToNative(readback->source_rectangle);
    graphicsDevice->value->GetBackBufferData(
        readback->has_source_rectangle == CNA_TRUE ? &sourceRectangle : nullptr,
        scratch.empty() ? nullptr : scratch.data(),
        0,
        static_cast<int>(readback->element_count));
    for (std::size_t index = 0U; index < scratch.size(); ++index) {
        const Color& pixel = scratch[index];
        destination[readback->start_index + index] = CNA_Color{
            pixel.getRProperty(),
            pixel.getGProperty(),
            pixel.getBProperty(),
            pixel.getAProperty()};
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceSetVertexBufferOffset(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_VertexBufferHandle vertexBufferHandle,
    const int32_t vertexOffset)
{
    if (vertexOffset < 0) {
        return OutOfRange("The vertex offset must not be negative.");
    }
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<VertexBufferResource> buffer;
    if (vertexBufferHandle != CNA_INVALID_HANDLE) {
        if (const CNA_Result result = GetVertexBuffer(vertexBufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }

    VertexBuffer* const native = buffer == nullptr ? nullptr : buffer->value.get();
    graphicsDevice->value->SetVertexBuffer(native, vertexOffset);

    DeviceBindingState& state = GetDeviceBindingState();
    std::lock_guard lock(state.mutex);
    state.vertexBindings.clear();
    if (native != nullptr) {
        state.vertexBindings.push_back(BoundBufferSlot{vertexBufferHandle, native});
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceSetVertexBuffer(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_VertexBufferHandle vertexBuffer)
{
    return GraphicsDeviceSetVertexBufferOffset(graphicsDeviceHandle, vertexBuffer, 0);
}

[[nodiscard]] CNA_Result GraphicsDeviceSetVertexBuffers(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_VertexBufferBinding* const bindings,
    const uint64_t bindingCount)
{
    if (bindings == nullptr && bindingCount != 0U) {
        return InvalidArgument("The vertex-buffer binding array is invalid.");
    }
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    // Everything is validated and resolved before a single native binding is applied.
    std::vector<VertexBufferBinding> nativeBindings;
    std::vector<BoundBufferSlot> recorded;
    nativeBindings.reserve(static_cast<std::size_t>(bindingCount));
    recorded.reserve(static_cast<std::size_t>(bindingCount));
    for (uint64_t index = 0U; index < bindingCount; ++index) {
        const CNA_VertexBufferBinding& binding = bindings[index];
        if (binding.vertex_offset < 0 || binding.instance_frequency < 0) {
            return OutOfRange("A vertex-buffer binding offset or frequency is negative.");
        }
        std::shared_ptr<VertexBufferResource> buffer;
        if (binding.vertex_buffer != CNA_INVALID_HANDLE) {
            if (const CNA_Result result = GetVertexBuffer(binding.vertex_buffer, &buffer);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }
        VertexBuffer* const native = buffer == nullptr ? nullptr : buffer->value.get();
        nativeBindings.push_back(
            native == nullptr
                ? VertexBufferBinding()
                : VertexBufferBinding(
                      native, binding.vertex_offset, binding.instance_frequency));
        recorded.push_back(BoundBufferSlot{binding.vertex_buffer, native});
    }

    graphicsDevice->value->SetVertexBuffers(nativeBindings);

    DeviceBindingState& state = GetDeviceBindingState();
    std::lock_guard lock(state.mutex);
    state.vertexBindings = std::move(recorded);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceCopyVertexBuffers(
    const CNA_Handle graphicsDeviceHandle,
    CNA_VertexBufferBinding* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The vertex-buffer binding destination or count output is invalid.");
    }
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    const std::vector<VertexBufferBinding> nativeBindings =
        graphicsDevice->value->GetVertexBuffers();
    *outCount = static_cast<uint64_t>(nativeBindings.size());
    if (capacity < nativeBindings.size()) {
        return BufferTooSmall(
            "The destination cannot hold every active vertex-buffer binding.");
    }
    for (std::size_t index = 0U; index < nativeBindings.size(); ++index) {
        const VertexBufferBinding& binding = nativeBindings[index];
        destination[index] = CNA_VertexBufferBinding{
            RecordedVertexBufferHandle(binding.getVertexBufferProperty()),
            binding.getVertexOffsetProperty(),
            binding.getInstanceFrequencyProperty()};
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceGetVertexBuffer(
    const CNA_Handle graphicsDeviceHandle,
    CNA_VertexBufferHandle* const outVertexBuffer)
{
    return DeviceQuery(graphicsDeviceHandle, outVertexBuffer, [](GraphicsDevice& device) {
        return RecordedVertexBufferHandle(device.GetVertexBuffer());
    });
}

[[nodiscard]] CNA_Result GraphicsDeviceSetIndexBuffer(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_IndexBufferHandle indexBufferHandle)
{
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<IndexBufferResource> buffer;
    if (indexBufferHandle != CNA_INVALID_HANDLE) {
        if (const CNA_Result result = GetIndexBuffer(indexBufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }

    const IndexBuffer* const native = buffer == nullptr ? nullptr : buffer->value.get();
    graphicsDevice->value->SetIndexBuffer(native);

    DeviceBindingState& state = GetDeviceBindingState();
    std::lock_guard lock(state.mutex);
    state.indexBuffer = native == nullptr ? CNA_INVALID_HANDLE : indexBufferHandle;
    state.indexNativePointer = native;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceGetIndexBuffer(
    const CNA_Handle graphicsDeviceHandle,
    CNA_IndexBufferHandle* const outIndexBuffer)
{
    return DeviceQuery(graphicsDeviceHandle, outIndexBuffer, [](GraphicsDevice& device) {
        return RecordedIndexBufferHandle(device.GetIndexBuffer());
    });
}

[[nodiscard]] CNA_Result GraphicsDeviceDrawUserPrimitives(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_UserPrimitives* const primitives)
{
    UserPrimitiveRequest request;
    if (const CNA_Result result = ResolveUserPrimitives(
            graphicsDeviceHandle, primitives, false, &request);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    DrawUserPrimitivesNative(request);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceDrawUserIndexedPrimitives(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_UserPrimitives* const primitives,
    const CNA_UserIndices* const indices)
{
    if (!IsVersionedStructure(indices) || indices->index_data == nullptr ||
        indices->index_offset < 0 ||
        (indices->index_element_size != CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS &&
         indices->index_element_size != CNA_INDEX_ELEMENT_SIZE_THIRTY_TWO_BITS)) {
        return InvalidArgument("The user index description is invalid.");
    }
    UserPrimitiveRequest request;
    if (const CNA_Result result = ResolveUserPrimitives(
            graphicsDeviceHandle, primitives, true, &request);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    DrawUserIndexedPrimitivesNative(request, *indices);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceSetCurrentEffect(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_EffectHandle effectHandle)
{
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<EffectResource> effect;
    if (effectHandle != CNA_INVALID_HANDLE) {
        const CNA_Result result = LookupHandle(effectHandle, ObjectKind::Effect, &effect);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }

    Effect* const native = effect == nullptr ? nullptr : effect->value.get();
    graphicsDevice->value->SetCurrentEffect(native);

    DeviceBindingState& state = GetDeviceBindingState();
    std::lock_guard lock(state.mutex);
    state.currentEffect = effect == nullptr ? nullptr : effect->value;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceRecreateRendererForMultiSampleCountExt(
    const CNA_Handle graphicsDeviceHandle,
    const int32_t multiSampleCount)
{
    if (multiSampleCount < 0) {
        return OutOfRange("The multisample count must not be negative.");
    }
    return DeviceCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
        device.RecreateRendererForMultiSampleCount(multiSampleCount);
    });
}

[[nodiscard]] CNA_Result OcclusionQueryCreate(
    const CNA_Handle graphicsDeviceHandle,
    CNA_OcclusionQueryHandle* const outOcclusionQuery)
{
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!graphicsDevice->value->SupportsCapability(CNA::GraphicsCapability::OcclusionQuery)) {
        return NotSupported(
            "The selected graphics renderer does not support occlusion queries.");
    }

    auto native = std::make_shared<OcclusionQuery>(*graphicsDevice->value);
    const auto resource = std::make_shared<OcclusionQueryResource>(
        OcclusionQueryResource{std::move(native), graphicsDevice->parentGame});
    if (const CNA_Result result =
            CreateHandle(ObjectKind::OcclusionQuery, resource, outOcclusionQuery);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result OcclusionQueryDestroy(const CNA_OcclusionQueryHandle occlusionQueryHandle)
{
    std::shared_ptr<OcclusionQueryResource> query;
    if (const CNA_Result result = GetOcclusionQuery(occlusionQueryHandle, &query);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    static_cast<System::IDisposable*>(query->value.get())->Dispose();
    if (const CNA_Result releaseResult = ReleaseHandle(occlusionQueryHandle);
        releaseResult != CNA_RESULT_SUCCESS) {
        return releaseResult;
    }
    RemoveOwnedGraphicsResourceFor(query->parentGame);
    return CNA_RESULT_SUCCESS;
}

} // namespace

// CBIND-093. The GraphicsDevice EXT surface: ten queries and one command that the engine-layer
// slices needed but never bound, because each is a method on the always-compiled device rather
// than on an engine-layer object. They live here with their siblings and work in every build.

namespace {

// CBIND-090 defined the colour-space identities; this is the same bound, restated here rather than
// exported, because the two routes below are the only users outside the engine layer.
[[nodiscard]] CNA_Result ValidateDisplayColorSpaceOrdinal(const uint32_t space) noexcept
{
    if (space > UINT32_C(2)) {
        return InvalidArgument("The display colour space is not a defined identity.");
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

// The routes generated from modules/c-api/spec/GraphicsDevice.toml.
#include "generated/GraphicsDevice.inc"
