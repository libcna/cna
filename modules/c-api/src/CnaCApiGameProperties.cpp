// SPDX-License-Identifier: MS-PL

#include "CNA/C/runtime_window.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "CNA/Platform/NativeWindowHandle.hpp"
#include "Microsoft/Xna/Framework/LaunchParameters.hpp"
#include "Microsoft/Xna/Framework/TitleContainer.hpp"
#include "Microsoft/Xna/Framework/TitleLocation.hpp"
#include "System/IO/Stream.hpp"
#include "System/ArgumentException.hpp"
#include "System/TimeSpan.hpp"

#include "Microsoft/Xna/Framework/FileDropEventArgsEXT.hpp"
#include "Microsoft/Xna/Framework/TextDropEventArgsEXT.hpp"
#include <functional>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetGameWindow;
using CNA::C::Detail::IoError;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;

namespace {

using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ValidateVersionedStructure;

using Microsoft::Xna::Framework::FrameworkDispatcher;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::LaunchParameters;
using Microsoft::Xna::Framework::TitleContainer;
using Microsoft::Xna::Framework::TitleLocation;

constexpr uint32_t NativeWindowHandleVersion = UINT32_C(1);

[[nodiscard]] CNA_NativeWindowSystem MapNativeWindowSystem(
    const CNA::Platform::NativeWindowSystem system) noexcept
{
    switch (system) {
        case CNA::Platform::NativeWindowSystem::Win32:
            return CNA_NATIVE_WINDOW_SYSTEM_WIN32;
        case CNA::Platform::NativeWindowSystem::X11:
            return CNA_NATIVE_WINDOW_SYSTEM_X11;
        case CNA::Platform::NativeWindowSystem::Wayland:
            return CNA_NATIVE_WINDOW_SYSTEM_WAYLAND;
        case CNA::Platform::NativeWindowSystem::Cocoa:
            return CNA_NATIVE_WINDOW_SYSTEM_COCOA;
        case CNA::Platform::NativeWindowSystem::Android:
            return CNA_NATIVE_WINDOW_SYSTEM_ANDROID;
        case CNA::Platform::NativeWindowSystem::Web:
            return CNA_NATIVE_WINDOW_SYSTEM_WEB;
        case CNA::Platform::NativeWindowSystem::Headless:
            return CNA_NATIVE_WINDOW_SYSTEM_HEADLESS;
        case CNA::Platform::NativeWindowSystem::Terminal:
            return CNA_NATIVE_WINDOW_SYSTEM_TERMINAL;
        case CNA::Platform::NativeWindowSystem::UIKit:
            return CNA_NATIVE_WINDOW_SYSTEM_UIKIT;
        case CNA::Platform::NativeWindowSystem::Unknown:
            break;
    }
    return CNA_NATIVE_WINDOW_SYSTEM_UNKNOWN;
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
    virtual void Invalidate() noexcept = 0;
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

    GameRegistration(std::weak_ptr<void> owner, Source* const source, const Token token)
        : owner_(std::move(owner))
        , source_(source)
        , token_(token)
    {
    }

    ~GameRegistration() override
    {
        if (!subscribed_) {
            return;
        }
        if (const auto owner = owner_.lock()) {
            source_->Remove(token_);
        }
    }

    void Invalidate() noexcept override
    {
        subscribed_ = false;
    }

private:
    std::weak_ptr<void> owner_;
    Source* source_;
    Token token_;
    bool subscribed_ = true;
};

struct LiveGameRegistrations final {
    std::mutex mutex;
    std::vector<std::weak_ptr<GameRegistrationBase>> entries;
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
    std::shared_ptr<GameRegistrationBase> registration)
{
    LiveGameRegistrations& live = GetLiveGameRegistrations();
    std::lock_guard lock(live.mutex);
    std::erase_if(live.entries, [](const std::weak_ptr<GameRegistrationBase>& entry) {
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
    for (const std::weak_ptr<GameRegistrationBase>& entry : live.entries) {
        if (const std::shared_ptr<GameRegistrationBase> registration = entry.lock()) {
            registration->Invalidate();
        }
    }
    live.entries.clear();
}

} // namespace CNA::C::Detail

namespace {

[[nodiscard]] CNA_Result GameSubscribe(
    const CNA_Handle gameHandle,
    const CNA_GameEvent event,
    const CNA_GameEventCallback callback,
    void* const context,
    CNA_GameEventRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The game event callback is null.");
    }
    if (event > CNA_GAME_EVENT_MAXIMUM) {
        return InvalidArgument("The game event is not a defined identity.");
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
    std::shared_ptr<void> owner;
    if (const auto result = LookupHandle(gameHandle, ObjectKind::Game, &owner);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto token = source->Add(
        [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
    return CreateHandle(ObjectKind::GameEventRegistration,
        TrackGameRegistration(std::make_shared<GameRegistration>(owner, source, token)),
        outRegistration);
}

[[nodiscard]] CNA_Result GameUnsubscribe(const CNA_GameEventRegistrationHandle registration)
{
    std::shared_ptr<GameRegistrationBase> value;
    if (const CNA_Result result =
            LookupHandle(registration, ObjectKind::GameEventRegistration, &value);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return ReleaseHandle(registration);
}

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
            ReadText(key, "The launch parameter name is not valid UTF-8.", &name);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const LaunchParameters& parameters = game->getLaunchParametersProperty();
    const auto found = parameters.find(name);
    if (found == parameters.end()) {
        return InvalidArgument("No launch parameter has that name.");
    }
    *outValue = found->second;
    return CNA_RESULT_SUCCESS;
}

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
        return InvalidArgument("The launch parameter index is outside the parameter count.");
    }
    *outName = names[static_cast<std::size_t>(index)];
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GameLaunchParametersParseExt(
    const CNA_Handle gameHandle,
    const CNA_StringView* const arguments,
    const uint64_t argumentCount)
{
    if (argumentCount != UINT64_C(0) && arguments == nullptr) {
        return InvalidArgument("The launch argument array is null.");
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
                ReadText(arguments[index], "A launch argument is not valid UTF-8.", &argument);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        parsed.push_back(std::move(argument));
    }
    game->getLaunchParametersProperty() = LaunchParameters(parsed);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TitleContainerReadExt(
    const CNA_Handle gameHandle,
    const CNA_StringView name,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if ((destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidArgument("The title read output is invalid.");
    }
    Game* game = nullptr;
    if (const CNA_Result result = BorrowGame(gameHandle, &game);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string path;
    if (const CNA_Result result =
            ReadText(name, "The title file name is not valid UTF-8.", &path);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // Keep invalid title names distinct from an existing but unreadable title entry.
    // OpenStream translates open failures to InvalidOperationException; this C route
    // intentionally retains its documented I/O category for those failures.
    std::unique_ptr<System::IO::Stream> stream;
    try {
        stream = TitleContainer::OpenStream(path);
    } catch (const System::ArgumentException& exception) {
        return InvalidArgument(exception.what());
    } catch (const std::exception& exception) {
        return IoError(exception.what());
    }
    if (!stream) {
        return IoError("The title file could not be opened.");
    }
    const int64_t length = static_cast<int64_t>(stream->getLengthProperty());
    if (length < 0) {
        return IoError("The title file has no length.");
    }
    *outBytes = static_cast<uint64_t>(length);
    if (capacity < *outBytes) {
        return BufferTooSmall("The destination capacity is smaller than the title file.");
    }
    if (*outBytes != UINT64_C(0)) {
        const auto read = static_cast<uint64_t>(
            stream->Read(destination, 0, static_cast<SharpRuntime::intcs>(*outBytes)));
        if (read != *outBytes) {
            return IoError("The title file could not be read completely.");
        }
    }
    return CNA_RESULT_SUCCESS;
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

[[nodiscard]] CNA_Result ReportLogicalSizeProviderFailure(Game& game);

[[nodiscard]] CNA_Result GameWindowGetClientBounds(
    const CNA_Handle gameHandle,
    CNA_Rectangle* const outBounds)
{
    Microsoft::Xna::Framework::GameWindow* window = nullptr;
    if (const CNA_Result result = GetGameWindow(gameHandle, &window);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Microsoft::Xna::Framework::Rectangle bounds = window->getClientBoundsProperty();
    Game* game = nullptr;
    if (const auto result = BorrowGame(gameHandle, &game); result != CNA_RESULT_SUCCESS) return result;
    if (const auto result = ReportLogicalSizeProviderFailure(*game);
        result != CNA_RESULT_SUCCESS) return result;
    CNA_Rectangle mapped = {};
    mapped.x = bounds.X;
    mapped.y = bounds.Y;
    mapped.width = bounds.Width;
    mapped.height = bounds.Height;
    *outBounds = mapped;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result NativeWindowHandleInit(CNA_NativeWindowHandle* const handle)
{
    if (handle == nullptr) {
        return InvalidArgument("The native-window-handle structure is null.");
    }
    *handle = CNA_NativeWindowHandle{};
    handle->struct_size = static_cast<uint32_t>(sizeof(CNA_NativeWindowHandle));
    handle->struct_version = NativeWindowHandleVersion;
    handle->system = CNA_NATIVE_WINDOW_SYSTEM_UNKNOWN;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GameWindowGetNativeWindowExt(
    const CNA_Handle gameHandle,
    CNA_NativeWindowHandle* const handle)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            handle, "The native-window-handle structure is invalid.",
            NativeWindowHandleVersion);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Microsoft::Xna::Framework::GameWindow* window = nullptr;
    if (const CNA_Result result = GetGameWindow(gameHandle, &window);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const CNA::Platform::NativeWindowHandle native = window->GetNativeWindowHandleEXT();
    handle->system = MapNativeWindowSystem(native.system);
    handle->display = native.display;
    handle->window = native.window;
    handle->surface = native.surface;
    handle->window_id = native.windowId;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GameWindowEndScreenDeviceChange(
    const CNA_Handle gameHandle,
    const CNA_StringView screenDeviceName,
    const int32_t clientWidth,
    const int32_t clientHeight)
{
    Microsoft::Xna::Framework::GameWindow* window = nullptr;
    if (const CNA_Result result = GetGameWindow(gameHandle, &window);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string name;
    if (const CNA_Result result =
            ReadText(screenDeviceName, "The display name is not valid UTF-8.", &name);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // The canonical name-only overload is this one with the current client size, so a caller
    // asks for that by passing a size this ABI reads as "keep it".
    const auto result = RequestWindowChange([&]() {
        if (clientWidth < 1 || clientHeight < 1) window->EndScreenDeviceChange(name);
        else window->EndScreenDeviceChange(name, clientWidth, clientHeight);
    });
    if (result != CNA_RESULT_SUCCESS) return result;
    Game* game = nullptr;
    if (const auto lookup = BorrowGame(gameHandle, &game); lookup != CNA_RESULT_SUCCESS) return lookup;
    return ReportLogicalSizeProviderFailure(*game);
}

[[nodiscard]] CNA_Result GameWindowSubscribe(
    const CNA_Handle gameHandle,
    const CNA_GameWindowEvent event,
    const CNA_GameEventCallback callback,
    void* const context,
    CNA_GameEventRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The window event callback is null.");
    }
    if (event > CNA_GAME_WINDOW_EVENT_MAXIMUM) {
        return InvalidArgument("The window event is not a defined identity.");
    }
    Microsoft::Xna::Framework::GameWindow* window = nullptr;
    if (const CNA_Result result = GetGameWindow(gameHandle, &window);
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
    std::shared_ptr<void> owner;
    if (const auto result = LookupHandle(gameHandle, ObjectKind::Game, &owner);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto token = source->Add(
        [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
    return CreateHandle(ObjectKind::GameEventRegistration,
        TrackGameRegistration(std::make_shared<GameRegistration>(owner, source, token)),
        outRegistration);
}

using Microsoft::Xna::Framework::GameWindow;
using Microsoft::Xna::Framework::FileDropEventArgsEXT;
using Microsoft::Xna::Framework::TextDropEventArgsEXT;

struct WindowCallbackState {
    bool active = true;
};

class WindowRegistration final : public GameRegistrationBase {
public:
    std::shared_ptr<WindowCallbackState> state = std::make_shared<WindowCallbackState>();
    std::weak_ptr<void> owner;
    std::function<void()> detach;
    ~WindowRegistration() override
    {
        // Registry exit cleanup can destroy the game before this registration.
        if (const auto sourceOwner = owner.lock(); sourceOwner && state->active && detach) detach();
        state->active = false;
    }
    void Invalidate() noexcept override { state->active = false; }
};

struct WindowCallbackScope {
    Game& game;
    explicit WindowCallbackScope(Game& value) : game(value) { CNA::C::Detail::EnterGameCallbackScope(game); }
    ~WindowCallbackScope() { CNA::C::Detail::LeaveGameCallbackScope(game); }
};

// A pointer to the base member allows the C adapter to invoke this protected hook on the
// existing window; no downcast to a fictitious derived window is involved.
struct LogicalSizeWindowAccess : GameWindow {
    static auto Setter() { return &LogicalSizeWindowAccess::SetLogicalSizeProviderEXT; }
};

struct LogicalSizeProviderState final {
    CNA_GameWindowLogicalSizeProviderEXT callback = nullptr;
    void* context = nullptr;
    int width = 0;
    int height = 0;
    bool invoking = false;
    bool failed = false;
};
struct LogicalSizeProviderEntry final {
    Game* game;
    std::weak_ptr<LogicalSizeProviderState> state;
};
std::vector<LogicalSizeProviderEntry> logicalSizeProviders;

CNA_Result ReportLogicalSizeProviderFailure(Game& game)
{
    for (const auto& entry : logicalSizeProviders)
        if (entry.game == &game)
            if (const auto state = entry.state.lock(); state && state->failed)
                return CNA::C::Detail::ReportGameCallbackFailure(game);
    return CNA_RESULT_SUCCESS;
}

CNA_Result WindowSetLogicalSizeProvider(CNA_Handle handle,
    CNA_GameWindowLogicalSizeProviderEXT callback, void* context)
{
    Game* game = nullptr;
    if (const auto result = BorrowGame(handle, &game); result != CNA_RESULT_SUCCESS)
    {
        // Provider replacement is an owner-thread registration operation, even when ordinary
        // foreign-thread handle calls have opted into the game's service queue.
        if (result == CNA_RESULT_THREAD) CNA::C::Detail::ClearForeignThreadRefusal();
        return result;
    }
    std::erase_if(logicalSizeProviders, [](const auto& entry) { return entry.state.expired(); });
    for (const auto& entry : logicalSizeProviders)
    {
        if (entry.game != game) continue;
        const auto state = entry.state.lock();
        state->callback = callback;
        state->context = callback ? context : nullptr;
        return CNA_RESULT_SUCCESS;
    }
    if (!callback)
    {
        (game->getWindowProperty().*LogicalSizeWindowAccess::Setter())({});
        return CNA_RESULT_SUCCESS;
    }
    auto state = std::make_shared<LogicalSizeProviderState>();
    const auto bounds = game->getWindowProperty().getClientBoundsProperty();
    state->width = bounds.Width;
    state->height = bounds.Height;
    state->callback = callback;
    state->context = context;
    logicalSizeProviders.push_back({game, state});
    (game->getWindowProperty().*LogicalSizeWindowAccess::Setter())(
        [state, game, handle](int& width, int& height) {
            const auto callbackSnapshot = state->callback;
            void* const contextSnapshot = state->context;
            if (!callbackSnapshot) return false;
            if (!state->invoking && !state->failed)
            {
                int32_t proposedWidth = state->width;
                int32_t proposedHeight = state->height;
                CNA_Bool available = CNA_FALSE;
                CNA_CallbackError error{sizeof(CNA_CallbackError), 1U, {nullptr, 0U}};
                state->invoking = true;
                const WindowCallbackScope scope(*game);
                const auto result = CallWithExceptionBarrier([&] {
                    return callbackSnapshot(handle, contextSnapshot, &proposedWidth,
                        &proposedHeight, &available, &error);
                });
                state->invoking = false;
                if (result != CNA_RESULT_SUCCESS || !CNA::C::Detail::IsCanonicalBool(available))
                {
                    state->failed = true;
                    if (result == CNA_RESULT_SUCCESS)
                        error.message = {"A logical-size callback returned a noncanonical boolean.", sizeof("A logical-size callback returned a noncanonical boolean.") - 1U};
                    static_cast<void>(CNA::C::Detail::RecordGameCallbackFailure(*game, error));
                }
                else if (available == CNA_TRUE && proposedWidth > 0 && proposedHeight > 0)
                {
                    state->width = proposedWidth;
                    state->height = proposedHeight;
                }
            }
            width = state->width;
            height = state->height;
            return width > 0 && height > 0;
        });
    return CNA_RESULT_SUCCESS;
}

CNA_Result WindowFileDrop(CNA_Handle gameHandle, CNA_GameWindowDropCallback callback,
    void* context, CNA_GameEventRegistrationHandle* output)
{
    if (!callback) return InvalidArgument("Null drop callback.");
    Game* game = nullptr;
    if (const auto result = BorrowGame(gameHandle, &game); result != CNA_RESULT_SUCCESS) return result;
    std::shared_ptr<void> owner;
    if (const auto result = LookupHandle(gameHandle, ObjectKind::Game, &owner);
        result != CNA_RESULT_SUCCESS) return result;
    auto* window = &game->getWindowProperty();
    auto registration = std::make_shared<WindowRegistration>();
    registration->owner = owner;
    const auto token = window->FileDropEXT.Add([state = registration->state, callback, context, game]
        (System::Object*, const FileDropEventArgsEXT& args) {
        if (!state->active) return;
        std::vector<CNA_StringView> views;
        for (const auto& file : args.getFilesProperty()) views.push_back({file.data(), file.size()});
        WindowCallbackScope scope(*game);
        callback(context, views.data(), views.size());
    });
    registration->detach = [window, token] { window->FileDropEXT.Remove(token); };
    return CreateHandle(ObjectKind::GameEventRegistration, TrackGameRegistration(registration), output);
}

CNA_Result WindowTextDrop(CNA_Handle gameHandle, CNA_GameWindowDropCallback callback,
    void* context, CNA_GameEventRegistrationHandle* output)
{
    if (!callback) return InvalidArgument("Null drop callback.");
    Game* game = nullptr;
    if (const auto result = BorrowGame(gameHandle, &game); result != CNA_RESULT_SUCCESS) return result;
    std::shared_ptr<void> owner;
    if (const auto result = LookupHandle(gameHandle, ObjectKind::Game, &owner);
        result != CNA_RESULT_SUCCESS) return result;
    auto* window = &game->getWindowProperty();
    auto registration = std::make_shared<WindowRegistration>();
    registration->owner = owner;
    const auto token = window->TextDropEXT.Add([state = registration->state, callback, context, game]
        (System::Object*, const TextDropEventArgsEXT& args) {
        if (!state->active) return;
        const auto& text = args.getTextProperty();
        const CNA_StringView view{text.data(), text.size()};
        WindowCallbackScope scope(*game);
        callback(context, &view, 1);
    });
    registration->detach = [window, token] { window->TextDropEXT.Remove(token); };
    return CreateHandle(ObjectKind::GameEventRegistration, TrackGameRegistration(registration), output);
}

CNA_Result WindowRaiseFile(CNA_Handle gameHandle, const CNA_StringView* files, uint64_t count)
{
    if (count > SIZE_MAX / sizeof(CNA_StringView)) return CNA::C::Detail::Overflow("Too many dropped paths.");
    if (count && !files) return InvalidArgument("Null dropped paths.");
    GameWindow* window = nullptr;
    if (const auto result = GetGameWindow(gameHandle, &window); result != CNA_RESULT_SUCCESS) return result;
    std::vector<std::string> paths;
    if (count > paths.max_size()) return CNA::C::Detail::Overflow("Too many dropped paths.");
    paths.reserve(static_cast<std::size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        std::string path;
        if (const auto result = ReadText(files[i], "Invalid dropped path.", &path, CNA::C::Detail::EmbeddedNul::Refused); result != CNA_RESULT_SUCCESS) return result;
        paths.push_back(std::move(path));
    }
    const FileDropEventArgsEXT args(std::move(paths));
    window->FileDropEXT.Raise(window, args);
    return CNA_RESULT_SUCCESS;
}

CNA_Result WindowRaiseText(CNA_Handle gameHandle, CNA_StringView text)
{
    GameWindow* window = nullptr;
    if (const auto result = GetGameWindow(gameHandle, &window); result != CNA_RESULT_SUCCESS) return result;
    std::string value;
    if (const auto result = ReadText(text, "Invalid dropped text.", &value, CNA::C::Detail::EmbeddedNul::Refused); result != CNA_RESULT_SUCCESS) return result;
    const TextDropEventArgsEXT args(std::move(value));
    window->TextDropEXT.Raise(window, args);
    return CNA_RESULT_SUCCESS;
}

} // namespace

// The routes generated from modules/c-api/spec/GameProperties.toml.
#include "generated/GameProperties.inc"
