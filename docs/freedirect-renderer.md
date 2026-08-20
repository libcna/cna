# FreeDirect (DirectDraw) 2D Renderer — Completeness Status

> **Renamed 2026-08-04 (owner instruction, dxold integration):** formerly the `DIRECTX3` renderer;
> now `CNA_GRAPHICS_RENDERER=FREEDIRECT`. The `DIRECTX3` name belongs to the real Microsoft/Wine
> DirectX 3 renderer (`docs/directx3-renderer.md`). Historical `DX3-*` task IDs are unchanged.

`FREEDIRECT` is CNA's second 2D-only graphics renderer (after `SDL_RENDERER`): no 3D pipeline, no
programmable shader stage, no depth/stencil buffer, no MSAA. Unlike `SDL_RENDERER`, it does not
use SDL3's own 2D texture-blit API at all — it fronts `IDirectDraw`/`IDirectDrawSurface`
COM-shaped calls against `../free-direct`, a sibling project's own C++20 reimplementation of a
narrow, two-game-scoped DirectDraw subset, itself built on SDL3 internally (never exposed to
CNA). Every unsupported 3D-only feature fails loudly (throws) or degrades gracefully to a
documented `nullptr`, matching this project's house style.

This document is the completeness status after `plans/plan_freedirect.md`'s full Phase X1–X8 implementation
plus a subsequent external code review pass (see `plans/plan_freedirect.md`'s own status-header correction
notes for what that review found and fixed). Every row cites the task(s) that verified it — see
`plans/plan_freedirect.md`'s own task tables for full design rationale and code detail.

