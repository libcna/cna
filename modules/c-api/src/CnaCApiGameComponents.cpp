// SPDX-License-Identifier: MS-PL

#include "CNA/C/runtime_components.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiGameComponentsDetail.hpp"
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
using CNA::C::Detail::IsVersionedStructure;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;

namespace {

using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::OutOfRange;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::RequireIndex;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ToCBool;

using Microsoft::Xna::Framework::DrawableGameComponent;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GameComponent;
using Microsoft::Xna::Framework::GameComponentCollection;
using Microsoft::Xna::Framework::GameComponentCollectionEventArgs;
using Microsoft::Xna::Framework::GameServiceContainer;
using Microsoft::Xna::Framework::GameTime;
using Microsoft::Xna::Framework::IGameComponent;

[[nodiscard]] CNA_Result NotDrawable()
{
    return InvalidState("This game component does not draw.");
}

[[nodiscard]] CNA_GameTime MakeCGameTime(const GameTime& value) noexcept
{
    CNA_GameTime mapped = {};
    mapped.total_game_time_ticks =
        static_cast<int64_t>(value.getTotalGameTimeProperty().getTicksProperty());
    mapped.elapsed_game_time_ticks =
        static_cast<int64_t>(value.getElapsedGameTimeProperty().getTicksProperty());
    mapped.is_running_slowly = ToCBool(value.getIsRunningSlowlyProperty());
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
        , owner_(game)
    {
    }

    // CBIND-129: the handler first, then the base -- the canonical shape, where an override does its
    // own work and then calls base.Initialize(). For a drawable component the base is what loads
    // content, so the reverse order ran load-content before the consumer's initialize had set up
    // what it loads (XNA's ParticleSample sets its texture name in Initialize). CGame::Initialize
    // orders its hook the same way for the same reason.
    void Initialize() override
    {
        if (callbacks_.initialize != nullptr) {
            const HandlerScope scope(owner_);
            callbacks_.initialize(callbacks_.context);
        }
        TBase::Initialize();
    }

    void Update(GameTime& gameTime) override
    {
        if (callbacks_.update != nullptr) {
            const CNA_GameTime mapped = MakeCGameTime(gameTime);
            const HandlerScope scope(owner_);
            callbacks_.update(&mapped, callbacks_.context);
        }
    }

protected:
    // CBIND-128: every C handler runs inside its game's callback scope (see EnterGameCallbackScope).
    class HandlerScope final {
    public:
        explicit HandlerScope(Game& game) noexcept
            : game_(game)
        {
            CNA::C::Detail::EnterGameCallbackScope(game_);
        }

        ~HandlerScope()
        {
            CNA::C::Detail::LeaveGameCallbackScope(game_);
        }

        HandlerScope(const HandlerScope&) = delete;
        HandlerScope& operator=(const HandlerScope&) = delete;

    private:
        Game& game_;
    };

    // Like an XNA Dispose(bool) override, every explicit call reaches the handler.
    // The consumer owns any idempotent resource-release guard inside that handler.
    void ForwardDispose()
    {
        if (callbacks_.dispose == nullptr) {
            return;
        }
        const HandlerScope scope(owner_);
        callbacks_.dispose(callbacks_.context);
    }

    CNA_GameComponentCallbacks callbacks_;
    Game& owner_;

};

class CGameComponent final : public ForwardingComponent<GameComponent> {
public:
    using ForwardingComponent::ForwardingComponent;

protected:
    void Dispose(bool disposing) override
    {
        if (disposing) {
            ForwardDispose();
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
            const HandlerScope scope(owner_);
            callbacks_.draw(&mapped, callbacks_.context);
        }
    }

protected:
    void LoadContent() override
    {
        if (callbacks_.load_content != nullptr) {
            const HandlerScope scope(owner_);
            callbacks_.load_content(callbacks_.context);
        }
    }

    void UnloadContent() override
    {
        if (callbacks_.unload_content != nullptr) {
            const HandlerScope scope(owner_);
            callbacks_.unload_content(callbacks_.context);
        }
    }

