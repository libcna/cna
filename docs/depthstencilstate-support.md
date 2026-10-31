# DepthStencilState Support Matrix

Phase 37 (graphics Tasks 311–320) audited and pixel-verified `DepthStencilState`
conformance against FNA. This document summarizes the findings.

---

## 1. `DepthStencilState` API surface (Task 311)

The full 16-property surface, all 3 static presets (`Default`/`DepthRead`/`None`), and the
default-constructor values (`DepthBufferEnable=DepthBufferWriteEnable=true`,
`DepthBufferFunction=LessEqual`, `StencilEnable=false`,
`StencilFunction=CounterClockwiseStencilFunction=Always`, all 4 stencil-op fields `=Keep`,
`TwoSidedStencilMode=false`, `StencilMask=StencilWriteMask=Int32.MaxValue`, `ReferenceStencil=0`)
already matched FNA exactly.

One real, fixed finding: FNA's private preset constructor sets `Name` on every preset (e.g.
`"DepthStencilState.Default"`); CNA's didn't — the same gap already fixed in `SamplerState`
(Task 291) and `BlendState` (Task 301). Fixed, closing `DepthStencilState`'s portion of Task 866;
`RasterizerState`'s portion remains open for its own Phase 38 audit task.

## 2. Default graphics states on `GraphicsDevice` (Tasks 312/896/955)

FNA's `GraphicsDevice` constructor assigns all three named presets: `BlendState.Opaque`,
`DepthStencilState.Default`, and `RasterizerState.CullCounterClockwise`. Tasks 302/312 corrected
CNA's public member initialization, but that alone did not apply the values to the renderer.
Tasks 896/955 completed the contract: after resolving the renderer, CNA applies all three presets
through their ordinary property setters. A renderer without 3D support retains the same public
`DepthStencilState.Default` value without attempting a native depth-state call.

Before Task 955, EasyGL therefore exposed `DepthStencilState.Default` through the property while
OpenGL's real depth test remained disabled until a game explicitly assigned the state. The shared
`graphicsdevice_default_state_occlusion_test.cpp` deliberately calls none of the three state
setters: it draws a near quad before a fully overlapping far quad, then reverses draw order as a
positive control. This proves the constructed default reaches actual rendering before the first
draw rather than merely appearing in getters. The regression remains registered for EasyGL and
SDLGPU. The original cross-repository investigation and screenshots remain in Git history at
`3de312870`.

## 3. Depth testing (Tasks 313–314)

`DepthBufferWriteEnable` and all 5 `CompareFunction` values tested via differential pixel tests
(a "stamp" quad establishes a known depth, then a second quad's write/compare behavior is read
back via a third quad drawn at a depth chosen to give an unambiguous result).

| Property | EasyGL |
|---|---|
| `DepthBufferWriteEnable` | ✅ correct |
| `DepthBufferFunction` (`Less`/`LessEqual`/`Greater`/`Always`/`Never`) | ✅ all correct |

**Clip-space gotcha found while building these tests**: with an identity World/View/Projection (as
these tests use), the vertex Z value becomes clip-space Z directly. XNA/DirectX use a `[0, +w]`
clip-space Z range, not OpenGL's `[-1, +1]` — a negative Z silently clips away on a `[0, +w]`
backend. Any future identity-matrix depth/stencil pixel test in this project must keep Z within
`[0, 1]`.

## 4. Stencil testing (Tasks 315–319)

Tested across Tasks 315–319, always via a genuine contrast check (a lesson learned mid-phase, see
below):

| Property tested | Task | EasyGL |
|---|---|---|
| `StencilEnable` | 315 | ✅ |
| `StencilMask` / `StencilWriteMask` | 316 | ✅ |
| `StencilFail` / `StencilDepthBufferFail` / `StencilPass` (front-face ops) | 317 | ✅ |
| `TwoSidedStencilMode` / CCW back-face ops | 318 | ✅ |
| `ReferenceStencil` propagation via `DepthStencilState` | 319 | ✅ (fixed) |

**Test-design lesson (important for any future stencil test in this codebase)**: a test where
every check expects the *same* pass/fail outcome cannot distinguish "the feature genuinely works"
from "the stencil test is bypassed entirely" — both produce identical results. Task 317's first
draft fell into exactly this trap: an earlier 3-column version (one column per `StencilOperation`
slot, all expecting PASS) would also pass against a fully-bypassed stencil test, which always
"passes." A 4th column with a deliberately-wrong read-back reference (which a working
implementation must *reject*) was required to give the test real discriminating power. Task 318 applied this lesson from the start.

