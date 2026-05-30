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
