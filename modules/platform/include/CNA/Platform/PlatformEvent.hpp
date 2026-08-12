// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace CNA::Platform {

    /** @brief Identifies a window within one platform instance. Zero means "no window". */
    using WindowId = std::uint32_t;

    /**
     * @brief Identifies a connected input device. Stable while the device stays connected.
     *
     * 64 bits rather than 32 because touch device identifiers genuinely are 64-bit while
     * keyboard, mouse and joystick ids are 32-bit. One width holds all of them losslessly, which
     * is what lets `DeviceEvent` and `IPlatformInputDevices` share a single identity vocabulary —
     * a caller enumerates once and then tracks changes, with nothing to reconcile.
     */
    using DeviceId = std::uint64_t;

    /** @brief The application is being asked to terminate. */
    struct QuitEvent
    {
        /** @brief Placeholder so the type is a distinct, non-empty alternative. */
        bool requested = true;
    };

    /** @brief What happened to a window. */
    enum class WindowEventKind
    {
        /** @brief The logical client size changed. */
        Resized,
        /** @brief The drawable pixel size changed, which may differ from a logical resize under high DPI. */
        PixelSizeChanged,
        /** @brief The window gained keyboard focus. */
        FocusGained,
        /** @brief The window lost keyboard focus. */
        FocusLost,
        /** @brief The user asked to close this window. */
        CloseRequested,
        /** @brief The window was minimised. */
        Minimized,
        /** @brief The window was maximised. */
        Maximized,
        /** @brief The window returned to its normal state. */
        Restored,
        /** @brief The window moved. */
        Moved,
        /** @brief The window's display scale changed. */
        DisplayScaleChanged,
        /** @brief The window moved to a different display. */
        DisplayChanged
    };

    /** @brief Something happened to a window. */
    struct WindowEvent
    {
        /** @brief Which window. */
        WindowId window = 0;
        /** @brief What happened. */
        WindowEventKind kind = WindowEventKind::Resized;
        /** @brief For size and move events, the first component (width or x); otherwise zero. */
        int data1 = 0;
        /** @brief For size and move events, the second component (height or y); otherwise zero. */
        int data2 = 0;
    };

    /** @brief A key was pressed or released. */
    struct KeyEvent
    {
        /** @brief The window with focus, or zero. */
        WindowId window = 0;
        /**
         * @brief Layout-independent physical key code.
         *
         * The value space is CNA's own; an implementation maps its native codes onto it. Games
         * that care about physical position (WASD) use this.
         */
        std::uint32_t scancode = 0;
        /** @brief Layout-dependent virtual key code, matching `Microsoft::Xna::Framework::Input::Keys`. */
        std::uint32_t keycode = 0;
        /**
         * @brief Modifier keys held at the time of the event, as a bitmask of `KeyModifier`
         * values — the same layout as `KeyboardSnapshot::modifiers`.
         */
        std::uint16_t modifiers = 0;
        /** @brief True for a press, false for a release. */
        bool pressed = false;
        /**
         * @brief True when this press was produced by auto-repeat rather than a fresh keystroke.
         *
         * Kept distinct because a platform without real key-release events must synthesise
         * releases from repeat timing, and a caller may need to know which it is getting.
         */
        bool repeat = false;
    };

    /** @brief Committed text was entered. */
    struct TextInputEvent
    {
        /** @brief The window with focus, or zero. */
        WindowId window = 0;
        /** @brief The committed text, UTF-8 encoded. */
        std::string text;
    };

    /** @brief In-progress input-method composition changed. */
    struct TextEditingEvent
    {
        /** @brief The window with focus, or zero. */
        WindowId window = 0;
        /** @brief The current composition text, UTF-8 encoded. */
        std::string text;
        /** @brief Cursor position within the composition, in characters. */
        int cursor = 0;
        /** @brief Length of the selected range within the composition, in characters. */
        int selectionLength = 0;
    };

    /** @brief The mouse moved. */
    struct MouseMotionEvent
    {
        /** @brief The window the pointer is over, or zero. */
        WindowId window = 0;
        /** @brief Pointer x position in the window's client area. */
        float x = 0.0f;
        /** @brief Pointer y position in the window's client area. */
        float y = 0.0f;
        /** @brief Horizontal movement since the previous motion event. */
        float deltaX = 0.0f;
        /** @brief Vertical movement since the previous motion event. */
        float deltaY = 0.0f;
    };

    /** @brief A mouse button was pressed or released. */
    struct MouseButtonEvent
    {
        /** @brief The window the pointer is over, or zero. */
        WindowId window = 0;
        /** @brief Button index; 1 is left, 2 middle, 3 right, matching the established convention. */
        std::uint8_t button = 0;
        /** @brief True for a press, false for a release. */
        bool pressed = false;
        /** @brief Number of clicks in this sequence: 1 for single, 2 for double. */
        std::uint8_t clicks = 1;
        /** @brief Pointer x position at the time of the event. */
        float x = 0.0f;
        /** @brief Pointer y position at the time of the event. */
        float y = 0.0f;
    };

    /** @brief The mouse wheel was scrolled. */
    struct MouseWheelEvent
    {
        /** @brief The window the pointer is over, or zero. */
        WindowId window = 0;
        /** @brief Horizontal scroll amount; positive is right. */
        float x = 0.0f;
        /** @brief Vertical scroll amount; positive is away from the user. */
        float y = 0.0f;
    };

    /** @brief What happened to a touch point. */
    enum class TouchEventKind
    {
        /** @brief A finger touched down. */
        Down,
        /** @brief A finger lifted. */
        Up,
        /** @brief A finger moved. */
        Motion,
        /** @brief The gesture was cancelled by the system. */
        Cancelled
    };

    /** @brief A touch point changed. */
    struct TouchEvent
    {
        /** @brief The window touched, or zero. */
        WindowId window = 0;
        /** @brief Identifies this finger for the duration of the gesture. */
        std::uint64_t fingerId = 0;
        /** @brief What happened. */
        TouchEventKind kind = TouchEventKind::Down;
        /** @brief Normalised x position in [0, 1]. */
        float x = 0.0f;
        /** @brief Normalised y position in [0, 1]. */
        float y = 0.0f;
        /** @brief Normalised pressure in [0, 1]. */
        float pressure = 0.0f;
    };

    /** @brief The class of device an add/remove event refers to. */
    enum class InputDeviceKind
    {
        /** @brief A keyboard. */
        Keyboard,
        /** @brief A mouse. */
        Mouse,
        /** @brief A gamepad, in the mapped-controller sense. */
        Gamepad,
        /** @brief A raw joystick. */
        Joystick,
        /** @brief A touchscreen or trackpad reporting touch points. */
        Touch,
        /** @brief A haptic device. */
        Haptic,
        /** @brief A motion or orientation sensor. */
        Sensor
    };

    /** @brief An input device was connected or disconnected. */
    struct DeviceEvent
    {
        /** @brief Which device. */
        DeviceId device = 0;
        /** @brief What class of device. */
        InputDeviceKind kind = InputDeviceKind::Gamepad;
        /** @brief True when connected, false when disconnected. */
        bool connected = false;
    };

    /** @brief A gamepad or joystick axis moved. */
    struct ControllerAxisEvent
    {
        /** @brief Which device. */
        DeviceId device = 0;
        /** @brief Axis index. */
        std::uint8_t axis = 0;
        /** @brief Axis position, normalised to [-1, 1]. */
        float value = 0.0f;
    };

    /** @brief A gamepad or joystick button changed. */
    struct ControllerButtonEvent
    {
        /** @brief Which device. */
        DeviceId device = 0;
        /** @brief Button index. */
        std::uint8_t button = 0;
        /** @brief True for a press, false for a release. */
        bool pressed = false;
    };

    /** @brief What lifecycle transition the application is making. */
    enum class AppLifecycleKind
    {
        /** @brief The application is about to move to the background. */
        WillEnterBackground,
        /** @brief The application has returned to the foreground. */
        DidEnterForeground,
        /** @brief The system is low on memory. */
        LowMemory
    };

    /** @brief The application's lifecycle state changed. */
    struct AppLifecycleEvent
    {
        /** @brief Which transition. */
        AppLifecycleKind kind = AppLifecycleKind::WillEnterBackground;
    };

    /**
     * @brief One event from the platform, in CNA's own vocabulary.
     *
     * A discriminated union rather than a tagged struct with a union of fields: the alternatives
     * carry genuinely different payloads (one of them owns a `std::string`), and `std::visit`
     * gives exhaustive handling that a `switch` over a type tag does not.
     *
     * No SDL enum value or type appears anywhere in this taxonomy. The mapping from a native
     * event to one of these lives in the implementation, which is what lets a terminal platform
     * that has no concept of a "window event" still produce a resize.
     *
     * Events are delivered in **batches** — see `IPlatform::PollEvents`. Per-event virtual
     * dispatch is exactly the design `cnaplatform.md` warns against.
     */
    using PlatformEvent = std::variant<
        QuitEvent,
        WindowEvent,
        KeyEvent,
        TextInputEvent,
        TextEditingEvent,
        MouseMotionEvent,
        MouseButtonEvent,
        MouseWheelEvent,
        TouchEvent,
        DeviceEvent,
        ControllerAxisEvent,
        ControllerButtonEvent,
        AppLifecycleEvent>;

    /**
     * @brief Returns the stable, human-readable name of an event's alternative.
     *
     * Used by the event-semantics golden tests (PLAT-6/PLAT-118), which compare recorded event
     * sequences across implementations and need a name that does not depend on variant index.
     *
     * @param event The event to name.
     * @return A stable name such as `"WindowEvent"` or `"KeyEvent"`.
     */
    [[nodiscard]] const std::string& GetEventTypeName(const PlatformEvent& event);

    /**
     * @brief Returns the window an event refers to, if it refers to one.
     *
     * @param event The event to inspect.
     * @return The window id, or zero for events that are not window-scoped (device connection,
     * application lifecycle, quit).
     */
    [[nodiscard]] WindowId GetEventWindow(const PlatformEvent& event);

    /**
     * @brief Returns the stable, human-readable name of a window event kind.
     *
     * @param kind The kind to name.
     * @return A stable name such as `"FocusGained"`.
     */
    [[nodiscard]] const std::string& ToString(WindowEventKind kind);

    /**
     * @brief Returns the stable, human-readable name of a touch event kind.
     *
     * @param kind The kind to name.
     * @return A stable name such as `"Motion"`.
     */
    [[nodiscard]] const std::string& ToString(TouchEventKind kind);

    /**
     * @brief Returns the stable, human-readable name of an input device kind.
     *
     * @param kind The kind to name.
     * @return A stable name such as `"Gamepad"`.
     */
    [[nodiscard]] const std::string& ToString(InputDeviceKind kind);

    /**
     * @brief Returns the stable, human-readable name of an application lifecycle transition.
     *
     * @param kind The kind to name.
     * @return A stable name such as `"WillEnterBackground"`.
     */
    [[nodiscard]] const std::string& ToString(AppLifecycleKind kind);

} // namespace CNA::Platform
