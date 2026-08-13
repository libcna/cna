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
| `SDL_INIT_SENSOR` | 22 | `Sdl3Platform::AcquireSubsystem(Sensor)` | `Sdl3Platform::ReleaseSubsystem(Sensor)` after the last platform session/reference |
| `SDL_INIT_VIDEO` | 14 | `GraphicsDevice` constructor | `GraphicsDevice::Dispose()` |
| `SDL_INIT_AUDIO` | 12 | SDL3 audio playback/recording backends and audio-facade queued streams | The same owners, after their callback/stream barriers — see below |
| `SDL_INIT_HAPTIC` | 11 | `Sdl3Platform::AcquireSubsystem(Haptic)` | `Sdl3Platform::ReleaseSubsystem(Haptic)` after the last platform client |
| `SDL_INIT_GAMEPAD` | 9 | `SdlInputBridge` (guarded by `SDL_WasInit`) | `SdlInputBridge` |

`SDL_INIT_EVENTS` is never named explicitly: it is initialised implicitly as a dependency of
`SDL_INIT_VIDEO`/`SDL_INIT_GAMEPAD`. A platform implementation that does not go through SDL must
account for that implicit dependency rather than inheriting it for free.

## Per-subsystem detail

### `SDL_INIT_VIDEO` — `GraphicsDevice`

> **PLAT-62 update:** the direct calls recorded below were the migration baseline. `GraphicsDevice`
> now acquires/releases `PlatformSubsystem::Video` through its enclosing `IPlatform`, records a
> successful acquisition, and balances it after destroying the renderer and its owned
> `IPlatformWindow`. SDL3 maps that pair onto these same native calls; HEADLESS/TERMINAL only
> maintain their platform-local reference count.

The acquisition is **conditional**, and the condition is load-bearing:

```cpp
if (!presentationParameters_.getHeadlessEXTProperty())
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) { throw makeSdlError(...); }
}
```

`HeadlessEXT` is what lets a device run with no display server at all — `plan_headless.md`'s
central promise ("`SDL_INIT_VIDEO` never called") depends on this branch. This historical
asymmetry was removed by PLAT-62's recorded conditional release, while the platform contract still
keeps unpaired release tolerant for cleanup after partial initialization.

### `SDL_INIT_AUDIO` — owned by the audio implementation

> **PLAT-96/112 update:** the ownership models recorded by the original audit no longer exist in
> framework/media code. Playback opens a selected `IAudioDevice`, recording opens a selected
> `IAudioRecordingDevice`, and queued PCM streams are opaque `MixerStream` objects behind
> `CNA::Internal::Audio::MixerEngine`. The SDL3 implementations acquire the native audio
> subsystem on a successful open/create and release the same reference only after the device or
> stream and its callback barrier have been torn down. Failed opens balance their provisional
> acquisition too.

`AudioMixer`, `Microphone`, `MediaPlayer`, and `VideoPlayer` therefore contain no direct native
subsystem lifecycle calls. In particular, `VideoPlayer` no longer has a special acquire/release
path: PLAT-112 connects its decoded float PCM to an initially-paused mixer playback stream and
destroys that opaque stream through the audio facade. The remaining `SDL_INIT_AUDIO` tokens are
implementation details under `modules/audio`, where the platform ratchet explicitly permits the
selected audio backend.

### `SDL_INIT_SENSOR` — platform sessions plus a process-wide mutex

> **PLAT-108 update:** `Accelerometer` and `Gyroscope` now acquire
> `PlatformSubsystem::Sensor` and open an independently owned `IPlatformSensorSession` through
> `IPlatformSensors`. `Detail::PlatformSensorSubsystem<TSensor>` owns only platform-neutral
> registration, callback barriers and per-type sharing; all native init/enumerate/open/close/quit
> operations live in `Sdl3Platform`/`Sdl3Sensors`.

`CNA::Input::Sensors` now participates through PLAT-29's platform refcount: each static
enumeration/read acquires and releases one `PlatformSubsystem::Sensor` reference, and a read closes
its `IPlatformSensors` handle before releasing that reference. SDL's own refcount therefore
aggregates it with the independent `Microsoft::Devices` holds instead of either surface using
`SDL_WasInit()` as an ownership shortcut.

