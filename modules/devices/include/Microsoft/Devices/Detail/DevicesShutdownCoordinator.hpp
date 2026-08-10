// SPDX-License-Identifier: MS-PL

#pragma once

#include <atomic>

namespace Microsoft::Devices::Detail
{
    /**
     * @brief Process-wide coordinator for orderly `Microsoft::Devices`
     * shutdown relative to the application's own `SDL_Quit()` call (Task
     * SDLCORE-011).
     *
     * `Microsoft::Devices::VibrateController::getDefaultProperty()` returns
     * a function-local static singleton whose destructor (via its owned
     * `Detail::SdlHapticVibrateBackend`) makes real `SDL_CloseHaptic()`/
     * `SDL_QuitSubSystem()` calls -- confirmed by reading
     * `SdlHapticVibrateBackend.cpp`'s destructor directly. Function-local
     * statics are destroyed at process-exit static teardown, at a point this
     * codebase does not control and the application does not directly see
     * or synchronize with its own explicit `SDL_Quit()` call. If the
     * application calls `SDL_Quit()` in `main()` (the ordinary,
     * documented way to shut SDL down) and `VibrateController::instance`
     * has ever been touched, its destructor runs *after* `main()` returns --
     * i.e. after `SDL_Quit()` already ran.
     *
     * Confirmed via `third_party/SDL/src/SDL.c`/`SDL_haptic.c`/`SDL_log.c`
     * (read-only reference, never modified -- see that tree's own
     * `CLAUDE.md`), with two genuinely different outcomes per call, not one
     * uniform risk:
     * - `SDL_CloseHaptic()` against a device handle SDL_Quit() already
     *   closed internally (`SDL_QuitHaptics()` calls `SDL_CloseHaptic()` on
     *   every still-open device, which frees the device struct via
     *   `SDL_free(haptic)` after `SDL_SetObjectValid(..., false)`) **is a
     *   genuine heap-use-after-free** -- `SDL_CloseHaptic()`'s own first
     *   action, `CHECK_HAPTIC_MAGIC(haptic)`, dereferences the pointer to
     *   validate it, reading already-freed memory. This is reasoned
     *   directly from SDL's own source, not assumed, but **not empirically
     *   reproduced under ASan in this environment**: reproducing it needs a
     *   real, successfully-`SDL_OpenHaptic()`-opened device (`haptic_`
     *   non-null), and no physical haptic device is ever opened by this
     *   container (confirmed by every other haptic-related task this pass —
     *   `VIB2-003`/`004` carry the identical limitation). SDL's own `dummy`
     *   haptic backend (`third_party/SDL/src/haptic/dummy/`) that could
     *   otherwise fake a device is a compile-time backend choice
     *   (`SDL_HAPTIC_DUMMY`), not a runtime-selectable one — not available
     *   without rebuilding SDL itself differently, out of this task's scope.
     * - `SDL_QuitSubSystem(SDL_INIT_HAPTIC)` after `SDL_Quit()`, by
     *   contrast, **was checked and found already safe** by SDL's own
     *   design: `SDL_QuitSubSystem()`'s haptic branch is gated by
     *   `SDL_ShouldQuitSubsystem()`, a refcount check — once `SDL_Quit()`
     *   has already driven every subsystem's refcount to zero, a redundant
     *   `SDL_QuitSubSystem(SDL_INIT_HAPTIC)` call is a documented-safe no-op,
     *   not a use-after-free. **This was verified empirically**, not just
     *   reasoned about: `tools/devices/shutdown_ordering_harness.cpp`,
     *   which exercises exactly this call (a real device is never opened in
     *   this container, so `subsystemHeld_`/`SDL_QuitSubSystem()` is the
     *   only branch reachable here), ran clean under
     *   `cmake-build-devices-asan` both **with and without** this
     *   coordinator's guard active (`--skip-shutdown-call`) — no ASan
     *   report either way. The guard is kept anyway as defense that does not
     *   depend on SDL's internal refcount implementation staying exactly as
     *   it is today (an internal detail, not a documented contract), not
     *   because this specific call was ever proven dangerous.
     *
     * The application must call `Shutdown()` before its own `SDL_Quit()`
     * call whenever any `Microsoft::Devices` object might still be alive at
     * that point (in practice: whenever `VibrateController::getDefaultProperty()`
     * has ever been called). `Detail::SdlHapticVibrateBackend`'s destructor
     * checks `IsShutdown()` and skips its native `SDL_CloseHaptic()`/
     * `SDL_QuitSubSystem()` calls once set -- safe, not a resource leak,
     * since `SDL_Quit()` itself already reclaims those resources as part of
     * its own subsystem teardown.
     *
     * `SDL_Log()` (used by `Detail::NativeDiagnosticSink`,
     * `SdlSensorSubsystem<TSensor>::LogAndRecordDispatchException()`, and
     * `RecordHapticDiagnostic()`) was investigated separately and is *not*
     * guarded by this coordinator: `SDL_QuitLog()` (called from `SDL_Quit()`)
     * only destroys its own internal mutexes and resets its own
     * initialized-state, and `SDL_LockMutex(NULL)` is a documented safe
     * no-op throughout SDL's own mutex implementations ("clang doesn't know
     * about NULL mutexes" -- every platform's `SDL_sysmutex.c`) -- so a log
     * call after `SDL_Quit()` degrades to a harmless no-op rather than a
     * use-after-free, unlike the haptic device handle case above.
     */
    class DevicesShutdownCoordinator
    {
    public:
        /**
         * @brief Marks `Microsoft::Devices` as shut down. Idempotent -- safe
         * to call more than once, and safe to call even if no
         * `Microsoft::Devices` object was ever constructed.
         */
        static void Shutdown()
        {
            GetFlag().store(true, std::memory_order_release);
        }

        /** @brief True once `Shutdown()` has been called; false otherwise (the default). */
        [[nodiscard]] static bool IsShutdown()
        {
            return GetFlag().load(std::memory_order_acquire);
        }

        /** @brief Test-only hook: resets back to "not shut down." */
        static void ResetForTesting()
        {
            GetFlag().store(false, std::memory_order_release);
        }

    private:
        static std::atomic<bool>& GetFlag()
        {
            static std::atomic<bool> flag{false};
            return flag;
        }
    };
} // namespace Microsoft::Devices::Detail
