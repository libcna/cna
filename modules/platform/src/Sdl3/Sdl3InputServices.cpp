// SPDX-License-Identifier: MS-PL

#include "Sdl3InputServices.hpp"

#include "Sdl3GamepadControls.hpp"
#include "Sdl3KeyCodes.hpp"
#include "Sdl3Modifiers.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "Sdl3Window.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>

namespace CNA::Platform::Sdl3 {

    namespace {

        Sdl3Window& RequireSdl3Window(IPlatformWindow& window, const char* operation)
        {
            auto* sdlWindow = dynamic_cast<Sdl3Window*>(&window);
            if (sdlWindow == nullptr)
            {
                throw PlatformException(operation, "window was not created by the SDL3 platform");
            }
            return *sdlWindow;
        }

        SDL_SystemCursor ToSdlCursor(const SystemCursor cursor)
        {
            switch (cursor)
            {
                case SystemCursor::Arrow:      return SDL_SYSTEM_CURSOR_DEFAULT;
                case SystemCursor::IBeam:      return SDL_SYSTEM_CURSOR_TEXT;
                case SystemCursor::Wait:       return SDL_SYSTEM_CURSOR_WAIT;
                case SystemCursor::Crosshair:  return SDL_SYSTEM_CURSOR_CROSSHAIR;
                case SystemCursor::Move:       return SDL_SYSTEM_CURSOR_MOVE;
                case SystemCursor::NotAllowed: return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
                case SystemCursor::Pointer:    return SDL_SYSTEM_CURSOR_POINTER;
                case SystemCursor::Progress:   return SDL_SYSTEM_CURSOR_PROGRESS;
                case SystemCursor::NwseResize: return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
                case SystemCursor::NeswResize: return SDL_SYSTEM_CURSOR_NESW_RESIZE;
                case SystemCursor::EwResize:   return SDL_SYSTEM_CURSOR_EW_RESIZE;
                case SystemCursor::NsResize:   return SDL_SYSTEM_CURSOR_NS_RESIZE;
            }
            return SDL_SYSTEM_CURSOR_DEFAULT;
        }

        void AccumulateWheel(int& total, const float notches)
        {
            const long long delta = static_cast<long long>(static_cast<int>(notches)) * 120;
            const long long next = static_cast<long long>(total) + delta;
            total = static_cast<int>(std::clamp(
                next,
                static_cast<long long>(std::numeric_limits<int>::min()),
                static_cast<long long>(std::numeric_limits<int>::max())));
        }

    } // namespace

    // --- keyboard (PLAT-79) --------------------------------------------------------------------


