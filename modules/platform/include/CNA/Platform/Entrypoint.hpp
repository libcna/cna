// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Platform/Entrypoint.hpp
 * @brief Include this header in the translation unit that defines `main()`.
 *
 * ### Why this cannot be an IPlatform method
 *
 * On Android, SDL's Java bridge (`SDLActivity.nativeRunMain`) locates the game's entry point by
 * `dlsym()`-ing a symbol literally named `SDL_main`. The rename that produces that symbol happens
 * at **preprocessing** time, in the translation unit that defines `main()` — before any platform
 * object exists and before any runtime selection could occur. So this stays a header plus
 * build-system concern; see docs/platform-entrypoint-audit.md.
 *
 * ### What changed when it moved here
 *
 * This header used to live in `modules/core` and key off renderer macros. Two defects came with
 * that: its non-Android branch tested `CNA_RENDERER_SDL`, a macro **defined nowhere in the
 * repository**, so it was dead; and the renderer is the wrong axis entirely now that renderer and
 * platform are separate build choices. It keys off `CNA_PLATFORM_*` instead, and lives in the
 * platform module so `modules/core` no longer includes an SDL header at all.
 *
 * Under a platform that manages no entry point — `HEADLESS`, and a future `TERMINAL` — this
 * header is empty, which is the correct answer rather than an omission.
 */

#if defined(CNA_PLATFORM_SDL3)
#  if defined(SDL_PLATFORM_ANDROID) || defined(__ANDROID__)
// The only case where the rename is load-bearing: without it the app starts and exits
// immediately, because the SDL_main symbol Android looks for is never exported.
#    include <SDL3/SDL_main.h>
#  endif
#endif
