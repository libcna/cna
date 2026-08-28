// SPDX-License-Identifier: MS-PL

#include "CNA/C/graphics_device.h"
#include "CnaCApiDetail.hpp"
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

template<typename TEnum>
[[nodiscard]] constexpr uint32_t NativeOrdinal(const TEnum value) noexcept
{
    return static_cast<uint32_t>(static_cast<std::underlying_type_t<TEnum>>(value));
}

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

using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
using CNA::C::Detail::AddOwnedGraphicsResource;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::EffectResource;
using CNA::C::Detail::IndexBufferResource;
using CNA::C::Detail::VertexBufferResource;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::OcclusionQueryResource;
using CNA::C::Detail::RemoveOwnedGraphicsResource;
using CNA::C::Detail::TextureResourceView;
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

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] Matrix ToNative(const CNA_Matrix value)
{
    return Matrix(
        value.m11, value.m12, value.m13, value.m14,
        value.m21, value.m22, value.m23, value.m24,
        value.m31, value.m32, value.m33, value.m34,
        value.m41, value.m42, value.m43, value.m44);
}

[[nodiscard]] Vector3 ToNative(const CNA_Vector3 value)
{
    return Vector3(value.x, value.y, value.z);
}

[[nodiscard]] Rectangle ToNative(const CNA_Rectangle value)
{
    return Rectangle(value.x, value.y, value.width, value.height);
}

[[nodiscard]] Viewport ToNative(const CNA_Viewport value)
{
    Viewport viewport(value.x, value.y, value.width, value.height);
    viewport.setMinDepthProperty(value.min_depth);
    viewport.setMaxDepthProperty(value.max_depth);
    return viewport;
}

[[nodiscard]] CNA_Vector3 ToC(const Vector3 value) noexcept
{
    return CNA_Vector3{value.X, value.Y, value.Z};
}

[[nodiscard]] CNA_Rectangle ToC(const Rectangle value) noexcept
{
    return CNA_Rectangle{value.X, value.Y, value.Width, value.Height};
}

[[nodiscard]] CNA_Viewport ToC(const Viewport& value)
{
    return CNA_Viewport{
        value.getXProperty(),
        value.getYProperty(),
        value.getWidthProperty(),
        value.getHeightProperty(),
        value.getMinDepthProperty(),
        value.getMaxDepthProperty()};
}

template<typename TValue, typename TCallable>
[[nodiscard]] CNA_Result StoreOutput(
    TValue* const output,
    const char* const nullMessage,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (output == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                nullMessage);
        }
        const TValue result = std::forward<TCallable>(callable)();
        *output = result;
        return CNA_RESULT_SUCCESS;
    });
}

template<typename TCallable>
[[nodiscard]] CNA_Result CopyFormattedString(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The string destination or required-byte output is invalid.");
        }

        const std::string text = std::forward<TCallable>(callable)();
        *outBytes = static_cast<uint64_t>(text.size());
        if (capacity < text.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the complete formatted value.");
        }
        if (!text.empty()) {
            std::memcpy(destination, text.data(), text.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

template<typename TValue, typename TCallable>
[[nodiscard]] CNA_Result DeviceQuery(
    const CNA_Handle graphicsDeviceHandle,
    TValue* const output,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (output == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The graphics-device query output is null.");
        }
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
    });
}

template<typename TCallable>
[[nodiscard]] CNA_Result DeviceCommand(
    const CNA_Handle graphicsDeviceHandle,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::forward<TCallable>(callable)(*graphicsDevice->value);
        return CNA_RESULT_SUCCESS;
    });
}

[[nodiscard]] CNA_Result EnsureThreeDSupported(GraphicsDevice& device) noexcept
{
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) {
        return Fail(
            CNA_RESULT_NOT_SUPPORTED,
            CNA_ERROR_CATEGORY_NOT_SUPPORTED,
            "The selected graphics backend does not support 3D draw submission.");
    }
    return CNA_RESULT_SUCCESS;
}

// A draw route that a 2D-only backend cannot serve is reported as an explicit capability refusal
// rather than as whatever generic failure that backend's first unsupported call happens to raise.
template<typename TCallable>
[[nodiscard]] CNA_Result DeviceGuardedCommand(
    const CNA_Handle graphicsDeviceHandle,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = EnsureThreeDSupported(*graphicsDevice->value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::forward<TCallable>(callable)(*graphicsDevice->value);
        return CNA_RESULT_SUCCESS;
    });
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
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The shader stage is not recognized.");
    }
    if (slot >= CNA_TEXTURE_COLLECTION_MAX_TEXTURES) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "The texture sampler slot is out of range.");
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
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::VertexBuffer, outBuffer);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The VertexBuffer handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetIndexBuffer(
    const CNA_IndexBufferHandle handle,
    std::shared_ptr<IndexBufferResource>* const outBuffer)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::IndexBuffer, outBuffer);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The IndexBuffer handle is invalid for this call.");
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
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The primitive topology is not recognized.");
    }
    return CNA_RESULT_SUCCESS;
}

