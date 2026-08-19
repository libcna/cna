// SPDX-License-Identifier: MS-PL

#include "CNA/C/runtime.h"
#include "CNA/C/graphics.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiPlatformOverride.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/TimeSpan.hpp"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::HasOwnedGraphicsResources;
using CNA::C::Detail::HandleRegistry;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ValidateStringView;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GameTime;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

constexpr uint32_t StructureVersion = UINT32_C(1);

struct RuntimeState final {
    std::mutex mutex;
    HandleRegistry handles;
    bool hasActiveGame = false;
    uint64_t ownedGraphicsResourceCount = 0U;
    uint64_t ownedContentManagerCount = 0U;
    uint64_t ownedAudioResourceCount = 0U;
    uint64_t ownedGameComponentCount = 0U;
};

[[nodiscard]] RuntimeState& GetRuntimeState()
{
    static RuntimeState state;
    return state;
}

[[nodiscard]] bool IsSupportedGameCallbacks(const CNA_GameCallbacks* const callbacks) noexcept
{
    return callbacks == nullptr ||
        (callbacks->struct_size >= sizeof(CNA_GameCallbacks) &&
         callbacks->struct_version == StructureVersion);
}

[[nodiscard]] bool IsSupportedGameCreateInfo(const CNA_GameCreateInfo* const createInfo) noexcept
{
    return createInfo != nullptr &&
        createInfo->struct_size >= sizeof(CNA_GameCreateInfo) &&
        createInfo->struct_version == StructureVersion;
}

[[nodiscard]] CNA_GameTime MakeCGameTime(const GameTime& value) noexcept
{
    return CNA_GameTime{
        .total_game_time_ticks = value.getTotalGameTimeProperty().getTicksProperty(),
        .elapsed_game_time_ticks = value.getElapsedGameTimeProperty().getTicksProperty(),
        .is_running_slowly = value.getIsRunningSlowlyProperty() ? CNA_TRUE : CNA_FALSE,
        .reserved = {0U, 0U, 0U, 0U, 0U, 0U, 0U}
    };
}

class CGame final : public Game {
public:
    explicit CGame(const CNA_GameCallbacks* const callbacks)
        : callbacks_{}, handle_(CNA_INVALID_HANDLE), callbackFailure_(CNA_RESULT_SUCCESS),
          isInsideCallback_(false), hasLoadedContent_(false), hasExited_(false), isShutDown_(false)
    {
        if (callbacks != nullptr) {
            callbacks_ = *callbacks;
        } else {
            callbacks_.struct_size = sizeof(CNA_GameCallbacks);
            callbacks_.struct_version = StructureVersion;
        }
    }

    void SetHandle(const CNA_Handle handle) noexcept
    {
        handle_ = handle;
    }

    void SetFrameHooks(const CNA_GameFrameHooks* const hooks) noexcept
    {
        if (hooks == nullptr) {
            frameHooks_ = CNA_GameFrameHooks{};
            return;
        }
        frameHooks_ = *hooks;
    }

    [[nodiscard]] bool IsInsideCallback() const noexcept
    {
        return isInsideCallback_;
    }

    [[nodiscard]] CNA_Result GetCallbackFailure() const noexcept
    {
        return callbackFailure_;
    }

    void SetWindowTitle(const std::string& title)
    {
        getWindowProperty().setTitleProperty(title);
    }

    [[nodiscard]] Microsoft::Xna::Framework::GameWindow& GetWindow()
    {
        return getWindowProperty();
    }

    void Clear(const CNA_Color color)
    {
        getGraphicsDeviceProperty().Clear(
            Microsoft::Xna::Framework::Color(color.r, color.g, color.b, color.a));
    }

    [[nodiscard]] CNA_Result BorrowGraphicsDevice(CNA_Handle* const outGraphicsDevice)
    {
        if (outGraphicsDevice == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The graphics-device output handle is null.");
        }
        *outGraphicsDevice = CNA_INVALID_HANDLE;
        if (!isInsideCallback_) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The graphics device may be borrowed only during a game lifecycle callback.");
        }
        if (borrowedGraphicsDeviceHandle_ != CNA_INVALID_HANDLE) {
            *outGraphicsDevice = borrowedGraphicsDeviceHandle_;
            return CNA_RESULT_SUCCESS;
        }

