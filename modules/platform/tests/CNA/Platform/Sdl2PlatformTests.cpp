// SPDX-License-Identifier: MS-PL
//
// The selected SDL2 backend is intentionally tested through the public factory and event
// contract.  SDL_PushEvent is application-style input injection, not a backdoor into the
// implementation: it proves the native SDL2 queue reaches CNA's platform-neutral values.

#include "CNA/Platform/PlatformFactory.hpp"

#include <SDL.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

using namespace CNA::Platform;

std::unique_ptr<IPlatform> MakeSdl2()
{
    const std::vector<std::string> available = PlatformFactory::GetAvailable();
    if (std::find(available.begin(), available.end(), "SDL2") == available.end())
    {
        return nullptr;
    }
    return PlatformFactory::Create("SDL2");
}

TEST(Sdl2PlatformTests, FactoryAndCapabilitiesDescribeTheImplementedBackend)
{
    std::unique_ptr<IPlatform> platform = MakeSdl2();
    ASSERT_NE(platform, nullptr);
    EXPECT_EQ(platform->GetName(), "SDL2");
    EXPECT_EQ(PlatformFactory::GetDefaultName(), "SDL2");

    const PlatformCapabilities capabilities = platform->GetCapabilities();
    EXPECT_TRUE(capabilities.openGlContext);
    EXPECT_TRUE(capabilities.exactKeyboardState);
    EXPECT_NE(platform->GetKeyboard(), nullptr);
    EXPECT_EQ(platform->GetMouse(), nullptr);
    EXPECT_EQ(platform->GetClipboard(), nullptr);
    EXPECT_EQ(platform->GetVulkanSurface(), nullptr);
}

TEST(Sdl2PlatformTests, NativeQueueEventsBecomePlatformEvents)
{
    std::unique_ptr<IPlatform> platform = MakeSdl2();
    ASSERT_NE(platform, nullptr);
    ASSERT_NO_THROW(platform->AcquireSubsystem(PlatformSubsystem::Video));

    SDL_Event key{};
    key.type = SDL_KEYDOWN;
    key.key.windowID = 41;
    key.key.keysym.scancode = SDL_SCANCODE_A;
    key.key.keysym.sym = SDLK_a;
    key.key.keysym.mod = KMOD_CTRL;
    ASSERT_EQ(SDL_PushEvent(&key), 1);

    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.windowID = 41;
    motion.motion.x = 18;
    motion.motion.y = 27;
    motion.motion.xrel = 3;
    motion.motion.yrel = -2;
    ASSERT_EQ(SDL_PushEvent(&motion), 1);

    SDL_Event close{};
    close.type = SDL_WINDOWEVENT;
    close.window.windowID = 41;
    close.window.event = SDL_WINDOWEVENT_CLOSE;
    ASSERT_EQ(SDL_PushEvent(&close), 1);

    std::vector<PlatformEvent> events;
    platform->PollEvents(events);
    ASSERT_EQ(events.size(), 3u);

    const auto* translatedKey = std::get_if<KeyEvent>(&events[0]);
    ASSERT_NE(translatedKey, nullptr);
    EXPECT_EQ(translatedKey->window, 41u);
    EXPECT_EQ(translatedKey->scancode, Scancode::A);
    EXPECT_EQ(translatedKey->keycode, KeyCode::A);
    EXPECT_TRUE(translatedKey->pressed);
    EXPECT_TRUE(HasModifier(translatedKey->modifiers, KeyModifier::Control));

    const auto* translatedMotion = std::get_if<MouseMotionEvent>(&events[1]);
    ASSERT_NE(translatedMotion, nullptr);
    EXPECT_EQ(translatedMotion->x, 18.0f);
    EXPECT_EQ(translatedMotion->deltaY, -2.0f);

    const auto* translatedClose = std::get_if<WindowEvent>(&events[2]);
    ASSERT_NE(translatedClose, nullptr);
    EXPECT_EQ(translatedClose->kind, WindowEventKind::CloseRequested);
    EXPECT_EQ(translatedClose->window, 41u);
}

