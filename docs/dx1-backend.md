# DX1 (real DirectDraw v1) 2D Backend — Completeness Status

`DX1` is CNA's third 2D-only graphics backend (after `SDL_RENDERER` and `DX3`): no 3D pipeline, no
programmable shader stage, no depth/stencil buffer, no MSAA. Unlike `DX3` (which fronts
`../free-direct`, a sibling project's own DirectDraw reimplementation), `DX1` talks to a **real
Windows `ddraw.h`**, using genuine COM `IDirectDraw`/`IDirectDrawSurface` **v1 interfaces only**
(never `IDirectDraw2+`/`IDirectDrawSurface2+`/`DDSURFACEDESC2`), cross-compiled via MinGW-w64 and
run under Wine — the same Route B delivery mechanism the shipping `D3D9`/`D3D11`/`D3D12` backends
already use. DirectX 1 (1995) shipped no Direct3D at all (added in DX2, 1996), so every 3D
`IGraphicsBackend` entry point throws `ThrowNo3D` because there genuinely is no Direct3D COM
interface to call — not merely by policy, as with `DX3`.

This document is the completeness status after `plan_dx1.md`'s full Phase O1–O8 implementation.
Every row cites the task(s) that verified it — see `plan_dx1.md`'s own task tables for full design
rationale and code detail.

**Status legend** (matches `docs/dx3-backend.md`'s own convention)

- ✅ — fully supported, matches FNA/XNA behavior exactly (or as closely as a 2D-only backend
  reasonably can).
- 🟨 — code exists but does not fully meet its own stated goal; a real, documented, permanent
  limitation rather than a hidden gap.
- ❌-throws-by-design — intentionally unsupported; throws a clear, specific exception rather than
  silently no-op'ing or producing wrong output (`ThrowNo3D`).
- ⚪-degrades-to-nullptr — intentionally unsupported, but via `IGraphicsBackend`'s own
  `return nullptr` default rather than a throw.

---

## 0. Existence-gate spike (`DX1-0`)

Run and recorded before any backend code was written (`plan_dx1.md` section 2), mirroring
`plan_dx9.md`'s `D9-0` discipline:

- MinGW-w64's `ddraw.h` genuinely defines the v1 `IDirectDraw`/`IDirectDrawSurface` vtables,
  `DDSURFACEDESC` (not `DDSURFACEDESC2`), and `DirectDrawCreate` — confirmed both by reading the
  header and by compiling a throwaway program against `-lddraw -ldxguid`.
- A real, running program (real Win32 window → `DirectDrawCreate` → `SetCooperativeLevel
  (DDSCL_NORMAL)` → primary/offscreen `CreateSurface` → `Lock`/`Unlock`/`Blt`) succeeds end to end
  under a fresh, vanilla Wine prefix (no DXVK, no `../free-direct`).
- **Real finding, not anticipated by the original design**: unlike `DX3`'s `../free-direct` (whose
  `Lock()` never exposes a writable pointer for the primary surface at all), real Wine `ddraw.dll`'s
  `Lock()` on the primary genuinely succeeds — but the primary surface it hands back is
  **desktop-sized**, not window-sized (matching real historical DirectDraw semantics: the primary
  *is* the display). This shaped `Present()`'s design (§1).

## 1. Device / window bring-up (Phase O1/O2)

| Feature | Status | Notes |
|---|---|---|
| `CNA_GRAPHICS_BACKEND=DX1` CMake selection, Windows-only gate, MinGW cross-compile | ✅ | Same `FATAL_ERROR` gate `D3D9`/`D3D11`/`D3D12` already share (design decision 1) — unlike `DX3`, this backend cannot build natively on Linux. |
| `DirectDrawCreate` → `SetCooperativeLevel(DDSCL_NORMAL)` → primary `CreateSurface` | ✅ | Real device/window bring-up against a **real Win32 `HWND`**, obtained via `SDL_GetPointerProperty(..., SDL_PROP_WINDOW_WIN32_HWND_POINTER, ...)` on CNA's own already-existing `SDL_Window*` — the same mechanism `D3D9GraphicsBackend.cpp` uses, never `DX3`'s `reinterpret_cast<HWND>(sdlWindow)` hack (design decision 3). No `SetDisplayMode` call: windowed mode never needs one (`DX1-0c`). |
| `Clear()` / `Present()` | ✅ | Owns an internal, always-Lockable "shadow backbuffer" offscreen surface that `Clear()`/`SpriteBatch` draws always target (design decision 4) — the same shadow-buffer *shape* `DX3` uses, but for a different reason here (the primary is desktop-sized, not because `Lock()` fails). `Present()` letterbox-scales the shadow buffer onto the primary via a single `Blt()`, with the destination rect recomputed **every frame** from the window's real client area (`GetClientRect`+`ClientToScreen`) — a genuine correctness improvement over `DX3`'s own documented stale-scale bug (`plan_dx3.md` DX3-16): a `SetVirtualResolution()`/window-resize change is correct on the very next `Present()`, since nothing here is cached. `Clear()` writes all 4 channels directly via `Lock()`/`Unlock()` (`FillSurfaceColor`), not `DDBLT_COLORFILL`, proactively avoiding the class of bug `DX3` found and fixed for its own alpha handling. |
| Pixel-exact readback (`Dx1_Smoke` CTest) | ✅ | Real window, `Clear()`+readback round-trip (RGB and alpha) via the shadow backbuffer, `Present()` doesn't throw. 4/4 checks. |
| `SetPresentationMode()` | 🟨 | `Present()` always applies a letterbox-equivalent uniform scale (`ComputeLetterbox`) regardless of the requested mode — `Stretch`/`Overscan`/`NativeBackBuffer` are not yet distinguished, the same honest scope `DX3-16` recorded. Unlike `DX3`, this is a real, first-class implementation choice (not an inherited third-party limitation), and the stale-after-resize sub-bug does **not** reproduce here. |
| `TransformWindowToLogical`/`TransformLogicalToWindow` | ✅ | Real letterbox scale+offset transform (`ComputeLetterbox`), shared with `Present()` itself so the two are always mutually consistent (`DX1-68`). Verified via `Dx1_LogicalTransform` CTest (5 checks). |

## 2. Texture2D / RenderTarget2D (Phase O3)

| Feature | Status | Notes |
|---|---|---|
| `Dx1TextureBackend`/`Dx1RenderTargetBackend` construction | ✅ | Both own a private offscreen `DDSCAPS_OFFSCREENPLAIN` 32bpp surface; both classes are defined entirely inside `Dx1GraphicsBackend.cpp` (never named outside it), keeping `<ddraw.h>` fully contained (`DX1-20`/`23`). |
| `SetData`/`UpdatePixels` round-trip | ✅ | Genuinely synchronous `Lock()`/`memcpy`/`Unlock()` (`DX1-21`). |
| Mip levels (`level>0` `SetData`) | ❌-throws-by-design | No native mip chain on `IDirectDrawSurface`; `level=0` unaffected (`DX1-22`). |
| `SetRenderTarget2D` / bind-redirect | ✅ | `Clear()`/`ReadBackbuffer()` redirect to whichever surface is currently bound via `Impl::ActiveSurface()`; `Present()` always targets the real shadow backbuffer regardless of binding (`DX1-26`). |
| `HasRealDepthBuffer()` | ✅ | Always `false` (`DX1-24`). |
| `RenderTargetUsage::DiscardContents` vs `PreserveContents` | ✅ | Entirely shared `GraphicsDevice.cpp` logic, came for free (`DX1-25`). |
| `SetRenderTargets` with 2+ bindings (MRT) | ❌-throws-by-design | Single-active-surface reality (`DX1-27`). |
| Dimension cap | ✅ (no artificial cap) | **Real finding, spike-confirmed rather than assumed, and the opposite of `DX3`'s own result**: unlike `../free-direct`'s hardcoded 4096×4096 `CreateSurface` cap, real Wine `ddraw.dll` has no such ceiling — a dedicated spike succeeded up to 16384×16384 offscreen surfaces and only failed at 65536×65536 (`E_INVALIDARG`). A 4096×4096 `Texture2D` (XNA's own real `HiDef`-profile ceiling) succeeds without throwing. CNA does **not** enforce a `GraphicsProfile`-based size ceiling outside the `D3D9` backend (`Texture2D.cpp`'s own `ValidateTextureSizeForProfileEXT` is `#ifdef CNA_BACKEND_D3D9`-only) — an honest, documented gap here, not silently dropped (`DX1-28`). |

## 3. SpriteBatch CPU compositor (Phase O4)

| Feature | Status | Notes |
|---|---|---|
| Identity fast path | ✅ | 1:1 scale, no rotation/flip/custom transform, white tint, `BlendState::Opaque` → a real `BltFast` straight copy (`DX1-31`). |
| General path (`CompositeQuad`) | ✅ | Ported **verbatim** from `DX3`'s own already-verified 2-triangle edge-function CPU rasterizer (design decision 5) — `IDirectDrawSurface::Blt`/`BltFast` has never supported rotation in any DirectX version, so every DirectDraw-family backend needs the same architecture (`DX1-32`). |
| Rotation about `origin` | ✅ | **Real test bug found and fixed during this port's own first test run** (not a compositor bug — the compositor math was ported verbatim from `DX3` and is correct): the rotation-by-π check's "top-left" sample point was 2px in from the actual bounding-box corner, already inside the default bilinear (`Linear`) sampler's blend gradient for a 2×2 source texture — confirmed empirically (it read a blended `(199,223,27)`, not pure yellow). Fixed by moving the sample point 1px in from the corner (mirroring the adjacent Check F's own "+1,+1 from corner" convention, the zone where `Clamp` addressing saturates all 4 bilinear taps to the same texel) (`DX1-33`). |
| `SpriteEffects::FlipHorizontally`/`FlipVertically` | ✅ | (`DX1-34`) |
| Scalar / `Vector2` scale | ✅ | Resolved entirely in shared `SpriteBatch.cpp`, no backend-specific code (`DX1-35`). |
| `SetTransformMatrix()` | ✅ | Applied as a point transform on the already-screen-space quad corners (`DX1-36`). |
| `SpriteSortMode` | ✅ | Fully handled by shared `SpriteBatch.cpp` (`DX1-37`). |
| Custom `Effect` via `Begin(effect)` | ❌-throws-by-design | No programmable shader stage exists (`DX1-38`). |
| Source-rectangle cropping | ✅ | Exercised implicitly by every `Draw()` call in `Dx1_SpriteBatch` (`DX1-39`). |

