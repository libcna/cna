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

TEST(GameWindowTest, NullWindow_EndScreenDeviceChangeOneArgIsSafe)
{
    GameWindow window;
    EXPECT_NO_THROW(window.EndScreenDeviceChange("test"));
}
