# SDL subsystem ownership and teardown

This is the current PLAT-4/PLAT-29 lifecycle contract, checked against source on 2026-11-01.
Historical token counts and pre-migration call sites are in Git history. See
[platform abstraction](platform-abstraction.md) and [renderer boundary](platform-renderer-boundary.md)
for the broader ownership model.

## Global lifetime and platform references

CNA production code does not call SDL_Init or SDL_Quit. Applications/tests may own global SDL
startup/termination; a CNA consumer need not preinitialize everything just to let CNA acquire the
subsystems it uses. Do not add a platform-wide Initialize/Shutdown that steals the host's global
SDL lifetime.

IPlatform exposes AcquireSubsystem, ReleaseSubsystem and IsSubsystemInitialized for five abstract
subsystems: Video, Audio, Gamepad, Haptic and Sensor. Sdl3Platform maps these to SDL_InitSubSystem,
SDL_QuitSubSystem and SDL_WasInit. A successful acquire increments that instance's ownedRefCounts;
a failed acquire throws without recording ownership. An unpaired release is a no-op.

IsSubsystemInitialized observes native global state; it does not prove that this instance owns a
reference. Destruction closes cached sensor/haptic handles and balances this instance's remaining
references, rather than shutting down the host's independent references.

## Actual owners

| Native subsystem | Current owner and release boundary |
|---|---|
| Video | GraphicsDevice conditionally acquires PlatformSubsystem::Video for a non-HeadlessEXT device. It records success and releases after renderer, presenter and window teardown. |
| Gamepad | Sdl3Platform lazily attempts acquisition when GetGamepad/GetJoystick is first used, then refreshes both services. The platform retains its successful reference until destruction. |
| Sensor | Platform-neutral device sessions and CNA input sensor helpers acquire/release through IPlatform. The last owned sensor release deactivates cached native handles before quitting. |
| Haptic | Clients acquire/release through IPlatform; the SDL3 service also retains a protected discovery reference, described below. |
| Audio | SDL3 playback, recording and mixer-stream implementations acquire native SDL audio references directly and release after stream/device/callback teardown, including failed-open cleanup. Framework/media callers use the audio facade. |
| Camera | A separate process-wide startup probe in Sdl3ServiceSupport, outside PlatformSubsystem and the per-instance reference map. Its retained lifetime is an explicit exception below. |

The gamepad bridge no longer guards direct SDL calls with SDL_WasInit. Game skips its controller
pump until the platform subsystem is initialized. The first service query attempts startup once;
failure is absorbed as no devices and is not retried on every frame. The Linux SDL3 constructor
seeds the classic joystick driver only when the embedding host has not specified its own hint;
startup cost and physical-device behavior still depend on that choice and host.

SDL also starts implicit dependencies: gamepad implies joystick, while audio, video, joystick,
sensor and camera imply events in the pinned SDL3 headers. The abstraction does not expose every
one of these native subsystem flags as its own enum.

## Serialization, retained discovery and camera

SdlGlobalStateMutex/SdlGlobalStateLock serialize native process-global mutations across platform
instances; a per-instance mutex alone is insufficient. GL attribute/window creation transactions
use the same coordination. Destruction during exit on a thread already holding that lock avoids
relocking it, with dedicated exit-under-lock coverage.

Subsystem operations must not synchronously dispatch to a main-thread queue whose pump is blocked
or absent. Standalone sensor Start/Stop can run without Game. This does not waive SDL/OS thread
requirements for video/window operations; Apple video startup, for example, requires the main
thread and has an explicit diagnostic.

Haptic enumeration retains one discovery acquisition per SDL3 platform. Client releases cannot
consume it. Releasing the last client closes cached handles/effects, while discovery preserves the
native ID list so an enumerated ID remains meaningful when opened later. Platform destruction
balances discovery as well as other owned references.

CameraSupported/whole capability queries may start SDL_INIT_CAMERA once. The probe uses the global
lock, reuses an already-started camera subsystem and caches startup success/failure. It deliberately
does not quit between enumeration and opening a camera: doing so invalidates IDs. This reference
is not balanced by Sdl3Platform destruction. It is not evidence that a physical camera exists or
permission was granted. Host SDL_Quit/reinitialization is not a transparent camera-service reset;
the cached once-only probe is not retried. See [camera contract](cna-devices-camera-design.md).

## Static vibration teardown

VibrateController's function-local singleton may outlive an application's SDL_Quit and the ambient
platform. The explicit safe sequence is to call DevicesShutdownCoordinator::Shutdown while the
platform/native services are alive, before the host's SDL_Quit. Shutdown destroys the adapter before
publishing its flag and is idempotent; subsequent controller calls have no backend.

The process-exit fallback is narrower: its atexit callback sets the flag and deliberately avoids
calling a platform that may already have been destroyed. PlatformVibrateBackend's destructor
skips service release after shutdown, and ordinary ReleaseService only calls a captured platform
if that same platform is still installed. The fallback does not promise timely physical-device
cleanup or make arbitrary host teardown order equivalent to explicit shutdown.

Closing a stale native haptic handle after SDL_Quit can dereference freed native state; this is a
source-based hazard, not a newly reproduced physical-haptic failure. A QuitSubSystem call's
native refcount guard alone does not make stale handle closure safe.

## Verification

Focused coverage includes Sdl3PlatformTests (balanced/independent references and concurrent
instances), PlatformConformance.SubsystemsAreRefcounted, CurrentPlatform transfer/reset tests,
SensorSubsystemOwnershipTests, DevicesShutdownCoordinatorTests and DevicesShutdownOrderingTests.
The latter spawns a harness that exercises real SDL_Quit both with explicit shutdown and the
fallback. VibrateController's concurrent-call test checks the adapter synchronization path.

These tests complement source inspection. Empty-device success does not qualify physical sensor/
haptic hotplug or native driver teardown on every OS. Run window/native suites through the private
runner; the application must keep borrowed platform services alive until their consumers finish.
