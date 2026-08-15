// SPDX-License-Identifier: MS-PL
//
// Task LLGL-56: LlglSdlSurface only ever expresses a window as an LLGL native handle on X11 (LLGL
// 0.04b compiles Wayland support in only when explicitly enabled, and this project's integration
// does not enable it). The constructor already rejects any other SDL video driver with an
// actionable message ("run with SDL_VIDEODRIVER=x11"), but that rejection path had no test
// coverage at all -- this covers it using SDL's own "dummy" video driver, which creates a real
// SDL_Window with no X11 handles at all without needing an actual non-X11 display.
#include <gtest/gtest.h>

// plan_runtimerenderer.md RTR-P9-9: PRESENT_, not the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running
// whenever that renderer is COMPILED IN -- in a multi-renderer build it need not be the
// selected one. Only the default renderer's CNA_RENDERER_LLGL is defined project-wide.
#if defined(CNA_RENDERER_LLGL) || defined(CNA_RENDERER_PRESENT_LLGL)
#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

#include "CNA/Internal/Renderers/Llgl/LlglSdlSurface.hpp"

using CNA::Internal::Renderers::Llgl::LlglSdlSurface;

TEST(LlglSdlSurfaceConstructor, NonX11DriverIsRejectedWithActionableMessage)
{
    // SDL selects its video driver once per SDL_InitSubSystem(SDL_INIT_VIDEO)/QuitSubSystem
    // cycle, so the override below only takes effect if no other test in this same process has an
    // outstanding video-subsystem reference already pinning a different driver -- gtest_discover_tests
    // runs each TEST() as its own process under ctest, so this is normally moot, but save and
    // restore the hint anyway so a direct, all-tests-in-one-process CnaTests invocation stays safe.
    const std::string previousDriverHint = SDL_GetHint(SDL_HINT_VIDEO_DRIVER) != nullptr
        ? SDL_GetHint(SDL_HINT_VIDEO_DRIVER) : "";
    // Normal-priority SDL_SetHint() does not override a hint already pinned by the SDL_VIDEODRIVER
    // environment variable (this project's own ctest registrations set it to "x11" for every LLGL
    // test) -- SDL_HINT_OVERRIDE is required to actually force "dummy" here.
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE);

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    const std::string driverInUse = SDL_GetCurrentVideoDriver() != nullptr ? SDL_GetCurrentVideoDriver() : "";
    if (driverInUse != "dummy")
    {
        // A prior test in this process already pinned a different driver for the whole
        // SDL_INIT_VIDEO lifetime (see comment above) -- nothing this test can do about that here.
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "could not select the dummy SDL video driver (currently: " << driverInUse << ")";
    }

    SDL_Window* window = SDL_CreateWindow("LlglSdlSurfaceTests", 64, 64, SDL_WINDOW_HIDDEN);
    ASSERT_NE(window, nullptr) << "SDL_CreateWindow failed: " << SDL_GetError();

    bool threw = false;
    std::string what;
    try
    {
        LlglSdlSurface surface(window);
    }
    catch (const std::runtime_error& ex)
    {
        threw = true;
        what = ex.what();
    }
    EXPECT_TRUE(threw) << "LlglSdlSurface must reject a window with no X11 handles";
    EXPECT_NE(what.find("SDL_VIDEODRIVER=x11"), std::string::npos)
        << "rejection message must tell the caller how to fix it, got: " << what;

    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, previousDriverHint.c_str(), SDL_HINT_OVERRIDE);
}
#endif
