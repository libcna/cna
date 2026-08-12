// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/PlatformEvent.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;

/// One event of every alternative, so the tests below cover the whole variant by construction.
std::vector<PlatformEvent> OneOfEach()
{
    WindowEvent window;
    window.window = 7;
    window.kind = WindowEventKind::Resized;

    KeyEvent key;
    key.window = 7;

    TextInputEvent textInput;
    textInput.window = 7;
    textInput.text = "a";

    TextEditingEvent textEditing;
    textEditing.window = 7;

    TextEditingCandidatesEvent candidates;
    candidates.window = 7;

    MouseMotionEvent motion;
    motion.window = 7;

    MouseButtonEvent button;
    button.window = 7;

    MouseWheelEvent wheel;
    wheel.window = 7;

    TouchEvent touch;
    touch.window = 7;

    return {QuitEvent{},        window,
            key,                textInput,
            textEditing,        candidates,
            motion,
            button,             wheel,
            touch,              DeviceEvent{},
            ControllerAxisEvent{}, ControllerButtonEvent{},
            AppLifecycleEvent{}};
}

TEST(PlatformEventTests, VariantCoversEveryDocumentedAlternative)
{
    EXPECT_EQ(std::variant_size_v<PlatformEvent>, 14u);
    EXPECT_EQ(OneOfEach().size(), std::variant_size_v<PlatformEvent>);
}

TEST(PlatformEventTests, EventTypeNamesAreDistinctAndNonEmpty)
{
    // The golden event-semantics tests (PLAT-6/PLAT-118) compare recorded sequences across
    // implementations by name, so names must not depend on variant index and must not collide.
    std::set<std::string> names;
    for (const PlatformEvent& event : OneOfEach())
    {
        const std::string& name = GetEventTypeName(event);
        EXPECT_FALSE(name.empty());
        names.insert(name);
    }
    EXPECT_EQ(names.size(), std::variant_size_v<PlatformEvent>);
}

TEST(PlatformEventTests, EventTypeNameIsStableStorage)
{
    for (const PlatformEvent& event : OneOfEach())
    {
        EXPECT_EQ(&GetEventTypeName(event), &GetEventTypeName(event));
    }
}

TEST(PlatformEventTests, WindowScopedEventsReportTheirWindow)
{
    for (const PlatformEvent& event : OneOfEach())
    {
        const std::string& name = GetEventTypeName(event);
        const bool windowScoped = name == "WindowEvent" || name == "KeyEvent" ||
                                  name == "TextInputEvent" || name == "TextEditingEvent" ||
                                  name == "TextEditingCandidatesEvent" ||
                                  name == "MouseMotionEvent" || name == "MouseButtonEvent" ||
                                  name == "MouseWheelEvent" || name == "TouchEvent";
        if (windowScoped)
        {
            EXPECT_EQ(GetEventWindow(event), 7u) << name;
        }
    }
}

TEST(PlatformEventTests, ProcessScopedEventsReportNoWindow)
{
    // Quit, device connection, controller input and application lifecycle are process-scoped.
    // Inventing a window id for them would be a lie a caller could act on.
    EXPECT_EQ(GetEventWindow(PlatformEvent{QuitEvent{}}), 0u);
    EXPECT_EQ(GetEventWindow(PlatformEvent{DeviceEvent{}}), 0u);
    EXPECT_EQ(GetEventWindow(PlatformEvent{ControllerAxisEvent{}}), 0u);
    EXPECT_EQ(GetEventWindow(PlatformEvent{ControllerButtonEvent{}}), 0u);
    EXPECT_EQ(GetEventWindow(PlatformEvent{AppLifecycleEvent{}}), 0u);
}

TEST(PlatformEventTests, KeyEventDistinguishesRepeatFromFreshPress)
{
    // A platform without real key-release events must synthesise releases from repeat timing,
    // so a caller may need to tell the two apart. Defaulting repeat to true would misreport
    // every ordinary keystroke.
    const KeyEvent key;
    EXPECT_FALSE(key.repeat);
    EXPECT_FALSE(key.pressed);
}

TEST(PlatformEventTests, TouchCoordinatesAreNormalised)
{
    // Documented as [0,1]; a caller scales by the window size. Defaults must sit inside the range.
    const TouchEvent touch;
    EXPECT_GE(touch.x, 0.0f);
    EXPECT_LE(touch.x, 1.0f);
    EXPECT_GE(touch.pressure, 0.0f);
    EXPECT_LE(touch.pressure, 1.0f);
    EXPECT_FLOAT_EQ(touch.deltaX, 0.0f);
    EXPECT_FLOAT_EQ(touch.deltaY, 0.0f);
}

TEST(PlatformEventTests, WindowEventKindNamesAreDistinct)
{
    const WindowEventKind kinds[] = {
        WindowEventKind::Resized,   WindowEventKind::PixelSizeChanged,
        WindowEventKind::FocusGained, WindowEventKind::FocusLost,
        WindowEventKind::CloseRequested, WindowEventKind::Minimized,
        WindowEventKind::Maximized, WindowEventKind::Restored,
        WindowEventKind::Moved,     WindowEventKind::DisplayScaleChanged,
        WindowEventKind::DisplayChanged};
    std::set<std::string> names;
    for (const WindowEventKind kind : kinds)
    {
        EXPECT_FALSE(ToString(kind).empty());
        names.insert(ToString(kind));
    }
    EXPECT_EQ(names.size(), 11u);
}

TEST(PlatformEventTests, ResizedAndPixelSizeChangedAreDistinctKinds)
{
    // Under high DPI a logical resize and a drawable-pixel-size change are different events, and
    // conflating them is how a renderer ends up drawing at the wrong scale.
    EXPECT_NE(ToString(WindowEventKind::Resized), ToString(WindowEventKind::PixelSizeChanged));
}

TEST(PlatformEventTests, TouchInputDeviceAndLifecycleKindNamesAreDistinct)
{
    std::set<std::string> touchNames;
    for (const TouchEventKind kind : {TouchEventKind::Down, TouchEventKind::Up,
                                      TouchEventKind::Motion, TouchEventKind::Cancelled})
    {
        touchNames.insert(ToString(kind));
    }
    EXPECT_EQ(touchNames.size(), 4u);

    std::set<std::string> deviceNames;
    for (const InputDeviceKind kind : {InputDeviceKind::Keyboard, InputDeviceKind::Mouse,
                                       InputDeviceKind::Gamepad, InputDeviceKind::Joystick,
                                       InputDeviceKind::Haptic, InputDeviceKind::Sensor})
    {
        deviceNames.insert(ToString(kind));
    }
    EXPECT_EQ(deviceNames.size(), 6u);

    std::set<std::string> lifecycleNames;
    for (const AppLifecycleKind kind : {AppLifecycleKind::WillEnterBackground,
                                        AppLifecycleKind::DidEnterForeground,
                                        AppLifecycleKind::LowMemory})
    {
        lifecycleNames.insert(ToString(kind));
    }
    EXPECT_EQ(lifecycleNames.size(), 3u);
}

TEST(PlatformEventTests, EventsAreCopyableForBatchBuffering)
{
    // PollEvents fills a caller-owned vector that is reused across frames, so events must be
    // ordinary copyable values.
    const std::vector<PlatformEvent> batch = OneOfEach();
    const std::vector<PlatformEvent> copy = batch;
    ASSERT_EQ(copy.size(), batch.size());
    for (std::size_t i = 0; i < copy.size(); ++i)
    {
        EXPECT_EQ(GetEventTypeName(copy[i]), GetEventTypeName(batch[i]));
    }
}

} // namespace
