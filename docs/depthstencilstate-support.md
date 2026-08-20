# DepthStencilState Support Matrix

> **Status update, 2026-07-11:** Task 870 (Vulkan's fake depth-compare/stencil-test pipeline,
> the central finding of §4 below) **is fixed** — real per-pipeline depth-compare op, full
> front/back `VkStencilOpState`, and stencil reference/masks as true dynamic state. `ReferenceStencil`
> independent-override is also now connected on Vulkan specifically (an undocumented side effect of
> the same fix); it remains unconnected on EasyGL/Bgfx (Task 872). §4 and the summary table below
> describe the pre-fix investigation and are kept for their diagnostic detail — the ❌/"not fixed"
> markers for Vulkan in them are historical, not current. See `AUDIT.md`'s `DepthStencilState` row
> or `NEXT.md` §5 for current status.

Phase 37 (`plans/plan_graphics.md` Tasks 311–320) audited and pixel-verified `DepthStencilState`
conformance against FNA across all three graphics renderers (EasyGL, Vulkan, Bgfx). This document
summarizes the findings.

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

## 2. Default depth/stencil state on `GraphicsDevice` (Task 312)

FNA's `GraphicsDevice` constructor sets `DepthStencilState = DepthStencilState.Default` and
`RasterizerState = RasterizerState.CullCounterClockwise`. CNA's `depthStencilState_`/
`rasterizerState_` members were plain default-constructed, never actually copied from those
presets — the same bug shape as Task 302's `BlendState` finding (values coincided, invisible until
`Name` existed to distinguish them). Fixed via the constructor's member-init list.

## 3. Depth testing (Tasks 313–314)

`DepthBufferWriteEnable` and all 5 `CompareFunction` values tested via differential pixel tests
(a "stamp" quad establishes a known depth, then a second quad's write/compare behavior is read
back via a third quad drawn at a depth chosen to give an unambiguous result).

| Property | EasyGL | Vulkan |
|---|---|---|
| `DepthBufferWriteEnable` | ✅ correct | ✅ correct |
| `DepthBufferFunction` (`Less`/`LessEqual`/`Greater`/`Always`/`Never`) | ✅ all correct | ❌ ignored entirely (Task 870) |

**Vulkan gotcha found while building these tests**: with an identity World/View/Projection (as
these tests use), the vertex Z value becomes clip-space Z directly. XNA/DirectX (and this
project's Vulkan renderer, correctly) use a `[0, +w]` clip-space Z range, not OpenGL's `[-1, +1]` —
a negative Z silently clips away on Vulkan only. Any future identity-matrix depth/stencil pixel
test in this project must keep Z within `[0, 1]`.

## 4. Stencil testing (Tasks 315–319) — the central Phase 37 finding

**Vulkan's stencil-test support is almost entirely fake — the same shape and severity as Task
868's `BlendState` finding, tracked as Task 870, not fixed.**
`VulkanRenderer::ApplyDepthStencilState` takes 15 parameters but only stores 2 of them
(`depthEnable`, `depthWriteEnable`); the other 13 — including every stencil-related parameter — are
unused. Concretely: `stencilTestEnable` is never set on any pipeline (confirmed via grep — it stays
at its zero-initialized `VK_FALSE` default), so the stencil test never gates a fragment regardless
of `StencilEnable`, `StencilFunction`, masks, or operations. `DepthBufferFunction` is separately
hardcoded per-pipeline-creation-function to `VK_COMPARE_OP_LESS` or `_LESS_OR_EQUAL` (see §3).
A compounding, Vulkan-only cause was also found: `FindDepthFormat()` checks `VK_FORMAT_D32_SFLOAT`
(a stencil-less format) *before* the two stencil-capable formats, so even a fixed
`ApplyDepthStencilState` would still need this format-preference order corrected.

Confirmed **five separate times** across Tasks 314–318, always via a genuine contrast check (a
lesson learned mid-phase, see below):

| Property tested | Task | EasyGL | Vulkan |
|---|---|---|---|
| `StencilEnable` | 315 | ✅ | ❌ (Task 870) |
| `StencilMask` / `StencilWriteMask` | 316 | ✅ | ❌ (Task 870) |
| `StencilFail` / `StencilDepthBufferFail` / `StencilPass` (front-face ops) | 317 | ✅ | ❌ (Task 870) |
| `TwoSidedStencilMode` / CCW back-face ops | 318 | ✅ | ❌ (Task 870) |
| `ReferenceStencil` propagation via `DepthStencilState` | 319 | ✅ (fixed) | n/a (moot, see Task 870) |

**Test-design lesson (important for any future stencil test in this codebase)**: a test where
every check expects the *same* pass/fail outcome cannot distinguish "the feature genuinely works"
from "the stencil test is bypassed entirely" (Task 870) — both produce identical results. Task
317's first draft fell into exactly this trap: an earlier 3-column version (one column per
`StencilOperation` slot, all expecting PASS) coincidentally passed every check on Vulkan too, since
a fully-bypassed stencil test also always "passes." A 4th column with a deliberately-wrong
read-back reference (which a working implementation must *reject*) was required to give the test
real discriminating power. Task 318 applied this lesson from the start.