        const auto reference = std::make_shared<BorrowedGraphicsDevice>(
            BorrowedGraphicsDevice{&getGraphicsDeviceProperty(), handle_});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::GraphicsDevice,
            reference,
            &borrowedGraphicsDeviceHandle_);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The callback-scoped graphics-device handle could not be created.");
        }
        *outGraphicsDevice = borrowedGraphicsDeviceHandle_;
        return CNA_RESULT_SUCCESS;
    }

    void Shutdown()
    {
        if (isShutDown_) {
            return;
        }
        isShutDown_ = true;

        NotifyExit();
        if (hasLoadedContent_) {
            Invoke(callbacks_.unload_content, nullptr);
            hasLoadedContent_ = false;
        }
        Dispose();
    }

protected:
    void LoadContent() override
    {
        hasLoadedContent_ = true;
        Invoke(callbacks_.load_content, nullptr);
    }

    void UnloadContent() override
    {
        if (!hasLoadedContent_) {
            return;
        }
        Invoke(callbacks_.unload_content, nullptr);
        hasLoadedContent_ = false;
    }

    void Update(GameTime& gameTime) override
    {
        const CNA_GameTime cGameTime = MakeCGameTime(gameTime);
        Invoke(callbacks_.update, &cGameTime);
        // The base pass is not optional and was missing: `Game::Update` is what walks the
        // updateable components and then runs `FrameworkDispatcher::Update()`. Without it a
        // component added through `cna_game_components_add` was constructed, initialized by the
        // add path, and then never ticked again -- and the dispatcher that refills
        // DynamicSoundEffectInstance buffers and raises MediaPlayer's song transitions never ran
        // at all. The hook goes first, then the base, which is the canonical shape: a derived
        // Update does its own work and *then* calls base.Update(gameTime).
        //
        // Skipped once a callback has failed, because that failure has already called Exit() and
        // every other route here stops doing work at that point -- Invoke() and BeginDraw() both
        // guard on the same flag rather than driving a game that is on its way out.
        if (callbackFailure_ == CNA_RESULT_SUCCESS) {
            Game::Update(gameTime);
        }
    }

    void Draw(const GameTime& gameTime) override
    {
        const CNA_GameTime cGameTime = MakeCGameTime(gameTime);
        Invoke(callbacks_.draw, &cGameTime);
        // Same omission and same ordering: `Game::Draw` draws the visible drawable components,
        // and drawing them after the consumer's own draw is what the canonical template does.
        if (callbackFailure_ == CNA_RESULT_SUCCESS) {
            Game::Draw(gameTime);
        }
    }

    void OnExiting(System::Object* sender, const System::EventArgs& args) override
    {
        NotifyExit();
        Game::OnExiting(sender, args);
    }

    void Initialize() override
    {
        // The hook runs BEFORE the base, and the order is the whole point. `Game::Initialize()`
        // ends by calling `LoadContent()`, exactly as XNA's does, so invoking the hook after it
        // delivered load_content first and initialize second -- the reverse of what this ABI's own
        // header promises ("invoked once while the game initializes, before content loads") and of
        // what a ported game expects, since most touch fields in LoadContent that Initialize set.
        // This mirrors the canonical C++ shape, where a subclass does its own work and *then* calls
        // base.Initialize().
        Invoke(frameHooks_.initialize, nullptr, frameHooks_.context);
        Game::Initialize();
    }

    void BeginRun() override
    {
        Game::BeginRun();
        Invoke(frameHooks_.begin_run, nullptr, frameHooks_.context);
    }

    void EndRun() override
    {
        Invoke(frameHooks_.end_run, nullptr, frameHooks_.context);
        Game::EndRun();
    }

    bool BeginDraw() override
    {
        if (!Game::BeginDraw()) {
            return false;
        }
        if (frameHooks_.begin_draw == nullptr || callbackFailure_ != CNA_RESULT_SUCCESS) {
            return true;
        }
        CNA_CallbackError callbackError = {
            .struct_size = sizeof(CNA_CallbackError),
            .struct_version = StructureVersion,
            .message = {nullptr, 0U}
        };
        CNA_Bool shouldDraw = CNA_TRUE;
        isInsideCallback_ = true;
        const CNA_Result result = CallWithExceptionBarrier([&]() {
            return frameHooks_.begin_draw(
                handle_,
                nullptr,
                frameHooks_.context,
                &shouldDraw,
                &callbackError);
        });
        isInsideCallback_ = false;
        InvalidateBorrowedGraphicsDevice();
        if (result != CNA_RESULT_SUCCESS) {
            RecordCallbackFailure(callbackError);
            return false;
        }
        return shouldDraw != CNA_FALSE;
    }

    void EndDraw() override
    {
        Invoke(frameHooks_.end_draw, nullptr, frameHooks_.context);
        Game::EndDraw();
    }

