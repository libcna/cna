// SPDX-License-Identifier: MS-PL

#include "CNA/C/runtime_graphics_manager.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceInformation.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/PreparingDeviceSettingsEventArgs.hpp"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ValidateCanonicalBool;

namespace {

using Microsoft::Xna::Framework::DisplayOrientation;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GraphicsDeviceInformation;
using Microsoft::Xna::Framework::GraphicsDeviceManager;
using Microsoft::Xna::Framework::PreparingDeviceSettingsEventArgs;
using Microsoft::Xna::Framework::PresentationMode;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::PresentInterval;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] CNA_Result InvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result CopyText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The manager text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the manager text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] bool IsSurfaceFormat(const CNA_SurfaceFormat value) noexcept
{
    return value <= CNA_SURFACE_FORMAT_USHORT_EXT;
}

[[nodiscard]] bool IsDepthFormat(const CNA_DepthFormat value) noexcept
{
    return value <= CNA_DEPTH_FORMAT_DEPTH24_STENCIL8;
}

[[nodiscard]] bool IsProfile(const CNA_GraphicsProfile value) noexcept
{
    return value <= CNA_GRAPHICS_PROFILE_HI_DEF;
}

[[nodiscard]] bool IsPresentInterval(const CNA_PresentInterval value) noexcept
{
    return value <= CNA_PRESENT_INTERVAL_IMMEDIATE;
}

[[nodiscard]] bool IsRenderTargetUsage(const CNA_RenderTargetUsage value) noexcept
{
    return value <= CNA_RENDER_TARGET_USAGE_PLATFORM_CONTENTS;
}

// A display orientation is a bit set rather than a single identity, so it is validated as one.
[[nodiscard]] bool IsDisplayOrientation(const CNA_DisplayOrientation value) noexcept
{
    constexpr CNA_DisplayOrientation Valid = CNA_DISPLAY_ORIENTATION_LANDSCAPE_LEFT |
        CNA_DISPLAY_ORIENTATION_LANDSCAPE_RIGHT | CNA_DISPLAY_ORIENTATION_PORTRAIT;
    return (value & ~Valid) == UINT32_C(0);
}

[[nodiscard]] bool IsPresentationParameters(
    const CNA_PresentationParameters& parameters) noexcept
{
    return parameters.struct_size >= sizeof(CNA_PresentationParameters) &&
        parameters.struct_version == StructureVersion &&
        IsSurfaceFormat(parameters.back_buffer_format) &&
        IsDepthFormat(parameters.depth_stencil_format) && parameters.multi_sample_count >= 0 &&
        IsPresentInterval(parameters.presentation_interval) &&
        IsDisplayOrientation(parameters.display_orientation) &&
        IsRenderTargetUsage(parameters.render_target_usage);
}

