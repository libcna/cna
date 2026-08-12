// SPDX-License-Identifier: MS-PL
//
// PLAT-33..38: the SDL event taxonomy mapped onto PlatformEvent.
//
// Driven by synthetic SDL_Event values rather than by real input, so every branch is reachable
// without a user, a device or a display server. This is the one test file that includes SDL
// directly: it is testing the SDL implementation, and modules/platform is the allowlisted place
// for that (PLAT-3).

#include "../../../src/Sdl3/Sdl3EventMapper.hpp"
#include "../../../src/Sdl3/Sdl3Scancodes.hpp"

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <string>

namespace {

using namespace CNA::Platform;
using CNA::Platform::Sdl3::MapSdlEvent;

SDL_Event MakeEvent(const SDL_EventType type)
{
    SDL_Event event{};
    event.type = type;
    return event;
}

// --- window events (PLAT-33) ----------------------------------------------------------------------

TEST(Sdl3EventMapperTests, WindowEventsMapToTheirKinds)
{
    const struct
    {
        SDL_EventType sdl;
        WindowEventKind expected;
    } cases[] = {
        {SDL_EVENT_WINDOW_RESIZED, WindowEventKind::Resized},
        {SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, WindowEventKind::PixelSizeChanged},
        {SDL_EVENT_WINDOW_FOCUS_GAINED, WindowEventKind::FocusGained},
        {SDL_EVENT_WINDOW_FOCUS_LOST, WindowEventKind::FocusLost},
        {SDL_EVENT_WINDOW_CLOSE_REQUESTED, WindowEventKind::CloseRequested},
        {SDL_EVENT_WINDOW_MINIMIZED, WindowEventKind::Minimized},
        {SDL_EVENT_WINDOW_MAXIMIZED, WindowEventKind::Maximized},
        {SDL_EVENT_WINDOW_RESTORED, WindowEventKind::Restored},
        {SDL_EVENT_WINDOW_MOVED, WindowEventKind::Moved},
        {SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED, WindowEventKind::DisplayScaleChanged},
        {SDL_EVENT_WINDOW_DISPLAY_CHANGED, WindowEventKind::DisplayChanged},
    };

    for (const auto& testCase : cases)
    {
        SDL_Event source = MakeEvent(testCase.sdl);
        source.window.windowID = 42;
        source.window.data1 = 1024;
        source.window.data2 = 768;

        PlatformEvent mapped;
        ASSERT_TRUE(MapSdlEvent(source, mapped)) << ToString(testCase.expected);
        ASSERT_EQ(GetEventTypeName(mapped), "WindowEvent");

        const auto& window = std::get<WindowEvent>(mapped);
        EXPECT_EQ(window.kind, testCase.expected);
        EXPECT_EQ(window.window, 42u);
        EXPECT_EQ(window.data1, 1024);
        EXPECT_EQ(window.data2, 768);
    }
}

TEST(Sdl3EventMapperTests, ResizeAndPixelSizeChangeStayDistinct)
{
    // Conflating them is how a renderer draws at the wrong scale under high DPI, so the mapping
    // is asserted rather than assumed.
    SDL_Event resized = MakeEvent(SDL_EVENT_WINDOW_RESIZED);
    SDL_Event pixels = MakeEvent(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);

    PlatformEvent a;
    PlatformEvent b;
    ASSERT_TRUE(MapSdlEvent(resized, a));
    ASSERT_TRUE(MapSdlEvent(pixels, b));
    EXPECT_NE(std::get<WindowEvent>(a).kind, std::get<WindowEvent>(b).kind);
}

// --- keyboard and text (PLAT-34) --------------------------------------------------------------------

TEST(Sdl3EventMapperTests, KeyDownCarriesScancodeKeycodeModifiersAndRepeat)
{
    SDL_Event source = MakeEvent(SDL_EVENT_KEY_DOWN);
    source.key.windowID = 7;
    source.key.scancode = SDL_SCANCODE_A;
    source.key.key = SDLK_A;
    source.key.mod = SDL_KMOD_LSHIFT;
    source.key.down = true;
    source.key.repeat = true;

    PlatformEvent mapped;
    ASSERT_TRUE(MapSdlEvent(source, mapped));
    const auto& key = std::get<KeyEvent>(mapped);
    EXPECT_EQ(key.window, 7u);
    EXPECT_EQ(key.scancode, Scancode::A);
    EXPECT_EQ(key.keycode, KeyCode::A);
    EXPECT_EQ(key.modifiers, static_cast<std::uint16_t>(SDL_KMOD_LSHIFT));
    EXPECT_TRUE(key.pressed);
    // Repeat must survive the mapping: a platform without real key-release synthesises releases
    // from repeat timing, so losing this flag would break that path silently.
    EXPECT_TRUE(key.repeat);
}

TEST(Sdl3EventMapperTests, KeyUpIsNotAPress)
{
    SDL_Event source = MakeEvent(SDL_EVENT_KEY_UP);
    source.key.down = false;

    PlatformEvent mapped;
    ASSERT_TRUE(MapSdlEvent(source, mapped));
    EXPECT_FALSE(std::get<KeyEvent>(mapped).pressed);
}

TEST(Sdl3EventMapperTests, TextInputCarriesItsText)
{
    SDL_Event source = MakeEvent(SDL_EVENT_TEXT_INPUT);
    source.text.windowID = 3;
    source.text.text = "ř";  // multi-byte on purpose: the payload is UTF-8, not ASCII

    PlatformEvent mapped;
    ASSERT_TRUE(MapSdlEvent(source, mapped));
    const auto& text = std::get<TextInputEvent>(mapped);
    EXPECT_EQ(text.window, 3u);
    EXPECT_EQ(text.text, "ř");
}

TEST(Sdl3EventMapperTests, TextInputWithNullTextDoesNotCrash)
{
    // SDL's text pointer is not guaranteed non-null on every path, and dereferencing it would be
    // a crash in the event pump -- the worst possible place for one.
    SDL_Event source = MakeEvent(SDL_EVENT_TEXT_INPUT);
    source.text.text = nullptr;

    PlatformEvent mapped;
    ASSERT_TRUE(MapSdlEvent(source, mapped));
    EXPECT_TRUE(std::get<TextInputEvent>(mapped).text.empty());
}

TEST(Sdl3EventMapperTests, TextEditingCarriesCompositionCursorAndSelection)
{
    SDL_Event source = MakeEvent(SDL_EVENT_TEXT_EDITING);
    source.edit.windowID = 3;
    source.edit.text = "compo";
    source.edit.start = 2;
    source.edit.length = 3;

    PlatformEvent mapped;
    ASSERT_TRUE(MapSdlEvent(source, mapped));
    const auto& editing = std::get<TextEditingEvent>(mapped);
    EXPECT_EQ(editing.text, "compo");
    EXPECT_EQ(editing.cursor, 2);
    EXPECT_EQ(editing.selectionLength, 3);
}

TEST(Sdl3EventMapperTests, TextEditingCandidatesCarryOwnedUtf8ListAndDisplayState)
{
    char first[] = "候補";
    const char* candidates[] = {first, nullptr, "選択"};
    SDL_Event source = MakeEvent(SDL_EVENT_TEXT_EDITING_CANDIDATES);
    source.edit_candidates.windowID = 11;
    source.edit_candidates.candidates = candidates;
    source.edit_candidates.num_candidates = 3;
    source.edit_candidates.selected_candidate = 2;
    source.edit_candidates.horizontal = true;

    PlatformEvent mapped;
    ASSERT_TRUE(MapSdlEvent(source, mapped));
    first[0] = '\0'; // the mapped payload must not retain SDL-owned string storage

    const auto& editing = std::get<TextEditingCandidatesEvent>(mapped);
    EXPECT_EQ(editing.window, 11u);
    EXPECT_EQ(editing.candidates, (std::vector<std::string>{"候補", "", "選択"}));
    EXPECT_EQ(editing.selectedCandidate, 2);
    EXPECT_TRUE(editing.horizontal);
}

TEST(Sdl3EventMapperTests, TextEditingCandidatesAcceptAnEmptySdlList)
{
    SDL_Event source = MakeEvent(SDL_EVENT_TEXT_EDITING_CANDIDATES);
    source.edit_candidates.candidates = nullptr;
    source.edit_candidates.num_candidates = 5; // ignored when SDL supplies no array
    source.edit_candidates.selected_candidate = -1;

    PlatformEvent mapped;
    ASSERT_TRUE(MapSdlEvent(source, mapped));
    const auto& editing = std::get<TextEditingCandidatesEvent>(mapped);
    EXPECT_TRUE(editing.candidates.empty());
    EXPECT_EQ(editing.selectedCandidate, -1);
    EXPECT_FALSE(editing.horizontal);
}

// --- mouse and touch (PLAT-35) ------------------------------------------------------------------------

TEST(Sdl3EventMapperTests, MouseMotionCarriesPositionAndDelta)
{
    SDL_Event source = MakeEvent(SDL_EVENT_MOUSE_MOTION);
    source.motion.windowID = 1;
    source.motion.x = 12.5f;
    source.motion.y = 34.5f;
    source.motion.xrel = -2.0f;
    source.motion.yrel = 3.0f;

    PlatformEvent mapped;
    ASSERT_TRUE(MapSdlEvent(source, mapped));
    const auto& motion = std::get<MouseMotionEvent>(mapped);
    EXPECT_FLOAT_EQ(motion.x, 12.5f);
    EXPECT_FLOAT_EQ(motion.y, 34.5f);
    EXPECT_FLOAT_EQ(motion.deltaX, -2.0f);
    EXPECT_FLOAT_EQ(motion.deltaY, 3.0f);
}

TEST(Sdl3EventMapperTests, MouseButtonCarriesButtonPressStateAndClickCount)
{
    SDL_Event source = MakeEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
    source.button.button = SDL_BUTTON_RIGHT;
    source.button.down = true;
    source.button.clicks = 2;
    source.button.x = 5.0f;
    source.button.y = 6.0f;

    PlatformEvent mapped;
    ASSERT_TRUE(MapSdlEvent(source, mapped));
    const auto& button = std::get<MouseButtonEvent>(mapped);
    EXPECT_EQ(button.button, SDL_BUTTON_RIGHT);
    EXPECT_TRUE(button.pressed);
    EXPECT_EQ(button.clicks, 2);
}

TEST(Sdl3EventMapperTests, FlippedWheelIsNormalisedToOneConvention)
{
    // SDL reports the flag and leaves the sign alone. Normalising here means every consumer sees
    // one convention rather than each rediscovering SDL_MOUSEWHEEL_FLIPPED.
    SDL_Event normal = MakeEvent(SDL_EVENT_MOUSE_WHEEL);
    normal.wheel.y = 1.0f;
    normal.wheel.direction = SDL_MOUSEWHEEL_NORMAL;

    SDL_Event flipped = MakeEvent(SDL_EVENT_MOUSE_WHEEL);
    flipped.wheel.y = 1.0f;
    flipped.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;

    PlatformEvent a;
    PlatformEvent b;
    ASSERT_TRUE(MapSdlEvent(normal, a));
    ASSERT_TRUE(MapSdlEvent(flipped, b));
    EXPECT_FLOAT_EQ(std::get<MouseWheelEvent>(a).y, 1.0f);
    EXPECT_FLOAT_EQ(std::get<MouseWheelEvent>(b).y, -1.0f);
}

TEST(Sdl3EventMapperTests, TouchEventsMapToTheirKinds)
{
    const struct
    {
        SDL_EventType sdl;
        TouchEventKind expected;
    } cases[] = {
        {SDL_EVENT_FINGER_DOWN, TouchEventKind::Down},
        {SDL_EVENT_FINGER_UP, TouchEventKind::Up},
        {SDL_EVENT_FINGER_MOTION, TouchEventKind::Motion},
        {SDL_EVENT_FINGER_CANCELED, TouchEventKind::Cancelled},
    };

    for (const auto& testCase : cases)
    {
        SDL_Event source = MakeEvent(testCase.sdl);
        source.tfinger.fingerID = 9;
        source.tfinger.x = 0.25f;
        source.tfinger.y = 0.75f;
        source.tfinger.pressure = 0.5f;

        PlatformEvent mapped;
        ASSERT_TRUE(MapSdlEvent(source, mapped)) << ToString(testCase.expected);
        const auto& touch = std::get<TouchEvent>(mapped);
        EXPECT_EQ(touch.kind, testCase.expected);
        EXPECT_EQ(touch.fingerId, 9u);
        // Normalised coordinates must pass through unscaled; multiplying by a window size here
        // would double-apply once the consumer scales them too.
        EXPECT_FLOAT_EQ(touch.x, 0.25f);
        EXPECT_FLOAT_EQ(touch.y, 0.75f);
    }
}

// --- devices (PLAT-36) ---------------------------------------------------------------------------------

TEST(Sdl3EventMapperTests, DeviceAddAndRemoveMapToTheRightKindAndConnectedFlag)
{
    const struct
    {
        SDL_EventType sdl;
        InputDeviceKind kind;
        bool connected;
    } cases[] = {
        {SDL_EVENT_GAMEPAD_ADDED, InputDeviceKind::Gamepad, true},
        {SDL_EVENT_GAMEPAD_REMOVED, InputDeviceKind::Gamepad, false},
        {SDL_EVENT_JOYSTICK_ADDED, InputDeviceKind::Joystick, true},
        {SDL_EVENT_JOYSTICK_REMOVED, InputDeviceKind::Joystick, false},
        {SDL_EVENT_KEYBOARD_ADDED, InputDeviceKind::Keyboard, true},
        {SDL_EVENT_KEYBOARD_REMOVED, InputDeviceKind::Keyboard, false},
        {SDL_EVENT_MOUSE_ADDED, InputDeviceKind::Mouse, true},
        {SDL_EVENT_MOUSE_REMOVED, InputDeviceKind::Mouse, false},
    };

    for (const auto& testCase : cases)
    {
        SDL_Event source = MakeEvent(testCase.sdl);
        PlatformEvent mapped;
        ASSERT_TRUE(MapSdlEvent(source, mapped)) << ToString(testCase.kind);
        const auto& device = std::get<DeviceEvent>(mapped);
        EXPECT_EQ(device.kind, testCase.kind);
        EXPECT_EQ(device.connected, testCase.connected);
    }
}

TEST(Sdl3EventMapperTests, GamepadAxisIsNormalisedWithoutExceedingMinusOne)
{
    // SDL's range is [-32768, 32767]. Dividing the whole range by 32767 would make the extreme
    // negative read as -1.00003, which a clamp-free consumer would carry straight into physics.
    SDL_Event extremeNegative = MakeEvent(SDL_EVENT_GAMEPAD_AXIS_MOTION);
    extremeNegative.gaxis.value = -32768;

    SDL_Event extremePositive = MakeEvent(SDL_EVENT_GAMEPAD_AXIS_MOTION);
    extremePositive.gaxis.value = 32767;

    SDL_Event centred = MakeEvent(SDL_EVENT_GAMEPAD_AXIS_MOTION);
    centred.gaxis.value = 0;

    PlatformEvent a;
    PlatformEvent b;
    PlatformEvent c;
    ASSERT_TRUE(MapSdlEvent(extremeNegative, a));
    ASSERT_TRUE(MapSdlEvent(extremePositive, b));
    ASSERT_TRUE(MapSdlEvent(centred, c));

    EXPECT_FLOAT_EQ(std::get<ControllerAxisEvent>(a).value, -1.0f);
    EXPECT_FLOAT_EQ(std::get<ControllerAxisEvent>(b).value, 1.0f);
    EXPECT_FLOAT_EQ(std::get<ControllerAxisEvent>(c).value, 0.0f);
}

TEST(Sdl3EventMapperTests, GamepadButtonCarriesPressState)
{
    SDL_Event down = MakeEvent(SDL_EVENT_GAMEPAD_BUTTON_DOWN);
    down.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    down.gbutton.down = true;

    PlatformEvent mapped;
    ASSERT_TRUE(MapSdlEvent(down, mapped));
    const auto& button = std::get<ControllerButtonEvent>(mapped);
    EXPECT_EQ(button.button, SDL_GAMEPAD_BUTTON_SOUTH);
    EXPECT_TRUE(button.pressed);
}

// --- lifecycle and quit (PLAT-37) ---------------------------------------------------------------------

TEST(Sdl3EventMapperTests, QuitMaps)
{
    SDL_Event source = MakeEvent(SDL_EVENT_QUIT);
    PlatformEvent mapped;
    ASSERT_TRUE(MapSdlEvent(source, mapped));
    EXPECT_EQ(GetEventTypeName(mapped), "QuitEvent");
}

TEST(Sdl3EventMapperTests, ApplicationLifecycleTransitionsMap)
{
    const struct
    {
        SDL_EventType sdl;
        AppLifecycleKind expected;
    } cases[] = {
        {SDL_EVENT_WILL_ENTER_BACKGROUND, AppLifecycleKind::WillEnterBackground},
        {SDL_EVENT_DID_ENTER_FOREGROUND, AppLifecycleKind::DidEnterForeground},
        {SDL_EVENT_LOW_MEMORY, AppLifecycleKind::LowMemory},
    };

    for (const auto& testCase : cases)
    {
        SDL_Event source = MakeEvent(testCase.sdl);
        PlatformEvent mapped;
        ASSERT_TRUE(MapSdlEvent(source, mapped)) << ToString(testCase.expected);
        EXPECT_EQ(std::get<AppLifecycleEvent>(mapped).kind, testCase.expected);
    }
}

// --- events CNA does not consume -----------------------------------------------------------------------

TEST(Sdl3EventMapperTests, UnconsumedSdlEventsAreReportedAsUnmappedNotAsEmptyEvents)
{
    // SDL emits far more than CNA uses. Returning false is what stops PollEvents appending a
    // default-constructed event -- which would look to a caller like a genuine QuitEvent, since
    // that is the variant's first alternative.
    SDL_Event source = MakeEvent(SDL_EVENT_CLIPBOARD_UPDATE);
    PlatformEvent mapped;
    EXPECT_FALSE(MapSdlEvent(source, mapped));
}

TEST(Sdl3EventMapperTests, AnUnmappedEventLeavesTheDestinationUntouched)
{
    PlatformEvent mapped = WindowEvent{};
    SDL_Event source = MakeEvent(SDL_EVENT_CLIPBOARD_UPDATE);
    EXPECT_FALSE(MapSdlEvent(source, mapped));
    EXPECT_EQ(GetEventTypeName(mapped), "WindowEvent") << "destination must not be overwritten";
}

// --- scancode translation (PLAT-78a) ----------------------------------------------------------

TEST(Sdl3EventMapperTests, EverySdlScancodeCnaNamesRoundTripsThroughTheContract)
{
    // Both numberings are HID usage IDs, so the translation is a validated cast rather than a
    // table. That makes it cheap and makes a silent mismatch invisible -- this walks SDL's entire
    // scancode range and checks that anything CNA recognises comes back as the same SDL value.
    int recognised = 0;
    for (int raw = 0; raw < SDL_SCANCODE_COUNT; ++raw)
    {
        const auto sdl = static_cast<SDL_Scancode>(raw);
        const Scancode mapped = CNA::Platform::Sdl3::ToScancode(sdl);
        if (mapped == Scancode::Unknown && sdl != SDL_SCANCODE_UNKNOWN)
        {
            continue;  // a key CNA does not name; refusing it is the correct answer
        }
        EXPECT_EQ(CNA::Platform::Sdl3::ToSdlScancode(mapped), sdl) << "raw " << raw;
        ++recognised;
    }

    // A translation that recognised nothing would pass every assertion above.
    EXPECT_GE(recognised, 120);
}

TEST(Sdl3EventMapperTests, AnSdlScancodeCnaDoesNotNameBecomesUnknown)
{
    // SDL's space runs past the HID keyboard page with its own media and system keys. Letting one
    // through as a raw cast would produce a Scancode no switch handles and ToString cannot name.
    EXPECT_EQ(CNA::Platform::Sdl3::ToScancode(SDL_SCANCODE_UNKNOWN), Scancode::Unknown);
    EXPECT_EQ(CNA::Platform::Sdl3::ToScancode(SDL_SCANCODE_CUT), Scancode::Unknown);
    EXPECT_EQ(CNA::Platform::Sdl3::ToScancode(SDL_SCANCODE_MEDIA_EJECT), Scancode::Unknown);
}

TEST(Sdl3EventMapperTests, TheLettersAndModifiersTranslateToTheKeysTheyName)
{
    // The cases a reader would want spot-checked by name rather than by round-trip.
    EXPECT_EQ(CNA::Platform::Sdl3::ToScancode(SDL_SCANCODE_A), Scancode::A);
    EXPECT_EQ(CNA::Platform::Sdl3::ToScancode(SDL_SCANCODE_W), Scancode::W);
    EXPECT_EQ(CNA::Platform::Sdl3::ToScancode(SDL_SCANCODE_LSHIFT), Scancode::LeftShift);
    EXPECT_EQ(CNA::Platform::Sdl3::ToScancode(SDL_SCANCODE_RGUI), Scancode::RightGui);
    EXPECT_EQ(CNA::Platform::Sdl3::ToScancode(SDL_SCANCODE_KP_5), Scancode::Keypad5);
    EXPECT_EQ(CNA::Platform::Sdl3::ToScancode(SDL_SCANCODE_SLEEP), Scancode::Sleep);
}

} // namespace
