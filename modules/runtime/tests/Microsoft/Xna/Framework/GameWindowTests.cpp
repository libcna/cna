// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <string>

#include "CNA/Platform.hpp"
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

// plan_apple.md APPLE-15: SupportedOrientations is CNA-internal bookkeeping on a desktop, and
// the SDL_HINT_ORIENTATIONS channel into the operating system on iOS/Android. The platform
// gating is what these two cases pin down -- a desktop build must not start declaring
// orientations to SDL, and a mobile build must declare exactly the requested set.
namespace
{
    // SetSupportedOrientations is protected (XNA declares it protected internal on GameWindow);
    // GraphicsDeviceManager reaches it as a friend, a test reaches it by deriving.
    class TestableGameWindow : public GameWindow
    {
    public:
        using GameWindow::GameWindow;
        using GameWindow::SetSupportedOrientations;
    };
}

TEST(GameWindowTest, SetSupportedOrientations_LeavesTheDefaultOrientationAlone)
{
    // DisplayOrientation::Default means "no orientation asserted" and counts as supported
    // whatever the supported set is, so narrowing the set does not force a concrete orientation
    // onto a window that never had one.
    TestableGameWindow window;

    window.SetSupportedOrientations(DisplayOrientation::Portrait);
    EXPECT_EQ(DisplayOrientation::Default, window.getCurrentOrientationProperty());
}

TEST(GameWindowTest, SetSupportedOrientations_FallsBackWhenTheCurrentOneStopsBeingSupported)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    // A concrete current orientation only comes from real window bounds, so this case needs a
    // window: 128x64 is landscape, which is in GameWindow's default supported set.
    SDL_Window* nativeWindow = SDL_CreateWindow("orientation", 128, 64, SDL_WINDOW_HIDDEN);
    if (!nativeWindow)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateWindow failed: " << SDL_GetError();
    }

    TestableGameWindow window(nativeWindow);
    ASSERT_EQ(DisplayOrientation::LandscapeLeft, window.getCurrentOrientationProperty());

    window.SetSupportedOrientations(DisplayOrientation::Portrait);
    EXPECT_EQ(DisplayOrientation::Portrait, window.getCurrentOrientationProperty());

    SDL_DestroyWindow(nativeWindow);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(GameWindowTest, SetSupportedOrientations_OrientationHintIsMobileOnly)
{
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "");

    TestableGameWindow window;
    window.SetSupportedOrientations(
        DisplayOrientation::Portrait | DisplayOrientation::LandscapeLeft);

    const char* hint = SDL_GetHint(SDL_HINT_ORIENTATIONS);
    const std::string value = hint != nullptr ? hint : "";

    if (CNA::isMobilePlatform())
    {
        EXPECT_EQ("LandscapeLeft Portrait ", value);
    }
    else
    {
        EXPECT_TRUE(value.empty());
    }
}