template<typename TCallable>
[[nodiscard]] CNA_Result DeviceBooleanCommand(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Bool value,
    const bool requiresThreeD,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value != CNA_TRUE && value != CNA_FALSE) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Boolean argument must be CNA_TRUE or CNA_FALSE.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (requiresThreeD) {
            if (const CNA_Result result = EnsureThreeDSupported(*graphicsDevice->value);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }
        std::forward<TCallable>(callable)(*graphicsDevice->value, value == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
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
    if (primitives == nullptr || primitives->struct_size < sizeof(CNA_UserPrimitives) ||
        primitives->struct_version != StructureVersion || primitives->reserved != 0U ||
        primitives->vertex_data == nullptr || primitives->vertex_offset < 0 ||
        primitives->primitive_count <= 0 ||
        primitives->vertex_source > CNA_USER_VERTEX_SOURCE_POSITION_NORMAL_TEXTURE ||
        (indexed && primitives->num_vertices <= 0)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The user vertex description is invalid.");
    }
    // The canonical declaration-less raw overloads read their bytes as an array of the native
    // VertexPositionColor object, which carries a vtable and can never be produced from C. A raw
    // stream therefore always needs its declaration; the equivalent C-safe route for that overload
    // is the POSITION_COLOR source, which converts before it reaches CNA.
    if (primitives->vertex_source == CNA_USER_VERTEX_SOURCE_RAW_STREAM &&
        primitives->vertex_declaration == CNA_INVALID_HANDLE) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "A raw vertex stream requires an explicit vertex declaration.");
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
        const CNA_Result result = GetRuntimeHandles().Get(
            primitives->vertex_declaration, ObjectKind::VertexDeclaration, &declaration);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The VertexDeclaration handle is invalid for this call.");
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
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::OcclusionQuery, outQuery);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The OcclusionQuery handle is invalid for this call.");
}

template<typename TCallable>
[[nodiscard]] CNA_Result OcclusionQueryCommand(
    const CNA_Handle handle,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<OcclusionQueryResource> query;
        if (const CNA_Result result = GetOcclusionQuery(handle, &query);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::forward<TCallable>(callable)(*query->value);
        return CNA_RESULT_SUCCESS;
    });
}

template<typename TValue, typename TCallable>
[[nodiscard]] CNA_Result OcclusionQueryValue(
    const CNA_Handle handle,
    TValue* const output,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (output == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The occlusion-query output is null.");
        }
        std::shared_ptr<OcclusionQueryResource> query;
        if (const CNA_Result result = GetOcclusionQuery(handle, &query);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const TValue value =
            static_cast<TValue>(std::forward<TCallable>(callable)(*query->value));
        *output = value;
        return CNA_RESULT_SUCCESS;
    });
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
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The device event-registration output handle is null.");
    }
    *outRegistration = CNA_INVALID_HANDLE;
    if (callback == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The device event callback is null.");
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

CNA_Result cna_viewport_init(CNA_Viewport* const outValue)
{
    return StoreOutput(outValue, "The viewport output is null.", [] {
        return ToC(Viewport());
    });
}

CNA_Result cna_viewport_init_bounds(
    const int32_t x,
    const int32_t y,
    const int32_t width,
    const int32_t height,
    CNA_Viewport* const outValue)
{
    return StoreOutput(outValue, "The viewport output is null.", [=] {
        return ToC(Viewport(x, y, width, height));
    });
}

CNA_Result cna_viewport_init_from_rectangle(
    const CNA_Rectangle bounds,
    CNA_Viewport* const outValue)
{
    return StoreOutput(outValue, "The viewport output is null.", [=] {
        return ToC(Viewport(ToNative(bounds)));
    });
}

CNA_Result cna_viewport_get_aspect_ratio(
    const CNA_Viewport value,
    float* const outAspectRatio)
{
    return StoreOutput(outAspectRatio, "The float output is null.", [=] {
        return ToNative(value).getAspectRatioProperty();
    });
}

CNA_Result cna_viewport_get_bounds(
    const CNA_Viewport value,
    CNA_Rectangle* const outBounds)
{
    return StoreOutput(outBounds, "The rectangle output is null.", [=] {
        return ToC(ToNative(value).getBoundsProperty());
    });
}

CNA_Result cna_viewport_set_bounds(
    CNA_Viewport* const viewport,
    const CNA_Rectangle bounds)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (viewport == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The viewport is null.");
        }
        Viewport native = ToNative(*viewport);
        native.setBoundsProperty(ToNative(bounds));
        *viewport = ToC(native);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_viewport_get_title_safe_area(
    const CNA_Viewport value,
    CNA_Rectangle* const outArea)
{
    return StoreOutput(outArea, "The rectangle output is null.", [=] {
        return ToC(ToNative(value).getTitleSafeAreaProperty());
    });
}

CNA_Result cna_viewport_project(
    const CNA_Viewport value,
    const CNA_Vector3 source,
    const CNA_Matrix projection,
    const CNA_Matrix view,
    const CNA_Matrix world,
    CNA_Vector3* const outValue)
{
    return StoreOutput(outValue, "The vector output is null.", [=] {
        return ToC(ToNative(value).Project(
            ToNative(source), ToNative(projection), ToNative(view), ToNative(world)));
    });
}

CNA_Result cna_viewport_unproject(
    const CNA_Viewport value,
    const CNA_Vector3 source,
    const CNA_Matrix projection,
    const CNA_Matrix view,
    const CNA_Matrix world,
    CNA_Vector3* const outValue)
{
    return StoreOutput(outValue, "The vector output is null.", [=] {
        return ToC(ToNative(value).Unproject(
            ToNative(source), ToNative(projection), ToNative(view), ToNative(world)));
    });
}

CNA_Result cna_viewport_get_string_size(
    const CNA_Viewport value,
    uint64_t* const outBytes)
{
    return StoreOutput(outBytes, "The required-byte output is null.", [=] {
        return static_cast<uint64_t>(ToNative(value).ToString().size());
    });
}

CNA_Result cna_viewport_copy_string(
    const CNA_Viewport value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CopyFormattedString(destination, capacity, outBytes, [=] {
        return ToNative(value).ToString();
    });
}

CNA_Result cna_graphics_device_get_is_disposed(
    const CNA_Handle graphicsDeviceHandle,
    CNA_Bool* const outIsDisposed)
{
    return DeviceQuery(graphicsDeviceHandle, outIsDisposed, [](GraphicsDevice& device) {
        return device.getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_graphics_device_get_status(
    const CNA_Handle graphicsDeviceHandle,
    CNA_GraphicsDeviceStatus* const outStatus)
{
    return DeviceQuery(graphicsDeviceHandle, outStatus, [](GraphicsDevice& device) {
        return NativeOrdinal(device.getGraphicsDeviceStatusProperty());
    });
}

CNA_Result cna_graphics_device_get_adapter_index(
    const CNA_Handle graphicsDeviceHandle,
    uint32_t* const outAdapterIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAdapterIndex == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The adapter-index output is null.");
        }
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
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "The device's graphics adapter is no longer one of the enumerated adapters.");
    });
}

