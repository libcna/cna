// SPDX-License-Identifier: MS-PL

#include "Sdl3EventMapper.hpp"

#include "Sdl3GamepadControls.hpp"
#include "Sdl3Modifiers.hpp"
#include "Sdl3KeyCodes.hpp"
#include "Sdl3Scancodes.hpp"

#include <SDL3/SDL.h>

#include <utility>

namespace CNA::Platform::Sdl3 {

    namespace {

        bool MapWindowEvent(const SDL_Event& source, PlatformEvent& destination)
        {
            WindowEvent window;
            window.window = static_cast<WindowId>(source.window.windowID);
            window.data1 = source.window.data1;
            window.data2 = source.window.data2;

            switch (source.type)
            {
                case SDL_EVENT_WINDOW_RESIZED:            window.kind = WindowEventKind::Resized; break;
                // Distinct from Resized on purpose: under high DPI the drawable pixel size can
                // change without the logical size doing so, and a renderer that treats them as
                // one draws at the wrong scale.
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: window.kind = WindowEventKind::PixelSizeChanged; break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED:       window.kind = WindowEventKind::FocusGained; break;
                case SDL_EVENT_WINDOW_FOCUS_LOST:         window.kind = WindowEventKind::FocusLost; break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:    window.kind = WindowEventKind::CloseRequested; break;
                case SDL_EVENT_WINDOW_MINIMIZED:          window.kind = WindowEventKind::Minimized; break;
                case SDL_EVENT_WINDOW_MAXIMIZED:          window.kind = WindowEventKind::Maximized; break;
                case SDL_EVENT_WINDOW_RESTORED:           window.kind = WindowEventKind::Restored; break;
                case SDL_EVENT_WINDOW_MOVED:              window.kind = WindowEventKind::Moved; break;
                case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                    window.kind = WindowEventKind::DisplayScaleChanged;
                    break;
                case SDL_EVENT_WINDOW_DISPLAY_CHANGED:    window.kind = WindowEventKind::DisplayChanged; break;
                default:
                    return false;
            }

            destination = window;
            return true;
        }

    } // namespace