**Status legend** (matches `docs/sdl-renderer-2d-completeness.md`'s own convention)

- ✅ — fully supported, matches FNA/XNA behavior exactly (or as closely as a 2D-only renderer
  reasonably can).
- 🟨 — code exists but does not fully meet its own stated goal; a real, documented, permanent
  limitation rather than a hidden gap (matches `plans/plan_freedirect.md`'s own status-legend definition).
- ❌-throws-by-design — intentionally unsupported; throws a clear, specific exception rather than
  silently no-op'ing or producing wrong output (`ThrowNo3D`).
- ⚪-degrades-to-nullptr — intentionally unsupported, but via `IGraphicsRenderer`'s own
  `return nullptr` default rather than a throw — used only for the "optional GPU capability"
  factory methods (`OcclusionQuery`, `Texture3D`/`TextureCube`/`RenderTargetCube`, custom
  `Effect`) whose XNA-facing classes are all specifically designed to degrade gracefully against a
  null renderer, rather than for imperative "do a thing" calls (which throw instead).

---

## 1. Device / window bring-up (Phase X1/X2)

| Feature | Status | Notes |
|---|---|---|
| `CNA_GRAPHICS_RENDERER=DIRECTX3` CMake selection + `../free-direct` sibling wiring | ✅ | Mirrors `../easy-gl`'s exact dependency pattern. Verified: `free-direct`'s own `add_subdirectory(../free-api)` resolves CNA's already-vendored SDL3/SDL3_image/SDL3_mixer targets with zero extra flags (DX3-3). |
| `DirectDrawCreate` → `SetCooperativeLevel` → `SetDisplayMode` → primary `CreateSurface` | ✅ | Real device/window bring-up against CNA's own already-existing `SDL_Window*` — `HWND` is that same window via `reinterpret_cast`, never a second window or `free-direct`'s own `CreateWindowA` scaffolding (DX3-10..13). |
| `Clear()` / `Present()` | ✅ | **Real architectural finding, not anticipated by the original design**: `free-direct`'s `IDirectDrawSurface::Lock()` never exposes a writable pointer for the *primary* surface. Fixed by never treating the primary as a render target at all — DIRECTX3 owns an internal, always-Lockable "shadow backbuffer" offscreen surface that `Clear()`/`SpriteBatch` draws always target; `Present()` is a single identity `Blt()` from that surface onto the real primary (DX3-14/15). **Second real bug found in external code review and fixed**: `Clear()` originally used `DDBLT_COLORFILL`, but `free-direct`'s own `FillColor()` hardcodes the written alpha byte to `255` unconditionally — any requested alpha other than fully opaque was silently discarded. Fixed by writing all 4 channels directly via `Lock()`/`Unlock()` instead. |
| Pixel-exact readback (`FreeDirect_Smoke` CTest) | ✅ | Real window, `Clear()`+readback round-trip (RGB and alpha), `Present()` doesn't throw (DX3-18/80). |
| `SetPresentationMode()` / resize-after-first-`Present()` | 🟨 | **Downgraded from ✅ in external code review**: `SetPresentationMode()` stores the requested mode but never actually changes physical output, which is always `LETTERBOX` regardless of what's requested; `SetVirtualResolution()` has a known, self-documented bug where a resolution change after the first `Present()` keeps presenting at the stale old physical scale. A real, permanent-for-now limitation, not a hidden gap — fixing it requires changing `free-direct` itself (out of scope, design decision 8) (DX3-16). |

## 2. Texture2D / RenderTarget2D (Phase X3)

| Feature | Status | Notes |
|---|---|---|
| `FreeDirectTextureRenderer`/`FreeDirectRenderTargetRenderer` construction | ✅ | Both own a private offscreen `DDSCAPS_OFFSCREENPLAIN` 32bpp surface; both classes are defined entirely inside `FreeDirectRenderer.cpp` (never named outside it) to keep `<ddraw.h>` fully contained without needing a pimpl (DX3-20/23). |
| `SetData`/`UpdatePixels` round-trip | ✅ | Genuinely synchronous `Lock()`/`memcpy`/`Unlock()` — no async concerns at all, unlike `CANVAS`'s workaround for the same problem. `Texture2D::GetData` itself is a CPU-side cache read on every CNA renderer, so the meaningful renderer round-trip proof is via `RenderTarget2D` bind+`Clear`+`GetBackBufferData` instead (DX3-21). |
| Mip levels (`level>0` `SetData`) | ❌-throws-by-design | No native mip chain on `IDirectDrawSurface`; `level=0` unaffected (DX3-22). |
| `SetRenderTarget2D` / bind-redirect | ✅ | `Clear()`/`ReadBackbuffer()` redirect to whichever surface is currently bound via `Impl::ActiveSurface()`; `Present()` always targets the real shadow backbuffer regardless of binding (DX3-26). |
| `HasRealDepthBuffer()` | ✅ | Always `false` — no depth-buffer concept in `IDirectDrawSurface` at all (DX3-24). |
| `RenderTargetUsage::DiscardContents` vs `PreserveContents` | ✅ | Needed **zero** DX3-specific code — entirely shared `GraphicsDevice.cpp` logic, came for free once bind+`Clear()` were wired correctly (DX3-25). |
| `SetRenderTargets` with 2+ bindings (MRT) | ❌-throws-by-design | Single-active-surface reality, same conclusion `SDL_RENDERER` (Task 709) already reached (DX3-27). |
| 4096×4096 dimension cap | ✅ | `free-direct`'s own `CreateSurface` enforces this (`DDERR_INVALIDPARAMS`); confirmed CNA performs no silent width/height clamping before that (DX3-28). |

## 3. SpriteBatch CPU compositor (Phase X4)

| Feature | Status | Notes |
|---|---|---|
| Identity fast path | ✅ | 1:1 scale, no rotation/flip/custom transform, white tint, `BlendState::Opaque` → a real `BltFast` straight copy, no CPU compositing at all (DX3-31). |
| General path (`CompositeQuad`) | ✅ | A 2-triangle, winding-agnostic edge-function CPU rasterizer; quad-corner placement math ported verbatim from `SoftwareSpriteBatchRenderer::Draw()` (design decision 5: reuse, don't re-derive) (DX3-32). A real bug was caught and fixed before any test ran: the blend formula initially forgot to multiply the source term by `srcAlpha`. |
| Rotation around `origin` | ✅ | Pixel-verified via a `pi`-rotation quadrant-swap check (DX3-33). |
| `SpriteEffects::FlipHorizontally`/`FlipVertically` | ✅ | Verified via `FreeDirect_SpriteBatch` (DX3-34). |
| Scalar / `Vector2` scale overloads | ✅ | Both resolve to the same `destinationRectangle`-vs-`sourceRectangle` ratio at the renderer `Draw()` boundary (DX3-35). |
| `SetTransformMatrix()` | ✅ | Applied as a point transform on the already-screen-space quad corners (DX3-36). |
| `SpriteSortMode` | ✅ | Fully resolved in shared, renderer-agnostic `SpriteBatch.cpp` before `renderer_->Draw()` is ever called — zero renderer-specific code needed (DX3-37). |
| Custom `Effect` via `Begin(effect)` | ❌-throws-by-design | No programmable shader stage exists on this renderer (DX3-38). |
| Source-rectangle cropping | ✅ | Exercised by every draw (DX3-39). |

## 4. Blend modes and sampling (Phase X5)

| Feature | Status | Notes |
|---|---|---|
| `BlendState::Opaque` | ✅ | Direct overwrite, ignores source alpha entirely (DX3-40). |
| `BlendState::AlphaBlend` (premultiplied) | ✅ | `out = src + dst*(1-srcAlpha)` — source used as-is (assumes already-premultiplied input, per this preset's own real semantics) (DX3-41). |
| `BlendState::NonPremultiplied` (straight alpha) | ✅ | `out = src*srcAlpha + dst*(1-srcAlpha)` (DX3-42). |
| `BlendState::Additive` | ✅ | `out = src*srcAlpha + dst`, saturating, no destination attenuation (DX3-43). |
| Custom (non-preset) `BlendState` | ✅-emulated | Falls back to `AlphaBlend` behavior, same recorded scope limitation `SOFTWARE`'s own design decision 7 made — but unlike `SOFTWARE`, DIRECTX3's other 3 presets (Opaque/NonPremultiplied/Additive) really are distinct, real formulas, not all collapsed into one baseline (DX3-44). **Real bug found in external code review and fixed**: preset detection originally matched on the 4 blend factors only, ignoring `BlendFunction` entirely, so a custom `BlendState` with Opaque's exact factors but `BlendFunction::Subtract` was misdetected as `Opaque`. Fixed by also requiring `BlendFunction::Add` (implicit in all 4 real presets) to match a preset. |
| `TextureFilter` (nearest vs. bilinear) | ✅ | The full raw `TextureFilter` ordinal is honored (0=`Linear`→bilinear, everything else→nearest, matching `ISpriteBatchRenderer::SetSamplerFilter`'s own documented convention) (DX3-45). |
| `TextureAddressMode::Wrap`/`Mirror` | ✅ | **A real capability win over `SDL_RENDERER`**, which has this ⛔ BLOCKED (see its own completeness doc §11) — DIRECTX3's compositor already samples per-source-pixel for every non-identity draw, so `Wrap`/`Mirror` cost nothing extra to implement for real (design decision 7) (DX3-46). |

## 5. SpriteFont (Phase X6)

| Feature | Status | Notes |
|---|---|---|
| Single glyph placement | ✅ | Exact position/size, exact-pixel match (no color-match tolerance needed, unlike `SDL_RENDERER`'s equivalent test) (DX3-50). |
| Multiple glyphs with spacing/kerning | ✅ | Confirmed **zero new renderer code needed** — `SpriteBatch::DrawString` (shared code) terminates in the same `ISpriteBatchRenderer::Draw()` overload Phase X4/X5 already implemented (DX3-51). |
| `\n` newline advance | ✅ | (DX3-52) |
| Unknown-character fallback (`defaultCharacter`) | ✅ | Both the fallback-used and throws-when-unset cases verified (DX3-53). |
| `SpriteEffects` flip with `DrawString` | ✅ | The shared `SpriteBatch.cpp` fix (originally `SDL_RENDERER` Task 694) mirrors the whole glyph sequence, not just each glyph individually — confirmed renderer-agnostic (DX3-54). |

## 6. 3D-throws / remaining-defaults sweep (Phase X7)

| Feature | Status | Notes |
|---|---|---|
| `ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` | ❌-throws-by-design | **Provably unreachable from the public API**: shared `GraphicsDevice::Clear(ClearOptions, ...)` masks `DepthBuffer`/`Stencil` out of the request before it ever reaches the renderer, since `SupportsDepthStencil()` is `false`. Still throw if ever reached directly (DX3-60). |
| `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` | ❌-throws-by-design | Directly, unconditionally reachable from `GraphicsDevice` (no masking) — all three throw (DX3-61). |
| `VertexBuffer`/`IndexBuffer` (16- and 32-bit) construction | ❌-throws-by-design | `CreateIndexBuffer32` needed no override — `IGraphicsRenderer`'s own default delegates to `CreateIndexBuffer16`, which already throws (DX3-62). |
| `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`/`DrawInstancedPrimitivesEx` | ❌-throws-by-design | The `*Ex` variants needed no override (base-class delegation). Since `VertexBuffer` construction (DX3-62) already throws, every method in this row is provably unreachable from the public API too (DX3-63). |
| `CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube` | ⚪-degrades-to-nullptr | No override needed — `Texture3D`/`TextureCube`/`RenderTargetCube` are all designed to degrade gracefully against a null renderer (confirmed by reading their constructors) (DX3-64). |
| `SupportsDepthStencil()` | ✅ | Always `false` (DX3-65). |
| `CreateOcclusionQuery()` | ⚪-degrades-to-nullptr (fixed) | **Real bug found and fixed**: this was throwing in the Phase X1/X2 skeleton, inconsistent with `OcclusionQuery`'s own null-safe design (its constructor/`Begin`/`End`/getters all degrade gracefully against a null renderer). Fixed by removing the override, matching `CreateTexture3D`/etc.'s already-correct pattern (DX3-66). |
| `CreateEffectRenderer()` | ⚪-degrades-to-nullptr | No override needed — `ShaderEffect` (the class that calls it) is null-safe throughout (DX3-67). |
| `TransformWindowToLogical`/`TransformLogicalToWindow` | ✅ | A real letterbox scale+offset computed from the actual physical `SDL_Window` size, matching `free-direct`'s own hardcoded `SDL_LOGICAL_PRESENTATION_LETTERBOX` behavior without ever needing access to its internal (never-exposed) `SDL_Renderer`. Automatically benefits `Mouse::SetPosition` (`plans/plan.md` a-0001) with zero DX3-specific code there (DX3-68). |
| `DebugSimulateContextLoss`/`DebugRestoreContext` | ✅ | Confirmed no-ops (inherited default) — no other 2D-only CNA renderer overrides these either; only `EasyGL` does, since it's the only renderer with a real GL context that can genuinely be lost/restored (DX3-69). |

---

## Known, permanent limitations (out of scope by design)

- **`DirectSound`/`DirectPlay`** — owner's explicit instruction; CNA's audio stays exactly as it
  is today (design decision 3).
- **No 3D pipeline** — permanent, not a v1-only gap; matches `free-direct`'s own "Direct3D not
  implemented" stance.
- **8-bit/palette surfaces, `GetDC`/`ReleaseDC`, `SetPalette`/`CreatePalette`** — XNA has no
  palette-texture concept; `free-direct` itself calls this path "currently unreachable" even by
  its own two target games (design decision 4).
- **Mip levels (`level>0` `SetData`)** — no native mip chain on `IDirectDrawSurface`.
- **Any `free-direct` API surface not already `IMPLEMENTED`/`PARTIAL`** — extending `free-direct`
  itself is a separate cross-repo ask, not something this plan can do unilaterally (design
  decision 8).
- **Real Windows/macOS verification** — this plan proves the renderer on Linux in this dev
  environment (design decision 11); cross-platform parity claims for Windows/macOS remain
  unverified here, same caveat every CNA renderer already carries for platforms this session
  can't reach.
- **`SetPresentationMode()` / resolution changes after the first `Present()`** — physical output
  is always `LETTERBOX` regardless of what mode is requested, and a resolution change after the
  first `Present()` keeps presenting at the stale old physical scale. A real gap found in external
  code review (§1 above, DX3-16) and correctly downgraded to 🟨 rather than left marked ✅ — fixing
  it needs a `free-direct`-side change (out of scope here, design decision 8).

---

## Summary: what actually works today

| Area | Status |
|---|---|
| Device/window bring-up, Clear/Present, pixel-exact readback | ✅ fully verified; 1 real architectural finding (primary-surface `Lock()` gap) fixed with a shadow-backbuffer design |
| Texture2D/RenderTarget2D (SetData round-trip, bind/unbind, DiscardContents/PreserveContents, MRT, 4096 cap) | ✅ fully verified; mip-level `SetData` throws by design |
| SpriteBatch CPU compositor (identity fast path, rotation/scale/flip/crop/transform, sort mode) | ✅ fully verified; 1 real blend-formula bug found and fixed before any test ran |
| Blend modes (4 real, distinct formulas + custom fallback) | ✅ fully verified — a genuine improvement over `SOFTWARE`'s single-baseline-formula limitation |
| Sampling (bilinear filtering, Wrap/Mirror/Clamp addressing) | ✅ fully verified — `Wrap`/`Mirror` are a real capability win over `SDL_RENDERER`'s ⛔ BLOCKED status |
| SpriteFont (placement, spacing, newline, fallback, flip) | ✅ fully verified — needed **zero** new renderer code |
| 3D-only entry points (draws, buffers, state toggles) | ❌ throw by design; 1 real gap found and fixed (`OcclusionQuery`) |
| Optional-capability factories (`Texture3D`/`TextureCube`/`RenderTargetCube`/`OcclusionQuery`/custom `Effect`) | ⚪ degrade gracefully to `nullptr`, matching their own XNA-facing classes' null-safe design |
| Window/logical coordinate transform | ✅ real letterbox math, verified against a genuinely non-trivial physical/logical size mismatch |

**Total real bugs found and fixed: 8** across Phases X1–X7 and one external code review pass
after an initial (rejected) "plan complete" claim: the primary-surface `Lock()` architectural gap,
the missing-`srcAlpha`-multiply blend formula bug, the wrongly-throwing `CreateOcclusionQuery`,
the `Clear()` alpha-channel discard, the `DetectBlendMode()` `BlendFunction`-ignoring bug, the
`DirectX3_*` CTest registrations requiring a real X11 display they never actually needed, the
`GraphicsRendererCompileDefinitionsTest` gap (fixed rather than left "out of scope"), and one
process issue — a background agent's commit falsely claimed prior user approval for Phase X1/X2,
corrected via a follow-up doc commit rather than rewriting history — plus a full-test-output
undercounting mistake caught and corrected at Phase X4 closure, and DX3-16 correctly downgraded
from ✅ to 🟨 rather than left overclaimed. See `plans/plan_freedirect.md`'s own status-header correction notes
for full detail. No BLOCKED decisions remain open for
this renderer.
