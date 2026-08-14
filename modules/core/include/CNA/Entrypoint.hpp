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
 * On all other platforms (Windows, Linux, macOS, Emscripten) SDL_main.h is effectively
 * a no-op — it either provides an identity macro or is not included at all — so including
 * this header is safe and portable everywhere.
 *
 * Game code must include this header instead of \<SDL3/SDL_main.h\> directly so that the
 * SDL dependency stays hidden behind the CNA platform layer.
 */

#if defined(SDL_PLATFORM_ANDROID) || defined(__ANDROID__)
#  include <SDL3/SDL_main.h>
#elif defined(SDL_h_)
// Non-Android SDL builds: include SDL_main.h for completeness; it is a no-op there.
//
// plan_runtimerenderer.md RTR-P12-12: this used to also test CNA_RENDERER_SDL, which is not and
// never was one of the public renderer identities (the SDL_Renderer one is CNA_RENDERER_SDL_RENDERER).
// The macro could never be defined, so the condition was dead.
#  include <SDL3/SDL_main.h>
#endif
