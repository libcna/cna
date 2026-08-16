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
 * iOS needs the rename for the equivalent reason on the other side of the fence: UIKit, not the
 * game, owns the process. The platform library renames the game's `main()` out of the way and
 * supplies its own, which starts the UIKit application and only then calls back into the game's
 * entry point from inside a running application. A game whose `main()` is not renamed never gets
 * a UIApplication, so it has no event loop, no view controller and no window.
 *
 * ### What changed when it moved here
 *
 * This header used to live in `modules/core` and key off renderer macros. Two defects came with
 * that: its non-Android branch tested `CNA_RENDERER_SDL`, a macro **defined nowhere in the
 * repository**, so it was dead; and the renderer is the wrong axis entirely now that renderer and
 * platform are separate build choices. It keys off `CNA_PLATFORM_*` instead, and lives in the
 * platform module so `modules/core` no longer includes an SDL header at all.
 *
 * Under a platform that manages no entry point — `HEADLESS` and `TERMINAL` — this header is
 * empty, which is the correct answer rather than an omission. Both report the
 * `managedEntrypoint` capability as false, so the emptiness is stated in the contract too and not
 * merely implied by this file compiling to nothing.
 */

#include "CNA/TargetPlatform.hpp"

#if defined(CNA_PLATFORM_SDL3)
// The two cases where the rename is load-bearing. Android: without it the app starts and exits
// immediately, because the entry-point symbol the Java bridge looks up is never exported. iOS:
// without it the process never hands control to UIKit, so the app has no application object.
#  if defined(SDL_PLATFORM_ANDROID) || defined(__ANDROID__) || defined(CNA_TARGET_IOS)
#    include <SDL3/SDL_main.h>
#  endif
#endif
