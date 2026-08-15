// SPDX-License-Identifier: MS-PL

#include "CNA/C/graphics_device.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "CNA/Unsupported3DGraphicsCallBehavior.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceStatus.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/EventArgs.hpp"
#include "System/Object.hpp"

#include <cmath>
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
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::TextureResourceView;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
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

    TextureSlotState& slots = GetTextureSlotState();
    std::lock_guard lock(slots.mutex);
    for (uint32_t slot = 0U; slot < CNA_TEXTURE_COLLECTION_MAX_TEXTURES; ++slot) {
        slots.pixel[slot] = BoundTextureSlot{};
        slots.vertex[slot] = BoundTextureSlot{};
    }
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