private:
    void NotifyExit()
    {
        if (hasExited_) {
            return;
        }
        hasExited_ = true;
        Invoke(callbacks_.exiting, nullptr);
    }

    void Invoke(
        const CNA_GameLifecycleCallback callback,
        const CNA_GameTime* const gameTime)
    {
        Invoke(callback, gameTime, callbacks_.context);
    }

    void Invoke(
        const CNA_GameLifecycleCallback callback,
        const CNA_GameTime* const gameTime,
        void* const context)
    {
        if (callback == nullptr || callbackFailure_ != CNA_RESULT_SUCCESS) {
            return;
        }

        CNA_CallbackError callbackError = {
            .struct_size = sizeof(CNA_CallbackError),
            .struct_version = StructureVersion,
            .message = {nullptr, 0U}
        };
        isInsideCallback_ = true;
        const CNA_Result result = CallWithExceptionBarrier([&]() {
            return callback(handle_, gameTime, context, &callbackError);
        });
        isInsideCallback_ = false;
        InvalidateBorrowedGraphicsDevice();
        if (result == CNA_RESULT_SUCCESS) {
            return;
        }
        RecordCallbackFailure(callbackError);
    }

    // Shared by every callback shape, so a pre-draw handler that fails stops the game exactly as an
    // update handler does.
    void RecordCallbackFailure(const CNA_CallbackError& callbackError)
    {
        std::string diagnostic;
        if (callbackError.struct_size >= sizeof(CNA_CallbackError) &&
            callbackError.struct_version == StructureVersion &&
            CopyStringView(callbackError.message, true, &diagnostic) == CNA_RESULT_SUCCESS &&
            !diagnostic.empty()) {
            static_cast<void>(Fail(CNA_RESULT_CALLBACK, CNA_ERROR_CATEGORY_CALLBACK, diagnostic));
        } else {
            static_cast<void>(Fail(
                CNA_RESULT_CALLBACK,
                CNA_ERROR_CATEGORY_CALLBACK,
                "A C game lifecycle callback failed."));
        }
        callbackFailure_ = CNA_RESULT_CALLBACK;
        Exit();
    }

    void InvalidateBorrowedGraphicsDevice() noexcept
    {
        if (borrowedGraphicsDeviceHandle_ == CNA_INVALID_HANDLE) {
            return;
        }
        static_cast<void>(GetRuntimeHandles().Release(borrowedGraphicsDeviceHandle_));
        borrowedGraphicsDeviceHandle_ = CNA_INVALID_HANDLE;
    }

    CNA_GameCallbacks callbacks_;
    CNA_GameFrameHooks frameHooks_{};
    CNA_Handle handle_;
    CNA_Result callbackFailure_;
    bool isInsideCallback_;
    bool hasLoadedContent_;
    bool hasExited_;
    bool isShutDown_;
    CNA_Handle borrowedGraphicsDeviceHandle_ = CNA_INVALID_HANDLE;
};