CNA_Result cna_graphics_device_get_graphics_profile(
    const CNA_Handle graphicsDeviceHandle,
    CNA_GraphicsProfile* const outProfile)
{
    return DeviceQuery(graphicsDeviceHandle, outProfile, [](GraphicsDevice& device) {
        return NativeOrdinal(device.getGraphicsProfileProperty());
    });
}

CNA_Result cna_graphics_device_get_scissor_rectangle(
    const CNA_Handle graphicsDeviceHandle,
    CNA_Rectangle* const outScissorRectangle)
{
    return DeviceQuery(graphicsDeviceHandle, outScissorRectangle, [](GraphicsDevice& device) {
        return ToC(device.getScissorRectangleProperty());
    });
}

CNA_Result cna_graphics_device_set_scissor_rectangle(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Rectangle scissorRectangle)
{
    return DeviceCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
        device.setScissorRectangleProperty(ToNative(scissorRectangle));
    });
}

CNA_Result cna_graphics_device_get_viewport(
    const CNA_Handle graphicsDeviceHandle,
    CNA_Viewport* const outViewport)
{
    return DeviceQuery(graphicsDeviceHandle, outViewport, [](GraphicsDevice& device) {
        return ToC(device.getViewportProperty());
    });
}

CNA_Result cna_graphics_device_set_viewport(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Viewport viewport)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!std::isfinite(viewport.min_depth) || !std::isfinite(viewport.max_depth)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The viewport depth range is not finite.");
        }
        return DeviceCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
            device.setViewportProperty(ToNative(viewport));
        });
    });
}

CNA_Result cna_graphics_device_get_blend_factor(
    const CNA_Handle graphicsDeviceHandle,
    CNA_Color* const outBlendFactor)
{
    return DeviceQuery(graphicsDeviceHandle, outBlendFactor, [](GraphicsDevice& device) {
        const Color value = device.getBlendFactorProperty();
        return CNA_Color{
            value.getRProperty(),
            value.getGProperty(),
            value.getBProperty(),
            value.getAProperty()};
    });
}

CNA_Result cna_graphics_device_set_blend_factor(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Color blendFactor)
{
    return DeviceCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
        device.setBlendFactorProperty(
            Color(blendFactor.r, blendFactor.g, blendFactor.b, blendFactor.a));
    });
}

CNA_Result cna_graphics_device_get_multi_sample_mask(
    const CNA_Handle graphicsDeviceHandle,
    int32_t* const outMultiSampleMask)
{
    return DeviceQuery(graphicsDeviceHandle, outMultiSampleMask, [](GraphicsDevice& device) {
        return static_cast<int32_t>(device.getMultiSampleMaskProperty());
    });
}

