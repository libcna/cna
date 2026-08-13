# SDL subsystem lifecycle audit (PLAT-4)

> Audit of every `SDL_Init*` / `SDL_Quit*` call site in CNA production code, and of the mechanisms
> that coordinate them. `plan_platform.md` PLAT-29 must reproduce this behaviour exactly — the
> ordering here is subtle, was arrived at deliberately (Tasks `SDLCORE-001`, `SDLCORE-011`,
> `P7-1`, `VIB2-003/004`), and already has dedicated regression tests. This document exists so
> PLAT-29 is a port, not a redesign.
>
> Hand-written, unlike `platform-sdl-classification.csv` and `platform-renderer-sdl-audit.md`,
> which are generated. Re-derive the call-site list with:
> `grep -rnE '\bSDL_(Init|InitSubSystem|QuitSubSystem|Quit|WasInit)\s*\(' modules/*/src modules/*/include`

## The single most important finding

**CNA production code never calls `SDL_Init()` or `SDL_Quit()`.**

Verified by scanning every `.cpp`/`.hpp`/`.mm` under `modules/` with comments and string literals
stripped: 30 files call `SDL_Init()`/`SDL_Quit()`, and **all 30 are tests or examples** — i.e.
code acting as *the application*. Production CNA acquires and releases individual subsystems and
nothing more.

The ownership split is therefore:

| Owner | Responsibility |
|---|---|
| The application (game, test, example) | `SDL_Init()` / `SDL_Quit()` — global SDL lifetime |
| CNA production code | `SDL_InitSubSystem()` / `SDL_QuitSubSystem()` — lazy, per-subsystem, refcounted by SDL |

This is a behavioural contract, not an accident, and it constrains PLAT-29 directly:

> `IPlatform::Initialize()` must **not** become `SDL_Init()`, and `IPlatform::Shutdown()` must
> **not** become `SDL_Quit()`.

Doing so would take global SDL lifetime away from the application and change the observable
behaviour of every existing game, test and example — including the ones that deliberately call
`SDL_Quit()` themselves in `main()`. The platform contract needs a subsystem-acquisition model
(acquire/release with refcounting), plus an explicitly documented position on who owns global
initialisation. The recommendation is that `IPlatform` keeps CNA's current stance: it acquires
what it needs and releases it, and global lifetime stays the host application's.

## Subsystems in use

Only five, all acquired lazily on first need:

| Subsystem | Refs | Acquired by | Released by |
|---|---:|---|---|
| `SDL_INIT_SENSOR` | 22 | `Detail::SdlSensorSubsystem<T>` (RAII) | RAII destructor + explicit calls in `Accelerometer`/`Gyroscope` |
| `SDL_INIT_VIDEO` | 14 | `GraphicsDevice` constructor | `GraphicsDevice::Dispose()` |
| `SDL_INIT_AUDIO` | 12 | `AudioMixer` (permanent pin), `Microphone`, `VideoPlayer` | `VideoPlayer` only — see below |
| `SDL_INIT_HAPTIC` | 11 | `SdlHapticVibrateBackend` | `SdlHapticVibrateBackend` destructor |
| `SDL_INIT_GAMEPAD` | 9 | `SdlInputBridge` (guarded by `SDL_WasInit`) | `SdlInputBridge` |

`SDL_INIT_EVENTS` is never named explicitly: it is initialised implicitly as a dependency of
`SDL_INIT_VIDEO`/`SDL_INIT_GAMEPAD`. A platform implementation that does not go through SDL must
account for that implicit dependency rather than inheriting it for free.

## Per-subsystem detail

### `SDL_INIT_VIDEO` — `GraphicsDevice`

`modules/graphics/src/Xna/GraphicsDevice.cpp:324` acquires, `:682` releases in `Dispose()`.

The acquisition is **conditional**, and the condition is load-bearing:

```cpp
if (!presentationParameters_.getHeadlessEXTProperty())
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) { throw makeSdlError(...); }
}
```

`HeadlessEXT` is what lets a device run with no display server at all — `plan_headless.md`'s
central promise ("`SDL_INIT_VIDEO` never called") depends on this branch. Note the asymmetry:
`Dispose()` calls `SDL_QuitSubSystem(SDL_INIT_VIDEO)` **unconditionally**, relying on SDL's
refcount making the unpaired release a no-op. PLAT-29 must either preserve that tolerance or
make the release conditional to match; it must not tighten the acquisition into an assertion.

### `SDL_INIT_AUDIO` — three different ownership models in one subsystem

This is the least uniform subsystem and needs the most care:

1. **`AudioMixer` pins it permanently.** `modules/audio/src/Internal/AudioMixer.cpp:75` sets a
   process-wide `g_audioSubsystemPinned` flag and **never releases**. The pin is retried on every
   `GetMixer()` call until it succeeds, so a first attempt on a machine with no audio hardware
   does not poison later attempts. Deliberate (`AUD-04-008/009`).
