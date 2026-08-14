// SPDX-License-Identifier: MS-PL

#include "CNA/C/display.h"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayModeCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentInterval.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using Microsoft::Xna::Framework::DisplayOrientation;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DisplayMode;
using Microsoft::Xna::Framework::Graphics::DisplayModeCollection;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::PresentInterval;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] bool IsBool(const CNA_Bool value) noexcept
{
    return value == CNA_FALSE || value == CNA_TRUE;
}

[[nodiscard]] bool IsSurfaceFormat(const CNA_SurfaceFormat value) noexcept
{
    return value <= CNA_SURFACE_FORMAT_USHORT_EXT;
}

[[nodiscard]] bool IsDepthFormat(const CNA_DepthFormat value) noexcept
{
    return value <= CNA_DEPTH_FORMAT_DEPTH24_STENCIL8;
}

[[nodiscard]] bool IsUsage(const CNA_RenderTargetUsage value) noexcept
{
    return value <= CNA_RENDER_TARGET_USAGE_PLATFORM_CONTENTS;
}

[[nodiscard]] bool IsProfile(const CNA_GraphicsProfile value) noexcept
{
    return value <= CNA_GRAPHICS_PROFILE_HI_DEF;
}

[[nodiscard]] bool IsPresentInterval(const CNA_PresentInterval value) noexcept
{
    return value <= CNA_PRESENT_INTERVAL_IMMEDIATE;
}

[[nodiscard]] bool IsDisplayOrientation(const CNA_DisplayOrientation value) noexcept
{
    constexpr CNA_DisplayOrientation Valid =
        CNA_DISPLAY_ORIENTATION_LANDSCAPE_LEFT |
        CNA_DISPLAY_ORIENTATION_LANDSCAPE_RIGHT |
        CNA_DISPLAY_ORIENTATION_PORTRAIT;
    return (value & ~Valid) == 0U;
}

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

template<typename T>
[[nodiscard]] bool HasOutputHeader(const T* const value) noexcept
{
    return value != nullptr && value->struct_size >= sizeof(T) &&
        value->struct_version == StructureVersion;
}

void ToCDisplayMode(const DisplayMode& source, CNA_DisplayMode* const destination) noexcept
{
    *destination = CNA_DisplayMode{
        .struct_size = sizeof(CNA_DisplayMode),
        .struct_version = StructureVersion,
        .width = source.getWidthProperty(),
        .height = source.getHeightProperty(),
        .aspect_ratio = source.getAspectRatioProperty(),
        .format = static_cast<CNA_SurfaceFormat>(source.getFormatProperty())};
}

[[nodiscard]] bool IsDisplayMode(const CNA_DisplayMode* const mode) noexcept
{
    return mode != nullptr && mode->struct_size >= sizeof(CNA_DisplayMode) &&
        mode->struct_version == StructureVersion && IsSurfaceFormat(mode->format);
}