CNA_Result cna_graphics_device_set_multi_sample_mask(
    const CNA_Handle graphicsDeviceHandle,
    const int32_t multiSampleMask)
{
    return DeviceCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
        device.setMultiSampleMaskProperty(multiSampleMask);
    });
}

CNA_Result cna_graphics_device_get_reference_stencil(
    const CNA_Handle graphicsDeviceHandle,
    int32_t* const outReferenceStencil)
{
    return DeviceQuery(graphicsDeviceHandle, outReferenceStencil, [](GraphicsDevice& device) {
        return static_cast<int32_t>(device.getReferenceStencilProperty());
    });
}

CNA_Result cna_graphics_device_set_reference_stencil(
    const CNA_Handle graphicsDeviceHandle,
    const int32_t referenceStencil)
{
    return DeviceCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
        device.setReferenceStencilProperty(referenceStencil);
    });
}

CNA_Result cna_graphics_device_get_type_name_size(
    const CNA_Handle graphicsDeviceHandle,
    uint64_t* const outBytes)
{
    return DeviceQuery(graphicsDeviceHandle, outBytes, [](GraphicsDevice& device) {
        return static_cast<uint64_t>(device.GetTypeName().size());
    });
}

CNA_Result cna_graphics_device_copy_type_name(
    const CNA_Handle graphicsDeviceHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyFormattedString(destination, capacity, outBytes, [&] {
            return graphicsDevice->value->GetTypeName();
        });
    });
}

CNA_Result cna_graphics_device_dispose(const CNA_Handle graphicsDeviceHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return Fail(
            CNA_RESULT_NOT_SUPPORTED,
            CNA_ERROR_CATEGORY_NOT_SUPPORTED,
            "The active game owns this graphics device; destroy the game to dispose it.");
    });
}

CNA_Result cna_graphics_device_subscribe_event(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_GraphicsDeviceEvent deviceEvent,
    const CNA_GraphicsDeviceEventCallback callback,
    void* const context,
    CNA_GraphicsDeviceEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The device event-registration output handle is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The device event callback is null.");
        }
        if (deviceEvent > CNA_GRAPHICS_DEVICE_EVENT_DEVICE_RESETTING) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The graphics-device event identity is not recognized.");
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
    });
}

CNA_Result cna_graphics_device_subscribe_resource_created(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_GraphicsDeviceResourceCreatedCallback callback,
    void* const context,
    CNA_GraphicsDeviceEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
                    args.getResourceProperty() != nullptr ? CNA_TRUE : CNA_FALSE;
                callback(graphicsDeviceHandle, &info, context);
            });
        return StoreRegistration(
            std::make_shared<ResourceCreatedRegistration>(&source, token),
            outRegistration);
    });
}

CNA_Result cna_graphics_device_subscribe_resource_destroyed(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_GraphicsDeviceResourceDestroyedCallback callback,
    void* const context,
    CNA_GraphicsDeviceEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
                info.has_tag = args.getTagProperty() != nullptr ? CNA_TRUE : CNA_FALSE;
                info.name = ToStringView(&args.getNameProperty());
                callback(graphicsDeviceHandle, &info, context);
            });
        return StoreRegistration(
            std::make_shared<ResourceDestroyedRegistration>(&source, token),
            outRegistration);
    });
}

CNA_Result cna_graphics_device_unsubscribe(
    const CNA_GraphicsDeviceEventRegistrationHandle registrationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<DeviceRegistration> registration;
        const CNA_Result result = GetRuntimeHandles().Get(
            registrationHandle,
            ObjectKind::GraphicsDeviceEventRegistration,
            &registration);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The graphics-device event-registration handle is invalid.");
        }
        registration->Unsubscribe();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(registrationHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The graphics-device event-registration handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_texture(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ShaderStage stage,
    const uint32_t slot,
    CNA_TextureSlotInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_TextureSlotInfo) ||
            outInfo->struct_version != StructureVersion) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The texture-slot output structure is invalid.");
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
        outInfo->bound = nativeTexture != nullptr ? CNA_TRUE : CNA_FALSE;
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
    });
}

CNA_Result cna_graphics_device_set_texture(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ShaderStage stage,
    const uint32_t slot,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_graphics_device_unbind_texture(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_graphics_device_clear_rgba(
    const CNA_Handle graphicsDeviceHandle,
    const float r,
    const float g,
    const float b,
    const float a)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A clear color channel is not finite.");
        }
        return DeviceCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
            device.Clear(r, g, b, a);
        });
    });
}

CNA_Result cna_graphics_device_clear_color_depth(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Color color,
    const float depth)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!std::isfinite(depth)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The clear depth is not finite.");
        }
        return DeviceCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
            device.Clear(Color(color.r, color.g, color.b, color.a), depth);
        });
    });
}

