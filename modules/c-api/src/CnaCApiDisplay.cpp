// SPDX-License-Identifier: MS-PL

#include "CNA/C/display.h"
#include "CNA/C/graphics_device.h"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiMarshal.hpp"

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

using CNA::C::Detail::ApplyPresentationParameters;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::IsCanonicalBool;
using CNA::C::Detail::IsDepthFormat;
using CNA::C::Detail::IsDisplayOrientation;
using CNA::C::Detail::IsGraphicsProfile;
using CNA::C::Detail::IsPresentInterval;
using CNA::C::Detail::IsRenderTargetUsage;
using CNA::C::Detail::IsSurfaceFormat;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::NotSupported;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToCPresentationParameters;
using CNA::C::Detail::ValidateCanonicalBool;
using Microsoft::Xna::Framework::DisplayOrientation;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DisplayMode;
using Microsoft::Xna::Framework::Graphics::DisplayModeCollection;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::PresentInterval;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

constexpr uint32_t StructureVersion = UINT32_C(1);

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

[[nodiscard]] CNA_Result GetAdapterContext(
    const CNA_Handle handle,
    std::shared_ptr<void>* const outContext)
{
    // The adapters belong to the system, not to a device, and XNA reads GraphicsAdapter.Adapters
    // anywhere -- most often in a game's constructor, before a lifecycle callback can lend a
    // device. So the handle proving the runtime and thread may also be the active game's own,
    // which its creating thread holds from construction on (CBIND-145, ABI 0.40.0).
    using CNA::C::Detail::ObjectKind;
    ObjectKind kind = ObjectKind::Unknown;
    if (GetRuntimeHandles().GetKind(handle, &kind) == CNA_RESULT_SUCCESS && kind == ObjectKind::Game) {
        const CNA_Result result = LookupHandle(handle, ObjectKind::Game, outContext);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CNA_RESULT_SUCCESS;
    }
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    const CNA_Result result = GetBorrowedGraphicsDevice(handle, &graphicsDevice);
    if (result == CNA_RESULT_SUCCESS) {
        *outContext = std::move(graphicsDevice);
    }
    return result;
}