    void Sdl3Keyboard::Update()
    {
        snapshot_.pressedKeys.clear();
        snapshot_.modifiers = ToPlatformModifiers(SDL_GetModState());
        std::array<bool, 256> seen{};

        int count = 0;
        const bool* keys = SDL_GetKeyboardState(&count);
        if (keys == nullptr)
        {
            return;
        }

        // One pass over SDL's whole key array per frame, producing a compact list of what is
        // held. The alternative -- querying per key on demand -- is exactly the per-call platform
        // traffic the input-snapshot rule exists to prevent.
        for (int scancode = 0; scancode < count; ++scancode)
        {
            if (!keys[scancode])
            {
                continue;
            }
            // Translate SDL's mostly-Unicode keycodes into the contract's virtual-key vocabulary;
            // the scancode is the physical position and is carried on KeyEvent instead.
            const SDL_Keycode nativeKeycode =
                SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(scancode), SDL_KMOD_NONE, false);
            const KeyCode keycode = ToKeyCode(nativeKeycode);
            if (keycode == KeyCode::None)
            {
                continue;
            }

            const std::uint32_t value = static_cast<std::uint32_t>(keycode);
            if (value < seen.size() && !seen[value])
            {
                seen[value] = true;
                snapshot_.pressedKeys.push_back(keycode);
            }
        }
    }

    const KeyboardSnapshot& Sdl3Keyboard::GetSnapshot() const { return snapshot_; }

    bool Sdl3Keyboard::HasKeyboard() const { return SDL_HasKeyboard(); }

    // --- mouse (PLAT-80/81) --------------------------------------------------------------------

    Sdl3Mouse::~Sdl3Mouse()
    {
        if (activeCursor_ != nullptr)
        {
            SDL_DestroyCursor(static_cast<SDL_Cursor*>(activeCursor_));
        }
    }

    void Sdl3Mouse::Update()
    {
        float x = 0.0f;
        float y = 0.0f;
        const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);

        if (SDL_Window* focused = SDL_GetMouseFocus())
        {
            snapshot_.window = SDL_GetWindowID(focused);
        }

        snapshot_.x = static_cast<int>(x);
        snapshot_.y = static_cast<int>(y);

        // Repacked into CNA's own bit order rather than passed through: SDL's mask is
        // 1-based-button-indexed, and leaking that convention would make every consumer depend
        // on an SDL detail.
        std::uint8_t mask = 0;
        if ((buttons & SDL_BUTTON_LMASK) != 0) { mask |= 0x01; }
        if ((buttons & SDL_BUTTON_MMASK) != 0) { mask |= 0x02; }
        if ((buttons & SDL_BUTTON_RMASK) != 0) { mask |= 0x04; }
        if ((buttons & SDL_BUTTON_X1MASK) != 0) { mask |= 0x08; }
        if ((buttons & SDL_BUTTON_X2MASK) != 0) { mask |= 0x10; }
        snapshot_.buttons = mask;

        if (relativeMode_)
        {
            float deltaX = 0.0f;
            float deltaY = 0.0f;
            (void)SDL_GetRelativeMouseState(&deltaX, &deltaY);
            relativeDeltaX_ += deltaX;
            relativeDeltaY_ += deltaY;
        }

        // Scroll is event-driven, not pollable. ObserveEvent() accumulates it, so Update()
        // deliberately leaves both wheel totals alone rather than resetting them each frame.
    }

    const MouseSnapshot& Sdl3Mouse::GetSnapshot() const { return snapshot_; }

    MouseDelta Sdl3Mouse::ConsumeRelativeDelta()
    {
        if (!relativeMode_)
        {
            return {};
        }

        const MouseDelta result{static_cast<int>(relativeDeltaX_),
                                static_cast<int>(relativeDeltaY_)};
        relativeDeltaX_ = 0.0f;
        relativeDeltaY_ = 0.0f;
        return result;
    }

    void Sdl3Mouse::ObserveEvent(const PlatformEvent& event)
    {
        const auto* wheel = std::get_if<MouseWheelEvent>(&event);
        if (wheel == nullptr)
        {
            return;
        }

        snapshot_.window = wheel->window;
        AccumulateWheel(snapshot_.scrollX, wheel->x);
        AccumulateWheel(snapshot_.scrollY, wheel->y);
    }

    void Sdl3Mouse::SetPosition(const WindowId window, const int x, const int y)
    {
        snapshot_.window = window;
        snapshot_.x = x;
        snapshot_.y = y;

        if (window == 0)
        {
            return;
        }
        SDL_Window* nativeWindow = SDL_GetWindowFromID(window);
        if (nativeWindow == nullptr)
        {
            throw PlatformException("Mouse::SetPosition", "window id is not owned by SDL3");
        }
        SDL_WarpMouseInWindow(nativeWindow, static_cast<float>(x), static_cast<float>(y));
    }

    void Sdl3Mouse::SetCursorVisible(const bool visible)
    {
        if (visible)
        {
            SDL_ShowCursor();
        }
        else
        {
            SDL_HideCursor();
        }
    }

    void Sdl3Mouse::SetCursor(const SystemCursor cursor)
    {
        SDL_Cursor* created = SDL_CreateSystemCursor(ToSdlCursor(cursor));
        if (created == nullptr)
        {
            throw PlatformException("Mouse::SetCursor", SDL_GetError());
        }

        InstallCursor(created);
    }

    void Sdl3Mouse::SetCursor(const CursorImage& cursor)
    {
        if (cursor.width <= 0 || cursor.height <= 0
            || cursor.width > std::numeric_limits<int>::max() / static_cast<int>(sizeof(std::uint32_t)))
        {
            throw PlatformException("Mouse::SetCursor", "custom cursor dimensions are invalid");
        }

        const auto width = static_cast<std::size_t>(cursor.width);
        const auto height = static_cast<std::size_t>(cursor.height);
        if (width > std::numeric_limits<std::size_t>::max() / height
            || cursor.hotSpotX < 0 || cursor.hotSpotX >= cursor.width
            || cursor.hotSpotY < 0 || cursor.hotSpotY >= cursor.height)
        {
            throw PlatformException("Mouse::SetCursor", "custom cursor pixels or hot spot are invalid");
        }
        const std::size_t pixelCount = width * height;
        if (cursor.rgba.size() != pixelCount
            || pixelCount > std::numeric_limits<std::size_t>::max() / 4u)
        {
            throw PlatformException("Mouse::SetCursor", "custom cursor pixel count is invalid");
        }

        // SDL_PIXELFORMAT_RGBA32 means the bytes in memory are R,G,B,A on every endian. Expand the
        // contract's numeric 0xAABBGGRR representation explicitly so that promise does not depend
        // on host byte order. SDL copies this temporary surface into the native cursor.
        std::vector<std::uint8_t> rgba(pixelCount * 4u);
        for (std::size_t i = 0; i < pixelCount; ++i)
        {
            const std::uint32_t packed = cursor.rgba[i];
            rgba[i * 4u] = static_cast<std::uint8_t>(packed);
            rgba[i * 4u + 1u] = static_cast<std::uint8_t>(packed >> 8u);
            rgba[i * 4u + 2u] = static_cast<std::uint8_t>(packed >> 16u);
            rgba[i * 4u + 3u] = static_cast<std::uint8_t>(packed >> 24u);
        }
        SDL_Surface* surface = SDL_CreateSurfaceFrom(
            cursor.width, cursor.height, SDL_PIXELFORMAT_RGBA32, rgba.data(),
            cursor.width * 4);
        if (surface == nullptr)
        {
            throw PlatformException("Mouse::SetCursor", SDL_GetError());
        }

        SDL_Cursor* created = SDL_CreateColorCursor(surface, cursor.hotSpotX, cursor.hotSpotY);
        SDL_DestroySurface(surface);
        if (created == nullptr)
        {
            throw PlatformException("Mouse::SetCursor", SDL_GetError());
        }

        InstallCursor(created);
    }

    void Sdl3Mouse::InstallCursor(void* cursor)
    {
        auto* created = static_cast<SDL_Cursor*>(cursor);
        if (!SDL_SetCursor(created))
        {
            const std::string error = SDL_GetError();
            SDL_DestroyCursor(created);
            throw PlatformException("Mouse::SetCursor", error);
        }

        // Destroy the previous cursor only after the new one is current: freeing a cursor that is
        // still set is a use-after-free inside SDL.
        if (activeCursor_ != nullptr)
        {
            SDL_DestroyCursor(static_cast<SDL_Cursor*>(activeCursor_));
        }
        activeCursor_ = created;
    }

    void Sdl3Mouse::SetRelativeMode(const WindowId window, const bool enabled)
    {
        // SDL3 scopes relative mode to a window. With no associated window there is nothing to
        // capture, so the request is refused rather than silently reporting success.
        SDL_Window* target = window != 0 ? SDL_GetWindowFromID(window) : nullptr;
        if (target == nullptr)
        {
            throw PlatformException("Mouse::SetRelativeMode", "no valid window to capture");
        }
        if (!SDL_SetWindowRelativeMouseMode(target, enabled))
        {
            throw PlatformException("Mouse::SetRelativeMode", SDL_GetError());
        }

        // Flush native and stored displacement on every transition. Otherwise movement that
        // occurred before capture would leak into the first relative GetState() result.
        float ignoredX = 0.0f;
        float ignoredY = 0.0f;
        (void)SDL_GetRelativeMouseState(&ignoredX, &ignoredY);
        relativeDeltaX_ = 0.0f;
        relativeDeltaY_ = 0.0f;
        snapshot_.window = window;
        relativeMode_ = enabled;
    }

    bool Sdl3Mouse::IsRelativeMode() const { return relativeMode_; }

    bool Sdl3Mouse::SetCapture(const bool enabled) { return SDL_CaptureMouse(enabled); }

    bool Sdl3Mouse::TryGetGlobalPosition(float& x, float& y) const
    {
        float globalX = 0.0f;
        float globalY = 0.0f;
        SDL_GetGlobalMouseState(&globalX, &globalY);

        // SDL reports the position unconditionally and has no "unknown" answer, so the only way
        // this can fail is with no video subsystem up -- in which case there is no desktop to
        // have a position in. The out parameters stay untouched, as the contract promises.
        if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
        {
            return false;
        }

        x = globalX;
        y = globalY;
        return true;
    }

    bool Sdl3Mouse::SetGlobalPosition(const float x, const float y)
    {
        return SDL_WarpMouseGlobal(x, y);
    }

    // --- gamepad (PLAT-82) ---------------------------------------------------------------------

    Sdl3Gamepad::~Sdl3Gamepad() { CloseAll(); }

    void Sdl3Gamepad::CloseAll()
    {
        for (void* handle : handles_)
        {
            if (handle != nullptr)
            {
                SDL_CloseGamepad(static_cast<SDL_Gamepad*>(handle));
            }
        }
        handles_.clear();
    }

    void Sdl3Gamepad::Update()
    {
        CloseAll();
        snapshots_.clear();

        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (ids == nullptr)
        {
            return;
        }

        handles_.reserve(static_cast<std::size_t>(count));
        snapshots_.reserve(static_cast<std::size_t>(count));

        for (int i = 0; i < count; ++i)
        {
            SDL_Gamepad* gamepad = SDL_OpenGamepad(ids[i]);
            handles_.push_back(gamepad);

            GamepadSnapshot snapshot;
            snapshot.connected = gamepad != nullptr;
            if (gamepad != nullptr)
            {
                for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
                {
                    const auto mappedButton = ToGamepadButton(static_cast<SDL_GamepadButton>(button));
                    if (mappedButton.has_value() &&
                        SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(button)))
                    {
                        snapshot.buttons |= static_cast<std::uint32_t>(mappedButton.value());
                    }
                }

                for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis)
                {
                    const auto mappedAxis = ToGamepadAxis(static_cast<SDL_GamepadAxis>(axis));
                    if (!mappedAxis.has_value())
                    {
                        continue;
                    }
                    const Sint16 raw = SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(axis));
                    const float value = NormalizeGamepadAxis(mappedAxis.value(), raw);
                    if (mappedAxis.value() == GamepadAxis::LeftTrigger ||
                        mappedAxis.value() == GamepadAxis::RightTrigger)
                    {
                        snapshot.triggers.push_back(value);
                    }
                    else
                    {
                        snapshot.axes.push_back(value);
                    }
                }
            }
            snapshots_.push_back(std::move(snapshot));
        }

        SDL_free(ids);
    }

    int Sdl3Gamepad::GetCount() const { return static_cast<int>(snapshots_.size()); }

    const GamepadSnapshot& Sdl3Gamepad::GetSnapshot(const int index) const
    {
        // Polling an empty or out-of-range slot is ordinary control flow -- XNA games read all
        // four player indices unconditionally -- so it reports not-connected instead of throwing.
        static const GamepadSnapshot disconnected;
        if (index < 0 || index >= static_cast<int>(snapshots_.size()))
        {
            return disconnected;
        }
        return snapshots_[static_cast<std::size_t>(index)];
    }

    std::string Sdl3Gamepad::GetName(const int index) const
    {
        if (index < 0 || index >= static_cast<int>(handles_.size()) || handles_[static_cast<std::size_t>(index)] == nullptr)
        {
            return {};
        }
        const char* name = SDL_GetGamepadName(static_cast<SDL_Gamepad*>(handles_[static_cast<std::size_t>(index)]));
        return name != nullptr ? std::string(name) : std::string();
    }

    bool Sdl3Gamepad::SetRumble(const int index, const float lowFrequency, const float highFrequency,
                                const std::uint32_t durationMilliseconds)
    {
        if (index < 0 || index >= static_cast<int>(handles_.size()) || handles_[static_cast<std::size_t>(index)] == nullptr)
        {
            return false;
        }

        const auto scale = [](const float value) {
            const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
            return static_cast<Uint16>(clamped * 65535.0f);
        };

        return SDL_RumbleGamepad(static_cast<SDL_Gamepad*>(handles_[static_cast<std::size_t>(index)]),
                                 scale(lowFrequency), scale(highFrequency), durationMilliseconds);
    }

    // --- text input (PLAT-87) --------------------------------------------------------------------

    void Sdl3TextInput::Start(IPlatformWindow& window)
    {
        Sdl3Window& sdlWindow = RequireSdl3Window(window, "TextInput::Start");
        if (!SDL_StartTextInput(sdlWindow.GetSdlWindow()))
        {
            throw PlatformException("TextInput::Start", SDL_GetError());
        }
        active_ = true;
    }

    void Sdl3TextInput::Stop(IPlatformWindow& window)
    {
        Sdl3Window& sdlWindow = RequireSdl3Window(window, "TextInput::Stop");
        SDL_StopTextInput(sdlWindow.GetSdlWindow());
        active_ = false;
    }

    bool Sdl3TextInput::IsActive() const { return active_; }

    void Sdl3TextInput::SetInputArea(IPlatformWindow& window, const TextInputArea& area)
    {
        Sdl3Window& sdlWindow = RequireSdl3Window(window, "TextInput::SetInputArea");
        const SDL_Rect rect{area.x, area.y, area.width, area.height};
        SDL_SetTextInputArea(sdlWindow.GetSdlWindow(), &rect, area.cursorOffset);
    }

    // --- input device enumeration (PLAT-77b) ----------------------------------------------------

    namespace {

        /// SDL's three enumeration calls have the same shape -- return a malloc'd id array and a
        /// count -- but different element types and name lookups, so the shared part is the
        /// ownership dance rather than the call itself.
        template <typename TId, typename TEnumerate, typename TName>
        std::vector<InputDeviceInfo> EnumerateDevices(const InputDeviceKind kind,
                                                      TEnumerate enumerate, TName nameOf)
        {
            int count = 0;
            TId* ids = enumerate(&count);
            if (ids == nullptr)
            {
                return {};
            }

            std::vector<InputDeviceInfo> devices;
            devices.reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i)
            {
                InputDeviceInfo info;
                info.id = static_cast<DeviceId>(ids[i]);
                info.kind = kind;
                const char* name = nameOf(ids[i]);
                info.name = name != nullptr ? std::string(name) : std::string();
                devices.push_back(std::move(info));
            }
            SDL_free(ids);
            return devices;
        }

        template <typename TId, typename TEnumerate>
        bool AnyDevice(TEnumerate enumerate)
        {
            int count = 0;
            TId* ids = enumerate(&count);
            if (ids == nullptr)
            {
                return false;
            }
            SDL_free(ids);
            return count > 0;
        }

    } // namespace

    std::vector<InputDeviceInfo> Sdl3InputDevices::GetDevices(const InputDeviceKind kind) const
    {
        switch (kind)
        {
            case InputDeviceKind::Keyboard:
                return EnumerateDevices<SDL_KeyboardID>(kind, SDL_GetKeyboards,
                                                        SDL_GetKeyboardNameForID);
            case InputDeviceKind::Mouse:
                return EnumerateDevices<SDL_MouseID>(kind, SDL_GetMice, SDL_GetMouseNameForID);
            case InputDeviceKind::Gamepad:
                return EnumerateDevices<SDL_JoystickID>(kind, SDL_GetGamepads,
                                                        SDL_GetGamepadNameForID);
            case InputDeviceKind::Joystick:
                return EnumerateDevices<SDL_JoystickID>(kind, SDL_GetJoysticks,
                                                        SDL_GetJoystickNameForID);
            case InputDeviceKind::Touch:
                return EnumerateDevices<SDL_TouchID>(kind, SDL_GetTouchDevices,
                                                     SDL_GetTouchDeviceName);
            case InputDeviceKind::Haptic:
                return EnumerateDevices<SDL_HapticID>(kind, SDL_GetHaptics,
                                                      SDL_GetHapticNameForID);
            case InputDeviceKind::Sensor:
                return EnumerateDevices<SDL_SensorID>(kind, SDL_GetSensors, SDL_GetSensorNameForID);
        }
        return {};
    }

    bool Sdl3InputDevices::HasDevice(const InputDeviceKind kind) const
    {
        switch (kind)
        {
            case InputDeviceKind::Keyboard: return AnyDevice<SDL_KeyboardID>(SDL_GetKeyboards);
            case InputDeviceKind::Mouse:    return AnyDevice<SDL_MouseID>(SDL_GetMice);
            case InputDeviceKind::Gamepad:  return AnyDevice<SDL_JoystickID>(SDL_GetGamepads);
            case InputDeviceKind::Joystick: return AnyDevice<SDL_JoystickID>(SDL_GetJoysticks);
            case InputDeviceKind::Touch:    return AnyDevice<SDL_TouchID>(SDL_GetTouchDevices);
            case InputDeviceKind::Haptic:   return AnyDevice<SDL_HapticID>(SDL_GetHaptics);
            case InputDeviceKind::Sensor:   return AnyDevice<SDL_SensorID>(SDL_GetSensors);
        }
        return false;
    }

} // namespace CNA::Platform::Sdl3