### Task 315's EasyGL fix

Before Task 315, no EasyGL window in this project ever requested `SDL_GL_STENCIL_SIZE` — the
stencil-related GL state-application code (`glStencilFunc`/`glStencilOp`/`glEnable(GL_STENCIL_TEST)`)
was always correct, but with zero stencil bits actually allocated in the framebuffer, the GL spec
says the stencil test trivially always passes, exactly mimicking a bypassed test. Fixed with a
one-line `SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8)` addition to `EasyGLRenderer`'s
context creation.

**Any stencil pixel test in this project also needs `PresentationParameters.DepthStencilFormat =
DepthFormat::Depth24Stencil8`** (set via `GraphicsDeviceManager` before the device is created) —
the default `DepthFormat::Depth24` has no stencil aspect at all.

## 5. `ReferenceStencil` (Task 319)

FNA's `GraphicsDevice.ReferenceStencil` is a real, independent device property
(`FNA3D_Get/SetReferenceStencil`), analogous to `GraphicsDevice.BlendFactor` (Task 309) — settable
without reassigning the whole `DepthStencilState`, and expected to immediately affect subsequent
stencil compares. (FNA's own source carries a `FIXME: Does this affect the value found in
DepthStencilState?` comment, confirming this is intentionally an override, not a read-through
proxy.)

Two distinct findings:

1. **Fixed** (same shape as Task 309): `GraphicsDevice::setDepthStencilStateProperty` never
   propagated an assigned state's own `ReferenceStencil` into `GraphicsDevice`'s own field, so
   `getReferenceStencilProperty()` could return stale data. Fixed.
2. **Confirmed broken, tracked as new Task 872, not fixed**:
   `GraphicsDevice::setReferenceStencilProperty` is a pure local no-op with **zero renderer
   connection** — `IGraphicsRenderer` has no `SetReferenceStencil` method at all; confirmed via a
   dedicated pixel test. Unlike `BlendFactor`'s case (Task 309), where the renderer-side method
   already existed and just wasn't being invoked, here there is no existing code path to wire up;
   fixing this needs new interface surface.

---

## Summary: what actually works today, per renderer

| Feature | EasyGL |
|---|---|
| `DepthStencilState` API/presets/`Name` | ✅ |
| Default `BlendState`/`DepthStencilState`/`RasterizerState` applied on `GraphicsDevice` construction | ✅ (fixed, Tasks 302/312/896/955) |
| `DepthBufferWriteEnable` | ✅ |
| `DepthBufferFunction` (all `CompareFunction` values) | ✅ |
| `StencilEnable` | ✅ (fixed, Task 315) |
| `StencilMask` / `StencilWriteMask` | ✅ |
| Front-face `StencilFail`/`StencilDepthBufferFail`/`StencilPass` | ✅ |
| `TwoSidedStencilMode` / back-face (CCW) ops | ✅ |
| `ReferenceStencil` via `DepthStencilState` assignment | ✅ (fixed, Task 319) |
| `ReferenceStencil` independent override (no state reassignment) | ❌ (Task 872) |
| `GraphicsDevice::Clear(ClearOptions::Stencil, ...)` actually clears stencil | ❌ (Task 871) |
| Physical stencil buffer exists on the default window | ✅ (fixed, Task 315) |

Legend: ✅ verified working · ❌ confirmed broken/absent · ⚠️ partial/inconsistent ·
❓ not investigated at all.

## Open, tracked follow-up work

- **Task 866** — `RasterizerState` static presets still don't set `Name` (the last remaining
  portion of this gap; `SamplerState`/`BlendState`/`DepthStencilState` are all fixed).
- **Task 869** — `GraphicsDevice` stores `BlendState`/`DepthStencilState`/`RasterizerState` by
  value, unlike FNA's reference-type aliasing (architectural, deliberate, not fixed).
- **Task 871** — `GraphicsDevice::Clear` ignores `ClearOptions::Stencil` (and the stencil clear
  value) on every renderer.
- **Task 872** — `GraphicsDevice.ReferenceStencil`'s independent-override behavior has no renderer
  connection on EasyGL; needs a new `IGraphicsRenderer::SetReferenceStencil` method and
  plumbing there.
