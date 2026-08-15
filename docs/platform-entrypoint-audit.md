# Entrypoint / `SDL_main` audit (PLAT-5)

> Audit of how CNA applications get a `main()`, and what of that the platform contract must own
> versus what stays a build-system and preprocessor concern. `plan_platform.md` PLAT-54 acts on
> this.
>
> Hand-written. Re-derive with:
> `grep -rn 'SDL_main\|SDL3main\|Entrypoint.hpp' --include=*.cpp --include=*.hpp --include=*.cmake .`

## Conclusion first

**Entrypoint handling cannot become an `IPlatform` method, and must not try to be one.** The
Android rename happens at *preprocessing* time in the translation unit that defines `main()`,
before any platform object exists and before any runtime selection could occur. It stays a
header-plus-build-system concern.

What *should* change is which condition drives it: today it keys off SDL and renderer macros;
after the split it must key off `CNA_PLATFORM`. `CNA/Entrypoint.hpp` is already the right seam —
it just needs to become real.

## The mechanism

On Android, SDL's Java bridge (`SDLActivity.nativeRunMain`) locates the game's entry point by
`dlsym()`-ing a symbol literally named `SDL_main`. `<SDL3/SDL_main.h>` renames `main` to
`SDL_main` via a preprocessor macro so that symbol exists. Without the rename the app starts and
exits immediately, because nothing is ever called.

On every other target SDL3's `SDL_main.h` is effectively a no-op for CNA's purposes.

## Current state

`modules/core/include/CNA/Entrypoint.hpp` already states exactly the right intent in its own doc
comment:

> "Game code must include this header instead of `<SDL3/SDL_main.h>` directly so that the SDL
> dependency stays hidden behind the CNA platform layer."

The design is correct. The implementation has three defects, all found by this audit.

### Finding 1 — the non-Android branch is dead code

```cpp
#if defined(SDL_PLATFORM_ANDROID) || defined(__ANDROID__)
#  include <SDL3/SDL_main.h>
#elif defined(CNA_RENDERER_SDL) || defined(SDL_h_)
#  include <SDL3/SDL_main.h>
#endif
```

**`CNA_RENDERER_SDL` is never defined anywhere in the repository.** Its only occurrence is this
`#elif`. The real compile definitions are `CNA_RENDERER_SDL_RENDERER`, `CNA_RENDERER_SDL_GPU`,
`CNA_RENDERER_EASYGL` and so on (`cmake/RendererSelection.cmake`); there is no bare
`CNA_RENDERER_SDL`.

So the second branch fires only through `defined(SDL_h_)` — that is, only when the translation
unit happened to include `<SDL3/SDL.h>` *before* this header. For its stated purpose it is dead.

This is currently harmless, because `SDL_main.h` is a no-op on those targets anyway. It is
recorded because it is exactly the kind of silently-inert condition that looks like coverage
during a migration and is not.

### Finding 2 — the header has no consumers

Nothing in the repository includes `CNA/Entrypoint.hpp`. Not one game, example, test or tool.

It is documented as the header game code must use, and no in-repo code demonstrates or exercises
it. An abstraction with zero call sites has never been proven to work.

### Finding 3 — the Android demos bypass it

The two files that most need this header do the thing its doc comment tells them not to:

- `modules/devices/examples/demo_devices/src/Main.cpp:1` — `#include <SDL3/SDL_main.h>`
- `modules/devices/examples/demo_devices/android/.../jni/src/Main.cpp:1` — same

Both even carry their own comment explaining the `dlsym`/`SDL_main` mechanism, duplicating the
rationale already in `Entrypoint.hpp`. So the one real Android entry point in the tree is coupled
directly to SDL, and the abstraction meant to cover it sits unused beside it.

### `SDL3::SDL3main` linkage

`cmake/Harnesses.cmake:166` links `SDL3::SDL3main`, guarded by `if(TARGET SDL3::SDL3main)`, for a
single target: `cna_reference_dump`, a manually-invoked developer comparison tool excluded from
Android and Emscripten builds. No production target links it. This is a small, contained,
build-system-only concern.

## What the platform contract must own

Nothing at runtime. Concretely, after the split:

1. **`Entrypoint.hpp` keys off `CNA_PLATFORM`, not off SDL or renderer macros.** Under
   `CNA_PLATFORM=SDL3` on Android it includes `<SDL3/SDL_main.h>`; under `HEADLESS`, `TERMINAL`, or
   any future non-SDL platform it includes nothing. The bare `CNA_RENDERER_SDL` condition is
   deleted rather than repaired — it never worked, and the renderer is the wrong axis anyway now
   that renderer and platform are separate choices.
2. **`Entrypoint.hpp` moves to `modules/platform`,** with `modules/core` no longer including SDL
   headers at all. It is a platform concern, not a core one.
3. **The Android demos are converted to include it,** which is what turns finding 2 from a
   documentation claim into a tested path. This is the acceptance criterion for PLAT-54: the
   abstraction is proven by its only real consumer using it.
4. **The `SDL3::SDL3main` link stays in CMake,** scoped to the SDL3 platform, and gains a comment
   pointing here.
5. **Windows is worth a deliberate check, not an assumption.** SDL3's `SDL_main.h` is header-only
   and can provide `WinMain` for GUI-subsystem applications. CNA builds work today, so nothing is
   broken — but with the dead branch above, no CNA target has ever actually exercised the
   non-Android path through this header. Before deleting the branch, confirm on a Windows build
   whether any target relies on it. Do not infer it from the Linux build succeeding.

## Requirement placed on PLAT-54

PLAT-54 is a *decision plus a migration*, not just a decision:

- delete the dead `CNA_RENDERER_SDL` condition;
- re-key the header on `CNA_PLATFORM` and move it into `modules/platform`;
- convert both `demo_devices` entry points to use it, removing their direct
  `<SDL3/SDL_main.h>` includes and their duplicated rationale comments;
- verify the Windows path explicitly rather than by inference.