**Bgfx**: `ApplyDepthStencilState`'s state-application code was confirmed fully correct by direct
reading (`depthFunc` mapped via a complete `BGFX_STATE_DEPTH_TEST_*` switch; real front/back
`BGFX_STENCIL_*` state built via a `BuildBgfxStencil` helper). Whether Bgfx's window/backbuffer
actually has a physical stencil buffer allocated (the exact class of gap found and fixed on EasyGL,
see below) has **not been verified** — worth checking before assuming Bgfx's stencil support is any
better in practice than EasyGL's was before Task 315's fix.

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
2. **Confirmed broken on all 3 renderers, tracked as new Task 872, not fixed**:
   `GraphicsDevice::setReferenceStencilProperty` is a pure local no-op with **zero renderer
   connection** — `IGraphicsRenderer` has no `SetReferenceStencil` method at all. Unlike Task 870,
   this is **not Vulkan-specific** — confirmed failing identically on both EasyGL and Vulkan via a
   dedicated pixel test. Unlike `BlendFactor`'s case (Task 309), where the renderer-side method
   already existed and just wasn't being invoked, here there is no existing code path to wire up;
   fixing this needs new interface surface across all 3 renderers.

---

## Summary: what actually works today, per renderer

**Updated 2026-07-11** — the Vulkan column below reflects Task 870's fix (previously all ❌; see
the status banner at the top of this document).

| Feature | EasyGL | Vulkan | Bgfx |
|---|---|---|---|
| `DepthStencilState` API/presets/`Name` | ✅ | ✅ | ✅ |
| Default `DepthStencilState`/`RasterizerState` on `GraphicsDevice` | ✅ (fixed) | ✅ (fixed) | ✅ (fixed) |
| `DepthBufferWriteEnable` | ✅ | ✅ | 🔍 not pixel-verified (no readback API) |
| `DepthBufferFunction` (all `CompareFunction` values) | ✅ | ✅ (fixed, Task 870) | 🔍 not pixel-verified |
| `StencilEnable` | ✅ (fixed, Task 315) | ✅ (fixed, Task 870) | 🔍 not pixel-verified |
| `StencilMask` / `StencilWriteMask` | ✅ | ✅ (fixed, Task 870) | 🔍 not pixel-verified |
| Front-face `StencilFail`/`StencilDepthBufferFail`/`StencilPass` | ✅ | ✅ (fixed, Task 870) | 🔍 not pixel-verified |
| `TwoSidedStencilMode` / back-face (CCW) ops | ✅ | ✅ (fixed, Task 870) | 🔍 not pixel-verified |
| `ReferenceStencil` via `DepthStencilState` assignment | ✅ (fixed, Task 319) | ✅ (fixed, Task 870) | 🔍 not pixel-verified |
| `ReferenceStencil` independent override (no state reassignment) | ❌ (Task 872) | ✅ (fixed — side effect of Task 870) | ❌ (Task 872) |
| `GraphicsDevice::Clear(ClearOptions::Stencil, ...)` actually clears stencil | ❌ (Task 871) | ❌ (Task 871) | ❓ unverified |
| Physical stencil buffer exists on the default window | ✅ (fixed, Task 315) | ✅ (Task 870 fixed the compare/stencil pipeline; format-preference order noted in §4 was corrected alongside it) | ❓ unverified |

Legend: ✅ verified working · ❌ confirmed broken/absent · ⚠️ partial/inconsistent ·
🔍 not empirically verified this phase (Bgfx has no GPU pixel-readback API in this project, so its
depth/stencil coverage is smoke-test/no-regression only by design) · ❓ not investigated at all.

## Open, tracked follow-up work

- **Task 866** — `RasterizerState` static presets still don't set `Name` (the last remaining
  portion of this gap; `SamplerState`/`BlendState`/`DepthStencilState` are all fixed).
- **Task 869** — `GraphicsDevice` stores `BlendState`/`DepthStencilState`/`RasterizerState` by
  value, unlike FNA's reference-type aliasing (architectural, deliberate, not fixed).
- ~~**Task 870**~~ — **fixed.** Vulkan's `DepthBufferFunction` and entire stencil-test pipeline are
  now real: per-pipeline compare op, full stencil ops, stencil-capable depth format preferred.
- **Task 871** — `GraphicsDevice::Clear` ignores `ClearOptions::Stencil` (and the stencil clear
  value) on every renderer.
- **Task 872** — `GraphicsDevice.ReferenceStencil`'s independent-override behavior has no renderer
  connection on EasyGL/Bgfx; needs a new `IGraphicsRenderer::SetReferenceStencil` method and
  per-renderer plumbing there. (Vulkan is already connected, an undocumented side effect of Task 870.)
- Bgfx's physical stencil-buffer existence (the same class of gap Task 315 found and fixed on
  EasyGL) has not been checked — worth verifying before relying on Bgfx stencil support in practice.
