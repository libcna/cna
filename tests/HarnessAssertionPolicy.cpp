// SPDX-License-Identifier: MS-PL
//
// plans/plan_fx.md FX-111: make a tripped native assertion fail this suite instead of wedging it.
//
// Pinned MojoShader's `assert` resolves to SDL's assertion macro, and SDL's DEFAULT handler stops
// and waits for an interactive answer. In an interactive session that is helpful; in a test run it
// means one malformed input hangs the whole suite instead of failing one case. That is not
// hypothetical -- a truncated effect binary trips `mojoshader_effects.c`'s `readvalue` assertion,
// and the run stops there with no output about which test did it (plans/plan_fx.md FX-065).
//
// The policy is selected through SDL's own `SDL_ASSERT` environment hint rather than through
// `SDL_SetAssertionHandler`, deliberately. This translation unit is compiled into CnaTests for
// EVERY platform and renderer combination, including the ones that link no SDL at all (HEADLESS,
// TERMINAL); calling the native function would make the whole suite depend on a library that most
// configurations have no other reason to link. The hint is read by SDL itself at the first
// assertion, so a build that does link SDL gets the behaviour and a build that does not is
// unaffected. Nothing here includes an SDL header or references an SDL symbol -- the name below is
// an environment variable, spelled as text.
//
// It cannot hide a CNA assertion, and that is a checkable fact rather than an intention: no
// production translation unit in this repository uses `SDL_assert`, `SDL_TriggerBreakpoint` or
// `SDL_SetAssertionHandler` at all. CNA's own internal assertions are plain `<cassert>` `assert`,
// which this hint does not reach. The only assertions it changes the handling of are the ones
// raised inside third-party code that assert through SDL -- pinned MojoShader being the one that
// actually does.
//
// `always_ignore` rather than `abort`, and the trade-off is real rather than obvious. `abort` gives
// a clean stack but kills the entire run for one bad input, which is only marginally better than
// today's hang. `always_ignore` returns from the assertion and lets the caller continue, so the
// surrounding code's own error handling decides the outcome -- which is what makes the case above
// report as an ordinary passing `EXPECT_ANY_THROW` in 70 ms. The cost is that execution continues
// past an invariant the third-party library considered violated, so a genuinely new assertion is
// reported by whatever goes wrong next rather than at its own site. Run the suite with
// `SDL_ASSERT=abort` in the environment to get the stack instead: the overwrite flag below is 0
// precisely so an explicit choice wins.

#include <cstdlib>

namespace
{
    const bool kNonInteractiveAssertionsSelected = [] {
#if defined(_WIN32)
        // _putenv_s always overwrites, so only set it when the caller has not.
        if (std::getenv("SDL_ASSERT") == nullptr) _putenv_s("SDL_ASSERT", "always_ignore");
#else
        ::setenv("SDL_ASSERT", "always_ignore", /*overwrite=*/0);
#endif
        return true;
    }();
}
