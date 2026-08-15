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

} // namespace
