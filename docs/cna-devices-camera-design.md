# `CNA::Devices::Camera` — Design Note (Task `DEVICES-CNA-010`)

**Status: implemented; platform migration completed by `PLAT-106`.** The original
design-only task established the public API and native-camera constraints. The
implementation now keeps that public API SDL-free and routes enumeration, permission
polling, format negotiation and capture through `IPlatformCameraProvider` and
`IPlatformCamera`; SDL3 details live only in `modules/platform/src/Sdl3/Sdl3Camera.*`.

**Why `Camera` is last, not first:** every other `CNA::Devices` capability (Phases
1-3, all closed — see `plans/plan_cna_devices.md`) was a thin, mostly-synchronous wrapper
around 1-15 SDL3 functions. `Camera` is different in three ways simultaneously:
asynchronous permission state, poll-based (not push-based) frame delivery, and a
mandatory bridge into this engine's own graphics-texture system. Any one of these
would be a normal-sized task; combined, they need their own design pass rather than
being squeezed into the same shape as `PowerInfo` or `Locale`.

---

## 1. What SDL3 actually provides

Confirmed by reading `third_party/SDL/include/SDL3/SDL_camera.h` and the vendored
per-platform renderers directly (not assumed):

- **Enumeration:** `SDL_GetCameras(int* count)` → array of `SDL_CameraID`.
  `SDL_GetCameraName()`/`SDL_GetCameraPosition()` (front/back-facing, relevant on
  mobile) per camera.
- **Format negotiation:** `SDL_GetCameraSupportedFormats(SDL_CameraID, int* count)` →
  array of `SDL_CameraSpec*` (width, height, pixel format, frame rate) the device can
  produce. `SDL_OpenCamera(SDL_CameraID, const SDL_CameraSpec* spec)` — passing `NULL`
  for `spec` requests the device's own default format.
- **Permission:** opening a camera does not, by itself, guarantee access — call
  `SDL_GetCameraPermissionState(SDL_Camera*)`, which returns `-1` (denied), `0`
  (pending — the platform is showing its own permission prompt), or `1` (approved).
  **This is the first genuinely async/stateful piece**: on platforms with a real
  permission model (Android, and browsers via Web), the app must poll this or handle
  an `SDL_EVENT_CAMERA_DEVICE_APPROVED`/`SDL_EVENT_CAMERA_DEVICE_DENIED` event (SDL3's
  event queue, not something any current `CNA::Devices` class touches) before frames
  will ever arrive.
- **Frame delivery — poll-based, not push-based:** `SDL_AcquireCameraFrame(SDL_Camera*,
  Uint64* timestampNS)` returns a non-owning `SDL_Surface*` if a new frame is ready,
  `NULL` otherwise (either "no new frame yet," which is normal, or a real device
  failure — SDL3's own doc comment is explicit that both cases return `NULL` and the
  caller must not conflate them without also checking `SDL_GetCameraPermissionState()`/
  device-loss events). The caller must call `SDL_ReleaseCameraFrame()` once done with
  each acquired surface. **This is a fundamentally different shape from every other
  sensor-like class in this codebase** — `Accelerometer`/`Compass`/`Motion` all push
  data to the caller via a callback the instant a new sample exists; `Camera` requires
  the caller to actively poll once per frame/tick, closer in spirit to "check this
  every `Update()`" than to an event subscription.
- **Real per-platform renderers confirmed present:** `v4l2` (Linux),
  `mediafoundation` (Windows), `coremedia` (macOS/iOS), `android`, and
  **`emscripten`** (browser `getUserMedia`) — `pipewire` (newer Linux desktops) and a
  `dummy` fallback also exist. This is the best cross-platform coverage of any
  capability surveyed in `noxna_devices.md`, better than the original analysis
  expected.

## 2. The permission/state machine this needs

Unlike every Phase 1-3 class (whose `getIsSupportedProperty()` is a single
synchronous yes/no), `Camera` needs a real state enum a consumer can poll or subscribe
to, closer in spirit to `Microsoft::Devices::Sensors::SensorState`:

```
NotSupported   — no camera hardware/renderer on this platform or device
Closed         — never opened, or explicitly closed
Opening        — SDL_OpenCamera() called, permission decision still pending
Denied         — user (or platform policy) denied camera access
Ready          — approved, frames may be polled
Lost           — device disconnected/failed after being Ready (SDL3 sends a
                 device-loss event for this; frames keep arriving as blank per SDL3's
                 own doc comment until the app notices and reacts)
```

This mirrors `SensorState`'s own honesty principle (`Microsoft::Devices::Sensors`,
closed in `plans/plan_devices.md`): a consumer must always be able to cheaply ask "can I use
this right now" rather than finding out by a confusing runtime failure.

## 3. The texture-upload bridge — simpler than `noxna_devices.md` originally assumed

