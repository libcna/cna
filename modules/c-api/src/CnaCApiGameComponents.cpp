// SPDX-License-Identifier: MS-PL

#include "CNA/C/runtime_components.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/DrawableGameComponent.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameComponent.hpp"
#include "Microsoft/Xna/Framework/GameComponentCollection.hpp"
#include "Microsoft/Xna/Framework/GameComponentCollectionEventArgs.hpp"
#include "Microsoft/Xna/Framework/GameServiceContainer.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/IGraphicsDeviceService.hpp"
#include "Microsoft/Xna/Framework/IGraphicsDeviceManager.hpp"
#include "System/TimeSpan.hpp"

#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ValidateActiveGameHandle;

namespace {

using Microsoft::Xna::Framework::DrawableGameComponent;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GameComponent;
using Microsoft::Xna::Framework::GameComponentCollection;
using Microsoft::Xna::Framework::GameComponentCollectionEventArgs;
using Microsoft::Xna::Framework::GameServiceContainer;
using Microsoft::Xna::Framework::GameTime;
using Microsoft::Xna::Framework::IGameComponent;

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] CNA_Result InvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result NotDrawable()
{
    return Fail(
        CNA_RESULT_INVALID_STATE,
        CNA_ERROR_CATEGORY_STATE,
        "This game component does not draw.");
}

[[nodiscard]] CNA_Result CopyText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The component text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the component text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_GameTime MakeCGameTime(const GameTime& value) noexcept
{
    CNA_GameTime mapped = {};
    mapped.total_game_time_ticks =
        static_cast<int64_t>(value.getTotalGameTimeProperty().getTicksProperty());
    mapped.elapsed_game_time_ticks =
        static_cast<int64_t>(value.getElapsedGameTimeProperty().getTicksProperty());
    mapped.is_running_slowly = value.getIsRunningSlowlyProperty() ? CNA_TRUE : CNA_FALSE;
    return mapped;
}

[[nodiscard]] GameTime ToGameTime(const CNA_GameTime& value)
{
    return GameTime(
        System::TimeSpan(static_cast<SharpRuntime::longcs>(value.total_game_time_ticks)),
        System::TimeSpan(static_cast<SharpRuntime::longcs>(value.elapsed_game_time_ticks)),
        value.is_running_slowly != CNA_FALSE);
}

// A component is the one place in this ABI where the caller supplies behavior instead of consuming
// it: the canonical types are C++ interfaces, and C cannot implement one. So the ABI derives, and
// the derived object forwards every lifecycle step to the callback set. A null handler is simply not
// called, which is how a C component opts out of a step.
template<typename TBase>
class ForwardingComponent : public TBase {
public:
    ForwardingComponent(Game& game, const CNA_GameComponentCallbacks& callbacks)
        : TBase(game)
        , callbacks_(callbacks)
    {
    }

    void Initialize() override
    {
        TBase::Initialize();
        if (callbacks_.initialize != nullptr) {
            callbacks_.initialize(callbacks_.context);
        }
    }

    void Update(GameTime& gameTime) override
    {
        if (callbacks_.update != nullptr) {
            const CNA_GameTime mapped = MakeCGameTime(gameTime);
            callbacks_.update(&mapped, callbacks_.context);
        }
    }

protected:
    // The canonical disposal is idempotent, so the forwarded one must be too: without this a second
    // disposal would call the C handler again while the canonical event stayed silent.
    void ForwardDisposeOnce()
    {
        if (disposeForwarded_ || callbacks_.dispose == nullptr) {
            return;
        }
        disposeForwarded_ = true;
        callbacks_.dispose(callbacks_.context);
    }

    CNA_GameComponentCallbacks callbacks_;

private:
    bool disposeForwarded_ = false;
};

class CGameComponent final : public ForwardingComponent<GameComponent> {
public:
    using ForwardingComponent::ForwardingComponent;

protected:
    void Dispose(bool disposing) override
    {
        if (disposing) {
            ForwardDisposeOnce();
        }
        GameComponent::Dispose(disposing);
    }
};

class CDrawableGameComponent final : public ForwardingComponent<DrawableGameComponent> {
public:
    using ForwardingComponent::ForwardingComponent;