[[nodiscard]] CNA_Result GetGame(
    const CNA_Handle handle,
    std::shared_ptr<CGame>* const outGame)
{
    const CNA_Result result = GetRuntimeHandles().Get(handle, ObjectKind::Game, outGame);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The CNA game handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetCallableGame(
    const CNA_Handle handle,
    std::shared_ptr<CGame>* const outGame)
{
    const CNA_Result result = GetGame(handle, outGame);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((*outGame)->IsInsideCallback()) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "A game-driving or destruction operation cannot be called from a lifecycle callback.");
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

namespace CNA::C::Detail {

HandleRegistry& GetRuntimeHandles() noexcept
{
    return GetRuntimeState().handles;
}

CNA_Result ValidateActiveGameHandle(const CNA_Handle handle)
{
    std::shared_ptr<CGame> game;
    return GetGame(handle, &game);
}

CNA_Result GetGameObject(const CNA_Handle handle, Microsoft::Xna::Framework::Game** const outGame)
{
    std::shared_ptr<CGame> game;
    if (const CNA_Result result = GetGame(handle, &game); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outGame = game.get();
    return CNA_RESULT_SUCCESS;
}

CNA_Result GetGameWindow(
    const CNA_Handle handle,
    Microsoft::Xna::Framework::GameWindow** const outWindow)
{
    std::shared_ptr<CGame> game;
    if (const CNA_Result result = GetGame(handle, &game); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outWindow = &game->GetWindow();
    return CNA_RESULT_SUCCESS;
}

CNA_Result GetBorrowedGraphicsDevice(
    const CNA_Handle handle,
    std::shared_ptr<BorrowedGraphicsDevice>* const outGraphicsDevice)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::GraphicsDevice,
        outGraphicsDevice);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The callback-scoped graphics-device handle is invalid for this call.");
}

CNA_Result BorrowGameGraphicsDevice(
    const CNA_Handle gameHandle,
    CNA_Handle* const outGraphicsDevice)
{
    std::shared_ptr<CGame> game;
    if (const CNA_Result result = GetGame(gameHandle, &game); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return game->BorrowGraphicsDevice(outGraphicsDevice);
}

void AddOwnedGraphicsResource() noexcept
{
    RuntimeState& state = GetRuntimeState();
    std::lock_guard lock(state.mutex);
    ++state.ownedGraphicsResourceCount;
}

void RemoveOwnedGraphicsResource() noexcept
{
    RuntimeState& state = GetRuntimeState();
    std::lock_guard lock(state.mutex);
    if (state.ownedGraphicsResourceCount != 0U) {
        --state.ownedGraphicsResourceCount;
    }
}

bool HasOwnedGraphicsResources() noexcept
{
    RuntimeState& state = GetRuntimeState();
    std::lock_guard lock(state.mutex);
    return state.ownedGraphicsResourceCount != 0U;
}

void AddOwnedGameComponent() noexcept
{
    RuntimeState& state = GetRuntimeState();
    std::lock_guard lock(state.mutex);
    ++state.ownedGameComponentCount;
}

void RemoveOwnedGameComponent() noexcept
{
    RuntimeState& state = GetRuntimeState();
    std::lock_guard lock(state.mutex);
    if (state.ownedGameComponentCount != 0U) {
        --state.ownedGameComponentCount;
    }
}

bool HasOwnedGameComponents() noexcept
{
    RuntimeState& state = GetRuntimeState();
    std::lock_guard lock(state.mutex);
    return state.ownedGameComponentCount != 0U;
}

void AddOwnedContentManager() noexcept
{
    RuntimeState& state = GetRuntimeState();
    std::lock_guard lock(state.mutex);
    ++state.ownedContentManagerCount;
}

void RemoveOwnedContentManager() noexcept
{
    RuntimeState& state = GetRuntimeState();
    std::lock_guard lock(state.mutex);
    if (state.ownedContentManagerCount != 0U) {
        --state.ownedContentManagerCount;
    }
}

bool HasOwnedContentManagers() noexcept
{
    RuntimeState& state = GetRuntimeState();
    std::lock_guard lock(state.mutex);
    return state.ownedContentManagerCount != 0U;
}

void AddOwnedAudioResource() noexcept
{
    RuntimeState& state = GetRuntimeState();
    std::lock_guard lock(state.mutex);
    ++state.ownedAudioResourceCount;
}

void RemoveOwnedAudioResource() noexcept
{
    RuntimeState& state = GetRuntimeState();
    std::lock_guard lock(state.mutex);
    if (state.ownedAudioResourceCount != 0U) {
        --state.ownedAudioResourceCount;
    }
}

bool HasOwnedAudioResources() noexcept
{
    RuntimeState& state = GetRuntimeState();
    std::lock_guard lock(state.mutex);
    return state.ownedAudioResourceCount != 0U;
}

} // namespace CNA::C::Detail

CNA_Result cna_game_create(
    const CNA_GameCreateInfo* const createInfo,
    CNA_Handle* const outGame)
{
    return CallWithExceptionBarrier([&]() {
        if (!IsSupportedGameCreateInfo(createInfo) || outGame == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The game creation structure or output handle is invalid.");
        }
        *outGame = CNA_INVALID_HANDLE;
        if ((createInfo->is_fixed_time_step != CNA_FALSE &&
             createInfo->is_fixed_time_step != CNA_TRUE) ||
            createInfo->target_elapsed_time_ticks <= 0 ||
            !IsSupportedGameCallbacks(createInfo->callbacks)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The game creation configuration is invalid.");
        }
        if (const CNA_Result titleResult = ValidateStringView(createInfo->window_title, true);
            titleResult != CNA_RESULT_SUCCESS) {
            return Fail(titleResult, ErrorCategoryForResult(titleResult), "The initial window title is not valid UTF-8.");
        }

        std::string title;
        if (const CNA_Result titleResult = CopyStringView(createInfo->window_title, true, &title);
            titleResult != CNA_RESULT_SUCCESS) {
            return Fail(titleResult, ErrorCategoryForResult(titleResult), "The initial window title could not be copied.");
        }

        RuntimeState& state = GetRuntimeState();
        std::lock_guard lock(state.mutex);
        if (state.hasActiveGame) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "Only one C-owned CNA game may be active at a time.");
        }

        const auto game = std::make_shared<CGame>(createInfo->callbacks);
        game->setIsFixedTimeStepProperty(createInfo->is_fixed_time_step == CNA_TRUE);
        game->setTargetElapsedTimeProperty(
            System::TimeSpan::FromTicks(createInfo->target_elapsed_time_ticks));
        game->SetWindowTitle(title);

        const CNA_Result createResult = state.handles.Create(ObjectKind::Game, game, outGame);
        if (createResult != CNA_RESULT_SUCCESS) {
            return Fail(
                createResult,
                ErrorCategoryForResult(createResult),
                "The native game handle could not be created.");
        }
        game->SetHandle(*outGame);
        state.hasActiveGame = true;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_run_one_frame(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetCallableGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->RunOneFrame();
        return game->GetCallbackFailure();
    });
}

CNA_Result cna_game_run(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetCallableGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->Run();
        return game->GetCallbackFailure();
    });
}