CNA_Result cna_graphics_device_clear_options(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ClearOptions options,
    const CNA_Color color,
    const float depth,
    const int32_t stencil)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        constexpr CNA_ClearOptions ValidOptions =
            CNA_CLEAR_OPTION_TARGET | CNA_CLEAR_OPTION_DEPTH_BUFFER | CNA_CLEAR_OPTION_STENCIL;
        if ((options & ~ValidOptions) != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The clear options contain an unknown bit.");
        }
        if (!std::isfinite(depth)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The clear depth is not finite.");
        }
        return DeviceCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
            device.Clear(
                static_cast<NativeGraphics::ClearOptions>(options),
                Color(color.r, color.g, color.b, color.a),
                depth,
                stencil);
        });
    });
}

CNA_Result cna_graphics_device_present(const CNA_Handle graphicsDeviceHandle)
{
    return DeviceCommand(graphicsDeviceHandle, [](GraphicsDevice& device) {
        device.Present();
    });
}

CNA_Result cna_graphics_device_get_backbuffer_data_window(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_BackBufferReadback* const readback,
    CNA_Color* const destination,
    const uint64_t capacity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (readback == nullptr || readback->struct_size < sizeof(CNA_BackBufferReadback) ||
            readback->struct_version != StructureVersion ||
            (readback->has_source_rectangle != CNA_TRUE &&
             readback->has_source_rectangle != CNA_FALSE) ||
            readback->reserved[0] != 0U || readback->reserved[1] != 0U ||
            readback->reserved[2] != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The back-buffer readback structure is invalid.");
        }
        if (destination == nullptr && capacity != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The back-buffer destination is invalid.");
        }
        if (readback->start_index > static_cast<uint64_t>(INT32_MAX) ||
            readback->element_count > static_cast<uint64_t>(INT32_MAX)) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The back-buffer readback window exceeds the native element range.");
        }
        if (readback->start_index + readback->element_count > capacity) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the requested back-buffer window.");
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
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
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
    });
}

CNA_Result cna_graphics_device_set_vertex_buffer(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_VertexBufferHandle vertexBuffer)
{
    return cna_graphics_device_set_vertex_buffer_offset(graphicsDeviceHandle, vertexBuffer, 0);
}

CNA_Result cna_graphics_device_set_vertex_buffer_offset(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_VertexBufferHandle vertexBufferHandle,
    const int32_t vertexOffset)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (vertexOffset < 0) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The vertex offset must not be negative.");
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
    });
}

CNA_Result cna_graphics_device_set_vertex_buffers(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_VertexBufferBinding* const bindings,
    const uint64_t bindingCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (bindings == nullptr && bindingCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The vertex-buffer binding array is invalid.");
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
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_RANGE,
                    "A vertex-buffer binding offset or frequency is negative.");
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
    });
}

CNA_Result cna_graphics_device_get_vertex_buffer_count(
    const CNA_Handle graphicsDeviceHandle,
    uint64_t* const outCount)
{
    return DeviceQuery(graphicsDeviceHandle, outCount, [](GraphicsDevice& device) {
        return static_cast<uint64_t>(device.GetVertexBuffers().size());
    });
}

CNA_Result cna_graphics_device_copy_vertex_buffers(
    const CNA_Handle graphicsDeviceHandle,
    CNA_VertexBufferBinding* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The vertex-buffer binding destination or count output is invalid.");
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
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
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
    });
}

CNA_Result cna_graphics_device_get_vertex_buffer(
    const CNA_Handle graphicsDeviceHandle,
    CNA_VertexBufferHandle* const outVertexBuffer)
{
    return DeviceQuery(graphicsDeviceHandle, outVertexBuffer, [](GraphicsDevice& device) {
        return RecordedVertexBufferHandle(device.GetVertexBuffer());
    });
}

CNA_Result cna_graphics_device_set_index_buffer(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_IndexBufferHandle indexBufferHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_graphics_device_get_index_buffer(
    const CNA_Handle graphicsDeviceHandle,
    CNA_IndexBufferHandle* const outIndexBuffer)
{
    return DeviceQuery(graphicsDeviceHandle, outIndexBuffer, [](GraphicsDevice& device) {
        return RecordedIndexBufferHandle(device.GetIndexBuffer());
    });
}

CNA_Result cna_primitive_type_get_vertex_count(
    const CNA_PrimitiveType primitiveType,
    const int32_t primitiveCount,
    int32_t* const outVertexCount)
{
    return StoreOutput(outVertexCount, "The vertex-count output is null.", [=] {
        return static_cast<int32_t>(GraphicsDevice::PrimitiveVerts(
            static_cast<PrimitiveType>(primitiveType), primitiveCount));
    });
}

CNA_Result cna_graphics_device_draw_primitives(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_PrimitiveType primitiveType,
    const int32_t vertexStart,
    const int32_t primitiveCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidatePrimitiveType(primitiveType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return DeviceGuardedCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
            device.DrawPrimitives(
                static_cast<PrimitiveType>(primitiveType), vertexStart, primitiveCount);
        });
    });
}