void ToCPresentationParameters(
    const PresentationParameters& source,
    CNA_PresentationParameters* const destination) noexcept
{
    CNA_PresentationParameters mapped = {};
    mapped.struct_size = sizeof(CNA_PresentationParameters);
    mapped.struct_version = StructureVersion;
    mapped.back_buffer_format = static_cast<CNA_SurfaceFormat>(source.getBackBufferFormatProperty());
    mapped.back_buffer_width = source.getBackBufferWidthProperty();
    mapped.back_buffer_height = source.getBackBufferHeightProperty();
    mapped.depth_stencil_format =
        static_cast<CNA_DepthFormat>(source.getDepthStencilFormatProperty());
    mapped.multi_sample_count = source.getMultiSampleCountProperty();
    mapped.presentation_interval =
        static_cast<CNA_PresentInterval>(source.getPresentationIntervalProperty());
    mapped.display_orientation =
        static_cast<CNA_DisplayOrientation>(source.getDisplayOrientationProperty());
    mapped.render_target_usage =
        static_cast<CNA_RenderTargetUsage>(source.getRenderTargetUsageProperty());
    mapped.is_full_screen = source.getIsFullScreenProperty() ? CNA_TRUE : CNA_FALSE;
    mapped.headless_ext = source.getHeadlessEXTProperty() ? CNA_TRUE : CNA_FALSE;
    *destination = mapped;
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

// The canonical configuration names an adapter by pointer; C names it by the index every adapter
// query in this ABI already uses, because a pointer into the runtime's adapter list is nothing a C
// caller could hold safely.
[[nodiscard]] int32_t AdapterIndexOf(const GraphicsAdapter* const adapter) noexcept
{
    if (adapter == nullptr) {
        return -1;
    }
    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    for (std::size_t index = 0U; index < adapters.size(); ++index) {
        if (adapters[index].get() == adapter) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

[[nodiscard]] GraphicsAdapter* AdapterAt(const int32_t index) noexcept
{
    if (index < 0) {
        return nullptr;
    }
    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    if (static_cast<std::size_t>(index) >= adapters.size()) {
        return nullptr;
    }
    return adapters[static_cast<std::size_t>(index)].get();
}

void ToCDeviceInformation(
    const GraphicsDeviceInformation& source,
    CNA_GraphicsDeviceInformation* const destination) noexcept
{
    CNA_GraphicsDeviceInformation mapped = {};
    mapped.struct_size = sizeof(CNA_GraphicsDeviceInformation);
    mapped.struct_version = StructureVersion;
    mapped.adapter_index = AdapterIndexOf(source.getAdapterProperty());
    mapped.graphics_profile = static_cast<CNA_GraphicsProfile>(source.getGraphicsProfileProperty());
    ToCPresentationParameters(source.getPresentationParametersProperty(), &mapped.presentation_parameters);
    *destination = mapped;
}

[[nodiscard]] bool IsDeviceInformation(
    const CNA_GraphicsDeviceInformation* const information) noexcept
{
    return information != nullptr &&
        information->struct_size >= sizeof(CNA_GraphicsDeviceInformation) &&
        information->struct_version == StructureVersion &&
        IsProfile(information->graphics_profile) &&
        IsPresentationParameters(information->presentation_parameters);
}

struct ManagerResource final {
    std::unique_ptr<GraphicsDeviceManager> value;
    CNA_Handle game = CNA_INVALID_HANDLE;
};

// The canonical game caches a raw `IGraphicsDeviceService*` the moment it first resolves one and
// **never clears it** -- not when the service is unregistered, not when the manager is disposed. A
// manager destroyed while its game still lives therefore leaves the game dereferencing freed memory
// on its very next frame, which is a canonical defect rather than anything this ABI can validate
// away. So releasing a manager handle disposes the manager and retires the object here, and the
// object is destroyed with the game. A disposed manager still answers the cached pointer correctly,
// because disposal does not touch the game-owned device it points at.
std::mutex& RetiredManagerMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::vector<std::shared_ptr<void>>& RetiredManagers()
{
    static std::vector<std::shared_ptr<void>> retired;
    return retired;
}

[[nodiscard]] CNA_Result BorrowManager(
    const CNA_Handle handle,
    std::shared_ptr<ManagerResource>* const outManager)
{
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Get(
        handle,
        ObjectKind::GraphicsDeviceManager,
        outManager);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The graphics device manager handle is invalid for this call.");
    }
    return CNA_RESULT_SUCCESS;
}

// Every device reconfiguration ends in the window and the renderer, and both report a refusal as a
// plain runtime error. That is the platform declining, not an internal fault -- the same rule the
// window state changes already follow.
template<typename TAction>
[[nodiscard]] CNA_Result RequestDeviceChange(TAction&& action)
{
    try {
        action();
    } catch (const std::runtime_error& exception) {
        return Fail(CNA_RESULT_PLATFORM, CNA_ERROR_CATEGORY_PLATFORM, exception.what());
    }
    return CNA_RESULT_SUCCESS;
}

class ManagerRegistrationBase {
public:
    ManagerRegistrationBase() = default;
    ManagerRegistrationBase(const ManagerRegistrationBase&) = delete;
    ManagerRegistrationBase& operator=(const ManagerRegistrationBase&) = delete;
    virtual ~ManagerRegistrationBase() = default;
};

template<typename TEventArgs>
class ManagerRegistration final : public ManagerRegistrationBase {
public:
    using Source = System::EventHandler<TEventArgs>;
    using Token = typename Source::Token;

    ManagerRegistration(std::shared_ptr<void> owner, Source* const source, const Token token)
        : owner_(std::move(owner))
        , source_(source)
        , token_(token)
    {
    }

    ~ManagerRegistration() override
    {
        source_->Remove(token_);
    }

private:
    std::shared_ptr<void> owner_;
    Source* source_;
    Token token_;
};

[[nodiscard]] CNA_Result PublishRegistration(
    std::shared_ptr<ManagerRegistrationBase> registration,
    CNA_Handle* const outRegistration)
{
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
        ObjectKind::GameEventRegistration,
        std::move(registration),
        outRegistration);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The manager registration could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_graphics_device_information_init(
    CNA_GraphicsDeviceInformation* const outInformation)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInformation == nullptr) {
            return InvalidInput("The device configuration output is null.");
        }
        const GraphicsDeviceInformation canonical;
        ToCDeviceInformation(canonical, outInformation);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_information_clone(
    const CNA_GraphicsDeviceInformation* const information,
    CNA_GraphicsDeviceInformation* const outInformation)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInformation == nullptr) {
            return InvalidInput("The device configuration output is null.");
        }
        if (!IsDeviceInformation(information)) {
            return InvalidInput("The device configuration is not a valid structure.");
        }
        GraphicsDeviceInformation canonical;
        canonical.setAdapterProperty(AdapterAt(information->adapter_index));
        canonical.setGraphicsProfileProperty(
            static_cast<GraphicsProfile>(information->graphics_profile));
        PresentationParameters parameters;
        ApplyPresentationParameters(information->presentation_parameters, &parameters);
        canonical.setPresentationParametersProperty(parameters);
        const GraphicsDeviceInformation clone = canonical.Clone();
        ToCDeviceInformation(clone, outInformation);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_information_get_type_name_size(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The type-name size output is null.");
        }
        const GraphicsDeviceInformation canonical;
        *outBytes = canonical.GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_information_copy_type_name(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        const GraphicsDeviceInformation canonical;
        return CopyText(canonical.GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_graphics_device_manager_create(
    const CNA_Handle gameHandle,
    CNA_GraphicsDeviceManagerHandle* const outManager)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outManager == nullptr) {
            return InvalidInput("The manager output is null.");
        }
        *outManager = CNA_INVALID_HANDLE;
        Game* game = nullptr;
        if (const CNA_Result result = CNA::C::Detail::GetGameObject(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<ManagerResource>();
        resource->game = gameHandle;
        resource->value = std::make_unique<GraphicsDeviceManager>(game);
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            ObjectKind::GraphicsDeviceManager,
            std::move(resource),
            outManager);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The manager handle could not be created.");
        }
        CNA::C::Detail::AddOwnedGameComponent();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_get_graphics_profile(
    const CNA_GraphicsDeviceManagerHandle manager,
    CNA_GraphicsProfile* const outProfile)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outProfile == nullptr) {
            return InvalidInput("The graphics profile output is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outProfile = static_cast<CNA_GraphicsProfile>(resource->value->getGraphicsProfileProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_set_graphics_profile(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_GraphicsProfile profile)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsProfile(profile)) {
            return InvalidInput("The graphics profile is not a defined identity.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setGraphicsProfileProperty(static_cast<GraphicsProfile>(profile));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_get_graphics_device(
    const CNA_GraphicsDeviceManagerHandle manager,
    CNA_Handle* const outGraphicsDevice)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGraphicsDevice == nullptr) {
            return InvalidInput("The graphics device output is null.");
        }
        *outGraphicsDevice = CNA_INVALID_HANDLE;
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (resource->value->getGraphicsDeviceProperty() == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The manager has no graphics device yet.");
        }
        return CNA::C::Detail::BorrowGameGraphicsDevice(resource->game, outGraphicsDevice);
    });
}

