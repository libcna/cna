// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include "CNA/Devices/DisplayInfo.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"

using CNA::Devices::DisplayInfo;
using Microsoft::Xna::Framework::GameWindow;
using Microsoft::Xna::Framework::Rectangle;

// CnaTests is a plain console/gtest binary, not a running Game, so most tests here
// exercise the "no underlying SDL window" path via a default-constructed GameWindow().
// One test (below) follows GameWindowTest.SetAndGetTitle_UsingSdlWindow's existing
// precedent (tests/Microsoft/Xna/Framework/GameWindowTests.cpp) and creates a real,
// hidden SDL window to exercise the actual SDL3 query path -- gracefully GTEST_SKIP()s
// if this container has no usable video subsystem, rather than failing or silently
// passing on an unexercised path.

TEST(DisplayInfoTests, GetContentScalePropertyReturnsZeroForWindowWithNoSdlWindow)
{
    const GameWindow window;
    EXPECT_EQ(DisplayInfo::getContentScaleProperty(window), 0.0f);
}

TEST(DisplayInfoTests, GetSafeAreaPropertyReturnsEmptyForWindowWithNoSdlWindow)
{
    const GameWindow window;
    EXPECT_EQ(DisplayInfo::getSafeAreaProperty(window), Rectangle::Empty);
}

TEST(DisplayInfoTests, RepeatedCallsDoNotCrash)
{
    const GameWindow window;
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW({
            (void)DisplayInfo::getContentScaleProperty(window);
            (void)DisplayInfo::getSafeAreaProperty(window);
        });
    }
}

TEST(DisplayInfoTests, QueriesAgainstARealSdlWindowDoNotCrashAndReturnDocumentedValues)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Window* nativeWindow = SDL_CreateWindow("display-info-test", 64, 64, SDL_WINDOW_HIDDEN);
    if (!nativeWindow)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateWindow failed: " << SDL_GetError();
    }

    const GameWindow window(nativeWindow);

    // A real (if hidden/headless) window should report a genuine, positive content
    // scale -- 0.0f would mean SDL itself considers the query to have failed, which
    // should not happen for a window it just successfully created.
    const float contentScale = DisplayInfo::getContentScaleProperty(window);
    EXPECT_GT(contentScale, 0.0f);

    // The safe area is expected to be a valid sub-rectangle of the window; not
    // asserting exact bounds (platform/window-manager dependent), only that the
    // query itself succeeds and returns a non-degenerate rectangle for a real window.
    const Rectangle safeArea = DisplayInfo::getSafeAreaProperty(window);
    EXPECT_GT(safeArea.Width, 0);
    EXPECT_GT(safeArea.Height, 0);

    SDL_DestroyWindow(nativeWindow);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

#endif // CNA_DEVICES
