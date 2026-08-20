# RasterizerState Support Matrix

Phase 38 (`plans/plan_graphics.md` Tasks 321–330) audited and pixel-verified `RasterizerState`
conformance against FNA across all three graphics renderers (EasyGL, Vulkan, Bgfx). This document
summarizes the findings.

---

## 1. `RasterizerState` API surface (Task 321)

The full 6-property surface (`CullMode`/`DepthBias`/`FillMode`/`MultiSampleAntiAlias`/
`ScissorTestEnable`/`SlopeScaleDepthBias`), all 3 static presets (`CullClockwise`/
`CullCounterClockwise`/`CullNone`), and the default-constructor values
(`CullMode=CullCounterClockwiseFace`, `FillMode=Solid`, `DepthBias=0`,
`MultiSampleAntiAlias=true`, `ScissorTestEnable=false`, `SlopeScaleDepthBias=0`) already matched
FNA exactly.

One real, fixed finding: FNA's private preset constructor sets `Name` on every preset (e.g.
`"RasterizerState.CullCounterClockwise"`); CNA's didn't — the last remaining portion of the gap
already fixed in `SamplerState` (Task 291), `BlendState` (Task 301), and `DepthStencilState`
(Task 311). Fixed by threading a `name` parameter through the private preset constructor. **This
closes Task 866 entirely** — all 4 XNA state classes now correctly set `Name` on every preset.

## 2. Default rasterizer state on `GraphicsDevice` (Task 322)

FNA's `GraphicsDevice` constructor sets `RasterizerState = RasterizerState.CullCounterClockwise`
(fixed for CNA back in Task 312, alongside the `DepthStencilState` default). Task 322 extended
verification to the full 6-property surface (not just `CullMode`/`FillMode`, which Task 312's
original test checked) — passed trivially, since `CullCounterClockwise` only ever diverges from a
plain default-constructed `RasterizerState` in `CullMode` and `Name`. No bug found; closes any
doubt with a full-surface regression pin, mirroring `DepthStencilState`'s own Task 312 test rigor.

## 3. `CullMode` (Tasks 323–325)

One comprehensive pixel test (`modules/renderers/easygl/examples/easygl_rasterizerstate_cullmode_test.cpp`, registered for
both EasyGL and Vulkan as `EasyGL_RasterizerState_CullMode`/`Vulkan_RasterizerState_CullMode`)
draws two quads of opposite, empirically-verified winding order (via signed area, not assumed)
side by side, redrawn under all 3 `CullMode` values:

| `CullMode` | Column 0 (CW winding) | Column 1 (CCW winding) |
|---|---|---|
| `None` | visible (RED) | visible (GREEN) |
| `CullCounterClockwiseFace` (XNA default) | visible (RED) | culled (background) |
| `CullClockwiseFace` | culled (background) | visible (GREEN) |

All 6 checks pass on **both** renderers. This directly satisfies Task 323's own goal (`CullNone`
disables culling for both windings) and, because each quad's visibility is checked under all 3
modes rather than just one, also fully satisfies Tasks 324 (`CullClockwiseFace`) and 325
(`CullCounterClockwiseFace`) — no separate test files were needed for them, mirroring the Task 295
precedent (one well-designed test covering multiple planned verification tasks).

**Cross-renderer consistency, confirmed empirically**: Vulkan's `colored3d.vert.glsl` flips clip-space
Y (`pos.y = -pos.y`) to convert from OpenGL's Y-up NDC convention, and `VulkanRenderer`
explicitly sets `VK_FRONT_FACE_CLOCKWISE` (instead of the API default `VK_FRONT_FACE_COUNTER_CLOCKWISE`)
to compensate. The net effect is that the *same* input vertex winding culls identically on both
renderers — this test is the first in the project to empirically confirm that compensation is
correct, rather than only asserting it in a comment.

**Real, minor finding (not a bug, noted for the record, not fixed)**: while designing this test,
direct pixel evidence showed that an earlier test (`modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_twosided_test.cpp`,
Task 318)'s `DrawQuadFront`/`DrawQuadBack` naming and accompanying comment ("front-facing... survives
the default state") is backwards. That test only ever draws with `CullMode::None`, so the claim was
never actually exercised under a real cull mode — Task 323's direct empirical measurement (signed
area plus pixel readback under all 3 `CullMode` values) shows the *opposite* winding is the one that
survives XNA's default `CullCounterClockwiseFace` state. This is harmless to Task 318's own
correctness (it never depended on the absolute label, only on the two quads having opposite
windings, which they still do) and was left unfixed there as out of scope.

## 4. `FillMode` (Tasks 326–327)

`modules/renderers/vulkan/examples/vulkan_fill_mode_test.cpp` (Task 327) already covered both `FillMode::Solid` (baseline,
checked before and after the `WireFrame` sub-test) and `FillMode::WireFrame`, but was registered
for Vulkan only. Task 326 found the source is fully renderer-agnostic (`VertexBuffer`+`BasicEffect`,
no Vulkan-specific API — confirmed by compiling it standalone outside any renderer target) and
registered the same source as a new EasyGL test (`EasyGL_FillMode_Solid`). All 3 sub-tests pass on
EasyGL too, confirming both the `Solid` baseline and `EasyGLRenderer`'s `GL_LINES`
re-expansion emulation for `WireFrame` (OpenGL ES has no `glPolygonMode`) work correctly. No bug
found — closed a real test-registration gap.