CNA_Result cna_graphics_device_manager_get_is_full_screen(
    const CNA_GraphicsDeviceManagerHandle manager,
    CNA_Bool* const outFullScreen)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFullScreen == nullptr) {
            return InvalidInput("The full-screen output is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outFullScreen = resource->value->getIsFullScreenProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_set_is_full_screen(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_Bool fullScreen)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(fullScreen, "full_screen");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setIsFullScreenProperty(fullScreen != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_get_prefer_multi_sampling(
    const CNA_GraphicsDeviceManagerHandle manager,
    CNA_Bool* const outPrefer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPrefer == nullptr) {
            return InvalidInput("The multisampling output is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outPrefer = resource->value->getPreferMultiSamplingProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_set_prefer_multi_sampling(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_Bool prefer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(prefer, "prefer");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setPreferMultiSamplingProperty(prefer != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_get_preferred_back_buffer_format(
    const CNA_GraphicsDeviceManagerHandle manager,
    CNA_SurfaceFormat* const outFormat)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFormat == nullptr) {
            return InvalidInput("The back-buffer format output is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outFormat =
            static_cast<CNA_SurfaceFormat>(resource->value->getPreferredBackBufferFormatProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_set_preferred_back_buffer_format(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_SurfaceFormat format)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsSurfaceFormat(format)) {
            return InvalidInput("The back-buffer format is not a defined identity.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setPreferredBackBufferFormatProperty(
            static_cast<SurfaceFormat>(format));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_get_preferred_back_buffer_width(
    const CNA_GraphicsDeviceManagerHandle manager,
    int32_t* const outWidth)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWidth == nullptr) {
            return InvalidInput("The back-buffer width output is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outWidth = static_cast<int32_t>(resource->value->getPreferredBackBufferWidthProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_set_preferred_back_buffer_width(
    const CNA_GraphicsDeviceManagerHandle manager,
    const int32_t width)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setPreferredBackBufferWidthProperty(
            static_cast<SharpRuntime::intcs>(width));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_get_preferred_back_buffer_height(
    const CNA_GraphicsDeviceManagerHandle manager,
    int32_t* const outHeight)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHeight == nullptr) {
            return InvalidInput("The back-buffer height output is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHeight = static_cast<int32_t>(resource->value->getPreferredBackBufferHeightProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_set_preferred_back_buffer_height(
    const CNA_GraphicsDeviceManagerHandle manager,
    const int32_t height)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setPreferredBackBufferHeightProperty(
            static_cast<SharpRuntime::intcs>(height));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_get_preferred_depth_stencil_format(
    const CNA_GraphicsDeviceManagerHandle manager,
    CNA_DepthFormat* const outFormat)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFormat == nullptr) {
            return InvalidInput("The depth format output is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outFormat = static_cast<CNA_DepthFormat>(
            resource->value->getPreferredDepthStencilFormatProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_set_preferred_depth_stencil_format(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_DepthFormat format)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsDepthFormat(format)) {
            return InvalidInput("The depth format is not a defined identity.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setPreferredDepthStencilFormatProperty(
            static_cast<DepthFormat>(format));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_get_synchronize_with_vertical_retrace(
    const CNA_GraphicsDeviceManagerHandle manager,
    CNA_Bool* const outSynchronize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSynchronize == nullptr) {
            return InvalidInput("The vertical-retrace output is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSynchronize =
            resource->value->getSynchronizeWithVerticalRetraceProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_set_synchronize_with_vertical_retrace(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_Bool synchronize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(synchronize, "synchronize");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setSynchronizeWithVerticalRetraceProperty(synchronize != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_get_supported_orientations(
    const CNA_GraphicsDeviceManagerHandle manager,
    CNA_DisplayOrientation* const outOrientations)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outOrientations == nullptr) {
            return InvalidInput("The orientation output is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outOrientations = static_cast<CNA_DisplayOrientation>(
            resource->value->getSupportedOrientationsProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_set_supported_orientations(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_DisplayOrientation orientations)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setSupportedOrientationsProperty(
            static_cast<DisplayOrientation>(orientations));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_get_preferred_presentation_mode_ext(
    const CNA_GraphicsDeviceManagerHandle manager,
    CNA_PresentationMode* const outMode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMode == nullptr) {
            return InvalidInput("The presentation mode output is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outMode = static_cast<CNA_PresentationMode>(
            resource->value->getPreferredPresentationModeProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_set_preferred_presentation_mode_ext(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_PresentationMode mode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (mode > CNA_PRESENTATION_MODE_MAXIMUM) {
            return InvalidInput("The presentation mode is not a defined identity.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setPreferredPresentationModeProperty(
            static_cast<PresentationMode>(mode));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_apply_changes(
    const CNA_GraphicsDeviceManagerHandle manager)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return RequestDeviceChange([&]() { resource->value->ApplyChanges(); });
    });
}

CNA_Result cna_graphics_device_manager_toggle_full_screen(
    const CNA_GraphicsDeviceManagerHandle manager)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return RequestDeviceChange([&]() { resource->value->ToggleFullScreen(); });
    });
}

CNA_Result cna_graphics_device_manager_create_device(
    const CNA_GraphicsDeviceManagerHandle manager)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return RequestDeviceChange([&]() { resource->value->CreateDevice(); });
    });
}

CNA_Result cna_graphics_device_manager_begin_draw(
    const CNA_GraphicsDeviceManagerHandle manager,
    CNA_Bool* const outShouldDraw)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outShouldDraw == nullptr) {
            return InvalidInput("The draw decision output is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outShouldDraw = resource->value->BeginDraw() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_end_draw(const CNA_GraphicsDeviceManagerHandle manager)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return RequestDeviceChange([&]() { resource->value->EndDraw(); });
    });
}

CNA_Result cna_graphics_device_manager_dispose(const CNA_GraphicsDeviceManagerHandle manager)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_get_type_name_size(
    const CNA_GraphicsDeviceManagerHandle manager,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The type-name size output is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = resource->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_copy_type_name(
    const CNA_GraphicsDeviceManagerHandle manager,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(resource->value->GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_graphics_device_manager_subscribe(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_GraphicsDeviceManagerEvent event,
    const CNA_GameEventCallback callback,
    void* const context,
    CNA_GameEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The manager registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The manager event callback is null.");
        }
        if (event > CNA_GRAPHICS_DEVICE_MANAGER_EVENT_MAXIMUM) {
            return InvalidInput("The manager event is not a defined identity.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::EventHandler<System::EventArgs>* source = nullptr;
        switch (event) {
        case CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DISPOSED:
            source = &resource->value->Disposed;
            break;
        case CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_CREATED:
            source = &resource->value->getDeviceCreatedEvent();
            break;
        case CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_DISPOSING:
            source = &resource->value->getDeviceDisposingEvent();
            break;
        case CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_RESET:
            source = &resource->value->getDeviceResetEvent();
            break;
        default:
            source = &resource->value->getDeviceResettingEvent();
            break;
        }
        const auto token = source->Add(
            [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
        return PublishRegistration(
            std::make_shared<ManagerRegistration<System::EventArgs>>(resource, source, token),
            outRegistration);
    });
}

CNA_Result cna_graphics_device_manager_subscribe_preparing_device_settings(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_PreparingDeviceSettingsCallback callback,
    void* const context,
    CNA_GameEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The manager registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The device settings callback is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const source = &resource->value->PreparingDeviceSettings;
        // The canonical handler collection delivers a const reference, so a subscriber -- in C++ as
        // much as in C -- can read the candidate settings and not change them. The C callback is
        // const for that reason rather than by choice.
        const auto token = source->Add(
            [callback, context](System::Object*, const PreparingDeviceSettingsEventArgs& args) {
                CNA_GraphicsDeviceInformation mapped = {};
                ToCDeviceInformation(args.getGraphicsDeviceInformationProperty(), &mapped);
                callback(&mapped, context);
            });
        return PublishRegistration(
            std::make_shared<ManagerRegistration<PreparingDeviceSettingsEventArgs>>(
                resource, source, token),
            outRegistration);
    });
}

CNA_Result cna_graphics_device_manager_subscribe_preparing_device_settings_ext(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_PreparingDeviceSettingsMutatorEXT callback,
    void* const context,
    CNA_GameEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The manager registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The device settings callback is null.");
        }
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const source = &resource->value->PreparingDeviceSettings;
        const auto token = source->Add(
            [callback, context](System::Object*, const PreparingDeviceSettingsEventArgs& args) {
                // The settings are held by pointer, so the const argument the handler collection
                // delivers never made them const. This accessor is what lets the canonical event
                // do in this runtime what it does in XNA.
                GraphicsDeviceInformation& canonical = args.getGraphicsDeviceInformationEXT();
                CNA_GraphicsDeviceInformation mapped = {};
                ToCDeviceInformation(canonical, &mapped);
                callback(&mapped, context);
                if (!IsDeviceInformation(&mapped)) {
                    // A handler that corrupted the structure or wrote an undefined identity is
                    // ignored rather than obeyed: half-applying it would fail device creation
                    // later, for a reason with no visible connection to what was written.
                    return;
                }
                canonical.setAdapterProperty(AdapterAt(mapped.adapter_index));
                canonical.setGraphicsProfileProperty(
                    static_cast<GraphicsProfile>(mapped.graphics_profile));
                PresentationParameters parameters =
                    canonical.getPresentationParametersProperty();
                ApplyPresentationParameters(mapped.presentation_parameters, &parameters);
                canonical.setPresentationParametersProperty(parameters);
            });
        return PublishRegistration(
            std::make_shared<ManagerRegistration<PreparingDeviceSettingsEventArgs>>(
                resource, source, token),
            outRegistration);
    });
}

CNA_Result cna_graphics_device_manager_destroy(const CNA_GraphicsDeviceManagerHandle manager)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Dispose();
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(manager);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The manager handle could not be released.");
        }
        {
            const std::lock_guard<std::mutex> lock(RetiredManagerMutex());
            RetiredManagers().push_back(std::move(resource));
        }
        CNA::C::Detail::RemoveOwnedGameComponent();
        return CNA_RESULT_SUCCESS;
    });
}

namespace CNA::C::Detail {

void ResetGraphicsDeviceManagerState() noexcept
{
    std::vector<std::shared_ptr<void>> retired;
    {
        const std::lock_guard<std::mutex> lock(RetiredManagerMutex());
        retired.swap(RetiredManagers());
    }
    retired.clear();
}

} // namespace CNA::C::Detail