CNA_Result cna_graphics_device_draw_indexed_primitives(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_PrimitiveType primitiveType,
    const int32_t baseVertex,
    const int32_t minVertexIndex,
    const int32_t numVertices,
    const int32_t startIndex,
    const int32_t primitiveCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidatePrimitiveType(primitiveType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return DeviceGuardedCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
            device.DrawIndexedPrimitives(
                static_cast<PrimitiveType>(primitiveType),
                baseVertex, minVertexIndex, numVertices, startIndex, primitiveCount);
        });
    });
}

CNA_Result cna_graphics_device_draw_instanced_primitives(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_PrimitiveType primitiveType,
    const int32_t baseVertex,
    const int32_t minVertexIndex,
    const int32_t numVertices,
    const int32_t startIndex,
    const int32_t primitiveCount,
    const int32_t instanceCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidatePrimitiveType(primitiveType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return DeviceGuardedCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
            device.DrawInstancedPrimitives(
                static_cast<PrimitiveType>(primitiveType),
                baseVertex, minVertexIndex, numVertices, startIndex, primitiveCount,
                instanceCount);
        });
    });
}

CNA_Result cna_graphics_device_draw_user_primitives(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_UserPrimitives* const primitives)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        UserPrimitiveRequest request;
        if (const CNA_Result result = ResolveUserPrimitives(
                graphicsDeviceHandle, primitives, false, &request);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        DrawUserPrimitivesNative(request);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_draw_user_indexed_primitives(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_UserPrimitives* const primitives,
    const CNA_UserIndices* const indices)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (indices == nullptr || indices->struct_size < sizeof(CNA_UserIndices) ||
            indices->struct_version != StructureVersion || indices->index_data == nullptr ||
            indices->index_offset < 0 ||
            (indices->index_element_size != CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS &&
             indices->index_element_size != CNA_INDEX_ELEMENT_SIZE_THIRTY_TWO_BITS)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The user index description is invalid.");
        }
        UserPrimitiveRequest request;
        if (const CNA_Result result = ResolveUserPrimitives(
                graphicsDeviceHandle, primitives, true, &request);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        DrawUserIndexedPrimitivesNative(request, *indices);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_tracked_resource_count(
    const CNA_Handle graphicsDeviceHandle,
    uint64_t* const outCount)
{
    return DeviceQuery(graphicsDeviceHandle, outCount, [](GraphicsDevice& device) {
        return static_cast<uint64_t>(device.GetTrackedResourceCount());
    });
}

CNA_Result cna_graphics_device_set_depth_test_enabled(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Bool enabled)
{
    return DeviceBooleanCommand(
        graphicsDeviceHandle, enabled, true, [](GraphicsDevice& device, const bool value) {
            device.SetDepthTestEnabled(value);
        });
}

CNA_Result cna_graphics_device_set_blend_enabled(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Bool enabled)
{
    return DeviceBooleanCommand(
        graphicsDeviceHandle, enabled, true, [](GraphicsDevice& device, const bool value) {
            device.SetBlendEnabled(value);
        });
}

CNA_Result cna_graphics_device_set_depth_write_enabled(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Bool enabled)
{
    return DeviceBooleanCommand(
        graphicsDeviceHandle, enabled, true, [](GraphicsDevice& device, const bool value) {
            device.SetDepthWriteEnabled(value);
        });
}

CNA_Result cna_graphics_device_set_graphics_profile_ext(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_GraphicsProfile profile)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (profile != NativeOrdinal(GraphicsProfile::Reach) &&
            profile != NativeOrdinal(GraphicsProfile::HiDef)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The graphics profile is not recognized.");
        }
        return DeviceCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
            device.SetGraphicsProfileEXT(static_cast<GraphicsProfile>(profile));
        });
    });
}

CNA_Result cna_graphics_device_set_context_recovery_enabled(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Bool enabled)
{
    return DeviceBooleanCommand(
        graphicsDeviceHandle, enabled, false, [](GraphicsDevice& device, const bool value) {
            device.SetContextRecoveryEnabled(value);
        });
}

CNA_Result cna_graphics_device_set_string_marker_ext(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_StringView marker)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string text;
        if (const CNA_Result result = CopyStringView(marker, true, &text);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return DeviceCommand(graphicsDeviceHandle, [&](GraphicsDevice& device) {
            device.SetStringMarkerEXT(text);
        });
    });
}

CNA_Result cna_graphics_device_get_unsupported_3d_call_behavior(
    const CNA_Handle graphicsDeviceHandle,
    CNA_Unsupported3DGraphicsCallBehavior* const outBehavior)
{
    return DeviceQuery(graphicsDeviceHandle, outBehavior, [](GraphicsDevice& device) {
        return NativeOrdinal(device.GetUnsupported3DGraphicsCallBehavior());
    });
}

