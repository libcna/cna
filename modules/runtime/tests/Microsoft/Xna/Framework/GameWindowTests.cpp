// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"

using namespace Microsoft::Xna::Framework;

TEST(GameWindowTest, SetAndGetTitle_UsingSdlWindow)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Window* nativeWindow = SDL_CreateWindow("initial-title", 64, 64, SDL_WINDOW_HIDDEN);
    if (!nativeWindow)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateWindow failed: " << SDL_GetError();
    }

    GameWindow window(nativeWindow);

    window.setTitleProperty("new-title");
    EXPECT_EQ(window.getTitleProperty(), "new-title");

    window.setTitleProperty("");
    EXPECT_EQ(window.getTitleProperty(), "");

    SDL_DestroyWindow(nativeWindow);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(GameWindowTest, NullWindow_IsSafeAndReturnsEmptyTitle)
{
    GameWindow window;

    EXPECT_EQ(window.getTitleProperty(), "");
    EXPECT_NO_THROW(window.setTitleProperty("ignored"));
}

TEST(GameTest, ExposesWindowProperty)
{
    using WindowGetterReturnType = decltype(std::declval<Game&>().getWindowProperty());
    EXPECT_TRUE((std::is_same_v<WindowGetterReturnType, GameWindow&>));
}

TEST(GameWindowTest, NullWindow_DefaultOrientationIsDefault)
{
    GameWindow window;
    EXPECT_EQ(window.getCurrentOrientationProperty(), DisplayOrientation::Default);
}

TEST(GameWindowTest, NullWindow_HandleIsNullptr)
{
    GameWindow window;
    EXPECT_EQ(window.getHandleProperty(), 0);
}

TEST(GameWindowTest, NullWindow_ScreenDeviceNameIsEmpty)
{
    GameWindow window;
    EXPECT_EQ(window.getScreenDeviceNameProperty(), "");
}

TEST(GameWindowTest, NullWindow_AllowUserResizingDefaultFalse)
{
    GameWindow window;
    EXPECT_FALSE(window.getAllowUserResizingProperty());
}

TEST(GameWindowTest, NullWindow_SetAllowUserResizingCaches)
{
    GameWindow window;
    window.setAllowUserResizingProperty(true);
    EXPECT_TRUE(window.getAllowUserResizingProperty());
}

TEST(GameWindowTest, NullWindow_IsBorderlessDefaultFalse)
{
    GameWindow window;
    EXPECT_FALSE(window.getIsBorderlessEXTProperty());
}

TEST(GameWindowTest, NullWindow_SetBorderlessCaches)
{
    GameWindow window;
    window.setIsBorderlessEXTProperty(true);
    EXPECT_TRUE(window.getIsBorderlessEXTProperty());
}

TEST(GameWindowTest, NullWindow_ClientSizeChangedEventFires)
{
    GameWindow window;
    int fired = 0;
    window.ClientSizeChanged += [&](System::Object*, const System::EventArgs&) { ++fired; };
    // EndScreenDeviceChange on null window: bounds stay (0,0,0,0) — no size change fires.
    // BeginScreenDeviceChange + EndScreenDeviceChange on null window is safe.
    window.BeginScreenDeviceChange(false);
    window.EndScreenDeviceChange("test", 0, 0);
    EXPECT_EQ(fired, 0);
}

TEST(GameWindowTest, NullWindow_MinimizeEXTIsSafe)
{
    GameWindow window;
    EXPECT_NO_THROW(window.MinimizeEXT());
}

TEST(GameWindowTest, NullWindow_RestoreEXTIsSafe)
{
    GameWindow window;
    EXPECT_NO_THROW(window.RestoreEXT());
}

TEST(GameWindowTest, MinimizeAndRestoreEXT_UsingSdlWindow)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Window* nativeWindow = SDL_CreateWindow("minimize-restore-test", 64, 64, SDL_WINDOW_HIDDEN);
    if (!nativeWindow)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateWindow failed: " << SDL_GetError();
    }

    GameWindow window(nativeWindow);

    EXPECT_NO_THROW(window.MinimizeEXT());
    EXPECT_NO_THROW(window.RestoreEXT());

    SDL_DestroyWindow(nativeWindow);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(GameWindowTest, NullWindow_EndScreenDeviceChangeOneArgIsSafe)
{
    GameWindow window;
    EXPECT_NO_THROW(window.EndScreenDeviceChange("test"));
}

TEST(GameWindowPlatformTest, DelegatesStateAndGeometryToTheSelectedPlatformWindow)
{
#if defined(CNA_PLATFORM_SDL3)
    GTEST_SKIP() << "the HEADLESS renderer intentionally creates no SDL platform window";
#else
    Game game;
    GameWindow& window = game.getWindowProperty();

    EXPECT_EQ(window.getTitleProperty(), "Game");
    EXPECT_TRUE(window.getAllowUserResizingProperty());
    EXPECT_EQ(window.getClientBoundsProperty(), Rectangle(0, 0, 800, 480));

    window.setTitleProperty("platform-window");
    EXPECT_EQ(window.getTitleProperty(), "platform-window");
    window.setAllowUserResizingProperty(false);
    EXPECT_FALSE(window.getAllowUserResizingProperty());
    window.setIsBorderlessEXTProperty(true);
    EXPECT_TRUE(window.getIsBorderlessEXTProperty());

    window.BeginScreenDeviceChange(false);
    window.EndScreenDeviceChange("virtual-display", 320, 240);
    EXPECT_EQ(window.getClientBoundsProperty(), Rectangle(0, 0, 320, 240));
    EXPECT_NO_THROW(window.MinimizeEXT());
    EXPECT_NO_THROW(window.RestoreEXT());
#endif
}