    bool MapSdlEvent(const SDL_Event& source, PlatformEvent& destination)
    {
        switch (source.type)
        {
            case SDL_EVENT_QUIT:
                destination = QuitEvent{};
                return true;

            // --- window ------------------------------------------------------------------------
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MOVED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                return MapWindowEvent(source, destination);

            // --- keyboard ----------------------------------------------------------------------
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            {
                KeyEvent key;
                key.window = static_cast<WindowId>(source.key.windowID);
                key.scancode = ToScancode(source.key.scancode);
                key.keycode = ToKeyCode(source.key.key);
                key.modifiers = ToPlatformModifiers(source.key.mod);
                // The event type is authoritative. SDL normally mirrors it in `down`, but
                // synthetic/injected events are allowed to leave that convenience field stale.
                key.pressed = source.type == SDL_EVENT_KEY_DOWN;
                key.repeat = source.key.repeat;
                destination = key;
                return true;
            }

            case SDL_EVENT_TEXT_INPUT:
            {
                TextInputEvent text;
                text.window = static_cast<WindowId>(source.text.windowID);
                text.text = source.text.text != nullptr ? source.text.text : "";
                destination = text;
                return true;
            }

            case SDL_EVENT_TEXT_EDITING:
            {
                TextEditingEvent editing;
                editing.window = static_cast<WindowId>(source.edit.windowID);
                editing.text = source.edit.text != nullptr ? source.edit.text : "";
                editing.cursor = source.edit.start;
                editing.selectionLength = source.edit.length;
                destination = editing;
                return true;
            }

            case SDL_EVENT_TEXT_EDITING_CANDIDATES:
            {
                TextEditingCandidatesEvent editing;
                editing.window = static_cast<WindowId>(source.edit_candidates.windowID);
                editing.selectedCandidate = source.edit_candidates.selected_candidate;
                editing.horizontal = source.edit_candidates.horizontal;

                // SDL owns both the array and its strings only for the lifetime of this event.
                // Copy them into the value event before PollEvents recycles the SDL_Event.
                const int count = source.edit_candidates.candidates != nullptr
                                      ? source.edit_candidates.num_candidates
                                      : 0;
                editing.candidates.reserve(static_cast<std::size_t>(count > 0 ? count : 0));
                for (int index = 0; index < count; ++index)
                {
                    const char* candidate = source.edit_candidates.candidates[index];
                    editing.candidates.emplace_back(candidate != nullptr ? candidate : "");
                }

                destination = std::move(editing);
                return true;
            }

            // --- mouse -------------------------------------------------------------------------
            case SDL_EVENT_MOUSE_MOTION:
            {
                MouseMotionEvent motion;
                motion.window = static_cast<WindowId>(source.motion.windowID);
                motion.x = source.motion.x;
                motion.y = source.motion.y;
                motion.deltaX = source.motion.xrel;
                motion.deltaY = source.motion.yrel;
                destination = motion;
                return true;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                MouseButtonEvent button;
                button.window = static_cast<WindowId>(source.button.windowID);
                button.button = source.button.button;
                button.pressed = source.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                button.clicks = source.button.clicks;
                button.x = source.button.x;
                button.y = source.button.y;
                destination = button;
                return true;
            }

            case SDL_EVENT_MOUSE_WHEEL:
            {
                MouseWheelEvent wheel;
                wheel.window = static_cast<WindowId>(source.wheel.windowID);
                // SDL has already applied the host's natural-scrolling preference to x/y.  The
                // direction member describes that fact; undoing it here would change the
                // established FNA/CNA input behaviour when this event replaces SDL_Event.
                wheel.x = source.wheel.x;
                wheel.y = source.wheel.y;
                destination = wheel;
                return true;
            }

            // --- touch -------------------------------------------------------------------------
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_MOTION:
            case SDL_EVENT_FINGER_CANCELED:
            {
                TouchEvent touch;
                touch.window = static_cast<WindowId>(source.tfinger.windowID);
                touch.fingerId = static_cast<std::uint64_t>(source.tfinger.fingerID);
                touch.x = source.tfinger.x;
                touch.y = source.tfinger.y;
                touch.pressure = source.tfinger.pressure;
                if (SDL_Window* window = SDL_GetWindowFromID(source.tfinger.windowID))
                {
                    int width = 0;
                    int height = 0;
                    if (SDL_GetWindowSize(window, &width, &height) && width > 0 && height > 0)
                    {
                        touch.clientWidth = width;
                        touch.clientHeight = height;
                    }
                }
                switch (source.type)
                {
                    case SDL_EVENT_FINGER_DOWN:     touch.kind = TouchEventKind::Down; break;
                    case SDL_EVENT_FINGER_UP:       touch.kind = TouchEventKind::Up; break;
                    case SDL_EVENT_FINGER_MOTION:
                        touch.kind = TouchEventKind::Motion;
                        touch.deltaX = source.tfinger.dx;
                        touch.deltaY = source.tfinger.dy;
                        break;
                    default:                        touch.kind = TouchEventKind::Cancelled; break;
                }
                destination = touch;
                return true;
            }

            // --- devices -----------------------------------------------------------------------
            case SDL_EVENT_GAMEPAD_ADDED:
            case SDL_EVENT_GAMEPAD_REMOVED:
            {
                DeviceEvent device;
                device.device = static_cast<DeviceId>(source.gdevice.which);
                device.kind = InputDeviceKind::Gamepad;
                device.connected = source.type == SDL_EVENT_GAMEPAD_ADDED;
                destination = device;
                return true;
            }

            case SDL_EVENT_JOYSTICK_ADDED:
            case SDL_EVENT_JOYSTICK_REMOVED:
            {
                DeviceEvent device;
                device.device = static_cast<DeviceId>(source.jdevice.which);
                device.kind = InputDeviceKind::Joystick;
                device.connected = source.type == SDL_EVENT_JOYSTICK_ADDED;
                destination = device;
                return true;
            }

            case SDL_EVENT_KEYBOARD_ADDED:
            case SDL_EVENT_KEYBOARD_REMOVED:
            {
                DeviceEvent device;
                device.device = static_cast<DeviceId>(source.kdevice.which);
                device.kind = InputDeviceKind::Keyboard;
                device.connected = source.type == SDL_EVENT_KEYBOARD_ADDED;
                destination = device;
                return true;
            }

            case SDL_EVENT_MOUSE_ADDED:
            case SDL_EVENT_MOUSE_REMOVED:
            {
                DeviceEvent device;
                device.device = static_cast<DeviceId>(source.mdevice.which);
                device.kind = InputDeviceKind::Mouse;
                device.connected = source.type == SDL_EVENT_MOUSE_ADDED;
                destination = device;
                return true;
            }

            case SDL_EVENT_SENSOR_UPDATE:
            {
                SensorEvent sensor;
                sensor.device = static_cast<DeviceId>(source.sensor.which);
                for (std::size_t index = 0; index < sensor.values.size(); ++index)
                {
                    sensor.values[index] = source.sensor.data[index];
                }
                // Use the sensor's sample time, not the later time at which SDL queued the event.
                sensor.timestampNanoseconds = source.sensor.sensor_timestamp;
                destination = sensor;
                return true;
            }

            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            {
                const auto mappedAxis = ToGamepadAxis(
                    static_cast<SDL_GamepadAxis>(source.gaxis.axis));
                if (!mappedAxis.has_value())
                {
                    return false;
                }

                ControllerAxisEvent axis;
                axis.device = static_cast<DeviceId>(source.gaxis.which);
                axis.axis = mappedAxis.value();
                axis.value = NormalizeGamepadAxis(axis.axis, source.gaxis.value);
                destination = axis;
                return true;
            }

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            {
                const auto mappedButton = ToGamepadButton(
                    static_cast<SDL_GamepadButton>(source.gbutton.button));
                if (!mappedButton.has_value())
                {
                    return false;
                }

                ControllerButtonEvent button;
                button.device = static_cast<DeviceId>(source.gbutton.which);
                button.button = mappedButton.value();
                button.pressed = source.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                destination = button;
                return true;
            }

            // --- application lifecycle -----------------------------------------------------------
            case SDL_EVENT_WILL_ENTER_BACKGROUND:
                destination = AppLifecycleEvent{AppLifecycleKind::WillEnterBackground};
                return true;
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                destination = AppLifecycleEvent{AppLifecycleKind::DidEnterForeground};
                return true;
            case SDL_EVENT_LOW_MEMORY:
                destination = AppLifecycleEvent{AppLifecycleKind::LowMemory};
                return true;

            default:
                // SDL emits far more than CNA consumes. Dropping the rest is deliberate, and
                // returning false is what keeps PollEvents from appending an empty event for it.
                return false;
        }
    }

} // namespace CNA::Platform::Sdl3
