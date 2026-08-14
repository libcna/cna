// SPDX-License-Identifier: MS-PL

#include "CNA/C/runtime.h"
#include "CNA/C/graphics.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/TimeSpan.hpp"

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
    }

    void Draw(const GameTime& gameTime) override
    {
        const CNA_GameTime cGameTime = MakeCGameTime(gameTime);
        Invoke(callbacks_.draw, &cGameTime);
    }

    void OnExiting(System::Object* sender, const System::EventArgs& args) override
    {
        NotifyExit();
        Game::OnExiting(sender, args);
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
            return callback(handle_, gameTime, callbacks_.context, &callbackError);
        });
        isInsideCallback_ = false;
        InvalidateBorrowedGraphicsDevice();
        if (result == CNA_RESULT_SUCCESS) {
            return;
        }

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
            CNA::C::Detail::HasOwnedAudioResources()) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "All owned C child resources must be destroyed before the game.");
        }

        game->Shutdown();
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
