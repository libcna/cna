// SPDX-License-Identifier: MS-PL

#include "CNA/C/runtime_window.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/LaunchParameters.hpp"
#include "Microsoft/Xna/Framework/TitleContainer.hpp"
#include "Microsoft/Xna/Framework/TitleLocation.hpp"
#include "System/IO/Stream.hpp"
#include "System/TimeSpan.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ValidateCanonicalBool;

namespace {

using Microsoft::Xna::Framework::FrameworkDispatcher;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::LaunchParameters;
using Microsoft::Xna::Framework::TitleContainer;
using Microsoft::Xna::Framework::TitleLocation;

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
        return InvalidInput("The game text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the game text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowText(
    const CNA_StringView view,
    const char* const message,
    std::string* const outText)
{
    if (const CNA_Result result = CNA::C::Detail::CopyStringView(view, false, outText);
        result != CNA_RESULT_SUCCESS) {
        return Fail(result, ErrorCategoryForResult(result), message);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowGame(const CNA_Handle handle, Game** const outGame)
{
    return CNA::C::Detail::GetGameObject(handle, outGame);
}

class GameRegistrationBase {
public:
    GameRegistrationBase() = default;
    GameRegistrationBase(const GameRegistrationBase&) = delete;
    GameRegistrationBase& operator=(const GameRegistrationBase&) = delete;
    virtual ~GameRegistrationBase() = default;
};

// A registration names a handler inside the game -- the game's own event, or its window's -- and the
// game can be destroyed while the caller still holds the handle: unlike a texture or a component,
// nothing about a subscription makes cna_game_destroy refuse. The registration is therefore tracked
// and invalidated when the game goes away, exactly as a graphics-device subscription is: the
// subscriber still observes the game's disposal, and the later unsubscribe detaches nothing rather
// than reaching into a destroyed handler collection.
class GameRegistration final : public GameRegistrationBase {
public:
    using Source = System::EventHandler<System::EventArgs>;
    using Token = Source::Token;

    GameRegistration(Source* const source, const Token token)
        : source_(source)
        , token_(token)
    {
    }

    ~GameRegistration() override
    {
        if (!subscribed_) {
            return;
        }
        source_->Remove(token_);
    }

    void Invalidate() noexcept
    {
        subscribed_ = false;
    }

private:
    Source* source_;
    Token token_;
    bool subscribed_ = true;
};

struct LiveGameRegistrations final {
    std::mutex mutex;
    std::vector<std::weak_ptr<GameRegistration>> entries;
};

[[nodiscard]] LiveGameRegistrations& GetLiveGameRegistrations()
{
    static LiveGameRegistrations registrations;
    return registrations;
}

// Publishes a registration and hands back the base pointer the handle registry stores. Expired
// entries are swept on the way in, so a program that subscribes and unsubscribes for hours does not
// accumulate dead weak pointers.
[[nodiscard]] std::shared_ptr<GameRegistrationBase> TrackGameRegistration(
    std::shared_ptr<GameRegistration> registration)
{
    LiveGameRegistrations& live = GetLiveGameRegistrations();
    std::lock_guard lock(live.mutex);
    std::erase_if(live.entries, [](const std::weak_ptr<GameRegistration>& entry) {
        return entry.expired();
    });
    live.entries.push_back(registration);
    return std::static_pointer_cast<GameRegistrationBase>(std::move(registration));
}

} // namespace

namespace CNA::C::Detail {

void ResetGameEventRegistrationState() noexcept
{
    LiveGameRegistrations& live = GetLiveGameRegistrations();
    std::lock_guard lock(live.mutex);
    for (const std::weak_ptr<GameRegistration>& entry : live.entries) {
        if (const std::shared_ptr<GameRegistration> registration = entry.lock()) {
            registration->Invalidate();
        }
    }
    live.entries.clear();
}

} // namespace CNA::C::Detail

CNA_Result cna_game_get_is_active(const CNA_Handle gameHandle, CNA_Bool* const outActive)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outActive == nullptr) {
            return InvalidInput("The active output is null.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outActive = game->getIsActiveProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_get_is_mouse_visible(const CNA_Handle gameHandle, CNA_Bool* const outVisible)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVisible == nullptr) {
            return InvalidInput("The mouse visibility output is null.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outVisible = game->getIsMouseVisibleProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_set_is_mouse_visible(const CNA_Handle gameHandle, const CNA_Bool visible)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(visible, "visible");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->setIsMouseVisibleProperty(visible != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_get_is_fixed_time_step(const CNA_Handle gameHandle, CNA_Bool* const outFixed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFixed == nullptr) {
            return InvalidInput("The fixed-time-step output is null.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outFixed = game->getIsFixedTimeStepProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_set_is_fixed_time_step(const CNA_Handle gameHandle, const CNA_Bool fixed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(fixed, "fixed");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->setIsFixedTimeStepProperty(fixed != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_get_target_elapsed_time_ticks(
    const CNA_Handle gameHandle,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return InvalidInput("The target step output is null.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTicks = static_cast<int64_t>(game->getTargetElapsedTimeProperty().getTicksProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_set_target_elapsed_time_ticks(
    const CNA_Handle gameHandle,
    const int64_t ticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (ticks <= INT64_C(0)) {
            return InvalidInput("The target step must be positive.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->setTargetElapsedTimeProperty(
            System::TimeSpan(static_cast<SharpRuntime::longcs>(ticks)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_get_inactive_sleep_time_ticks(
    const CNA_Handle gameHandle,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return InvalidInput("The inactive sleep output is null.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTicks = static_cast<int64_t>(game->getInactiveSleepTimeProperty().getTicksProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_set_inactive_sleep_time_ticks(
    const CNA_Handle gameHandle,
    const int64_t ticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (ticks < INT64_C(0)) {
            return InvalidInput("The inactive sleep duration must not be negative.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->setInactiveSleepTimeProperty(
            System::TimeSpan(static_cast<SharpRuntime::longcs>(ticks)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_get_target_fps_ext(const CNA_Handle gameHandle, double* const outFps)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFps == nullptr) {
            return InvalidInput("The frame rate output is null.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outFps = game->getTargetFPSProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_get_target_ms_frame_time_ext(
    const CNA_Handle gameHandle,
    double* const outMilliseconds)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMilliseconds == nullptr) {
            return InvalidInput("The frame time output is null.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outMilliseconds = game->getTargetMsFrameTimeProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_fps_to_milliseconds_per_frame_ext(
    const int32_t framesPerSecond,
    double* const outMilliseconds)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMilliseconds == nullptr) {
            return InvalidInput("The frame time output is null.");
        }
        *outMilliseconds =
            Game::fpsToMillisecondsPerFrame(static_cast<SharpRuntime::intcs>(framesPerSecond));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_get_run_application_ext(const CNA_Handle gameHandle, CNA_Bool* const outRunning)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRunning == nullptr) {
            return InvalidInput("The run-application output is null.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outRunning = game->RunApplication ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_set_run_application_ext(const CNA_Handle gameHandle, const CNA_Bool running)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(running, "running");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->RunApplication = running != CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_reset_elapsed_time(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->ResetElapsedTime();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_suppress_draw(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->SuppressDraw();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_get_type_name_size(const CNA_Handle gameHandle, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The type-name size output is null.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = game->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_copy_type_name(
    const CNA_Handle gameHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(game->GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_game_subscribe(
    const CNA_Handle gameHandle,
    const CNA_GameEvent event,
    const CNA_GameEventCallback callback,
    void* const context,
    CNA_GameEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The game registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The game event callback is null.");
        }
        if (event > CNA_GAME_EVENT_MAXIMUM) {
            return InvalidInput("The game event is not a defined identity.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::EventHandler<System::EventArgs>* source = nullptr;
        switch (event) {
        case CNA_GAME_EVENT_ACTIVATED:
            source = &game->Activated;
            break;
        case CNA_GAME_EVENT_DEACTIVATED:
            source = &game->Deactivated;
            break;
        case CNA_GAME_EVENT_DISPOSED:
            source = &game->Disposed;
            break;
        default:
            // The canonical exiting event carries an argument type of its own, but the handler
            // collection is the plain one, so every game event registers the same way.
            source = &game->Exiting;
            break;
        }
        const auto token = source->Add(
            [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            ObjectKind::GameEventRegistration,
            TrackGameRegistration(std::make_shared<GameRegistration>(source, token)),
            outRegistration);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The game registration could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_unsubscribe(const CNA_GameEventRegistrationHandle registration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GameRegistrationBase> value;
        if (const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Get(
                registration,
                ObjectKind::GameEventRegistration,
                &value);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The game registration handle is invalid for this call.");
        }
        const CNA_Result releaseResult =
            CNA::C::Detail::GetRuntimeHandles().Release(registration);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The game registration handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_launch_parameters_get_count(
    const CNA_Handle gameHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The launch parameter count output is null.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(game->getLaunchParametersProperty().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_launch_parameters_contains_key(
    const CNA_Handle gameHandle,
    const CNA_StringView key,
    CNA_Bool* const outPresent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPresent == nullptr) {
            return InvalidInput("The launch parameter presence output is null.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string name;
        if (const CNA_Result result =
                BorrowText(key, "The launch parameter name is not valid UTF-8.", &name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outPresent =
            game->getLaunchParametersProperty().ContainsKey(name) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

[[nodiscard]] CNA_Result FindLaunchParameter(
    const CNA_Handle gameHandle,
    const CNA_StringView key,
    std::string* const outValue)
{
    Game* game = nullptr;
    if (const CNA_Result result = BorrowGame(gameHandle, &game); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string name;
    if (const CNA_Result result =
            BorrowText(key, "The launch parameter name is not valid UTF-8.", &name);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const LaunchParameters& parameters = game->getLaunchParametersProperty();
    const auto found = parameters.find(name);
    if (found == parameters.end()) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "No launch parameter has that name.");
    }
    *outValue = found->second;
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_game_launch_parameters_get_value_size(
    const CNA_Handle gameHandle,
    const CNA_StringView key,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The launch parameter size output is null.");
        }
        std::string value;
        if (const CNA_Result result = FindLaunchParameter(gameHandle, key, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = value.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_launch_parameters_copy_value(
    const CNA_Handle gameHandle,
    const CNA_StringView key,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string value;
        if (const CNA_Result result = FindLaunchParameter(gameHandle, key, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(value, destination, capacity, outBytes);
    });
}

namespace {

/// The parameter names in the order the ABI publishes: by name, ordinal, ascending.
///
/// Rebuilt per call rather than cached. The canonical container is a hash map the game owns and
/// anything may add to, so a cache would need an invalidation signal it does not offer; a command
/// line holds a handful of entries, and recomputing a handful is cheaper than being wrong.
[[nodiscard]] CNA_Result SortedLaunchParameterNames(
    const CNA_Handle gameHandle,
    std::vector<std::string>* const outNames)
{
    Game* game = nullptr;
    if (const CNA_Result result = BorrowGame(gameHandle, &game);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& parameters = game->getLaunchParametersProperty();
    outNames->clear();
    outNames->reserve(parameters.size());
    for (const auto& entry : parameters) {
        outNames->push_back(entry.first);
    }
    std::sort(outNames->begin(), outNames->end());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result FindLaunchParameterName(
    const CNA_Handle gameHandle,
    const uint64_t index,
    std::string* const outName)
{
    std::vector<std::string> names;
    if (const CNA_Result result = SortedLaunchParameterNames(gameHandle, &names);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (index >= names.size()) {
        return InvalidInput("The launch parameter index is outside the parameter count.");
    }
    *outName = names[static_cast<std::size_t>(index)];
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_game_launch_parameters_get_key_size(
    const CNA_Handle gameHandle,
    const uint64_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The launch parameter name size output is null.");
        }
        std::string name;
        if (const CNA_Result result = FindLaunchParameterName(gameHandle, index, &name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = name.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_launch_parameters_copy_key(
    const CNA_Handle gameHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string name;
        if (const CNA_Result result = FindLaunchParameterName(gameHandle, index, &name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(name, destination, capacity, outBytes);
    });
}

CNA_Result cna_game_launch_parameters_add(
    const CNA_Handle gameHandle,
    const CNA_StringView key,
    const CNA_StringView value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string name;
        std::string text;
        if (const CNA_Result result =
                BorrowText(key, "The launch parameter name is not valid UTF-8.", &name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                BorrowText(value, "The launch parameter value is not valid UTF-8.", &text);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->getLaunchParametersProperty().Add(name, text);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_launch_parameters_parse_ext(
    const CNA_Handle gameHandle,
    const CNA_StringView* const arguments,
    const uint64_t argumentCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (argumentCount != UINT64_C(0) && arguments == nullptr) {
            return InvalidInput("The launch argument array is null.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<std::string> parsed;
        parsed.reserve(static_cast<std::size_t>(argumentCount));
        for (uint64_t index = UINT64_C(0); index < argumentCount; ++index) {
            std::string argument;
            if (const CNA_Result result =
                    BorrowText(arguments[index], "A launch argument is not valid UTF-8.", &argument);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            parsed.push_back(std::move(argument));
        }
        game->getLaunchParametersProperty() = LaunchParameters(parsed);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_framework_dispatcher_update(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        FrameworkDispatcher::Update();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_title_location_get_path_size(const CNA_Handle gameHandle, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The title path size output is null.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = TitleLocation::getPathProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_title_location_copy_path(
    const CNA_Handle gameHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical alias returns the same string as the property; C needs only one route.
        return CopyText(TitleLocation::Path(), destination, capacity, outBytes);
    });
}

CNA_Result cna_title_location_set_path_ext(const CNA_Handle gameHandle, const CNA_StringView path)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string value;
        if (const CNA_Result result =
                BorrowText(path, "The title path is not valid UTF-8.", &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        TitleLocation::setPathProperty(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_title_container_read_ext(
    const CNA_Handle gameHandle,
    const CNA_StringView name,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The title read output is invalid.");
        }
        Game* game = nullptr;
        if (const CNA_Result result = BorrowGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string path;
        if (const CNA_Result result =
                BorrowText(name, "The title file name is not valid UTF-8.", &path);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // A missing or unreadable title file is a plain runtime error naming the file, which the
        // exception firewall would otherwise report as an internal failure. It is I/O, and the
        // route says so.
        std::unique_ptr<System::IO::Stream> stream;
        try {
            stream = TitleContainer::OpenStream(path);
        } catch (const std::runtime_error& exception) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
        }
        if (!stream) {
            return Fail(
                CNA_RESULT_IO,
                CNA_ERROR_CATEGORY_IO,
                "The title file could not be opened.");
        }
        const int64_t length = static_cast<int64_t>(stream->getLengthProperty());
        if (length < 0) {
            return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, "The title file has no length.");
        }
        *outBytes = static_cast<uint64_t>(length);
        if (capacity < *outBytes) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the title file.");
        }
        if (*outBytes != UINT64_C(0)) {
            const auto read = static_cast<uint64_t>(
                stream->Read(destination, 0, static_cast<SharpRuntime::intcs>(*outBytes)));
            if (read != *outBytes) {
                return Fail(
                    CNA_RESULT_IO,
                    CNA_ERROR_CATEGORY_IO,
                    "The title file could not be read completely.");
            }
        }
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

[[nodiscard]] CNA_Result BorrowWindow(
    const CNA_Handle gameHandle,
    Microsoft::Xna::Framework::GameWindow** const outWindow)
{
    return CNA::C::Detail::GetGameWindow(gameHandle, outWindow);
}

// Every canonical window state change reports an SDL failure as a plain runtime error naming the
// call. That is the platform refusing, not an internal fault, and the route says so: a headless
// video driver refuses to minimize a window it never really showed.
template<typename TAction>
[[nodiscard]] CNA_Result RequestWindowChange(TAction&& action)
{
    try {
        action();
    } catch (const std::runtime_error& exception) {
        return Fail(CNA_RESULT_PLATFORM, CNA_ERROR_CATEGORY_PLATFORM, exception.what());
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_game_window_get_allow_user_resizing(
    const CNA_Handle gameHandle,
    CNA_Bool* const outAllowed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAllowed == nullptr) {
            return InvalidInput("The resizing output is null.");
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outAllowed = window->getAllowUserResizingProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_window_set_allow_user_resizing(
    const CNA_Handle gameHandle,
    const CNA_Bool allowed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(allowed, "allowed");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return RequestWindowChange([&]() { window->setAllowUserResizingProperty(allowed != CNA_FALSE); });
    });
}

CNA_Result cna_game_window_get_client_bounds(
    const CNA_Handle gameHandle,
    CNA_Rectangle* const outBounds)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBounds == nullptr) {
            return InvalidInput("The client bounds output is null.");
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const Microsoft::Xna::Framework::Rectangle bounds = window->getClientBoundsProperty();
        CNA_Rectangle mapped = {};
        mapped.x = bounds.X;
        mapped.y = bounds.Y;
        mapped.width = bounds.Width;
        mapped.height = bounds.Height;
        *outBounds = mapped;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_window_get_current_orientation(
    const CNA_Handle gameHandle,
    CNA_DisplayOrientation* const outOrientation)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outOrientation == nullptr) {
            return InvalidInput("The orientation output is null.");
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outOrientation =
            static_cast<CNA_DisplayOrientation>(window->getCurrentOrientationProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_window_get_native_handle_ext(
    const CNA_Handle gameHandle,
    uint64_t* const outHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHandle == nullptr) {
            return InvalidInput("The native window output is null.");
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical property and the canonical native accessor answer the same pointer.
        *outHandle = static_cast<uint64_t>(window->getHandleProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_window_get_screen_device_name_size(
    const CNA_Handle gameHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The display name size output is null.");
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = window->getScreenDeviceNameProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_window_copy_screen_device_name(
    const CNA_Handle gameHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(window->getScreenDeviceNameProperty(), destination, capacity, outBytes);
    });
}

CNA_Result cna_game_window_get_title_size(const CNA_Handle gameHandle, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The window title size output is null.");
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = window->getTitleProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_window_copy_title(
    const CNA_Handle gameHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(window->getTitleProperty(), destination, capacity, outBytes);
    });
}

CNA_Result cna_game_window_get_is_borderless_ext(
    const CNA_Handle gameHandle,
    CNA_Bool* const outBorderless)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBorderless == nullptr) {
            return InvalidInput("The borderless output is null.");
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBorderless = window->getIsBorderlessEXTProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_window_set_is_borderless_ext(
    const CNA_Handle gameHandle,
    const CNA_Bool borderless)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(borderless, "borderless");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return RequestWindowChange([&]() { window->setIsBorderlessEXTProperty(borderless != CNA_FALSE); });
    });
}

CNA_Result cna_game_window_minimize_ext(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return RequestWindowChange([&]() { window->MinimizeEXT(); });
    });
}

CNA_Result cna_game_window_restore_ext(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return RequestWindowChange([&]() { window->RestoreEXT(); });
    });
}

CNA_Result cna_game_window_begin_screen_device_change(
    const CNA_Handle gameHandle,
    const CNA_Bool willBeFullScreen)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCanonicalBool(willBeFullScreen, "will_be_full_screen");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        window->BeginScreenDeviceChange(willBeFullScreen != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_window_end_screen_device_change(
    const CNA_Handle gameHandle,
    const CNA_StringView screenDeviceName,
    const int32_t clientWidth,
    const int32_t clientHeight)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string name;
        if (const CNA_Result result =
                BorrowText(screenDeviceName, "The display name is not valid UTF-8.", &name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical name-only overload is this one with the current client size, so a caller
        // asks for that by passing a size this ABI reads as "keep it".
        if (clientWidth < 1 || clientHeight < 1) {
            return RequestWindowChange([&]() { window->EndScreenDeviceChange(name); });
        }
        return RequestWindowChange([&]() {
            window->EndScreenDeviceChange(
                name,
                static_cast<SharpRuntime::intcs>(clientWidth),
                static_cast<SharpRuntime::intcs>(clientHeight));
        });
    });
}

CNA_Result cna_game_window_get_type_name_size(const CNA_Handle gameHandle, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The type-name size output is null.");
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = window->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_window_copy_type_name(
    const CNA_Handle gameHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(window->GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_game_window_subscribe(
    const CNA_Handle gameHandle,
    const CNA_GameWindowEvent event,
    const CNA_GameEventCallback callback,
    void* const context,
    CNA_GameEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The window registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The window event callback is null.");
        }
        if (event > CNA_GAME_WINDOW_EVENT_MAXIMUM) {
            return InvalidInput("The window event is not a defined identity.");
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::EventHandler<System::EventArgs>* source = nullptr;
        switch (event) {
        case CNA_GAME_WINDOW_EVENT_CLIENT_SIZE_CHANGED:
            source = &window->ClientSizeChanged;
            break;
        case CNA_GAME_WINDOW_EVENT_ORIENTATION_CHANGED:
            source = &window->OrientationChanged;
            break;
        default:
            source = &window->ScreenDeviceNameChanged;
            break;
        }
        const auto token = source->Add(
            [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            ObjectKind::GameEventRegistration,
            TrackGameRegistration(std::make_shared<GameRegistration>(source, token)),
            outRegistration);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The window registration could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}
