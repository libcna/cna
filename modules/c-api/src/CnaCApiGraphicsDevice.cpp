// SPDX-License-Identifier: MS-PL

#include "CNA/C/graphics_device.h"
#include "CnaCApiDetail.hpp"

#include "CNA/Unsupported3DGraphicsCallBehavior.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceStatus.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

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

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::Viewport;

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

} // namespace

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