`Dx1_SpriteBatch` CTest: 10/10 checks, including a second real bug found and fixed alongside the
rotation one — the zero-alpha check originally used `BlendState::AlphaBlend` (this codebase's
**premultiplied** preset, matching real XNA) with a `(255,0,0,0)` source texel, which is not
validly premultiplied data (a truly transparent premultiplied red pixel is `(0,0,0,0)`) — so the
full red channel legitimately bled through regardless of alpha, correct XNA behavior, not a
compositor bug. Fixed by switching the check to `BlendState::NonPremultiplied` (straight alpha),
the preset a zero-alpha source pixel actually needs to leave the destination untouched.

## 4. Blend-mode compositing math (Phase O5)

| Feature | Status | Notes |
|---|---|---|
| `Opaque` | ✅ | Direct overwrite (`DX1-40`). |
| `AlphaBlend` (premultiplied) | ✅ | `out = src + dst*(1-srcAlpha)` (`DX1-41`). |
| `NonPremultiplied` (straight alpha) | ✅ | `out = src*srcAlpha + dst*(1-srcAlpha)` (`DX1-42`). |
| `Additive` | ✅ | `out = src*srcAlpha + dst`, saturating (`DX1-43`). |
| Custom `BlendState` (non-preset) | ✅ (falls back to `AlphaBlend`) | Matches both factors **and** `BlendFunction::Add`, ported from `DX3-44`'s own bug-fixed logic (`DX1-44`). |
| `TextureFilter` (`Point`/`Linear`) | ✅ | (`DX1-45`) |
| `TextureAddressMode::Wrap`/`Mirror`/`Clamp` | ✅ | Real, low-cost win the same way `DX3` already demonstrated (per-source-pixel sampling makes this free) — a genuine advantage over `SDL_RENDERER`'s ⛔ BLOCKED status for the same modes (`DX1-46`). |

