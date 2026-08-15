// SPDX-License-Identifier: MS-PL

#include "CNA/C/runtime.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/LaunchParameters.hpp"
#include "Microsoft/Xna/Framework/TitleContainer.hpp"
#include "Microsoft/Xna/Framework/TitleLocation.hpp"
#include "System/IO/Stream.hpp"
#include "System/TimeSpan.hpp"

#include <cstring>
#include <stdexcept>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ObjectKind;

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

// The game outlives every registration a caller can hold, because a game refuses to be destroyed
// while owned child handles are alive and a registration is released explicitly.
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
        source_->Remove(token_);
    }

private:
    Source* source_;
    Token token_;
};

} // namespace

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
            std::static_pointer_cast<GameRegistrationBase>(
                std::make_shared<GameRegistration>(source, token)),
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
