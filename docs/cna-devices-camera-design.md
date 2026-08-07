# `CNA::Devices::Camera` — Design Note (Task `DEVICES-CNA-010`)

**Status: design only, no implementation.** Per `plan_cna_devices.md`'s own Phase 4
scoping, this task is explicitly "write a design note, do not implement `Camera`
itself." This document exists so a future implementation task starts from a concrete
plan grounded in this codebase's actual APIs, not from scratch.

**Why `Camera` is last, not first:** every other `CNA::Devices` capability (Phases
1-3, all closed — see `plan_cna_devices.md`) was a thin, mostly-synchronous wrapper
around 1-15 SDL3 functions. `Camera` is different in three ways simultaneously:
asynchronous permission state, poll-based (not push-based) frame delivery, and a
mandatory bridge into this engine's own graphics-texture system. Any one of these
would be a normal-sized task; combined, they need their own design pass rather than
being squeezed into the same shape as `PowerInfo` or `Locale`.

---

## 1. What SDL3 actually provides

Confirmed by reading `third_party/SDL/include/SDL3/SDL_camera.h` and the vendored
per-platform backends directly (not assumed):

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
- **Real per-platform backends confirmed present:** `v4l2` (Linux),
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
NotSupported   — no camera hardware/backend on this platform or device
Closed         — never opened, or explicitly closed
Opening        — SDL_OpenCamera() called, permission decision still pending
Denied         — user (or platform policy) denied camera access
Ready          — approved, frames may be polled
Lost           — device disconnected/failed after being Ready (SDL3 sends a
                 device-loss event for this; frames keep arriving as blank per SDL3's
                 own doc comment until the app notices and reacts)
```

This mirrors `SensorState`'s own honesty principle (`Microsoft::Devices::Sensors`,
closed in `plan_devices.md`): a consumer must always be able to cheaply ask "can I use
this right now" rather than finding out by a confusing runtime failure.

## 3. The texture-upload bridge — simpler than `noxna_devices.md` originally assumed

`noxna_devices.md`'s original Section 4.9 flagged this as the hardest part, assuming
it would need graphics-backend-specific code. **Re-reading this codebase's actual
texture pipeline found it is already solved, generically, by existing infrastructure:**

- `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`'s `ITextureBackend`
  interface already has `virtual void UpdatePixels(const uint8_t* rgba, int stride)`
  — an in-place, full-level-0 RGBA pixel update, implemented per graphics backend
  (`OPENGLES`/`SDL_RENDERER`/`VULKAN`/`BGFX`) but exposed through one common,
  backend-agnostic virtual call.
- `Microsoft::Xna::Framework::Graphics::Texture2D` already exposes
  `SetDataRGBA(const uint8_t* data, int pixelCount)` (`Texture2D.hpp:196`, `NOXNA`) as
  a public entry point that (per its own doc comment, "Prefer the Texture2D(device, w,
  h) + SetData pattern for XNA-compatible code") is intended for exactly this kind of
  raw-pixel-source use case.
- **Conclusion: a `Camera` frame does not need its own bespoke upload path per
  backend.** The plan is: request an RGBA-compatible `SDL_CameraSpec` when opening the
  camera (or convert the acquired `SDL_Surface` to a known RGBA format if the device
  can't natively produce one — `SDL_Surface` conversion helpers already exist in
  SDL3's own surface API, not camera-specific), then call the existing
  `Texture2D::SetDataRGBA()`/`ITextureBackend::UpdatePixels()` path once per acquired
  frame. This still needs to be verified empirically once implementation starts (does
  `SDL_OpenCamera()`'s format negotiation reliably produce RGBA, or does every
  platform hand back a native/YUV format requiring conversion first? — flagged as an
  open question below, not resolved by this document), but the upload mechanism
  itself requires no new graphics-backend code.

## 4. Proposed class shape (illustrative, not final)

```cpp
namespace CNA::Devices {
    enum class CameraState { NotSupported, Closed, Opening, Denied, Ready, Lost };

    class Camera {
    public:
        static bool getIsSupportedProperty();
        static std::vector<CameraDeviceInfo> getAvailableCamerasProperty(); // name, front/back-facing

        explicit Camera(/* device selection, or default */);
        Camera(/* device selection */, std::unique_ptr<Detail::ICameraBackend> backend); // test-only, mirrors SystemTray's constructor-injection pattern
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

## 5. Testability — same backend-injection lesson from Phases 1-3, applied up front

Both `DEVICES-CNA-008` (`FileDialog`) and `DEVICES-CNA-009` (`SystemTray`) found real
bugs specifically because their real backends have side effects an automated test
cannot safely trigger (an orphaned `zenity` dialog; a real tray icon). `Camera`'s real
backend is at least as unsafe to exercise in CI — it would either fail loudly (no
camera hardware in most CI containers) or, worse, actually request OS camera
permission and open a real device if one happens to be present. **`Detail::ICameraBackend`
must exist from the very first line of implementation**, not be retrofitted after an
incident — following `SystemTray`'s constructor-injection pattern (a fake backend
supplied before any real device-opening call happens), not `FileDialog`'s original
post-construction `SetBackendForTesting()` mistake.

## 6. Open questions for whoever implements this

1. **RGBA format negotiation reliability** (Section 3) — needs empirical verification
   per platform, not resolved here.
2. **Permission UX ownership** — should `Camera` surface `SDL_EVENT_CAMERA_DEVICE_APPROVED`/
   `_DENIED` itself (meaning `CNA::Devices` needs its own event-pump integration, which
   no current class has), or should the consumer be responsible for pumping SDL's
   event queue and informing `Camera` of the outcome? This is a real architectural
   fork depending on how this engine's main loop already handles the SDL event queue
   elsewhere (not investigated as part of this design-only task).
3. **Multiple simultaneous `Camera` instances** — SDL3 itself supports opening more
   than one camera device; whether this codebase's `Camera` class needs to support
   that from day one, or can start single-instance-only (mirroring `VibrateController`'s
   singleton simplicity), is a scope decision for the implementation task, not this
   note.
4. **Device-loss recovery** (the `Lost` state above) — SDL3 sends a separate event for
   this per its own doc comment; exact recovery semantics (does the app need to
   re-`SDL_OpenCamera()`, or does the same handle recover on its own) needs verifying
   against SDL3's own behavior before writing the `Lost`→`Ready` transition logic.

## 7. Recommended scope for the first implementation task, if/when prioritized

Given the above, a first `Camera` implementation task should probably be scoped to:
single camera device (default/first available), synchronous permission check only
(no event-queue integration — poll `SDL_GetCameraPermissionState()` each tick instead,
simpler and sufficient for a first pass), RGBA-only format request (fail/report
`NotSupported` rather than converting non-RGBA formats, deferring format conversion to
a follow-up task if a real device needs it). This keeps the first pass small enough to
verify end-to-end on at least one real platform before expanding scope — consistent
with this plan's own "verify before generalizing" discipline used throughout Phases
1-3.