`Dx1_Blend` (5 checks) and `Dx1_AddressMode` (5 checks) CTests: all pass.

## 5. `SpriteFont` (Phase O6)

| Feature | Status | Notes |
|---|---|---|
| Single glyph, kerning, `\n`, `defaultCharacter` fallback, flip/rotation | ✅ | Confirmed to need **zero** new backend code beyond Phase O4's `Draw()` path, the same finding `DX3-50`/`51` made — `Dx1_SpriteFont` (5 checks) exists to prove that claim empirically. |

## 6. `ThrowNo3D` wiring (Phase O7)

| Feature | Status | Notes |
|---|---|---|
| `ClearColorAndDepth`/etc., `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`, `CreateVertexBuffer`/`CreateIndexBuffer16`, `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` | ❌-throws-by-design | `Dx1_No3D` CTest confirms both the graceful-degrade path (masked out before reaching the backend) and the direct-call throw path (`DX1-60`..`63`). |
| `CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube`/`CreateOcclusionQuery`/`CreateEffectBackend` | ⚪-degrades-to-nullptr | Deliberately never overridden — `IGraphicsBackend`'s own nullptr-returning default applies directly, ported from `DX3-64`/`66`/`67`'s own corrected final state, not re-discovered as a bug here (`DX1-64`/`66`/`67`). |
| `SupportsDepthStencil()` | ✅ (`false`) | (`DX1-65`) |
| `DebugSimulateContextLoss`/`DebugRestoreContext` | ✅ (no-op) | Inherited `IGraphicsBackend` default — no real "context" to lose in a CPU/DirectDraw compositor (`DX1-69`). |

