# Viewport / DisplayMode / GraphicsAdapter support matrix

`Viewport`, `DisplayMode`, `DisplayModeCollection` and `GraphicsAdapter` against FNA and, where
they differ, measured Microsoft XNA 4.0, together with the window-resize and render-target-switch
chains and the current cross-platform boundary.

---

## 1. Viewport properties and math

`Project`/`Unproject` math, `AspectRatio`, `Bounds` and `TitleSafeArea` match FNA exactly. `X`,
`Y`, `Width`, `Height`, `MinDepth` and `MaxDepth` are properties
(`getXProperty()`/`setXProperty()`). `MinDepth`/`MaxDepth` have no validation or clamping, in FNA
and CNA alike (FNA's unguarded arithmetic). `Project`/`Unproject` tests use non-identity matrices,
which exercise the perspective-divide branch.

## 2. `DisplayModeCollection` `SurfaceFormat` indexer

`DisplayModeCollection` has FNA's `this[SurfaceFormat format]` indexer as
`operator[](SurfaceFormat)`. CNA's `getCountProperty()` and integer `operator[](int)` do not exist
in FNA's API and are marked `CNAEXT`.

## 3. `GraphicsAdapter` enumeration and lifetime

Against FNA's `GraphicsAdapter.cs` and `SDL3_FNAPlatform.cs`:

- `DeviceName` is a synthetic Windows-style path and `Description` the real display name — two
  different values.
- Display modes are enumerated in **reverse** and deduplicated by `(width,height)`, dropping
  same-resolution/different-refresh-rate entries.
- There is no static `GraphicsAdapter&` field: `AdaptersChanged()` clears and repopulates
  `adapters_` on every call (FNA's display-reconfiguration usage), so the default adapter is read
  through `getDefaultAdapterProperty()`, which is re-evaluated after every `AdaptersChanged()`.
  Callers must not retain an adapter reference across a refresh.
- `DeviceId`/`VendorId` are backed by a Linux `/sys/class/drm` PCI query.

When the platform exposes no display service or no display, `AdaptersChanged()` creates one
synthetic 800×480 `Default Display` adapter.

**Lifecycle (`BINDFIX-001`).** `AdaptersChanged()` raises and pins the active platform's video
subsystem **before** enumeration, so call ordering cannot cache the fallback on a host with a real
display. The pin stays alive with the cache and follows the ambient platform lifecycle
(`SOFTWARE-157`); after a platform/video-session transition, an adapter whose native display ID
expired rebinds by display name instead of reporting a fictional 800×480 mode. Cache refresh/use is
serialized (`RUST-UPSTREAM-023`) so concurrent device construction cannot free another thread's
adapter.

Deterministic platform tests cover names and native IDs, reverse-order mode enumeration,
refresh-rate deduplication, current-mode fallback, absent display services and empty enumerations.

## 4. XNA corrections and device selection

### 4.1 Widescreen threshold (`BINDFIX-033`)

FNA guesses that a mode is widescreen above 4:3; recovered XNA 4.0 code compares strictly greater
than **1.6**. CNA follows XNA: 3:2 and exactly 16:10 are not widescreen, while a value just above
1.6 is. The decision is recorded in `xna-fna-behavior-decisions.md` and pinned independently of the
host display by `GraphicsAdapterTest.IsWideScreenUsesXnasLimitOfOnePointSixExclusive`.

### 4.2 `UseNullDevice` / `UseReferenceDevice` (`XNA-MISSING-010`)

The two documented static XNA properties are implemented as process-wide state. A new device
captures their resolution at construction; changing them does not mutate an existing device.

| Flags | Resolved CNA intent | Required renderer |
|---|---|---|
| both clear | hardware/default | the renderer already selected by the build/runtime |
| `UseReferenceDevice=true` | software reference intent | `SOFTWARE` |
| `UseNullDevice=true` | no drawing/presentation; wins when both flags are set | `HEADLESS` |

This preserves the intent rather than pretending CNA's software rasterizer is Direct3D 9 REF.
When the required renderer is not compiled into the process, device construction throws
`NoSuitableGraphicsDeviceException`; it never claims to honor a flag while silently using the
selected hardware renderer. `GetCurrentDeviceTypeEXT()` and `GetRequiredRendererEXT()` expose that
resolution explicitly, and the nine `GraphicsAdapterDeviceSelectionTest` cases cover precedence,
process scope, refusal, successful matching, default construction and live-device stability.

### 4.3 Renderer-aware capability queries

`IsProfileSupported`, `QueryRenderTargetFormat` and `QueryBackBufferFormat` use the selected
renderer registry's static adapter-query hooks. DirectX9 answers profile, target/backbuffer format
and MSAA questions from real D3D9 caps; the GLES/WebGL EasyGL profiles publish only their guaranteed
pre-context render-target set, and several families publish their MSAA clamp. A missing hook uses
the documented framework/profile fallback instead of inventing a hardware capability table.
Per-renderer results are in `graphics-renderer-feature-matrix.md`.

## 5. Window resize and backbuffer resize

The OS-level resize chain is `SDL_EVENT_WINDOW_RESIZED` → `GameWindow` → `GraphicsDeviceManager` →
`GraphicsDevice::UpdateViewportFromWindow()`. Deliberate CNA divergence from FNA:
`PresentationParameters.BackBufferWidth`/`Height` do **not** follow a real window resize; only
`Viewport` does, via `FixedHeightDynamicWidth` scaling.

`UpdateViewportFromWindow()` (called by `Present()` every frame) detects a size change by comparing
against the last renderer-derived size kept in dedicated fields, not against `Viewport`'s own
width/height, so a custom sub-region `Viewport` (e.g. split-screen) survives frames without an
actual resize and is reset only by a real `ApplyChanges()`-driven backbuffer resize.

Native regressions: `EasyGL_RealWindowResize`, `EasyGL_ViewportResetAfterResize` and
`WebGPU_RealWindowResize`. The pure `ViewportTest` suite covers offsets, bounds, depth-range
arithmetic, perspective and orthographic `Project`/`Unproject`, round trips and XNA string
formatting.

---

## 6. Non-desktop status: execution is real, adapter-value qualification is narrower

The display enumeration is platform-neutral and goes through CNA's platform display service; the
only host-specific code in `GraphicsAdapter.cpp` is the optional Linux sysfs PCI-ID query. That
shared implementation runs on both non-desktop targets, but an application launch is not a
dedicated oracle for every returned adapter value.

| Target | What is verified | What is not yet qualified |
|---|---|---|
| Web / Emscripten | Real `emcmake`/`emcc` builds; EasyGL `WEBGL2` applications in Chrome (including threaded content loading and context loss); browser `WEBGPU` 2D, 3D, stock/compiled effects, canvas resize and readback; one `WEBGL2;WEBGPU` bundle selected both at runtime. These application paths create the SDL window and CNA graphics device rather than stopping at link time. | No browser-specific test asserts the exact `GraphicsAdapter` count/name, `DisplayModeCollection`, monitor handle or `IsWideScreen` result. A browser surface is a canvas, not an OS display-mode switcher, so those values remain SDL/browser-shaped rather than desktop-qualified. |
| Android | CNA builds into an installable APK and runs through the default `OPENGLES3` renderer on an x86_64 emulator; the devices demo and cna-cs samples render real frames. | No physical Android device has run CNA, `SDLGPU` is unqualified on Android, and no Android-specific oracle records exact adapter/mode/viewport values. Emulator execution proves the shared window/device path, not a multi-device display matrix. |

The browser details are in `web-emscripten-graphics-limitations.md`, `webgl2-renderer.md`,
`webgpu-renderer.md` and `runtime-renderer-selection.md`. Android evidence and its boundary are in
`android-graphics-limitations.md` and `devices-build.md` §4.1.

The open items here are platform validation coverage: exact browser/Android adapter-value
assertions, physical Android hardware, and the Android `SDLGPU` route.