CNA_Result cna_graphics_device_set_unsupported_3d_call_behavior(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Unsupported3DGraphicsCallBehavior behavior)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (behavior != CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_THROW &&
            behavior != CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_WARN_AND_STUB) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The unsupported-3D-call policy is not recognized.");
        }
        return DeviceCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
            device.SetUnsupported3DGraphicsCallBehavior(
                static_cast<CNA::Unsupported3DGraphicsCallBehavior>(behavior));
        });
    });
}

CNA_Result cna_graphics_device_set_current_effect(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_EffectHandle effectHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<EffectResource> effect;
        if (effectHandle != CNA_INVALID_HANDLE) {
            const CNA_Result result =
                GetRuntimeHandles().Get(effectHandle, ObjectKind::Effect, &effect);
            if (result != CNA_RESULT_SUCCESS) {
                return Fail(
                    result,
                    ErrorCategoryForResult(result),
                    "The Effect handle is invalid for this call.");
            }
        }

        Effect* const native = effect == nullptr ? nullptr : effect->value.get();
        graphicsDevice->value->SetCurrentEffect(native);

        DeviceBindingState& state = GetDeviceBindingState();
        std::lock_guard lock(state.mutex);
        state.currentEffect = effect == nullptr ? nullptr : effect->value;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_recreate_renderer_for_multi_sample_count_ext(
    const CNA_Handle graphicsDeviceHandle,
    const int32_t multiSampleCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (multiSampleCount < 0) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The multisample count must not be negative.");
        }
        return DeviceCommand(graphicsDeviceHandle, [=](GraphicsDevice& device) {
            device.RecreateRendererForMultiSampleCount(multiSampleCount);
        });
    });
}

CNA_Result cna_occlusion_query_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_OcclusionQueryHandle* const outOcclusionQuery)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outOcclusionQuery == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The occlusion-query output handle is null.");
        }
        *outOcclusionQuery = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!graphicsDevice->value->SupportsCapability(CNA::GraphicsCapability::OcclusionQuery)) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "The selected graphics backend does not support occlusion queries.");
        }

        auto native = std::make_shared<OcclusionQuery>(*graphicsDevice->value);
        const auto resource = std::make_shared<OcclusionQueryResource>(
            OcclusionQueryResource{std::move(native), graphicsDevice->parentGame});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::OcclusionQuery, resource, outOcclusionQuery);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned occlusion-query handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_occlusion_query_begin(const CNA_OcclusionQueryHandle occlusionQueryHandle)
{
    return OcclusionQueryCommand(occlusionQueryHandle, [](OcclusionQuery& query) {
        query.Begin();
    });
}

CNA_Result cna_occlusion_query_end(const CNA_OcclusionQueryHandle occlusionQueryHandle)
{
    return OcclusionQueryCommand(occlusionQueryHandle, [](OcclusionQuery& query) {
        query.End();
    });
}