## 5. Depth bias / slope-scale depth bias (Task 328)

Already verified via `modules/renderers/vulkan/examples/vulkan_depth_bias_test.cpp` (`Vulkan_DepthBias`) prior to this
phase's audit tasks. `DepthBias=-1e6` is a pre-existing, documented failure (its extreme magnitude
sub-case; the other 3 sub-cases — flat `DepthBias=0`, tilted `SlopeScaleDepthBias=0`/`-2000` —
consistently pass). Not re-investigated in Phase 38; tracked as a pre-existing issue, not
`RasterizerState`-specific.

## 6. Scissor test (Task 329)

Already verified via `modules/renderers/vulkan/examples/vulkan_scissor_test.cpp` (`Vulkan_ScissorTest`, 4/4 PASS: no-scissor
full coverage, scissor-to-top-left-quadrant clipping) and `modules/renderers/easygl/examples/easygl_scissor_test.cpp` prior
to this phase's audit tasks. No new findings in Phase 38.

## 7. State object immutability/freeze behavior (Task 330)

Confirmed via direct FNA source read (`Graphics/States/RasterizerState.cs`) that FNA has **no**
freeze/immutability enforcement for `RasterizerState` either — no "throws if mutated after first
use" behavior anywhere in the file. This is the same finding Task 310 (Phase 36) already
established generically for `BlendState`/`DepthStencilState`/`RasterizerState`. CNA correctly
matches (also none) — no bug, no implementation needed.

Added `GraphicsDeviceDefaultStateTest.MutatingRasterizerStateAfterAssignmentDoesNotAffectDevice`,
mirroring the existing `BlendState` test exactly: CNA's `GraphicsDevice` stores `RasterizerState`
**by value** (a deliberate, project-wide pattern — see Task 869, not fixed), so mutating a
`RasterizerState` object *after* assigning it to a `GraphicsDevice` does not affect the device's
already-applied copy, unlike FNA's reference-type aliasing (where the same C# object is stored, so
post-assignment mutation would be visible). This closes Phase 38's last open task.

---

## Summary: what actually works today, per renderer

| Feature | EasyGL | Vulkan | Bgfx |
|---|---|---|---|
| `RasterizerState` API/presets/`Name` | ✅ (fixed, Task 321) | ✅ (fixed, Task 321) | ✅ (fixed, Task 321) |
| Default `RasterizerState` on `GraphicsDevice` (values + `Name`) | ✅ (fixed, Tasks 312/321/322) | ✅ (fixed, Tasks 312/321/322) | ✅ (fixed, Tasks 312/321/322) |
| `CullMode::None`/`CullClockwiseFace`/`CullCounterClockwiseFace` | ✅ | ✅ | 🔍 not pixel-verified (no readback API) |
| `FillMode::Solid` | ✅ | ✅ | 🔍 not pixel-verified |
| `FillMode::WireFrame` | ✅ (GL_LINES emulation) | ✅ (`VK_POLYGON_MODE_LINE`) | 🔍 not pixel-verified |
| `DepthBias` / `SlopeScaleDepthBias` | 🔍 not pixel-verified (no EasyGL test registered) | ✅ (except the pre-existing `-1e6` sub-case) | 🔍 not pixel-verified |
| `ScissorTestEnable` + `GraphicsDevice.ScissorRectangle` | ✅ | ✅ | 🔍 not pixel-verified |
| State object freeze/immutability | N/A — FNA has none either, confirmed no bug | N/A | N/A |

Legend: ✅ verified working · ❌ confirmed broken/absent · 🔍 not empirically verified this phase
(Bgfx has no GPU pixel-readback API in this project, so its rasterizer-state coverage is
smoke-test/no-regression only by design).

## Open, tracked follow-up work

- No new tracked bugs were opened in Phase 38 — `RasterizerState` conformance was already solid on
  both EasyGL and Vulkan going in (unlike Phases 36/37's `BlendState`/`DepthStencilState`
  findings). The only gaps found were test-coverage/registration gaps (`Name` on presets, missing
  EasyGL `FillMode` registration), both closed within this phase.
- `DepthBias`/`SlopeScaleDepthBias` still has no EasyGL pixel test registered (only Vulkan). Not
  blocking — `modules/renderers/vulkan/examples/vulkan_depth_bias_test.cpp` appears renderer-agnostic like the `FillMode`
  test was, so registering it for EasyGL too would likely be a similarly small, low-risk task if
  ever prioritized.
- Pre-existing, unrelated to `RasterizerState`: `Vulkan_DepthBias`'s `DepthBias=-1e6` sub-case
  (documented since before this phase), and `Vulkan_FillMode_WireFrame`/`Vulkan_RenderTargetUsage`'s
  order-dependent full-suite flakiness (tracked since Task 279).
- Tasks 866/869 (closed/tracked in earlier phases) and Tasks 868/870/871/872 (Vulkan blend/stencil
  fakes, `Clear` stencil gap, `ReferenceStencil` renderer gap) remain as documented in
  `docs/sampler-state-support.md` and `docs/depthstencilstate-support.md` — none are
  `RasterizerState`-specific and none were touched in this phase.
