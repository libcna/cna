# CNA::Devices::Camera

`CNA::Devices::Camera` is an implemented CNA extension in `modules/devices-ext`, enabled by
`CNA_DEVICES=ON`. It captures through `IPlatformCameraProvider` / `IPlatformCamera`;
its public header is SDL-free. SDL3 implementation details live in
`modules/platform/src/Sdl3/Sdl3Camera*`. The legacy filename records the original design task;
the completed DEVICES-CNA-010 / PLAT-106 work is in Git history.

## Public API and ownership

| API | Current behavior |
|---|---|
| `getIsSupportedProperty()` | Queries the selected platform's Camera capability without constructing a capture session. It does not establish hardware presence or permission. |
| `getAvailableCamerasProperty()` | Returns copied names/positions for the provider's current enumeration. Enumeration does not open a session. |
| `Camera()` | Opens the first enumerated device through the provider, if any. No public device selector is implemented. |
| Destructor | Destroys/closes the independently owned session. Camera is noncopyable. |
| `getStateProperty()` | Maps the session's current permission/device state, or NotSupported when no session exists. |
| Width/height getters | Read negotiated session dimensions; zero when unavailable/not yet known. |
| `TryAcquireFrame(texture)` | Polls one frame and uploads valid RGBA8 pixels to an existing correctly sized Texture2D. |

There is no public Open/Close/reopen method, frame callback, format-selection API or exposed capture
timestamp in this class. The platform frame has a timestamp, but this wrapper only uploads pixels.
Constructing Camera can open hardware or trigger the platform's permission interaction.

The platform provider must outlive its sessions. Camera owns the session, while the destination
texture and its graphics device remain application-owned.

## States

| CameraState | Observable meaning |
|---|---|
| NotSupported | No session: no provider/device, or open failure. A positive capability probe can coexist with this state. |
| Closed | Defined in the public enum, but the current constructor/destructor-only wrapper has no path that reports it. |
| Opening | A session exists; permission or final format resolution is pending. |
| Denied | The provider reports permission denial. |
| Ready | The provider permits polling; a new frame is not guaranteed on every poll. |
| Lost | The provider reports unusable capture: SDL3 removed the device, or negotiated an invalid format. |

`getStateProperty()` does not consume the global event queue. In the SDL3 session, Opening polls
permission; approval resolves the format, denial becomes Denied, and unavailable format metadata
keeps Opening for a later poll. Denied and Lost are cached terminal states in that session; Ready
is left for Denied or Lost as described below.

Each SDL3 session owns an event watch (`Sdl3CameraEventLatch`, CNA-FIX-L18) that SDL calls as it
pushes `SDL_EVENT_CAMERA_DEVICE_REMOVED` or `SDL_EVENT_CAMERA_DEVICE_DENIED`, on any thread. The
next state poll turns a removal of this camera into Lost and a denial into Denied, even after Ready,
so frames stop instead of SDL's blank frames after an unplug. A Ready session also re-reads SDL's
cached permission state on each poll, which notices a revocation SDL records without an event.
The watch is installed with the session and removed before it is freed.
`Sdl3PlatformTest.ACameraEventLatchRecordsOnlyItsOwnRemovalAndDenial` pushes the events SDL would
send; no physical unplug or revocation has been run. Recovery and reopening after Lost remain
outside the implemented boundary.

## RGBA frame and texture contract

The SDL3 provider requests RGBA32, using the first advertised dimensions/rate when available.
It validates the negotiated positive dimensions and RGBA format after permission approval.
There is no separate CNA YUV/format-conversion fallback for a non-RGBA negotiated result.

Each acquired SDL surface is released through SDL exactly once. The provider validates format,
dimensions, pixels and pitch, removes row padding, and returns owned tightly packed RGBA8 bytes
through the platform seam. No SDL surface/pointer escapes to the device wrapper.

Camera then validates positive dimensions, representable byte/pixel counts, exact pixel storage and
matching texture dimensions before calling the ordinary `Texture2D::SetDataRGBA` path. It does not
resize or recreate the caller's texture and adds no renderer-specific upload route.

False means no usable new frame, a non-ready session, invalid frame storage or a size mismatch.
These pre-upload refusals leave the texture unchanged, but the platform frame has already been
polled/consumed before wrapper validation; a mismatch is not a promise that the same frame can be
retried. Native texture-format, disposal, renderer or allocation exceptions can still propagate
from upload. The Boolean is not a universal exception/firewall result.

## Platform and test boundaries

The SDL source tree contains camera drivers for several OS targets, but source presence does not
qualify that target's driver, permission flow or physical capture. Null-provider platforms yield
an inert Camera. Default-device selection, RGBA delivery and permission polling remain the bounded
public feature set.

`modules/devices-ext/tests/CNA/Devices/CameraTests.cpp` installs
`CannedCameraPlatform` before constructing Camera. It checks capability versus enumeration,
first-device selection, failed opens, state mapping, dimensions, session destruction, empty/refused
frames, size/storage validation and a real Texture2D upload from scripted RGBA bytes. Those tests
do not open a physical camera.

The C adapter retains owned camera handles and supplies an adapter-private scripted platform
provider through its test extension routes; see [c-api/DEVICES.md](c-api/DEVICES.md).
That C seam is not a second public native Camera constructor.

Real hardware permission, frame timing, unplug/revocation and recovery require separate platform
qualification. The ordinary canned tests establish the abstraction/wrapper contract, not those
hardware results.

Audit verification (2026-11-01): `CnaDevicesExtTests` builds in the existing
`cmake-build-devices-ubsan` configuration with `CNA_DEVICES=ON`; all 13 `CameraTests.*` pass
through the private GPU runner. This is scripted-provider evidence, with no physical capture.