CNA_Result cna_occlusion_query_get_is_complete(
    const CNA_OcclusionQueryHandle occlusionQueryHandle,
    CNA_Bool* const outIsComplete)
{
    return OcclusionQueryValue(occlusionQueryHandle, outIsComplete, [](OcclusionQuery& query) {
        return query.getIsCompleteProperty() ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_occlusion_query_get_pixel_count(
    const CNA_OcclusionQueryHandle occlusionQueryHandle,
    int32_t* const outPixelCount)
{
    return OcclusionQueryValue(occlusionQueryHandle, outPixelCount, [](OcclusionQuery& query) {
        return static_cast<int32_t>(query.getPixelCountProperty());
    });
}

// CBIND-104: whether the count above is a tally or a flag. A coverage ratio computed from a boolean
// count is 1/area rather than a fraction, so a caller that divides has to be able to ask first.
CNA_Result cna_occlusion_query_get_is_pixel_count_precise_ext(
    const CNA_OcclusionQueryHandle occlusionQueryHandle,
    CNA_Bool* const outIsPrecise)
{
    return OcclusionQueryValue(occlusionQueryHandle, outIsPrecise, [](OcclusionQuery& query) {
        return query.isPixelCountPreciseEXT() ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_occlusion_query_has_renderer(
    const CNA_OcclusionQueryHandle occlusionQueryHandle,
    CNA_Bool* const outHasRenderer)
{
    return OcclusionQueryValue(occlusionQueryHandle, outHasRenderer, [](OcclusionQuery& query) {
        return query.HasRenderer() ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_occlusion_query_destroy(const CNA_OcclusionQueryHandle occlusionQueryHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<OcclusionQueryResource> query;
        if (const CNA_Result result = GetOcclusionQuery(occlusionQueryHandle, &query);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        static_cast<System::IDisposable*>(query->value.get())->Dispose();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(occlusionQueryHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned occlusion-query handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(query->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

// CBIND-093. The GraphicsDevice EXT surface: ten queries and one command that the engine-layer
// slices needed but never bound, because each is a method on the always-compiled device rather
// than on an engine-layer object. They live here with their siblings and work in every build.

CNA_Result cna_graphics_device_notify_content_lost_resources_ext(
    const CNA_Handle graphicsDeviceHandle)
{
    return DeviceCommand(
        graphicsDeviceHandle,
        [](Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) { device.NotifyContentLostResourcesEXT(); });
}

CNA_Result cna_graphics_device_supports_surface_format_as_render_target_ext(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_SurfaceFormat format,
    CNA_Bool* const outSupported)
{
    return DeviceQuery<CNA_Bool>(
        graphicsDeviceHandle, outSupported, [format](const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) {
            return device.SupportsSurfaceFormatAsRenderTargetEXT(
                       static_cast<Microsoft::Xna::Framework::Graphics::SurfaceFormat>(format))
                ? CNA_TRUE
                : CNA_FALSE;
        });
}

CNA_Result cna_graphics_device_executes_shader_effect_source_ext(
    const CNA_Handle graphicsDeviceHandle, CNA_Bool* const outExecutes)
{
    return DeviceQuery<CNA_Bool>(
        graphicsDeviceHandle, outExecutes, [](const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) {
            return device.ExecutesShaderEffectSourceEXT() ? CNA_TRUE : CNA_FALSE;
        });
}

CNA_Result cna_graphics_device_supports_image_based_lighting_ext(
    const CNA_Handle graphicsDeviceHandle, CNA_Bool* const outSupported)
{
    return DeviceQuery<CNA_Bool>(
        graphicsDeviceHandle, outSupported, [](const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) {
            return device.SupportsImageBasedLightingEXT() ? CNA_TRUE : CNA_FALSE;
        });
}

CNA_Result cna_graphics_device_get_display_color_space_ext(
    const CNA_Handle graphicsDeviceHandle, uint32_t* const outSpace)
{
    return DeviceQuery<uint32_t>(
        graphicsDeviceHandle, outSpace, [](const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) {
            return static_cast<uint32_t>(device.GetDisplayColorSpaceEXT());
        });
}

namespace {

// CBIND-090 defined the colour-space identities; this is the same bound, restated here rather than
// exported, because the two routes below are the only users outside the engine layer.
[[nodiscard]] CNA_Result ValidateDisplayColorSpaceOrdinal(const uint32_t space) noexcept
{
    if (space > UINT32_C(2)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The display colour space is not a defined identity.");
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_graphics_device_set_display_color_space_ext(
    const CNA_Handle graphicsDeviceHandle, const uint32_t space, CNA_Bool* const outChanged)
{
    if (const CNA_Result result = ValidateDisplayColorSpaceOrdinal(space);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // Reports whether it worked rather than refusing: a display that cannot enter a space is an
    // ordinary answer, not a caller mistake.
    return DeviceQuery<CNA_Bool>(
        graphicsDeviceHandle, outChanged, [space](Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) {
            return device.SetDisplayColorSpaceEXT(static_cast<CNA::DisplayColorSpace>(space))
                ? CNA_TRUE
                : CNA_FALSE;
        });
}

CNA_Result cna_graphics_device_supports_display_color_space_ext(
    const CNA_Handle graphicsDeviceHandle, const uint32_t space, CNA_Bool* const outSupported)
{
    if (const CNA_Result result = ValidateDisplayColorSpaceOrdinal(space);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return DeviceQuery<CNA_Bool>(
        graphicsDeviceHandle, outSupported, [space](const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) {
            return device.SupportsDisplayColorSpaceEXT(
                       static_cast<CNA::DisplayColorSpace>(space))
                ? CNA_TRUE
                : CNA_FALSE;
        });
}

CNA_Result cna_graphics_device_get_max_compute_work_group_count_ext(
    const CNA_Handle graphicsDeviceHandle, const int32_t axis, int32_t* const outCount)
{
    // An axis outside zero to two answers zero rather than being refused, which is the canonical
    // behaviour: a caller looping over axes gets a usable number instead of an error to special-case.
    return DeviceQuery<int32_t>(
        graphicsDeviceHandle, outCount, [axis](const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) {
            return static_cast<int32_t>(
                device.GetMaxComputeWorkGroupCountEXT(static_cast<int>(axis)));
        });
}

CNA_Result cna_graphics_device_get_max_compute_work_group_size_ext(
    const CNA_Handle graphicsDeviceHandle, const int32_t axis, int32_t* const outSize)
{
    return DeviceQuery<int32_t>(
        graphicsDeviceHandle, outSize, [axis](const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) {
            return static_cast<int32_t>(
                device.GetMaxComputeWorkGroupSizeEXT(static_cast<int>(axis)));
        });
}

CNA_Result cna_graphics_device_get_max_compute_work_group_invocations_ext(
    const CNA_Handle graphicsDeviceHandle, int32_t* const outInvocations)
{
    return DeviceQuery<int32_t>(
        graphicsDeviceHandle, outInvocations, [](const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) {
            return static_cast<int32_t>(device.GetMaxComputeWorkGroupInvocationsEXT());
        });
}