`noxna_devices.md`'s original Section 4.9 flagged this as the hardest part, assuming
it would need graphics-renderer-specific code. **Re-reading this codebase's actual
texture pipeline found it is already solved, generically, by existing infrastructure:**

- `include/CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp`'s `ITextureRenderer`
  interface already has `virtual void UpdatePixels(const uint8_t* rgba, int stride)`
  — an in-place, full-level-0 RGBA pixel update, implemented per graphics renderer
  (`OPENGLES3`/`SDL_RENDERER`/`VULKAN`/`BGFX`) but exposed through one common,
  renderer-agnostic virtual call.
- `Microsoft::Xna::Framework::Graphics::Texture2D` already exposes
  `SetDataRGBA(const uint8_t* data, int pixelCount)` (`Texture2D.hpp:196`, `CNAEXT`) as
  a public entry point that (per its own doc comment, "Prefer the Texture2D(device, w,
  h) + SetData pattern for XNA-compatible code") is intended for exactly this kind of
  raw-pixel-source use case.
- **Conclusion: a `Camera` frame does not need its own bespoke upload path per
  renderer.** The plan is: request an RGBA-compatible `SDL_CameraSpec` when opening the
  camera (or convert the acquired `SDL_Surface` to a known RGBA format if the device
  can't natively produce one — `SDL_Surface` conversion helpers already exist in
  SDL3's own surface API, not camera-specific), then call the existing
  `Texture2D::SetDataRGBA()`/`ITextureRenderer::UpdatePixels()` path once per acquired
  frame. This still needs to be verified empirically once implementation starts (does
  `SDL_OpenCamera()`'s format negotiation reliably produce RGBA, or does every
  platform hand back a native/YUV format requiring conversion first? — flagged as an
  open question below, not resolved by this document), but the upload mechanism
  itself requires no new graphics-renderer code.

## 4. Implemented class shape

```cpp
namespace CNA::Devices {
    enum class CameraState { NotSupported, Closed, Opening, Denied, Ready, Lost };

    class Camera {
    public:
        static bool getIsSupportedProperty();
        static std::vector<CameraDeviceInfo> getAvailableCamerasProperty(); // name, front/back-facing

        Camera(); // Opens the first camera reported by the selected platform.
        ~Camera();

        [[nodiscard]] CameraState getStateProperty() const;

        // Poll-based, mirrors SDL3's own AcquireCameraFrame() shape directly rather
        // than inventing a push-callback model this capability doesn't actually have.
        // Returns true and updates `outTexture` if a new frame was available this call.
        bool TryAcquireFrame(Microsoft::Xna::Framework::Graphics::Texture2D& outTexture);
    };
}
```

**Key departure from every other `CNA::Devices` class:** no result callback. Every
Phase 1-3 async class (`FileDialog`) used a callback because SDL3's own dialog API is
callback-shaped. `Camera`'s SDL3 API is poll-shaped (`SDL_AcquireCameraFrame()` called
once per tick, not "call me when ready"), so `Camera`'s own public shape should match
that instead of forcing an artificial callback/event model on top of it — a consumer
already calls this once per `Game::Update()`/`Draw()` anyway to actually use the
video feed.

## 5. Testability through the platform seam

The real implementation is unsafe to exercise in ordinary CI: it could request OS
camera permission and open actual hardware. Tests therefore install a
`CannedCameraPlatform` before constructing `Camera`. The canned provider scripts
enumeration, open failure, permission/device states, dimensions and RGBA frames while
recording calls and session destruction. This exercises the same public platform seam
as production without retaining a second devices-local backend abstraction.

## 6. Decisions and remaining limitation

1. **RGBA negotiation:** the platform requests `SDL_PIXELFORMAT_RGBA32`, starting from
   the first advertised dimensions/rate when available. It validates the negotiated
   format only after permission is approved and rejects any non-RGBA result.
2. **Permission ownership:** `IPlatformCamera::GetState()` polls permission. Camera
   users do not consume SDL events and a pending decision remains `Opening`.
3. **Multiple sessions:** the platform provider creates independently owned sessions;
   the current public `Camera` constructor still selects the first enumerated device.
4. **Device-loss recovery:** SDL3 sends a separate event for
   this per its own doc comment; exact recovery semantics (does the app need to
   re-`SDL_OpenCamera()`, or does the same handle recover on its own) needs verifying
   against real hardware. The contract preserves `Lost`, but automatic recovery is
   deliberately outside the first implementation.

## 7. First implementation scope

The completed first implementation intentionally remains bounded to the default/first
camera, permission polling without event-queue integration and RGBA-only delivery.
Native row padding is compacted before the frame crosses the contract. Unsupported
platforms expose a null provider and the public class remains inert rather than
touching native camera APIs.
