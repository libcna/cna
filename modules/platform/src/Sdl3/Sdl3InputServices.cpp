// SPDX-License-Identifier: MS-PL

#include "Sdl3InputServices.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "Sdl3Window.hpp"

#include <SDL3/SDL.h>

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
            }
            return SDL_SYSTEM_CURSOR_DEFAULT;
        }

    } // namespace

    // --- keyboard (PLAT-79) --------------------------------------------------------------------

    void Sdl3Keyboard::Update()
    {
        snapshot_.pressedKeys.clear();
        snapshot_.modifiers = static_cast<std::uint16_t>(SDL_GetModState());

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
            // Reported as a layout-dependent keycode, which is what XNA's Keys values are; the
            // scancode is the physical position and is carried on KeyEvent instead.
            const SDL_Keycode keycode =
                SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(scancode), SDL_KMOD_NONE, false);
            snapshot_.pressedKeys.push_back(static_cast<std::uint32_t>(keycode));
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

        snapshot_.x = static_cast<int>(x);
        snapshot_.y = static_cast<int>(y);

        // Repacked into CNA's own bit order rather than passed through: SDL's mask is
        // 1-based-button-indexed, and leaking that convention would make every consumer depend
        // on an SDL detail.
        std::uint8_t mask = 0;
        if ((buttons & SDL_BUTTON_LMASK) != 0) { mask |= 0x01; }
        if ((buttons & SDL_BUTTON_MMASK) != 0) { mask |= 0x02; }
        if ((buttons & SDL_BUTTON_RMASK) != 0) { mask |= 0x04; }
        snapshot_.buttons = mask;

        // Scroll is event-driven, not pollable: it accumulates from MouseWheelEvent, so Update()
        // deliberately leaves it alone rather than resetting it to zero every frame.
    }

    const MouseSnapshot& Sdl3Mouse::GetSnapshot() const { return snapshot_; }

    void Sdl3Mouse::SetPosition(IPlatformWindow& window, const int x, const int y)
    {
        Sdl3Window& sdlWindow = RequireSdl3Window(window, "Mouse::SetPosition");
        SDL_WarpMouseInWindow(sdlWindow.GetSdlWindow(), static_cast<float>(x), static_cast<float>(y));
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

        SDL_SetCursor(created);

        // Destroy the previous cursor only after the new one is current: freeing a cursor that is
        // still set is a use-after-free inside SDL.
        if (activeCursor_ != nullptr)
        {
            SDL_DestroyCursor(static_cast<SDL_Cursor*>(activeCursor_));
        }
        activeCursor_ = created;
    }

    void Sdl3Mouse::SetRelativeMode(const bool enabled)
    {
        // SDL3 scopes relative mode to a window. With no window focused there is nothing to
        // capture, so the request is recorded and refused rather than silently ignored.
        SDL_Window* focused = SDL_GetKeyboardFocus();
        if (focused == nullptr)
        {
            throw PlatformException("Mouse::SetRelativeMode", "no focused window to capture");
        }
        if (!SDL_SetWindowRelativeMouseMode(focused, enabled))
        {
            throw PlatformException("Mouse::SetRelativeMode", SDL_GetError());
        }
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
                    if (SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(button)))
                    {
                        snapshot.buttons |= (1u << button);
                    }
                }

                for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis)
                {
                    const Sint16 raw = SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(axis));
                    // Each half scaled by its own magnitude, so the extreme negative is exactly
                    // -1 rather than -1.00003 -- the same normalisation the event mapper uses.
                    const float value = raw < 0 ? static_cast<float>(raw) / 32768.0f
                                                : static_cast<float>(raw) / 32767.0f;
                    if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
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