`Dx1_No3D` (9 checks) and `Dx1_GraphicsCapability` CTests: all pass.

## 7. What actually works today

A CNA game built with `CNA_GRAPHICS_BACKEND=DX1` and MinGW-cross-compiled: creates a real window,
initializes a real `IDirectDraw` (v1) device against it, clears/presents/reads back pixels exactly,
creates textures and render targets, draws full `SpriteBatch` content (rotation, scale, tint, flip,
custom transform, all 4 blend modes, `Wrap`/`Mirror`/`Clamp` sampling, `Point`/`Linear` filtering),
draws `SpriteFont` text, and correctly maps window↔logical coordinates under letterbox scaling. Any
3D API call throws a clear `DX1 (DirectDraw v1) does not support 3D: <method>` message, or degrades
to a documented `nullptr` for the optional-capability factory methods.

## 7a. Full `CnaTests` regression (`DX1-88`)

A full `CnaTests` run (all ~5,400 tests, not just the 10 dedicated `DX1` CTests) through Wine on the
virtual display: **5336 passed, 11 skipped, 48 failed.** Every one of the 48 is confirmed
pre-existing and unrelated to DX1 itself — 34 are 3D-content-loading tests (`SkinnedModelEXTPartTest`,
`RuntimeGltfModelTest`, `CnjModelTest`/`CnjEffectTest`, `ModelContentTypeReaderTest`, …) hitting
DX1's correct `ThrowNo3D` via a plain `GraphicsDevice gd;` fixture with no 2D-backend gate — the
identical structural gap `plan_dx3.md`'s own regression already documented; 5 are
`GraphicsDeviceCapabilityTest`'s `SupportsThreeD`/etc. checks, which have **no backend gate at
all** and would fail identically under `DX3`/`SDL_RENDERER`/`ASCII`/`CANVAS`; 6 are
`MediaLibraryTestFixture` duration/metadata tests, a real consequence of `CNA_FFMPEG_AVAILABLE=OFF`
on every Windows target (predates this session); the remaining 4 are Windows/Wine filesystem or
non-ASCII-encoding quirks unrelated to graphics. See `plan_dx1.md` `DX1-88`'s own row for the full
per-category breakdown and the list of pre-existing CMake/test gaps found and fixed along the way
(none specific to DX1's own logic).

## 8. Known permanent limitations

- **No 3D pipeline, ever** — DirectX 1 has no Direct3D at all; this is not a v1-only gap.
- **`DirectSound`/`DirectInput`/`DirectPlay` are out of scope** — CNA's existing audio/input stack
  is untouched.
- **8-bit/palette surfaces, `GetDC`/`ReleaseDC`, `SetPalette`/`CreatePalette`** — XNA has no
  palette-texture concept.
- **Mip levels (`level>0` `SetData`)** — no native mip chain on `IDirectDrawSurface`.
- **Exclusive fullscreen (`DDSCL_EXCLUSIVE`)** — v1 scope is windowed (`DDSCL_NORMAL`) only.
- **`Stretch`/`Overscan`/`NativeBackBuffer` presentation modes** — `Present()` always applies a
  letterbox-equivalent uniform scale regardless of the requested mode.
- **`IDirectDraw2+` features of any kind** — permanently out of scope for the `DX1` name
  specifically; belongs to a later entry in `plan_dxold.md`'s roadmap.
- **Real Windows/macOS hardware verification** — this backend is proven via MinGW cross-compile +
  Wine on Linux in this dev environment, same caveat every Route-B CNA backend already carries.
- **Not held to the `D3D9` oracle bar** — a retro/alternative backend (peer to `DX3`/`ASCII`/
  `CANVAS`), validated by its own pixel checks, not by indistinguishability from real XNA 4.0.

## See also

- `plan_dx1.md` — the full implementation plan (design decisions, phase task tables).
- `plan_dxold.md` — the roadmap this backend is row 1 of (DX1/2/3/5/6/7/8/10).
- `docs/dx3-backend.md` — the shipping `../free-direct`-backed DX3, the architecture and math this
  backend ports verbatim wherever the surface-layer difference doesn't matter.
- `docs/directx-legacy-backends-analysis.md` — the feasibility analysis that authorized this whole
  backend family.
- `scripts/run-wine-dx1.sh`, `scripts/check-dx1-v1-only.sh` — this backend's own Wine wrapper and
  v1-only-discipline check.