[[nodiscard]] CNA_Result GetAdapter(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t adapterIndex,
    std::shared_ptr<void>* const outContext,
    GraphicsAdapter** const outAdapter)
{
    if (outContext == nullptr || outAdapter == nullptr) {
        return InvalidArgument("The graphics-adapter query outputs are invalid.");
    }
    if (const CNA_Result result = GetAdapterContext(graphicsDeviceHandle, outContext);
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
    std::shared_ptr<void> context;
    GraphicsAdapter* adapter = nullptr;
    if (const CNA_Result result = GetAdapter(
            graphicsDeviceHandle, adapterIndex, &context, &adapter);
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
    if (!IsCanonicalBool(filterByFormat) || !IsSurfaceFormat(format) || outModes == nullptr) {
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
        IsRenderTargetUsage(parameters->render_target_usage) && IsCanonicalBool(parameters->is_full_screen) &&
        IsCanonicalBool(parameters->headless_ext) && parameters->reserved[0] == 0U &&
        parameters->reserved[1] == 0U;
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
    if (!HasOutputHeader(outSelection) || !IsGraphicsProfile(profile) || !IsSurfaceFormat(format) ||
        !IsDepthFormat(depthFormat) || multiSampleCount < 0) {
        return InvalidArgument("The graphics-format query is invalid.");
    }
    std::shared_ptr<void> context;
    GraphicsAdapter* adapter = nullptr;
    if (const CNA_Result result = GetAdapter(
            graphicsDeviceHandle, adapterIndex, &context, &adapter);
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
        .exact_match = ToCBool(exact),
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
        *outEqual = ToCBool(nativeLeft == nativeRight);
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
        std::shared_ptr<void> context;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &context, &adapter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outInfo = CNA_GraphicsAdapterInfo{
            .struct_size = sizeof(CNA_GraphicsAdapterInfo),
            .struct_version = StructureVersion,
            .adapter_index = adapterIndex,
            .is_default_adapter = ToCBool(adapter->getIsDefaultAdapterProperty()),
            .is_wide_screen = ToCBool(adapter->getIsWideScreenProperty()),
            .use_null_device = ToCBool(adapter->getUseNullDeviceProperty()),
            .use_reference_device = ToCBool(adapter->getUseReferenceDeviceProperty()),
            .vendor_id = adapter->getVendorIdProperty(),
            .device_id = adapter->getDeviceIdProperty(),
            .revision = adapter->getRevisionProperty(),
            .subsystem_id = adapter->getSubSystemIdProperty(),
            .description_byte_length = adapter->getDescriptionProperty().size(),
            .device_name_byte_length = adapter->getDeviceNameProperty().size()};
        return CNA_RESULT_SUCCESS;
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
        std::shared_ptr<void> context;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &context, &adapter);
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
        if (const CNA_Result result = ValidateCanonicalBool(filterByFormat, "filter_by_format");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<void> context;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &context, &adapter);
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
        if (const CNA_Result result = ValidateCanonicalBool(filterByFormat, "filter_by_format");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<void> context;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &context, &adapter);
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
        if (!IsCanonicalBool(useNullDevice) || !IsCanonicalBool(useReferenceDevice)) {
            return InvalidArgument("The graphics-adapter device preference is invalid.");
        }
        std::shared_ptr<void> context;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &context, &adapter);
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
        if (outSupported == nullptr || !IsGraphicsProfile(profile)) {
            return InvalidArgument("The graphics-profile query is invalid.");
        }
        std::shared_ptr<void> context;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &context, &adapter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = ToCBool(adapter->IsProfileSupported(static_cast<GraphicsProfile>(profile)));
        return CNA_RESULT_SUCCESS;
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
        std::shared_ptr<void> context;
        GraphicsAdapter* adapter = nullptr;
        if (const CNA_Result result = GetAdapter(
                graphicsDeviceHandle, adapterIndex, &context, &adapter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        static_cast<void>(adapter);
        return NotSupported("Native monitor handles do not cross the stable CNA C ABI.");
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
        return NotSupported("Native window handles do not cross the stable CNA C ABI.");
    });
}

CNA_Result cna_graphics_device_create(
    const uint32_t adapterIndex,
    const uint32_t graphicsProfile,
    const CNA_PresentationParameters* const parameters,
    CNA_Handle* const outGraphicsDevice)
{
    return CallWithExceptionBarrier([&]() {
        if (outGraphicsDevice == nullptr) {
            return InvalidArgument("The GraphicsDevice output handle is null.");
        }
        *outGraphicsDevice = CNA_INVALID_HANDLE;
        if (!IsPresentationParameters(parameters)) {
            return InvalidArgument("The PresentationParameters source structure is invalid.");
        }
        if (graphicsProfile != CNA_GRAPHICS_PROFILE_REACH &&
            graphicsProfile != CNA_GRAPHICS_PROFILE_HI_DEF) {
            return InvalidArgument("The graphics profile is not a public CNA profile identity.");
        }
        const auto& adapters = GraphicsAdapter::getAdaptersProperty();
        if (adapterIndex >= adapters.size()) {
            return InvalidArgument("The graphics adapter index is out of range.");
        }

        PresentationParameters nativeParameters;
        ApplyPresentationParameters(*parameters, &nativeParameters);

        auto owned = std::make_shared<CNA::C::Detail::OwnedGraphicsDevice>();
        // Construction is what acquires the platform video subsystem and the renderer, so it is
        // also what can fail. Letting it throw into the barrier keeps the failure a documented
        // result rather than a half-registered handle.
        owned->value = std::make_unique<GraphicsDevice>(
            *adapters[adapterIndex],
            static_cast<GraphicsProfile>(graphicsProfile),
            nativeParameters);

        if (const CNA_Result result = CreateHandle(CNA::C::Detail::ObjectKind::OwnedGraphicsDevice, owned, outGraphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The owner token is the device's own handle, so resources created on it are as
        // distinguishable from another device's as two games' resources are from each other.
        owned->view = std::make_shared<BorrowedGraphicsDevice>(
            BorrowedGraphicsDevice{owned->value.get(), *outGraphicsDevice});
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_destroy(const CNA_Handle graphicsDeviceHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CNA::C::Detail::OwnedGraphicsDevice> owned;
        const CNA_Result getResult = LookupHandle(graphicsDeviceHandle, CNA::C::Detail::ObjectKind::OwnedGraphicsDevice, &owned);
        if (getResult != CNA_RESULT_SUCCESS) {
            return getResult;
        }
        owned->value->Dispose();
        return ReleaseHandle(graphicsDeviceHandle);
    });
}

CNA_Result cna_graphics_device_reset_with_parameters(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_PresentationParameters* const parameters,
    const uint32_t* const adapterIndex)
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

        GraphicsAdapter* adapter = nullptr;
        if (adapterIndex != nullptr) {
            const auto& adapters = GraphicsAdapter::getAdaptersProperty();
            if (*adapterIndex >= adapters.size()) {
                return InvalidArgument("The graphics adapter index is out of range.");
            }
            adapter = adapters[*adapterIndex].get();
        }

        // The canonical window handle is renderer-private state that a C caller never supplies,
        // so it is preserved across the reset exactly as the set-parameters route does.
        PresentationParameters nativeParameters =
            graphicsDevice->value->getPresentationParametersProperty().Clone();
        const PresentationParameters::IntPtr windowHandle =
            nativeParameters.getDeviceWindowHandleProperty();
        ApplyPresentationParameters(*parameters, &nativeParameters);
        nativeParameters.setDeviceWindowHandleProperty(windowHandle);
        graphicsDevice->value->Reset(nativeParameters, adapter);
        return CNA_RESULT_SUCCESS;
    });
}

// The mechanically regular routes of this family, generated from modules/c-api/spec/Display.toml.
#include "generated/Display.inc"
