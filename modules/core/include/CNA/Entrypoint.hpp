#pragma once

/**
 * @file CNA/Entrypoint.hpp
 * @brief Include this header in the translation unit that defines main() (i.e. Program.cpp).
 *
 * On Android/SDL builds, SDL_main.h renames main() to SDL_main() via a preprocessor
 * macro so that SDL's Java bridge (SDLActivity.nativeRunMain) can locate and call it.
 * Without this rename the app exits immediately because the "SDL_main" symbol is never
 * exported from the shared library.
 *
 * iOS needs the same rename for the equivalent reason on the other side of the fence: UIKit,
 * not the game, owns the process. SDL_main.h renames the game's main() out of the way and
 * supplies its own, which calls SDL_RunApp/UIApplicationMain to start the UIKit application and
 * only then calls SDL_main() from inside a running application. A game whose main() is not
 * renamed never gets a UIApplication, so it has no event loop, no view controller and no window.
 *
 * On all other platforms (Windows, Linux, macOS, Emscripten) SDL_main.h is effectively
 * a no-op — it either provides an identity macro or is not included at all — so including
 * this header is safe and portable everywhere.
 *
 * Game code must include this header instead of \<SDL3/SDL_main.h\> directly so that the
 * SDL dependency stays hidden behind the CNA platform layer.
 */

#include "CNA/Platform.hpp"

#if defined(SDL_PLATFORM_ANDROID) || defined(__ANDROID__)
#  include <SDL3/SDL_main.h>
#elif defined(SDL_PLATFORM_IOS) || defined(CNA_PLATFORM_IOS)
#  include <SDL3/SDL_main.h>
#elif defined(CNA_RENDERER_SDL) || defined(SDL_h_)
// Non-Android SDL builds: include SDL_main.h for completeness; it is a no-op there.
#  include <SDL3/SDL_main.h>
#endif