CNA_Result cna_game_request_exit(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetGame(gameHandle, &game); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->Exit();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_clear(const CNA_Handle gameHandle, const CNA_Color color)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetGame(gameHandle, &game); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->Clear(color);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_tick(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CGame> game;
        // A frame step drives the game, so it is refused from inside a lifecycle callback for the
        // same reason running or destroying the game is: it would re-enter the loop it is part of.
        if (const CNA_Result result = GetCallableGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->Tick();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_set_frame_hooks_ext(
    const CNA_Handle gameHandle,
    const CNA_GameFrameHooks* const hooks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (hooks != nullptr &&
            (hooks->struct_size < sizeof(CNA_GameFrameHooks) ||
             hooks->struct_version != StructureVersion)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The game frame hook table is not a valid structure.");
        }
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->SetFrameHooks(hooks);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_set_window_title(
    const CNA_Handle gameHandle,
    const CNA_StringView title)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetGame(gameHandle, &game); result != CNA_RESULT_SUCCESS) {
            return result;
        }

        std::string titleCopy;
        if (const CNA_Result titleResult = CopyStringView(title, true, &titleCopy);
            titleResult != CNA_RESULT_SUCCESS) {
            return Fail(titleResult, ErrorCategoryForResult(titleResult), "The window title is not valid UTF-8.");
        }
        game->SetWindowTitle(titleCopy);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_destroy(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetCallableGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (HasOwnedGraphicsResources() || CNA::C::Detail::HasOwnedContentManagers() ||
            CNA::C::Detail::HasOwnedAudioResources() ||
            CNA::C::Detail::HasOwnedGameComponents()) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "All owned C child resources must be destroyed before the game.");
        }

        game->Shutdown();
        // Shutdown has already disposed the canonical graphics device, so any C subscriber has
        // observed its Disposing event. Drop the subscriptions before the device object itself
        // goes away with the game.
        CNA::C::Detail::ResetGraphicsDeviceAdapterState();
        // CBIND-048: and the platform override with it. The game owns the platform the
        // override forwards to, so one left installed past this point forwards into freed
        // memory -- which is what the sanitized tree caught the first time this seam existed.
        CNA::C::Detail::ResetPlatformOverride();
        // Same rule for the game's own events and its window's: the subscriber has just observed
        // the disposal, and the handler collections are about to go with the game.
        CNA::C::Detail::ResetGameEventRegistrationState();
        // Any manager the caller released is still alive here, because the game cached a raw
        // pointer to it; the game is going away now, so it can finally go too.
        CNA::C::Detail::ResetGraphicsDeviceManagerState();
        CNA::C::Detail::ResetGameContentManagerState();
        const CNA_Result callbackResult = game->GetCallbackFailure();
        RuntimeState& state = GetRuntimeState();
        const CNA_Result releaseResult = state.handles.Release(gameHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The native game handle could not be released.");
        }
        {
            std::lock_guard lock(state.mutex);
            state.hasActiveGame = false;
        }
        return callbackResult;
    });
}

CNA_Result cna_game_get_graphics_device(
    const CNA_Handle gameHandle,
    CNA_Handle* const outGraphicsDevice)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetGame(gameHandle, &game); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return game->BorrowGraphicsDevice(outGraphicsDevice);
    });
}