    void Draw(const GameTime& gameTime) override
    {
        if (callbacks_.draw != nullptr) {
            const CNA_GameTime mapped = MakeCGameTime(gameTime);
            callbacks_.draw(&mapped, callbacks_.context);
        }
    }

protected:
    void LoadContent() override
    {
        if (callbacks_.load_content != nullptr) {
            callbacks_.load_content(callbacks_.context);
        }
    }

    void UnloadContent() override
    {
        if (callbacks_.unload_content != nullptr) {
            callbacks_.unload_content(callbacks_.context);
        }
    }

    void Dispose(bool disposing) override
    {
        if (disposing) {
            ForwardDisposeOnce();
        }
        DrawableGameComponent::Dispose(disposing);
    }
};

struct ComponentResource final {
    std::unique_ptr<GameComponent> value;
    DrawableGameComponent* drawable = nullptr;
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_Handle handle = CNA_INVALID_HANDLE;
};

// The canonical collection stores raw interface pointers, so a handle cannot be recovered from one
// without this. It is what lets the collection routes and the collection events answer in handles
// rather than in addresses a C consumer could do nothing with.
std::mutex& ComponentRegistryMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<IGameComponent*, CNA_Handle>& ComponentRegistry()
{
    static std::map<IGameComponent*, CNA_Handle> registry;
    return registry;
}

[[nodiscard]] CNA_Handle HandleForComponent(IGameComponent* const component)
{
    if (component == nullptr) {
        return CNA_INVALID_HANDLE;
    }
    const std::lock_guard<std::mutex> lock(ComponentRegistryMutex());
    const auto found = ComponentRegistry().find(component);
    return found == ComponentRegistry().end() ? CNA_INVALID_HANDLE : found->second;
}

class ComponentRegistrationBase {
public:
    ComponentRegistrationBase() = default;
    ComponentRegistrationBase(const ComponentRegistrationBase&) = delete;
    ComponentRegistrationBase& operator=(const ComponentRegistrationBase&) = delete;
    virtual ~ComponentRegistrationBase() = default;
};

template<typename TEventArgs>
class ComponentRegistration final : public ComponentRegistrationBase {
public:
    using Source = System::EventHandler<TEventArgs>;
    using Token = typename Source::Token;

    ComponentRegistration(std::shared_ptr<void> owner, Source* const source, const Token token)
        : owner_(std::move(owner))
        , source_(source)
        , token_(token)
    {
    }

    ~ComponentRegistration() override
    {
        source_->Remove(token_);
    }

private:
    std::shared_ptr<void> owner_;
    Source* source_;
    Token token_;
};

