# Viewport / DisplayMode / GraphicsAdapter support matrix

> **Current-state reconciliation, 2026-10-31:** this page preserves the Phase 40 findings and now
> includes the later adapter-lifecycle, XNA widescreen and device-selection corrections. Its former
> claim that Android and Web had never executed CNA is obsolete: `OPENGLES3` has run on an Android
> emulator, and both `WEBGL2` and browser `WEBGPU` have run in Chrome. The exact browser/Android
> `GraphicsAdapter` value surface is still not covered by a dedicated platform oracle (§6).

Phase 40 (graphics Tasks 341–350) audited `Viewport`, `DisplayMode`,
`DisplayModeCollection`, and `GraphicsAdapter` against FNA, verified the real window-resize and
render-target-switch chains end-to-end, and fixed several real bugs. This document records those
findings together with the current cross-platform boundary.

---

## 1. Viewport property-convention fix and math audit (Tasks 341–344)

`Project`/`Unproject` math, `AspectRatio`, `Bounds`, and `TitleSafeArea` already matched FNA
exactly — no math bug found. One real, fixed `CLAUDE.md` violation: `X`, `Y`, `MinDepth`,
`MaxDepth` were public raw fields while `Height`/`Width` in the same class already used the
project's `getXProperty()`/`setXProperty()` convention — converted all four to match. Added
non-identity-matrix `Project`/`Unproject` tests (the only prior tests used identity matrices,
which never exercise the perspective-divide branch) and confirmed `MinDepth`/`MaxDepth` have zero
validation/clamping in both FNA and CNA, by design (matches FNA's own unguarded arithmetic).

## 2. `DisplayModeCollection` `SurfaceFormat` indexer (Tasks 345/347)

Task 345's `GraphicsAdapter` audit found FNA has a real `this[SurfaceFormat format]` indexer on
`DisplayModeCollection` that CNA was missing entirely. Task 347 added it as a second
`operator[](SurfaceFormat)` overload (coexisting with the pre-existing `operator[](int)`), and
retroactively wrapped CNA's own `getCountProperty()`/integer indexer in `CNAEXT` (neither exists in
FNA's real API).

## 3. `GraphicsAdapter` enumeration and lifetime (Tasks 345–346 and later fixes)

Four real, fixed findings from a line-by-line audit against FNA's `GraphicsAdapter.cs` and
`SDL3_FNAPlatform.cs`:

- `AdaptersChanged()` used the same string for both `DeviceName` and `Description` — FNA gives
  them different values (`DeviceName` = a synthetic Windows-style path, `Description` = the real
  display name). Fixed to match.
- Display-mode enumeration never deduplicated same-resolution/different-refresh-rate entries and
  iterated forward; FNA iterates in **reverse** and dedupes by `(width,height)`. Fixed.
- **The most severe finding**: `static GraphicsAdapter& DefaultAdapter` was a raw C++ reference
  bound once at static-init time. Since `AdaptersChanged()` clears and repopulates `adapters_` on
  every call (an intended, FNA-documented usage for display reconfiguration), any later call left
  `DefaultAdapter` — and its 2 production call sites — dangling. Fixed by removing the field
  entirely in favor of the already-correct `getDefaultAdapterProperty()`.
- Stale doc comments claimed `DeviceId`/`VendorId` were "not implemented" when they're actually
  backed by a real Linux `/sys/class/drm` PCI query. Corrected.

Task 346 verified the no-display fallback: when the platform exposes no display service or no
display, `AdaptersChanged()` creates one synthetic 800×480 `Default Display` adapter. Its original
interpretation of an uninitialized video subsystem as a normal reason to take that fallback is no
longer current.

`BINDFIX-001` later established the actual lifecycle contract. `AdaptersChanged()` raises and pins
the active platform's video subsystem **before** enumeration, so ordinary call ordering cannot
silently cache the fallback on a host with a real display. The pin stays alive with the cache;
after a platform/video-session transition, an adapter whose native display ID expired rebinds by
display name instead of reporting a fictional 800×480 mode. `SOFTWARE-157` made the pin follow the
ambient platform lifecycle, and `RUST-UPSTREAM-023` serialized cache refresh/use so concurrent
device construction cannot free another thread's adapter.

The current deterministic platform tests cover names and native IDs, reverse-order mode
enumeration, refresh-rate deduplication, current-mode fallback, absent display services and empty
enumerations. `getDefaultAdapterProperty()` is deliberately re-evaluated after every
`AdaptersChanged()`; callers must not retain an adapter reference across a refresh.

## 4. Post-Phase 40 XNA corrections

### 4.1 Widescreen threshold (`BINDFIX-033`)

The Phase 40 FNA comparison did not catch a real XNA/FNA disagreement. FNA guesses that a mode is
widescreen above 4:3; recovered XNA 4.0 code compares strictly greater than **1.6**. CNA now follows
XNA: 3:2 and exactly 16:10 are not widescreen, while a value just above 1.6 is. The decision is
recorded in `xna-fna-behavior-decisions.md` and pinned independently of the host display by
`GraphicsAdapterTest.IsWideScreenUsesXnasLimitOfOnePointSixExclusive`.

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

`IsProfileSupported`, `QueryRenderTargetFormat` and `QueryBackBufferFormat` now use the selected
renderer registry's static adapter-query hooks. DirectX9 answers profile, target/backbuffer format
and MSAA questions from real D3D9 caps; the GLES/WebGL EasyGL profiles publish only their guaranteed
pre-context render-target set, and several families publish their honest MSAA clamp. A missing hook
uses the explicitly documented framework/profile fallback instead of inventing a hardware
capability table. The current per-renderer results belong in
`graphics-renderer-feature-matrix.md`, not in this platform-history page.

## 5. Real window-resize and backbuffer-resize verification (Tasks 348–349)

Task 348 traced the actual OS-level resize chain (`SDL_EVENT_WINDOW_RESIZED` → `GameWindow` →
`GraphicsDeviceManager` → `GraphicsDevice::UpdateViewportFromWindow()`) end-to-end with a real
`SDL_SetWindowSize()` test, and confirmed a deliberate, documented CNA divergence from FNA:
`PresentationParameters.BackBufferWidth`/`Height` do **not** follow a real window resize (only
`Viewport` does, via `FixedHeightDynamicWidth` scaling) — this is intentional, not a bug.

Task 349 found and fixed a real bug while checking whether a `GraphicsDeviceManager::ApplyChanges()`
-driven backbuffer resize correctly resets a previously-set **custom sub-region** `Viewport` (e.g.
split-screen): `UpdateViewportFromWindow()` decided "did the size change?" by comparing against
`Viewport`'s own current width/height — so a custom `Viewport` was silently stomped back to
full-window size on the very next frame (`Present()` calls this every frame), even with no actual
resize. Fixed by tracking the last-known renderer-derived size in dedicated fields instead of
diffing against `Viewport` itself.

The current native regressions are `EasyGL_RealWindowResize`,
`EasyGL_ViewportResetAfterResize` and `WebGPU_RealWindowResize`. The pure `ViewportTest` suite
separately covers offsets, bounds, depth-range arithmetic, perspective and orthographic
`Project`/`Unproject`, round trips and XNA string formatting.

---

## 6. Non-desktop status: execution is real, adapter-value qualification is narrower

The display enumeration itself is platform-neutral and goes through CNA's current platform display
service. The only host-specific code in `GraphicsAdapter.cpp` is the optional Linux sysfs PCI-ID
query. That shared implementation has now executed on both non-desktop targets, but an application
launch is not the same evidence as a dedicated oracle for every returned adapter value.

| Target | What is verified | What is not yet qualified |
|---|---|---|
| Web / Emscripten | Real `emcmake`/`emcc` builds; EasyGL `WEBGL2` applications in Chrome (including threaded content loading and context loss); browser `WEBGPU` 2D, 3D, stock/compiled effects, canvas resize and readback; one `WEBGL2;WEBGPU` bundle selected both at runtime. These application paths create the SDL window and CNA graphics device rather than stopping at link time. | No browser-specific test asserts the exact `GraphicsAdapter` count/name, `DisplayModeCollection`, monitor handle or `IsWideScreen` result. A browser surface is a canvas, not an OS display-mode switcher, so those values remain SDL/browser-shaped rather than desktop-qualified. |
| Android | CNA builds into an installable APK and runs through the default `OPENGLES3` renderer on an x86_64 emulator. The devices demo and cna-cs samples launched and rendered real frames; the old missing-Gradle and missing-KVM blockers are closed. | No physical Android device has run CNA, `SDLGPU` is unqualified on Android, and no Android-specific oracle records exact adapter/mode/viewport values. Emulator execution proves the shared window/device path, not a multi-device display matrix. |

The authoritative browser details are `web-emscripten-graphics-limitations.md`,
`webgl2-renderer.md`, `webgpu-renderer.md` and `runtime-renderer-selection.md`. Android evidence and
its remaining boundary are in `android-graphics-limitations.md` and `devices-build.md` §4.1.

There is no open Phase 40 implementation item in this section. The remaining entries are platform
validation coverage: exact browser/Android adapter-value assertions, physical Android hardware,
and the Android `SDLGPU` route.

---

Phase 40 (graphics Tasks 341–350) remains closed; this reconciliation removes the later-invalidated
platform status from its historical record.
