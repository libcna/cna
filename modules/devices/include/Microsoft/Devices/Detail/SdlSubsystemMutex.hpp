// SPDX-License-Identifier: MS-PL

#pragma once

#include <mutex>

namespace Microsoft::Devices::Detail
{
    /**
     * @brief Process-wide mutex serializing every real SDL subsystem call this
     * project's `Microsoft::Devices` area makes (Task SDLCORE-001).
     *
     * Shared by `Detail::SdlSensorSubsystem<TSensor>` (`SDL_INIT_SENSOR`,
     * used by `Accelerometer`/`Gyroscope`, reached here via that header's
     * own `GetGlobalSdlSensorMutex()` forwarding call) and
     * `Detail::SdlHapticVibrateBackend` (`SDL_INIT_HAPTIC`, used by
     * `VibrateController`) — previously two entirely independent
     * mechanisms: the sensor side already had a shared, process-wide mutex
     * (Task P7-1), while the haptic backend guarded only its own private,
     * per-instance `mutex_`, so two concurrently-constructed
     * `SdlHapticVibrateBackend` instances (or a haptic call racing a sensor
     * call) had no shared serialization at all for the underlying
     * `SDL_InitSubSystem()`/`SDL_QuitSubSystem()`/enumeration/open/close
     * calls, which touch SDL's own global, cross-subsystem state.
     *
     * **Why this is a mutex, not a dispatch to a designated "main thread"
     * (the audit's own literal suggested design), re-examined with fresh
     * eyes rather than implemented unchecked:** SDL3 does provide
     * `SDL_RunOnMainThread()`/`SDL_IsMainThread()`
     * (`third_party/SDL/include/SDL3/SDL_init.h`) for exactly this kind of
     * marshaling. Reading SDL's own *implementation*, not just its header
     * doc comments, before choosing a design:
     * - `SDL_RunOnMainThread(fn, ud, wait_complete=true)`'s queued callback
     *   is drained *only* by `SDL_RunMainThreadCallbacks()`
     *   (`third_party/SDL/src/events/SDL_events.c`), called *only* from
     *   `SDL_PumpEventsInternal()` — i.e. only when something calls
     *   `SDL_PumpEvents()`/`SDL_PollEvent()`/`SDL_WaitEvent()`/
     *   `SDL_WaitEventTimeout()` on the real main thread. There is no
     *   timeout and no other drain mechanism (`SDL_WaitSemaphore()` blocks
     *   indefinitely if nothing ever pumps events).
     * - This project's own Devices test suite — and, as a real usage
     *   pattern rather than just a test artifact, any real game that calls
     *   `Start()`/`Stop()`/`Dispose()` from a background thread not
     *   synchronized with its own render/event loop's pump cadence —
     *   legitimately calls into `Accelerometer`/`Gyroscope`/`Compass`/
     *   `Motion`/`VibrateController` from threads other than whichever one
     *   happens to be SDL's notion of "the main thread," with no
     *   guarantee anything is actively pumping SDL events at that moment
     *   (Devices classes are fully usable standalone, with no running
     *   `Game`/`GraphicsDeviceManager` instance at all — confirmed: zero
     *   `tests/Microsoft/Devices/` test file constructs one). Naively
     *   routing every `SDL_InitSubSystem()`/`SDL_QuitSubSystem()` call
     *   through `SDL_RunOnMainThread(..., wait_complete=true)` would
     *   therefore risk hanging forever in exactly the scenarios this
     *   project's own existing concurrent `Start()`/`Stop()` stress tests
     *   (`SensorSubsystemOwnershipTests`, `VibrateControllerTests.
     *   ConcurrentCallsFromMultipleThreadsDoNotCrashOrDeadlock`) already
     *   cover and rely on working correctly.
     * - Critically, reading `SDL_InitSubSystem()`'s own real implementation
     *   (`third_party/SDL/src/SDL.c`) shows the "should only be called on
     *   the main thread" doc comment is a *blanket* statement applied to
     *   every subsystem uniformly, not a reflection of actual per-subsystem
     *   enforcement: `SDL_INIT_HAPTIC`/`SDL_INIT_SENSOR` call straight into
     *   `SDL_InitHaptics()`/`SDL_InitSensors()` with **no**
     *   `SDL_IsMainThread()`/`SDL_RunOnMainThread()` check anywhere in
     *   either path (`third_party/SDL/src/haptic/SDL_haptic.c`,
     *   `third_party/SDL/src/sensor/SDL_sensor.c` — pure hardware/device
     *   enumeration, confirmed by direct inspection, not assumed). The only
     *   *real*, enforced main-thread requirement anywhere in SDL's own
     *   source is `SDL_INIT_VIDEO` on Apple platforms specifically
     *   (`SDL_VideoThreadID`/`SDL_assert(SDL_VideoThreadID ==
     *   SDL_MainThreadID)` in `SDL_SDL.c`; `Cocoa_CreateDevice()` in
     *   `third_party/SDL/src/video/cocoa/SDL_cocoavideo.m` outright
     *   returns `NULL` if not called from `[NSThread isMainThread]`) — a
     *   subsystem this project's Devices area does not touch at all.
     *
     * **Conclusion:** for the two subsystems `Microsoft::Devices` actually
     * uses (`SDL_INIT_SENSOR`/`SDL_INIT_HAPTIC`), the real, enforced
     * requirement is thread-safety (no two threads racing the same global
     * SDL call), not main-thread affinity — which a single, process-wide
     * mutex correctly and portably provides, without the real deadlock risk
     * a naive `SDL_RunOnMainThread()`-based redesign would introduce for
     * this project's own already-supported multi-threaded usage. If this
     * project ever adds `SDL_INIT_VIDEO`-touching code to this same call
     * path (it currently does not — `GraphicsDevice`'s own
     * `SDL_InitSubSystem(SDL_INIT_VIDEO)` call is a separate, unrelated
     * code path with its own thread-affinity considerations, out of this
     * task's scope), that specific addition would need the
     * `SDL_RunOnMainThread()` treatment SDLCORE-001 originally asked for —
     * revisit this decision if that ever happens, rather than assuming it
     * still holds unchecked.
     */
    inline std::mutex& GetGlobalSdlSubsystemMutex()
    {
        static std::mutex mutex;
        return mutex;
    }
} // namespace Microsoft::Devices::Detail