2. **`Microphone` acquires without releasing** (`Microphone.cpp:41`, `:183`).
3. **`VideoPlayer` acquires and releases in pairs** (`VideoPlayer.cpp:103`, `:113`, `:134`,
   `:243` — the last carries an explicit "paired with the `SDL_InitSubSystem()` call that opened
   it" comment).

So the audio subsystem's refcount is intentionally never driven to zero once the mixer has
started. A platform contract that assumes symmetric acquire/release per caller would change this.

### `SDL_INIT_SENSOR` — RAII plus a process-wide mutex

`Detail::SdlSensorSubsystem<TSensor>` (`modules/devices/include/.../SdlSensorSubsystem.hpp:246`)
acquires in its constructor and releases in its destructor, and `Accelerometer`/`Gyroscope` also
call `SDL_QuitSubSystem(SDL_INIT_SENSOR)` explicitly at three sites each. Serialised by
`Detail::SdlSubsystemMutex`, a **process-wide** mutex shared with the haptic backend.

`CNA::Input::Sensors` now participates through PLAT-29's platform refcount: each static
enumeration/read acquires and releases one `PlatformSubsystem::Sensor` reference, and a read closes
its `IPlatformSensors` handle before releasing that reference. SDL's own refcount therefore
aggregates it with the independent `Microsoft::Devices` holds instead of either surface using
`SDL_WasInit()` as an ownership shortcut.

`SdlSubsystemMutex`'s own header documents why it is a mutex rather than main-thread dispatch:
`SDL_RunOnMainThread(..., wait_complete=true)` is drained only by `SDL_RunMainThreadCallbacks()`,
which runs only from `SDL_PumpEventsInternal()` — so routing subsystem calls through it would
hang whenever nothing is pumping events, which is exactly what CNA's own concurrent
`Start()`/`Stop()` stress tests do. **A platform implementation must not "fix" this by moving
subsystem calls onto the main thread.** `Microsoft::Devices` classes are usable standalone with
no `Game`/`GraphicsDeviceManager` at all, so there may be no event pump running.

### `SDL_INIT_HAPTIC` — `SdlHapticVibrateBackend`

`modules/devices/src/Detail/SdlHapticVibrateBackend.cpp:230` acquires, `:217` releases. Shares
`SdlSubsystemMutex` with the sensor path.

### `SDL_INIT_GAMEPAD` — `SdlInputBridge`

`modules/input/src/Internal/SdlInputBridge.cpp:1599` guards on `SDL_WasInit(SDL_INIT_GAMEPAD)`
before acquiring at `:1606`; releases at `:1611`. The `SDL_WasInit` guard is the one place CNA
inspects SDL's refcount rather than just adjusting it, so the platform contract needs an
equivalent "is this subsystem already up?" query.

## Static-teardown ordering: `DevicesShutdownCoordinator`

The subtlest constraint, and the one most likely to be broken by a naive port.

`VibrateController::getDefaultProperty()` returns a function-local static whose destructor makes
real `SDL_CloseHaptic()`/`SDL_QuitSubSystem()` calls. Function-local statics are destroyed after
`main()` returns — i.e. **after** an application's own `SDL_Quit()`. Reading SDL's implementation,
the two calls have genuinely different outcomes, which the coordinator's header records honestly:

- `SDL_CloseHaptic()` on a handle `SDL_Quit()` already freed is a **real heap-use-after-free**
  (`CHECK_HAPTIC_MAGIC` dereferences to validate). Reasoned from SDL's source and *documented as
  not empirically reproduced* in this environment, because no physical haptic device is ever
  opened here.
- `SDL_QuitSubSystem(SDL_INIT_HAPTIC)` after `SDL_Quit()` **was checked and found already safe** —
  it is gated by `SDL_ShouldQuitSubsystem()`, a refcount check.

`DevicesShutdownCoordinator::Shutdown()` is the application's opt-in fix, to be called before its
own `SDL_Quit()`. PLAT-29 must keep this escape hatch: any platform implementation whose shutdown
runs at static-teardown time inherits the same hazard, and it is not SDL-specific.

## Regression tests that pin this behaviour

PLAT-29 is verified by these passing unchanged, not by inspection:

- `modules/devices/tests/Microsoft/Devices/Detail/DevicesShutdownOrderingTests.cpp`
- `modules/devices/tests/Microsoft/Devices/Detail/DevicesShutdownCoordinatorTests.cpp`
- `modules/devices/tests/Microsoft/Devices/Sensors/SensorSubsystemOwnershipTests.cpp`
- `modules/devices/tests/Microsoft/Devices/VibrateControllerTests.cpp`
  (`ConcurrentCallsFromMultipleThreadsDoNotCrashOrDeadlock`)

## Requirements this places on PLAT-19 / PLAT-29

1. **Acquire/release, not init/shutdown.** The contract needs
   `AcquireSubsystem(PlatformSubsystem)` / `ReleaseSubsystem(...)` semantics with refcounting,
   plus an `IsSubsystemInitialized(...)` query for the `SDL_WasInit` case.
2. **Global lifetime stays with the application.** `IPlatform::Initialize()` ≠ `SDL_Init()`.
   Document this explicitly in the contract, or it will be "fixed" later by someone reasonably
   assuming otherwise.
3. **Tolerate unpaired releases.** `GraphicsDevice` releases video unconditionally; audio is
   pinned and never released. Both are deliberate.
4. **Never route subsystem calls through a main-thread dispatch.** It deadlocks without an event
   pump, and CNA supports device usage with no pump running.
5. **Preserve the headless branch.** Skipping video acquisition entirely, not merely hiding a
   window, is what makes display-server-free operation possible.
6. **Keep an explicit pre-`SDL_Quit()` shutdown hook** for the static-teardown ordering hazard.
7. **Account for implicit `SDL_INIT_EVENTS`.** It is never named but is always present as a
   dependency of video/gamepad.
