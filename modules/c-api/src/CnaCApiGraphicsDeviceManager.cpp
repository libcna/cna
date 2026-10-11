// SPDX-License-Identifier: MS-PL

#include "CNA/C/runtime_graphics_manager.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiMarshal.hpp"

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
using CNA::C::Detail::Fail;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;

namespace {

using CNA::C::Detail::ApplyPresentationParameters;
using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::IsDepthFormat;
using CNA::C::Detail::IsDisplayOrientation;
using CNA::C::Detail::IsGraphicsProfile;
using CNA::C::Detail::IsPresentInterval;
using CNA::C::Detail::IsRenderTargetUsage;
using CNA::C::Detail::IsSurfaceFormat;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::ToCPresentationParameters;

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

// A display orientation is a bit set rather than a single identity, so it is validated as one.

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
        IsGraphicsProfile(information->graphics_profile) &&
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
    const CNA_Result result = LookupHandle(handle, ObjectKind::GraphicsDeviceManager, outManager);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
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
    return CreateHandle(ObjectKind::GameEventRegistration,
        std::move(registration),
        outRegistration);
}

} // namespace

CNA_Result cna_graphics_device_information_init(
    CNA_GraphicsDeviceInformation* const outInformation)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInformation == nullptr) {
            return InvalidArgument("The device configuration output is null.");
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
            return InvalidArgument("The device configuration output is null.");
        }
        if (!IsDeviceInformation(information)) {
            return InvalidArgument("The device configuration is not a valid structure.");
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
            return InvalidArgument("The type-name size output is null.");
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
            return InvalidArgument("The manager output is null.");
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
        if (const CNA_Result result = CreateHandle(ObjectKind::GraphicsDeviceManager,
            std::move(resource),
            outManager);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        CNA::C::Detail::AddOwnedGameComponent();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_manager_set_graphics_profile(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_GraphicsProfile profile)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsGraphicsProfile(profile)) {
            return InvalidArgument("The graphics profile is not a defined identity.");
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
            return InvalidArgument("The graphics device output is null.");
        }
        *outGraphicsDevice = CNA_INVALID_HANDLE;
        std::shared_ptr<ManagerResource> resource;
        if (const CNA_Result result = BorrowManager(manager, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (resource->value->getGraphicsDeviceProperty() == nullptr) {
            return InvalidState("The manager has no graphics device yet.");
        }
        return CNA::C::Detail::BorrowGameGraphicsDevice(resource->game, outGraphicsDevice);
    });
}

CNA_Result cna_graphics_device_manager_set_preferred_back_buffer_format(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_SurfaceFormat format)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsSurfaceFormat(format)) {
            return InvalidArgument("The back-buffer format is not a defined identity.");
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

CNA_Result cna_graphics_device_manager_set_preferred_depth_stencil_format(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_DepthFormat format)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsDepthFormat(format)) {
            return InvalidArgument("The depth format is not a defined identity.");
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

CNA_Result cna_graphics_device_manager_set_preferred_presentation_mode_ext(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_PresentationMode mode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (mode > CNA_PRESENTATION_MODE_MAXIMUM) {
            return InvalidArgument("The presentation mode is not a defined identity.");
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

CNA_Result cna_graphics_device_manager_subscribe(
    const CNA_GraphicsDeviceManagerHandle manager,
    const CNA_GraphicsDeviceManagerEvent event,
    const CNA_GameEventCallback callback,
    void* const context,
    CNA_GameEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidArgument("The manager registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidArgument("The manager event callback is null.");
        }
        if (event > CNA_GRAPHICS_DEVICE_MANAGER_EVENT_MAXIMUM) {
            return InvalidArgument("The manager event is not a defined identity.");
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
            return InvalidArgument("The manager registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidArgument("The device settings callback is null.");
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
            return InvalidArgument("The manager registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidArgument("The device settings callback is null.");
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
        if (const CNA_Result result = ReleaseHandle(manager);
            result != CNA_RESULT_SUCCESS) {
            return result;
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

// The mechanically regular routes of this family, generated from modules/c-api/spec/GraphicsDeviceManager.toml.
#include "generated/GraphicsDeviceManager.inc"
