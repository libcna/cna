# RasterizerState Support Matrix

Phase 38 (graphics Tasks 321–330) audited and pixel-verified `RasterizerState`
conformance against FNA. This document summarizes the findings.

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

One comprehensive pixel test (`modules/renderers/easygl/examples/easygl_rasterizerstate_cullmode_test.cpp`,
`EasyGL_RasterizerState_CullMode`) draws two quads of opposite, empirically-verified winding order (via signed area, not assumed)
side by side, redrawn under all 3 `CullMode` values:

| `CullMode` | Column 0 (CW winding) | Column 1 (CCW winding) |
|---|---|---|
| `None` | visible (RED) | visible (GREEN) |
| `CullCounterClockwiseFace` (XNA default) | visible (RED) | culled (background) |
| `CullClockwiseFace` | culled (background) | visible (GREEN) |

All 6 checks pass. This directly satisfies Task 323's own goal (`CullNone`
disables culling for both windings) and, because each quad's visibility is checked under all 3
modes rather than just one, also fully satisfies Tasks 324 (`CullClockwiseFace`) and 325
(`CullCounterClockwiseFace`) — no separate test files were needed for them, mirroring the Task 295
precedent (one well-designed test covering multiple planned verification tasks).

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

`modules/graphics/examples/fill_mode_test.cpp` (Task 327) covers both `FillMode::Solid` (baseline,
checked before and after the `WireFrame` sub-test) and `FillMode::WireFrame`. Task 326 found the
source is fully renderer-agnostic (`VertexBuffer`+`BasicEffect`) and registered it as an EasyGL
test (`EasyGL_FillMode_Solid`). All 3 sub-tests pass, confirming both the `Solid` baseline and `EasyGLRenderer`'s `GL_LINES`
re-expansion emulation for `WireFrame` (OpenGL ES has no `glPolygonMode`) work correctly. No bug
found — closed a real test-registration gap.

## 5. Depth bias / slope-scale depth bias (Task 328)

Not re-investigated in Phase 38. EasyGL's coverage is `EasyGL_DepthBias` (Task 767,
`glPolygonOffset`) and `EasyGL_DepthBias_Magnitude`.

## 6. Scissor test (Task 329)

Already verified via `modules/renderers/easygl/examples/easygl_scissor_test.cpp` prior to this
phase's audit tasks. No new findings in Phase 38.

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

## 8. `MultiSampleAntiAlias` reaches no renderer, by decision (2026-09-07)

`IGraphicsRenderer::ApplyRasterizerState` takes `(cullMode, fillMode, scissorTestEnable,
depthBias, slopeScaleDepthBias)` — five parameters, none of them this flag — so the value stops at the shared interface for **every** renderer family.

**What XNA actually does with it, measured rather than read.**
`spikes/xna-multisample-antialias-spike/` runs the property on the real XNA 4.0 runtime in
`~/.wine-cna-xna40` (D3D9 through DXVK, RADV PHOENIX). Setting
`RasterizerState.MultiSampleAntiAlias = false` makes the D3D9 layer report

```
warn:  D3D9DeviceEx::SetRenderState: Unhandled render state 161
```

and **161 is `D3DRS_MULTISAMPLEANTIALIAS`** (`d3d9types.h:1008`). The attribution is measured, not
inferred: re-running with `PROBE_SKIP_FALSE=1`, so that no leg ever sets the flag to `false`,
produces the warning **zero** times. So XNA does carry the property down to the D3D9 render state,
and writes it only when the value is not D3D9's default — the field is live upstream, not inert.

**What it does to pixels could not be measured here**, and the probe says so rather than guessing:
DXVK reports the state as unhandled, so the edge-pixel counts are identical either way
(4× multisampled triangle: 151 blended pixels with the flag true, 151 with it false, against 0 on
an unmultisampled target — multisampling itself demonstrably worked, which is what rules out the
"MSAA never happened" reading). Answering the behavioural half needs a native D3D9 stack, which
this machine does not have.

**Why CNA leaves it dropped.** Adding a sixth parameter to an interface every renderer family
implements, so that every one of them can refuse it, buys nothing that the current silence does
not — and it is not a parity gap, because no renderer receives the field, so no two renderers
disagree.

## Summary: what actually works today, per renderer

| Feature | EasyGL |
|---|---|
| `RasterizerState` API/presets/`Name` | ✅ (fixed, Task 321) |
| Default `RasterizerState` on `GraphicsDevice` (values + `Name`) | ✅ (fixed, Tasks 312/321/322) |
| `CullMode::None`/`CullClockwiseFace`/`CullCounterClockwiseFace` | ✅ |
| `FillMode::Solid` | ✅ |
| `FillMode::WireFrame` | ✅ (GL_LINES emulation) |
| `DepthBias` / `SlopeScaleDepthBias` | 🔍 not pixel-verified in this phase; `EasyGL_DepthBias` registered later (Task 767) |
| `ScissorTestEnable` + `GraphicsDevice.ScissorRectangle` | ✅ |
| State object freeze/immutability | N/A — FNA has none either, confirmed no bug |

Legend: ✅ verified working · ❌ confirmed broken/absent · 🔍 not empirically verified this phase.

## Open, tracked follow-up work

- No new tracked bugs were opened in Phase 38 — `RasterizerState` conformance was already solid
  going in (unlike Phases 36/37's `BlendState`/`DepthStencilState` findings). The only gaps found
  were test-coverage/registration gaps (`Name` on presets, missing EasyGL `FillMode`
  registration), both closed within this phase.
- Tasks 866/869 (closed/tracked in earlier phases) and Tasks 871/872 (`Clear` stencil gap,
  `ReferenceStencil` renderer gap) remain as documented in
  `docs/sampler-state-support.md` and `docs/depthstencilstate-support.md` — none are
  `RasterizerState`-specific and none were touched in this phase.