`Sdl3Platform::subsystemMutex_` preserves the former process-wide serialization. It remains a
mutex rather than main-thread dispatch because
`SDL_RunOnMainThread(..., wait_complete=true)` is drained only by `SDL_RunMainThreadCallbacks()`,
which runs only from `SDL_PumpEventsInternal()` — so routing subsystem calls through it would
hang whenever nothing is pumping events, which is exactly what CNA's own concurrent
`Start()`/`Stop()` stress tests do. **A platform implementation must not "fix" this by moving
subsystem calls onto the main thread.** `Microsoft::Devices` classes are usable standalone with
no `Game`/`GraphicsDeviceManager` at all, so there may be no event pump running.

### `SDL_INIT_HAPTIC` — platform haptics

> **PLAT-108 update:** `VibrateController` now uses a private `PlatformVibrateBackend` adapter.
> Device selection, non-gamepad correlation, simple rumble, two-motor effects and native handle
> ownership all live in `IPlatformHaptics`/`Sdl3Haptics`. `Sdl3Platform::subsystemMutex_`
> serializes its native subsystem acquire/release with the sensor path.

### `SDL_INIT_GAMEPAD` — `SdlInputBridge`

`modules/input/src/Internal/SdlInputBridge.cpp:1599` guards on `SDL_WasInit(SDL_INIT_GAMEPAD)`
before acquiring at `:1606`; releases at `:1611`. The `SDL_WasInit` guard is the one place CNA
inspects SDL's refcount rather than just adjusting it, so the platform contract needs an
equivalent "is this subsystem already up?" query.

## Static-teardown ordering: `DevicesShutdownCoordinator`

The subtlest constraint, and the one most likely to be broken by a naive port.

`VibrateController::getDefaultProperty()` returns a function-local static whose platform adapter
can retain an `IPlatformHaptics` handle and subsystem reference. Function-local statics are
destroyed after `main()` returns — potentially **after** an application's own `SDL_Quit()`.
Reading SDL's implementation, the underlying native close/quit operations have genuinely
different outcomes:

- `SDL_CloseHaptic()` on a handle `SDL_Quit()` already freed is a **real heap-use-after-free**
  (`CHECK_HAPTIC_MAGIC` dereferences to validate). Reasoned from SDL's source and *documented as
  not empirically reproduced* in this environment, because no physical haptic device is ever
  opened here.
- `SDL_QuitSubSystem(SDL_INIT_HAPTIC)` after `SDL_Quit()` **was checked and found already safe** —
  it is gated by `SDL_ShouldQuitSubsystem()`, a refcount check.

`DevicesShutdownCoordinator::Shutdown()` is the application's opt-in fix, to be called before its
own `SDL_Quit()`. PLAT-108 strengthened it: it destroys the controller's platform adapter
*before* publishing the shutdown flag, so retained effects/devices close and the platform
reference is balanced while native services are still valid. The controller is inert afterward
and its eventual static destructor has no platform resource left to release. Any platform
implementation whose shutdown runs at static-teardown time inherits the same hazard, so this
escape hatch remains platform-neutral.

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
3. **Tolerate unpaired releases.** PLAT-62 now balances `GraphicsDevice` video ownership, but
   cleanup after partial initialization may still release an unacquired subsystem and must remain
   harmless. (The audio statement was the pre-PLAT-97 baseline; audio now has per-object leases.)
4. **Never route subsystem calls through a main-thread dispatch.** It deadlocks without an event
   pump, and CNA supports device usage with no pump running.
5. **Preserve the headless branch.** Skipping video acquisition entirely, not merely hiding a
   window, is what makes display-server-free operation possible.
6. **Keep an explicit pre-`SDL_Quit()` shutdown hook** for the static-teardown ordering hazard.
7. **Account for implicit `SDL_INIT_EVENTS`.** It is never named but is always present as a
   dependency of video/gamepad.