// PLAT-SDL2-3 regression. The keycode translation used one `SDLK_F1 .. SDLK_F24` range to map the
// function keys, but SDL2 leaves a gap between F12 (scancode 69) and F13 (scancode 104) and fills
// it with the navigation, editing and keypad keys. Every one of them therefore matched the F-key
// arm and came out as a function key -- SDLK_LEFT translated to 134, i.e. F23 -- and the mapping
// never reached the switch that handles them. The original suite could not see it: it injected one
// letter key and a mouse event.
//
// Driven through the public keyboard service rather than the internal helper, so it pins the
// contract a game observes and not an implementation detail. Every case below has a scancode
// numerically inside the old bad range, which is exactly what made them wrong.
TEST(Sdl2PlatformTests, KeysBetweenF12AndF13AreNotTranslatedAsFunctionKeys)
{
    std::unique_ptr<IPlatform> platform = MakeSdl2();
    ASSERT_NE(platform, nullptr);
    IPlatformKeyboard* keyboard = platform->GetKeyboard();
    ASSERT_NE(keyboard, nullptr);

    const struct { Scancode scancode; KeyCode expected; const char* what; } cases[] = {
        {Scancode::Left,       KeyCode::Left,      "Left"},
        {Scancode::Right,      KeyCode::Right,     "Right"},
        {Scancode::Up,         KeyCode::Up,        "Up"},
        {Scancode::Down,       KeyCode::Down,      "Down"},
        {Scancode::Home,       KeyCode::Home,      "Home"},
        {Scancode::End,        KeyCode::End,       "End"},
        {Scancode::PageUp,     KeyCode::PageUp,    "PageUp"},
        {Scancode::PageDown,   KeyCode::PageDown,  "PageDown"},
        {Scancode::Insert,     KeyCode::Insert,    "Insert"},
        {Scancode::Delete,     KeyCode::Delete,    "Delete"},
        {Scancode::CapsLock,   KeyCode::CapsLock,  "CapsLock"},
        {Scancode::Keypad1,    KeyCode::NumPad1,   "Keypad1"},
        {Scancode::KeypadPlus, KeyCode::Add,       "KeypadPlus"},
    };

    for (const auto& item : cases)
    {
        EXPECT_EQ(keyboard->GetKeyFromScancode(item.scancode), item.expected)
            << item.what << " must not translate as a function key";
    }
}

// The other half of the same defect: the two function-key ranges are contiguous in SDL2's scancodes
// but NOT in Windows virtual-key codes, which restart at 0x7C for F13. Arithmetic from F1 gave F13
// the value 0x9E instead of 0x7C.
TEST(Sdl2PlatformTests, BothFunctionKeyRangesTranslateToTheirVirtualKeyCodes)
{
    std::unique_ptr<IPlatform> platform = MakeSdl2();
    ASSERT_NE(platform, nullptr);
    IPlatformKeyboard* keyboard = platform->GetKeyboard();
    ASSERT_NE(keyboard, nullptr);

    EXPECT_EQ(keyboard->GetKeyFromScancode(Scancode::F1), KeyCode::F1);
    EXPECT_EQ(keyboard->GetKeyFromScancode(Scancode::F12), KeyCode::F12);
    EXPECT_EQ(keyboard->GetKeyFromScancode(Scancode::F13), KeyCode::F13);
    EXPECT_EQ(keyboard->GetKeyFromScancode(Scancode::F24), KeyCode::F24);

    // Values, not just enum identity: F13 is 0x7C and must not be 0x9E, the answer the F1-relative
    // arithmetic produced.
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::F13), 0x7Cu);
    EXPECT_EQ(static_cast<std::uint16_t>(keyboard->GetKeyFromScancode(Scancode::F13)), 0x7Cu);
}

// Letters and digits use SDL2's ASCII keycodes, which really are contiguous -- the ranges the fix
// deliberately left alone. Pinned so a later attempt to "simplify" the mapping cannot quietly break
// the cases that were always correct.
TEST(Sdl2PlatformTests, AsciiLetterAndDigitRangesStillTranslate)
{
    std::unique_ptr<IPlatform> platform = MakeSdl2();
    ASSERT_NE(platform, nullptr);
    IPlatformKeyboard* keyboard = platform->GetKeyboard();
    ASSERT_NE(keyboard, nullptr);

    EXPECT_EQ(keyboard->GetKeyFromScancode(Scancode::A), KeyCode::A);
    EXPECT_EQ(keyboard->GetKeyFromScancode(Scancode::Z), KeyCode::Z);
    EXPECT_EQ(keyboard->GetKeyFromScancode(Scancode::D0), KeyCode::D0);
    EXPECT_EQ(keyboard->GetKeyFromScancode(Scancode::D9), KeyCode::D9);
}

} // namespace