[[nodiscard]] CNA_Result PublishRegistration(
    std::shared_ptr<ComponentRegistrationBase> registration,
    CNA_Handle* const outRegistration)
{
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
        ObjectKind::GameComponentEventRegistration,
        std::move(registration),
        outRegistration);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The component registration could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowComponent(
    const CNA_Handle handle,
    std::shared_ptr<ComponentResource>* const outComponent)
{
    const CNA_Result result =
        CNA::C::Detail::GetRuntimeHandles().Get(handle, ObjectKind::GameComponent, outComponent);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The game component handle is invalid for this call.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowComponents(
    const CNA_Handle gameHandle,
    GameComponentCollection** const outComponents)
{
    Game* game = nullptr;
    if (const CNA_Result result = CNA::C::Detail::GetGameObject(gameHandle, &game);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outComponents = &game->getComponentsProperty();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowServices(
    const CNA_Handle gameHandle,
    GameServiceContainer** const outServices)
{
    Game* game = nullptr;
    if (const CNA_Result result = CNA::C::Detail::GetGameObject(gameHandle, &game);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outServices = &game->getServicesProperty();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] bool ValidCallbacks(const CNA_GameComponentCallbacks* const callbacks) noexcept
{
    return callbacks != nullptr && callbacks->struct_size >= sizeof(CNA_GameComponentCallbacks) &&
        callbacks->struct_version == StructureVersion;
}

template<typename TComponent>
[[nodiscard]] CNA_Result CreateComponent(
    const CNA_Handle gameHandle,
    const CNA_GameComponentCallbacks* const callbacks,
    CNA_GameComponentHandle* const outComponent)
{
    if (outComponent == nullptr) {
        return InvalidInput("The game component output is null.");
    }
    *outComponent = CNA_INVALID_HANDLE;
    if (!ValidCallbacks(callbacks)) {
        return InvalidInput("The game component callback set is not a valid structure.");
    }
    Game* game = nullptr;
    if (const CNA_Result result = CNA::C::Detail::GetGameObject(gameHandle, &game);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto resource = std::make_shared<ComponentResource>();
    auto component = std::make_unique<TComponent>(*game, *callbacks);
    if constexpr (std::is_base_of_v<DrawableGameComponent, TComponent>) {
        resource->drawable = component.get();
    }
    resource->game = gameHandle;
    IGameComponent* const key = component.get();
    resource->value = std::move(component);
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
        ObjectKind::GameComponent,
        resource,
        outComponent);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The game component handle could not be created.");
    }
    resource->handle = *outComponent;
    {
        const std::lock_guard<std::mutex> lock(ComponentRegistryMutex());
        ComponentRegistry()[key] = *outComponent;
    }
    CNA::C::Detail::AddOwnedGameComponent();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowDrawable(
    const CNA_Handle handle,
    std::shared_ptr<ComponentResource>* const outResource)
{
    if (const CNA_Result result = BorrowComponent(handle, outResource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((*outResource)->drawable == nullptr) {
        return NotDrawable();
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_game_component_callbacks_init(CNA_GameComponentCallbacks* const outCallbacks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCallbacks == nullptr) {
            return InvalidInput("The component callback output is null.");
        }
        CNA_GameComponentCallbacks callbacks = {};
        callbacks.struct_size = sizeof(CNA_GameComponentCallbacks);
        callbacks.struct_version = StructureVersion;
        *outCallbacks = callbacks;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_component_create(
    const CNA_Handle gameHandle,
    const CNA_GameComponentCallbacks* const callbacks,
    CNA_GameComponentHandle* const outComponent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CreateComponent<CGameComponent>(gameHandle, callbacks, outComponent);
    });
}

CNA_Result cna_drawable_game_component_create(
    const CNA_Handle gameHandle,
    const CNA_GameComponentCallbacks* const callbacks,
    CNA_GameComponentHandle* const outComponent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CreateComponent<CDrawableGameComponent>(gameHandle, callbacks, outComponent);
    });
}

CNA_Result cna_game_component_get_is_drawable(
    const CNA_GameComponentHandle component,
    CNA_Bool* const outDrawable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDrawable == nullptr) {
            return InvalidInput("The drawable output is null.");
        }
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outDrawable = resource->drawable != nullptr ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_component_get_game(
    const CNA_GameComponentHandle component,
    CNA_Handle* const outGame)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGame == nullptr) {
            return InvalidInput("The game output is null.");
        }
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // Reading the canonical property proves the component still points at the same game the
        // handle records; the handle is what a C caller can use.
        (void)&resource->value->getGameProperty();
        *outGame = resource->game;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_component_get_enabled(
    const CNA_GameComponentHandle component,
    CNA_Bool* const outEnabled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEnabled == nullptr) {
            return InvalidInput("The enabled output is null.");
        }
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEnabled = resource->value->getEnabledProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_component_set_enabled(
    const CNA_GameComponentHandle component,
    const CNA_Bool enabled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setEnabledProperty(enabled != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_component_get_update_order(
    const CNA_GameComponentHandle component,
    int32_t* const outOrder)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outOrder == nullptr) {
            return InvalidInput("The update order output is null.");
        }
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outOrder = static_cast<int32_t>(resource->value->getUpdateOrderProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_component_set_update_order(
    const CNA_GameComponentHandle component,
    const int32_t order)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setUpdateOrderProperty(static_cast<SharpRuntime::intcs>(order));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_drawable_game_component_get_draw_order(
    const CNA_GameComponentHandle component,
    int32_t* const outOrder)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outOrder == nullptr) {
            return InvalidInput("The draw order output is null.");
        }
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowDrawable(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outOrder = static_cast<int32_t>(resource->drawable->getDrawOrderProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_drawable_game_component_set_draw_order(
    const CNA_GameComponentHandle component,
    const int32_t order)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowDrawable(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->drawable->setDrawOrderProperty(static_cast<SharpRuntime::Int32>(order));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_drawable_game_component_get_visible(
    const CNA_GameComponentHandle component,
    CNA_Bool* const outVisible)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVisible == nullptr) {
            return InvalidInput("The visibility output is null.");
        }
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowDrawable(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outVisible = resource->drawable->getVisibleProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_drawable_game_component_set_visible(
    const CNA_GameComponentHandle component,
    const CNA_Bool visible)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowDrawable(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->drawable->setVisibleProperty(visible != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_drawable_game_component_get_graphics_device(
    const CNA_GameComponentHandle component,
    CNA_Handle* const outGraphicsDevice)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGraphicsDevice == nullptr) {
            return InvalidInput("The graphics device output is null.");
        }
        *outGraphicsDevice = CNA_INVALID_HANDLE;
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowDrawable(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // Reading the canonical property first keeps the failure the canonical one when the game has
        // no device; the handle a caller gets is the game's own borrowed device.
        (void)&resource->drawable->getGraphicsDeviceProperty();
        return CNA::C::Detail::BorrowGameGraphicsDevice(resource->game, outGraphicsDevice);
    });
}

CNA_Result cna_game_component_compare_to(
    const CNA_GameComponentHandle component,
    const CNA_GameComponentHandle other,
    int32_t* const outOrder)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outOrder == nullptr) {
            return InvalidInput("The comparison output is null.");
        }
        std::shared_ptr<ComponentResource> first;
        if (const CNA_Result result = BorrowComponent(component, &first);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ComponentResource> second;
        if (const CNA_Result result = BorrowComponent(other, &second);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outOrder = static_cast<int32_t>(first->value->CompareTo(*second->value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_component_initialize(const CNA_GameComponentHandle component)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Initialize();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_component_update(
    const CNA_GameComponentHandle component,
    const CNA_GameTime* const gameTime)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (gameTime == nullptr) {
            return InvalidInput("The game time is null.");
        }
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        GameTime value = ToGameTime(*gameTime);
        resource->value->Update(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_drawable_game_component_draw(
    const CNA_GameComponentHandle component,
    const CNA_GameTime* const gameTime)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (gameTime == nullptr) {
            return InvalidInput("The game time is null.");
        }
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowDrawable(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const GameTime value = ToGameTime(*gameTime);
        resource->drawable->Draw(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_component_dispose(const CNA_GameComponentHandle component)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_component_get_type_name_size(
    const CNA_GameComponentHandle component,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The type-name size output is null.");
        }
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = resource->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_component_copy_type_name(
    const CNA_GameComponentHandle component,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(resource->value->GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_game_component_subscribe(
    const CNA_GameComponentHandle component,
    const CNA_GameComponentEvent event,
    const CNA_GameComponentEventCallback callback,
    void* const context,
    CNA_GameComponentEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The component registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The component event callback is null.");
        }
        if (event > CNA_GAME_COMPONENT_EVENT_MAXIMUM) {
            return InvalidInput("The component event is not a defined identity.");
        }
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if ((event == CNA_GAME_COMPONENT_EVENT_DRAW_ORDER_CHANGED ||
             event == CNA_GAME_COMPONENT_EVENT_VISIBLE_CHANGED) &&
            resource->drawable == nullptr) {
            return NotDrawable();
        }
        System::EventHandler<System::EventArgs>* source = nullptr;
        switch (event) {
        case CNA_GAME_COMPONENT_EVENT_ENABLED_CHANGED:
            source = &resource->value->getEnabledChangedEvent();
            break;
        case CNA_GAME_COMPONENT_EVENT_UPDATE_ORDER_CHANGED:
            source = &resource->value->getUpdateOrderChangedEvent();
            break;
        case CNA_GAME_COMPONENT_EVENT_DRAW_ORDER_CHANGED:
            source = &resource->drawable->getDrawOrderChangedEvent();
            break;
        case CNA_GAME_COMPONENT_EVENT_VISIBLE_CHANGED:
            source = &resource->drawable->getVisibleChangedEvent();
            break;
        default:
            source = &resource->value->Disposed;
            break;
        }
        const auto token = source->Add(
            [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
        return PublishRegistration(
            std::make_shared<ComponentRegistration<System::EventArgs>>(resource, source, token),
            outRegistration);
    });
}

CNA_Result cna_game_component_unsubscribe(
    const CNA_GameComponentEventRegistrationHandle registration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComponentRegistrationBase> value;
        if (const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Get(
                registration,
                ObjectKind::GameComponentEventRegistration,
                &value);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The component registration handle is invalid for this call.");
        }
        const CNA_Result releaseResult =
            CNA::C::Detail::GetRuntimeHandles().Release(registration);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The component registration handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_component_destroy(const CNA_GameComponentHandle component)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // A handle-based ABI must not leave the runtime holding a pointer to something it has
        // released, so membership ends here even though the canonical destructor leaves it alone.
        GameComponentCollection* components = nullptr;
        if (BorrowComponents(resource->game, &components) == CNA_RESULT_SUCCESS) {
            while (components->Remove(resource->value.get())) {
            }
        }
        {
            const std::lock_guard<std::mutex> lock(ComponentRegistryMutex());
            ComponentRegistry().erase(resource->value.get());
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(component);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The game component handle could not be released.");
        }
        CNA::C::Detail::RemoveOwnedGameComponent();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_components_get_count(const CNA_Handle gameHandle, uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The component count output is null.");
        }
        GameComponentCollection* components = nullptr;
        if (const CNA_Result result = BorrowComponents(gameHandle, &components);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(components->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_components_get_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    CNA_GameComponentHandle* const outComponent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outComponent == nullptr) {
            return InvalidInput("The component output is null.");
        }
        *outComponent = CNA_INVALID_HANDLE;
        GameComponentCollection* components = nullptr;
        if (const CNA_Result result = BorrowComponents(gameHandle, &components);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index >= static_cast<uint64_t>(components->getCountProperty())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The component index is at or past the collection count.");
        }
        *outComponent = HandleForComponent(
            (*components)[static_cast<GameComponentCollection::size_type>(index)]);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_components_add(
    const CNA_Handle gameHandle,
    const CNA_GameComponentHandle component)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        GameComponentCollection* components = nullptr;
        if (const CNA_Result result = BorrowComponents(gameHandle, &components);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        components->Add(resource->value.get());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_components_insert(
    const CNA_Handle gameHandle,
    const uint64_t index,
    const CNA_GameComponentHandle component)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        GameComponentCollection* components = nullptr;
        if (const CNA_Result result = BorrowComponents(gameHandle, &components);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index > static_cast<uint64_t>(components->getCountProperty())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The insert index is past the collection count.");
        }
        components->Insert(
            static_cast<GameComponentCollection::size_type>(index),
            resource->value.get());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_components_remove(
    const CNA_Handle gameHandle,
    const CNA_GameComponentHandle component,
    CNA_Bool* const outRemoved)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRemoved == nullptr) {
            return InvalidInput("The removal output is null.");
        }
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        GameComponentCollection* components = nullptr;
        if (const CNA_Result result = BorrowComponents(gameHandle, &components);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outRemoved = components->Remove(resource->value.get()) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_components_remove_at(const CNA_Handle gameHandle, const uint64_t index)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        GameComponentCollection* components = nullptr;
        if (const CNA_Result result = BorrowComponents(gameHandle, &components);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index >= static_cast<uint64_t>(components->getCountProperty())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The component index is at or past the collection count.");
        }
        components->RemoveAt(static_cast<GameComponentCollection::size_type>(index));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_components_clear(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        GameComponentCollection* components = nullptr;
        if (const CNA_Result result = BorrowComponents(gameHandle, &components);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        components->Clear();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_components_contains(
    const CNA_Handle gameHandle,
    const CNA_GameComponentHandle component,
    CNA_Bool* const outContains)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContains == nullptr) {
            return InvalidInput("The containment output is null.");
        }
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        GameComponentCollection* components = nullptr;
        if (const CNA_Result result = BorrowComponents(gameHandle, &components);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outContains = components->Contains(resource->value.get()) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_components_index_of(
    const CNA_Handle gameHandle,
    const CNA_GameComponentHandle component,
    int32_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr) {
            return InvalidInput("The index output is null.");
        }
        std::shared_ptr<ComponentResource> resource;
        if (const CNA_Result result = BorrowComponent(component, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        GameComponentCollection* components = nullptr;
        if (const CNA_Result result = BorrowComponents(gameHandle, &components);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIndex = static_cast<int32_t>(components->IndexOf(resource->value.get()));
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

[[nodiscard]] CNA_Result SubscribeCollection(
    const CNA_Handle gameHandle,
    const bool added,
    const CNA_GameComponentCollectionCallback callback,
    void* const context,
    CNA_GameComponentEventRegistrationHandle* const outRegistration)
{
    if (outRegistration == nullptr) {
        return InvalidInput("The component registration output is null.");
    }
    *outRegistration = CNA_INVALID_HANDLE;
    if (callback == nullptr) {
        return InvalidInput("The component collection callback is null.");
    }
    GameComponentCollection* components = nullptr;
    if (const CNA_Result result = BorrowComponents(gameHandle, &components);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const source = added ? &components->ComponentAdded : &components->ComponentRemoved;
    const auto token = source->Add(
        [callback, context](System::Object*, const GameComponentCollectionEventArgs& args) {
            callback(HandleForComponent(args.getGameComponentProperty()), context);
        });
    // The collection belongs to the game, which outlives every registration a caller can hold: a
    // game refuses to be destroyed while any component handle is alive, and releasing the last one
    // is the caller's own signal to release these too.
    return PublishRegistration(
        std::make_shared<ComponentRegistration<GameComponentCollectionEventArgs>>(
            nullptr, source, token),
        outRegistration);
}

} // namespace

CNA_Result cna_game_components_subscribe_added(
    const CNA_Handle gameHandle,
    const CNA_GameComponentCollectionCallback callback,
    void* const context,
    CNA_GameComponentEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SubscribeCollection(gameHandle, true, callback, context, outRegistration);
    });
}

CNA_Result cna_game_components_subscribe_removed(
    const CNA_Handle gameHandle,
    const CNA_GameComponentCollectionCallback callback,
    void* const context,
    CNA_GameComponentEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SubscribeCollection(gameHandle, false, callback, context, outRegistration);
    });
}

CNA_Result cna_game_components_get_type_name_size(
    const CNA_Handle gameHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The type-name size output is null.");
        }
        GameComponentCollection* components = nullptr;
        if (const CNA_Result result = BorrowComponents(gameHandle, &components);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = components->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_components_copy_type_name(
    const CNA_Handle gameHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        GameComponentCollection* components = nullptr;
        if (const CNA_Result result = BorrowComponents(gameHandle, &components);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(components->GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_game_services_contains_ext(
    const CNA_Handle gameHandle,
    const CNA_GameServiceType service,
    CNA_Bool* const outPresent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPresent == nullptr) {
            return InvalidInput("The service presence output is null.");
        }
        if (service > CNA_GAME_SERVICE_TYPE_MAXIMUM) {
            return InvalidInput("The game service is not a defined identity.");
        }
        GameServiceContainer* services = nullptr;
        if (const CNA_Result result = BorrowServices(gameHandle, &services);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const void* found =
            service == CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER
            ? static_cast<const void*>(
                  services->GetService<Microsoft::Xna::Framework::IGraphicsDeviceManager>())
            : static_cast<const void*>(
                  services->GetService<Microsoft::Xna::Framework::Graphics::IGraphicsDeviceService>());
        *outPresent = found != nullptr ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_services_remove_ext(
    const CNA_Handle gameHandle,
    const CNA_GameServiceType service)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (service > CNA_GAME_SERVICE_TYPE_MAXIMUM) {
            return InvalidInput("The game service is not a defined identity.");
        }
        GameServiceContainer* services = nullptr;
        if (const CNA_Result result = BorrowServices(gameHandle, &services);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (service == CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER) {
            services->RemoveService<Microsoft::Xna::Framework::IGraphicsDeviceManager>();
        } else {
            services->RemoveService<Microsoft::Xna::Framework::Graphics::IGraphicsDeviceService>();
        }
        return CNA_RESULT_SUCCESS;
    });
}