    void Dispose(bool disposing) override
    {
        if (disposing) {
            ForwardDispose();
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
    return CreateHandle(ObjectKind::GameComponentEventRegistration,
        std::move(registration),
        outRegistration);
}

[[nodiscard]] CNA_Result BorrowComponent(
    const CNA_Handle handle,
    std::shared_ptr<ComponentResource>* const outComponent)
{
    return LookupHandle(handle, ObjectKind::GameComponent, outComponent);
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
    return IsVersionedStructure(callbacks);
}

template<typename TComponent>
[[nodiscard]] CNA_Result CreateComponent(
    const CNA_Handle gameHandle,
    const CNA_GameComponentCallbacks* const callbacks,
    CNA_GameComponentHandle* const outComponent)
{
    if (outComponent == nullptr) {
        return InvalidArgument("The game component output is null.");
    }
    *outComponent = CNA_INVALID_HANDLE;
    if (!ValidCallbacks(callbacks)) {
        return InvalidArgument("The game component callback set is not a valid structure.");
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
    if (const CNA_Result result = CreateHandle(ObjectKind::GameComponent,
        resource,
        outComponent);
        result != CNA_RESULT_SUCCESS) {
        return result;
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

namespace CNA::C::Detail {

CNA_Result CreateOwnedCanonicalGameComponent(
    const CNA_Handle gameHandle,
    std::unique_ptr<Microsoft::Xna::Framework::GameComponent> component,
    CNA_Handle* const outComponent)
{
    if (component == nullptr || outComponent == nullptr) {
        return InvalidArgument("The canonical game component factory arguments are invalid.");
    }
    *outComponent = CNA_INVALID_HANDLE;
    auto resource = std::make_shared<ComponentResource>();
    resource->game = gameHandle;
    // A canonical component is never one this ABI derived, so it has no drawable half to record and
    // no callback set behind it -- only the handle, the registry entry and the ownership count are
    // the same as for a component a caller built.
    IGameComponent* const key = component.get();
    resource->value = std::move(component);
    if (const CNA_Result result = CreateHandle(ObjectKind::GameComponent,
        resource,
        outComponent);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    resource->handle = *outComponent;
    {
        const std::lock_guard<std::mutex> lock(ComponentRegistryMutex());
        ComponentRegistry()[key] = *outComponent;
    }
    CNA::C::Detail::AddOwnedGameComponent();
    return CNA_RESULT_SUCCESS;
}

} // namespace CNA::C::Detail

namespace {

[[nodiscard]] CNA_Result GameComponentCallbacksInit(CNA_GameComponentCallbacks* const outCallbacks)
{
    CNA_GameComponentCallbacks callbacks = {};
    callbacks.struct_size = sizeof(CNA_GameComponentCallbacks);
    callbacks.struct_version = StructureVersion;
    *outCallbacks = callbacks;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GameComponentGetGame(
    const CNA_GameComponentHandle component,
    CNA_Handle* const outGame)
{
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
}

[[nodiscard]] CNA_Result DrawableGameComponentGetGraphicsDevice(
    const CNA_GameComponentHandle component,
    CNA_Handle* const outGraphicsDevice)
{
    std::shared_ptr<ComponentResource> resource;
    if (const CNA_Result result = BorrowDrawable(component, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // Reading the canonical property first keeps the failure the canonical one when the game has
    // no device; the handle a caller gets is the game's own borrowed device.
    (void)&resource->drawable->getGraphicsDeviceProperty();
    return CNA::C::Detail::BorrowGameGraphicsDevice(resource->game, outGraphicsDevice);
}

[[nodiscard]] CNA_Result GameComponentUpdate(
    const CNA_GameComponentHandle component,
    const CNA_GameTime* const gameTime)
{
    if (gameTime == nullptr) {
        return InvalidArgument("The game time is null.");
    }
    std::shared_ptr<ComponentResource> resource;
    if (const CNA_Result result = BorrowComponent(component, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    GameTime value = ToGameTime(*gameTime);
    resource->value->Update(value);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result DrawableGameComponentDraw(
    const CNA_GameComponentHandle component,
    const CNA_GameTime* const gameTime)
{
    if (gameTime == nullptr) {
        return InvalidArgument("The game time is null.");
    }
    std::shared_ptr<ComponentResource> resource;
    if (const CNA_Result result = BorrowDrawable(component, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const GameTime value = ToGameTime(*gameTime);
    resource->drawable->Draw(value);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GameComponentSubscribe(
    const CNA_GameComponentHandle component,
    const CNA_GameComponentEvent event,
    const CNA_GameComponentEventCallback callback,
    void* const context,
    CNA_GameComponentEventRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The component event callback is null.");
    }
    if (event > CNA_GAME_COMPONENT_EVENT_MAXIMUM) {
        return InvalidArgument("The component event is not a defined identity.");
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
}

[[nodiscard]] CNA_Result GameComponentUnsubscribe(
    const CNA_GameComponentEventRegistrationHandle registration)
{
    std::shared_ptr<ComponentRegistrationBase> value;
    if (const CNA_Result result =
            LookupHandle(registration, ObjectKind::GameComponentEventRegistration, &value);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return ReleaseHandle(registration);
}

[[nodiscard]] CNA_Result GameComponentDestroy(const CNA_GameComponentHandle component)
{
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
    if (const CNA_Result result = ReleaseHandle(component);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    CNA::C::Detail::RemoveOwnedGameComponent();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GameComponentsGetAt(
    const CNA_Handle gameHandle,
    const uint64_t index,
    CNA_GameComponentHandle* const outComponent)
{
    GameComponentCollection* components = nullptr;
    if (const CNA_Result result = BorrowComponents(gameHandle, &components);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (index >= static_cast<uint64_t>(components->getCountProperty())) {
        return OutOfRange("The component index is at or past the collection count.");
    }
    *outComponent = HandleForComponent(
        (*components)[static_cast<GameComponentCollection::size_type>(index)]);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GameComponentsInsert(
    const CNA_Handle gameHandle,
    const uint64_t index,
    const CNA_GameComponentHandle component)
{
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
        return OutOfRange("The insert index is past the collection count.");
    }
    components->Insert(
        static_cast<GameComponentCollection::size_type>(index),
        resource->value.get());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SubscribeCollection(
    const CNA_Handle gameHandle,
    const bool added,
    const CNA_GameComponentCollectionCallback callback,
    void* const context,
    CNA_GameComponentEventRegistrationHandle* const outRegistration)
{
    if (outRegistration == nullptr) {
        return InvalidArgument("The component registration output is null.");
    }
    *outRegistration = CNA_INVALID_HANDLE;
    if (callback == nullptr) {
        return InvalidArgument("The component collection callback is null.");
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

[[nodiscard]] CNA_Result GameServicesContainsExt(
    const CNA_Handle gameHandle,
    const CNA_GameServiceType service,
    CNA_Bool* const outPresent)
{
    if (service > CNA_GAME_SERVICE_TYPE_MAXIMUM) {
        return InvalidArgument("The game service is not a defined identity.");
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
    *outPresent = ToCBool(found != nullptr);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GameServicesRemoveExt(
    const CNA_Handle gameHandle,
    const CNA_GameServiceType service)
{
    if (service > CNA_GAME_SERVICE_TYPE_MAXIMUM) {
        return InvalidArgument("The game service is not a defined identity.");
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
}

} // namespace

// The routes generated from modules/c-api/spec/GameComponents.toml.
#include "generated/GameComponents.inc"