[[nodiscard]] CNA_Result GetAdapter(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    std::shared_ptr<BorrowedGraphicsDevice>* const outGraphicsDevice,
    GraphicsAdapter** const outAdapter)
{
    if (outGraphicsDevice == nullptr || outAdapter == nullptr) {
        return InvalidArgument("The graphics-adapter query outputs are invalid.");
    }
    if (const CNA_Result result = GetBorrowedGraphicsDevice(
            graphicsDeviceHandle, outGraphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    if (adapterIndex >= adapters.size()) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "The graphics-adapter index is out of range.");
    }
    *outAdapter = adapters[adapterIndex].get();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyAdapterString(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    const bool description,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The graphics-adapter string output buffer is invalid.");
    }
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    GraphicsAdapter* adapter = nullptr;
    if (const CNA_Result result = GetAdapter(
            graphicsDeviceHandle, adapterIndex, &graphicsDevice, &adapter);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::string& value = description
        ? adapter->getDescriptionProperty()
        : adapter->getDeviceNameProperty();
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The graphics-adapter string output buffer is too small.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CollectDisplayModes(
    GraphicsAdapter& adapter,
    const CNA_Bool filterByFormat,
    const CNA_SurfaceFormat format,
    std::vector<DisplayMode>* const outModes)
{
    if (!IsBool(filterByFormat) || !IsSurfaceFormat(format) || outModes == nullptr) {
        return InvalidArgument("The display-mode filter is invalid.");
    }
    const DisplayModeCollection& collection = adapter.getSupportedDisplayModesProperty();
    if (filterByFormat == CNA_TRUE) {
        *outModes = collection[static_cast<SurfaceFormat>(format)];
    } else {
        outModes->assign(collection.begin(), collection.end());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] bool IsPresentationParameters(
    const CNA_PresentationParameters* const parameters) noexcept
{
    return parameters != nullptr &&
        parameters->struct_size >= sizeof(CNA_PresentationParameters) &&
        parameters->struct_version == StructureVersion &&
        IsSurfaceFormat(parameters->back_buffer_format) &&
        IsDepthFormat(parameters->depth_stencil_format) &&
        parameters->multi_sample_count >= 0 &&
        IsPresentInterval(parameters->presentation_interval) &&
        IsDisplayOrientation(parameters->display_orientation) &&
        IsUsage(parameters->render_target_usage) && IsBool(parameters->is_full_screen) &&
        IsBool(parameters->headless_ext) && parameters->reserved[0] == 0U &&
        parameters->reserved[1] == 0U;
}

void ApplyPresentationParameters(
    const CNA_PresentationParameters& source,
    PresentationParameters* const destination)
{
    destination->setBackBufferFormatProperty(
        static_cast<SurfaceFormat>(source.back_buffer_format));
    destination->setBackBufferWidthProperty(source.back_buffer_width);
    destination->setBackBufferHeightProperty(source.back_buffer_height);
    destination->setDepthStencilFormatProperty(
        static_cast<DepthFormat>(source.depth_stencil_format));
    destination->setMultiSampleCountProperty(source.multi_sample_count);
    destination->setPresentationIntervalProperty(
        static_cast<PresentInterval>(source.presentation_interval));
    destination->setDisplayOrientationProperty(
        static_cast<DisplayOrientation>(source.display_orientation));
    destination->setRenderTargetUsageProperty(
        static_cast<RenderTargetUsage>(source.render_target_usage));
    destination->setIsFullScreenProperty(source.is_full_screen == CNA_TRUE);
    destination->setHeadlessEXTProperty(source.headless_ext == CNA_TRUE);
}

void ToCPresentationParameters(
    const PresentationParameters& source,
    CNA_PresentationParameters* const destination) noexcept
{
    *destination = CNA_PresentationParameters{
        .struct_size = sizeof(CNA_PresentationParameters),
        .struct_version = StructureVersion,
        .back_buffer_format = static_cast<CNA_SurfaceFormat>(
            source.getBackBufferFormatProperty()),
        .back_buffer_width = source.getBackBufferWidthProperty(),
        .back_buffer_height = source.getBackBufferHeightProperty(),
        .depth_stencil_format = static_cast<CNA_DepthFormat>(
            source.getDepthStencilFormatProperty()),
        .multi_sample_count = source.getMultiSampleCountProperty(),
        .presentation_interval = static_cast<CNA_PresentInterval>(
            source.getPresentationIntervalProperty()),
        .display_orientation = static_cast<CNA_DisplayOrientation>(
            source.getDisplayOrientationProperty()),
        .render_target_usage = static_cast<CNA_RenderTargetUsage>(
            source.getRenderTargetUsageProperty()),
        .is_full_screen = source.getIsFullScreenProperty() ? CNA_TRUE : CNA_FALSE,
        .headless_ext = source.getHeadlessEXTProperty() ? CNA_TRUE : CNA_FALSE,
        .reserved = {0U, 0U}};
}

[[nodiscard]] CNA_Result QueryFormat(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    const CNA_GraphicsProfile profile,
    const CNA_SurfaceFormat format,
    const CNA_DepthFormat depthFormat,
    const int32_t multiSampleCount,
    CNA_GraphicsFormatSelection* const outSelection,
    const bool renderTarget)
{
    if (!HasOutputHeader(outSelection) || !IsProfile(profile) || !IsSurfaceFormat(format) ||
        !IsDepthFormat(depthFormat) || multiSampleCount < 0) {
        return InvalidArgument("The graphics-format query is invalid.");
    }
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    GraphicsAdapter* adapter = nullptr;
    if (const CNA_Result result = GetAdapter(
            graphicsDeviceHandle, adapterIndex, &graphicsDevice, &adapter);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    SurfaceFormat selectedFormat = SurfaceFormat::Color;
    DepthFormat selectedDepthFormat = DepthFormat::None;
    int selectedMultiSampleCount = 0;
    const bool exact = renderTarget
        ? adapter->QueryRenderTargetFormat(
            static_cast<GraphicsProfile>(profile),
            static_cast<SurfaceFormat>(format),
            static_cast<DepthFormat>(depthFormat),
            multiSampleCount,
            selectedFormat,
            selectedDepthFormat,
            selectedMultiSampleCount)
        : adapter->QueryBackBufferFormat(
            static_cast<GraphicsProfile>(profile),
            static_cast<SurfaceFormat>(format),
            static_cast<DepthFormat>(depthFormat),
            multiSampleCount,
            selectedFormat,
            selectedDepthFormat,
            selectedMultiSampleCount);
    *outSelection = CNA_GraphicsFormatSelection{
        .struct_size = sizeof(CNA_GraphicsFormatSelection),
        .struct_version = StructureVersion,
        .exact_match = exact ? CNA_TRUE : CNA_FALSE,
        .reserved = {0U, 0U, 0U},
        .format = static_cast<CNA_SurfaceFormat>(selectedFormat),
        .depth_format = static_cast<CNA_DepthFormat>(selectedDepthFormat),
        .multi_sample_count = selectedMultiSampleCount};
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_display_mode_init(
    const int32_t width,
    const int32_t height,
    const CNA_SurfaceFormat format,
    CNA_DisplayMode* const outMode)
{
    return CallWithExceptionBarrier([&]() {
        if (outMode == nullptr || !IsSurfaceFormat(format)) {
            return InvalidArgument("The DisplayMode initialization arguments are invalid.");
        }
        ToCDisplayMode(DisplayMode(width, height, static_cast<SurfaceFormat>(format)), outMode);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_display_mode_equals(
    const CNA_DisplayMode* const left,
    const CNA_DisplayMode* const right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() {
        if (!IsDisplayMode(left) || !IsDisplayMode(right) || outEqual == nullptr) {
            return InvalidArgument("The DisplayMode comparison arguments are invalid.");
        }
        const DisplayMode nativeLeft(left->width, left->height, static_cast<SurfaceFormat>(left->format));
        const DisplayMode nativeRight(
            right->width, right->height, static_cast<SurfaceFormat>(right->format));
        *outEqual = nativeLeft == nativeRight ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_adapter_get_count(
    const CNA_Handle graphicsDeviceHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() {
        if (outCount == nullptr) {
            return InvalidArgument("The graphics-adapter count output is null.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = GraphicsAdapter::getAdaptersProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_adapter_get_info(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    CNA_GraphicsAdapterInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() {
        if (!HasOutputHeader(outInfo)) {
            return InvalidArgument("The graphics-adapter output structure is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &graphicsDevice, &adapter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outInfo = CNA_GraphicsAdapterInfo{
            .struct_size = sizeof(CNA_GraphicsAdapterInfo),
            .struct_version = StructureVersion,
            .adapter_index = adapterIndex,
            .is_default_adapter = adapter->getIsDefaultAdapterProperty() ? CNA_TRUE : CNA_FALSE,
            .is_wide_screen = adapter->getIsWideScreenProperty() ? CNA_TRUE : CNA_FALSE,
            .use_null_device = adapter->getUseNullDeviceProperty() ? CNA_TRUE : CNA_FALSE,
            .use_reference_device = adapter->getUseReferenceDeviceProperty()
                ? CNA_TRUE : CNA_FALSE,
            .vendor_id = adapter->getVendorIdProperty(),
            .device_id = adapter->getDeviceIdProperty(),
            .revision = adapter->getRevisionProperty(),
            .subsystem_id = adapter->getSubSystemIdProperty(),
            .description_byte_length = adapter->getDescriptionProperty().size(),
            .device_name_byte_length = adapter->getDeviceNameProperty().size()};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_adapter_copy_description(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() {
        return CopyAdapterString(
            graphicsDeviceHandle, adapterIndex, true, destination, capacity, outBytes);
    });
}

CNA_Result cna_graphics_adapter_copy_device_name(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() {
        return CopyAdapterString(
            graphicsDeviceHandle, adapterIndex, false, destination, capacity, outBytes);
    });
}

CNA_Result cna_graphics_adapter_get_current_display_mode(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    CNA_DisplayMode* const outMode)
{
    return CallWithExceptionBarrier([&]() {
        if (!HasOutputHeader(outMode)) {
            return InvalidArgument("The DisplayMode output structure is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &graphicsDevice, &adapter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ToCDisplayMode(adapter->getCurrentDisplayModeProperty(), outMode);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_adapter_get_display_mode_count(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    const CNA_Bool filterByFormat,
    const CNA_SurfaceFormat format,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() {
        if (outCount == nullptr) {
            return InvalidArgument("The display-mode count output is null.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &graphicsDevice, &adapter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<DisplayMode> modes;
        if (const CNA_Result result = CollectDisplayModes(
                *adapter, filterByFormat, format, &modes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = modes.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_adapter_copy_display_modes(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    const CNA_Bool filterByFormat,
    const CNA_SurfaceFormat format,
    CNA_DisplayMode* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The display-mode output buffer is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &graphicsDevice, &adapter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<DisplayMode> modes;
        if (const CNA_Result result = CollectDisplayModes(
                *adapter, filterByFormat, format, &modes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = modes.size();
        if (capacity < modes.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The display-mode output buffer is too small.");
        }
        for (std::size_t index = 0U; index < modes.size(); ++index) {
            ToCDisplayMode(modes[index], &destination[index]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_adapter_set_device_preferences(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    const CNA_Bool useNullDevice,
    const CNA_Bool useReferenceDevice)
{
    return CallWithExceptionBarrier([&]() {
        if (!IsBool(useNullDevice) || !IsBool(useReferenceDevice)) {
            return InvalidArgument("The graphics-adapter device preference is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &graphicsDevice, &adapter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        adapter->setUseNullDeviceProperty(useNullDevice == CNA_TRUE);
        adapter->setUseReferenceDeviceProperty(useReferenceDevice == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_adapter_is_profile_supported(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    const CNA_GraphicsProfile profile,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() {
        if (outSupported == nullptr || !IsProfile(profile)) {
            return InvalidArgument("The graphics-profile query is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &graphicsDevice, &adapter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = adapter->IsProfileSupported(static_cast<GraphicsProfile>(profile))
            ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_adapter_query_render_target_format(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    const CNA_GraphicsProfile profile,
    const CNA_SurfaceFormat format,
    const CNA_DepthFormat depthFormat,
    const int32_t multiSampleCount,
    CNA_GraphicsFormatSelection* const outSelection)
{
    return CallWithExceptionBarrier([&]() {
        return QueryFormat(
            graphicsDeviceHandle,
            adapterIndex,
            profile,
            format,
            depthFormat,
            multiSampleCount,
            outSelection,
            true);
    });
}

CNA_Result cna_graphics_adapter_query_backbuffer_format(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    const CNA_GraphicsProfile profile,
    const CNA_SurfaceFormat format,
    const CNA_DepthFormat depthFormat,
    const int32_t multiSampleCount,
    CNA_GraphicsFormatSelection* const outSelection)
{
    return CallWithExceptionBarrier([&]() {
        return QueryFormat(
            graphicsDeviceHandle,
            adapterIndex,
            profile,
            format,
            depthFormat,
            multiSampleCount,
            outSelection,
            false);
    });
}

CNA_Result cna_graphics_adapter_get_native_monitor_handle(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    CNA_NativeHandleValue* const outValue)
{
    return CallWithExceptionBarrier([&]() {
        if (outValue == nullptr) {
            return InvalidArgument("The native monitor-handle output is null.");
        }
        *outValue = UINT64_C(0);
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &graphicsDevice, &adapter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        static_cast<void>(adapter);
        return Fail(
            CNA_RESULT_NOT_SUPPORTED,
            CNA_ERROR_CATEGORY_NOT_SUPPORTED,
            "Native monitor handles do not cross the stable CNA C ABI.");
    });
}

CNA_Result cna_graphics_adapters_refresh(const CNA_Handle graphicsDeviceHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return Fail(
            CNA_RESULT_NOT_SUPPORTED,
            CNA_ERROR_CATEGORY_NOT_SUPPORTED,
            "The active C GraphicsDevice retains its adapter; refreshing the global native "
            "adapter cache would invalidate that reference.");
    });
}

CNA_Result cna_presentation_parameters_init(CNA_PresentationParameters* const outParameters)
{
    return CallWithExceptionBarrier([&]() {
        if (outParameters == nullptr) {
            return InvalidArgument("The PresentationParameters output is null.");
        }
        const PresentationParameters nativeParameters;
        ToCPresentationParameters(nativeParameters, outParameters);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_presentation_parameters_clone(
    const CNA_PresentationParameters* const source,
    CNA_PresentationParameters* const outParameters)
{
    return CallWithExceptionBarrier([&]() {
        if (!IsPresentationParameters(source) || outParameters == nullptr) {
            return InvalidArgument("The PresentationParameters clone arguments are invalid.");
        }
        PresentationParameters nativeSource;
        ApplyPresentationParameters(*source, &nativeSource);
        const PresentationParameters clone = nativeSource.Clone();
        ToCPresentationParameters(clone, outParameters);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_presentation_parameters_get_bounds(
    const CNA_PresentationParameters* const parameters,
    CNA_Rectangle* const outBounds)
{
    return CallWithExceptionBarrier([&]() {
        if (!IsPresentationParameters(parameters) || outBounds == nullptr) {
            return InvalidArgument("The PresentationParameters bounds arguments are invalid.");
        }
        PresentationParameters nativeParameters;
        ApplyPresentationParameters(*parameters, &nativeParameters);
        const Microsoft::Xna::Framework::Rectangle bounds = nativeParameters.getBoundsProperty();
        *outBounds = CNA_Rectangle{bounds.X, bounds.Y, bounds.Width, bounds.Height};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_presentation_parameters(
    const CNA_Handle graphicsDeviceHandle,
    CNA_PresentationParameters* const outParameters)
{
    return CallWithExceptionBarrier([&]() {
        if (!HasOutputHeader(outParameters)) {
            return InvalidArgument("The PresentationParameters output structure is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ToCPresentationParameters(
            graphicsDevice->value->getPresentationParametersProperty(), outParameters);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_set_presentation_parameters(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_PresentationParameters* const parameters)
{
    return CallWithExceptionBarrier([&]() {
        if (!IsPresentationParameters(parameters)) {
            return InvalidArgument("The PresentationParameters source structure is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        PresentationParameters nativeParameters =
            graphicsDevice->value->getPresentationParametersProperty().Clone();
        const PresentationParameters::IntPtr windowHandle =
            graphicsDevice->value->getPresentationParametersProperty()
                .getDeviceWindowHandleProperty();
        ApplyPresentationParameters(*parameters, &nativeParameters);
        nativeParameters.setDeviceWindowHandleProperty(windowHandle);
        graphicsDevice->value->SetPresentationParameters(nativeParameters);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_display_mode(
    const CNA_Handle graphicsDeviceHandle,
    CNA_DisplayMode* const outMode)
{
    return CallWithExceptionBarrier([&]() {
        if (!HasOutputHeader(outMode)) {
            return InvalidArgument("The DisplayMode output structure is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ToCDisplayMode(graphicsDevice->value->getDisplayModeProperty(), outMode);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_native_window_handle(
    const CNA_Handle graphicsDeviceHandle,
    CNA_NativeHandleValue* const outValue)
{
    return CallWithExceptionBarrier([&]() {
        if (outValue == nullptr) {
            return InvalidArgument("The native window-handle output is null.");
        }
        *outValue = UINT64_C(0);
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return Fail(
            CNA_RESULT_NOT_SUPPORTED,
            CNA_ERROR_CATEGORY_NOT_SUPPORTED,
            "Native window handles do not cross the stable CNA C ABI.");
    });
}
