# Audit: include/CNA/Entrypoint.hpp

## Metadata

- Source file: `include/CNA/Entrypoint.hpp`
- Audit status: AUDITED
- Subsystem: `cna-root-utilities` shard
- File type: C++ header
- XNA/FNA relevance: N/A — pure `CNA` namespace infrastructure (exception type, platform/OS detection,
  logging, entrypoint glue, backend/capability enums), not part of the `Microsoft::Xna` API surface
- Graphics backend relevance: foundational, consumed across the whole project
- Main related tests: see Missing or Weak Tests

## Purpose

Provides SDL_main.h inclusion glue so game code never needs to include <SDL3/SDL_main.h> directly (required on Android for SDL's Java bridge to locate the renamed main() entrypoint).

## Executive Verdict

Needs attention — 2 confirmed findings: the non-Android branch checks a preprocessor macro that is never actually defined, and the header itself has zero consumers anywhere in this repository.

## Checklist Results

### Behavioral correctness / API design / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Confirmed: `#elif defined(CNA_BACKEND_SDL) || defined(SDL_h_)` (line 22) checks a macro, `CNA_BACKEND_SDL`, that is never defined anywhere in the build system** — confirmed via exhaustive grep of `cmake/`: the real compile definitions are `CNA_BACKEND_SDL_RENDERER` and `CNA_BACKEND_SDL_GPU` (different tokens, with suffixes), neither of which satisfies a bare `defined(CNA_BACKEND_SDL)` check. This means, on any non-Android build, this `#elif` branch's condition reduces to just `defined(SDL_h_)` (an internal SDL header's own include-guard macro) — the branch only actually includes `<SDL3/SDL_main.h>` if some OTHER header already transitively included `<SDL3/SDL.h>` earlier in the same translation unit, not reliably by this header's own logic. The header's own doc comment claims non-Android inclusion is "safe and portable" (implying it always happens, harmlessly, as a no-op) — in practice it usually does NOT happen via this header's own condition at all. **Confirmed zero consumers**: exhaustive grep of the entire repository for `CNA/Entrypoint` finds only this file itself — no example, test, or tool anywhere in this repo actually includes it (`examples/ascii_fontatlas_test.cpp`'s own `int main()`, checked as a representative sample, includes neither this header nor `<SDL3/SDL_main.h>` directly). Since real, runnable game projects using CNA as a library would live outside this repository, zero in-repo consumers may be expected — but it also means the macro-mismatch bug above has never been exercised/caught by any build in this repo.

### Testing
No dedicated GTest coverage found for this specific file's own logic.

## Detailed Findings

**Confirmed: `#elif defined(CNA_BACKEND_SDL) || defined(SDL_h_)` (line 22) checks a macro, `CNA_BACKEND_SDL`, that is never defined anywhere in the build system** — confirmed via exhaustive grep of `cmake/`: the real compile definitions are `CNA_BACKEND_SDL_RENDERER` and `CNA_BACKEND_SDL_GPU` (different tokens, with suffixes), neither of which satisfies a bare `defined(CNA_BACKEND_SDL)` check. This means, on any non-Android build, this `#elif` branch's condition reduces to just `defined(SDL_h_)` (an internal SDL header's own include-guard macro) — the branch only actually includes `<SDL3/SDL_main.h>` if some OTHER header already transitively included `<SDL3/SDL.h>` earlier in the same translation unit, not reliably by this header's own logic. The header's own doc comment claims non-Android inclusion is "safe and portable" (implying it always happens, harmlessly, as a no-op) — in practice it usually does NOT happen via this header's own condition at all. **Confirmed zero consumers**: exhaustive grep of the entire repository for `CNA/Entrypoint` finds only this file itself — no example, test, or tool anywhere in this repo actually includes it (`examples/ascii_fontatlas_test.cpp`'s own `int main()`, checked as a representative sample, includes neither this header nor `<SDL3/SDL_main.h>` directly). Since real, runnable game projects using CNA as a library would live outside this repository, zero in-repo consumers may be expected — but it also means the macro-mismatch bug above has never been exercised/caught by any build in this repo.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated GTest coverage found for this specific file's own logic.

## Positive Findings

The underlying design intent (hiding the SDL_main dependency behind a CNA-owned header, critical for the real Android SDL_main rename requirement) is sound and well-documented; only the non-Android condition's implementation has a bug.

## Final Assessment

See findings above.
