# DepthStencilState Support Matrix

`DepthStencilState` conformance against FNA/XNA, pixel-verified on EasyGL; the shared regressions
named below also run on SDLGPU (and Software where stated).

---

## 1. `DepthStencilState` API surface

The full 16-property surface, all 3 static presets (`Default`/`DepthRead`/`None`), and the
default-constructor values match FNA exactly: `DepthBufferEnable=DepthBufferWriteEnable=true`,
`DepthBufferFunction=LessEqual`, `StencilEnable=false`,
`StencilFunction=CounterClockwiseStencilFunction=Always`, all 4 stencil-op fields `=Keep`,
`TwoSidedStencilMode=false`, `StencilMask=StencilWriteMask=Int32.MaxValue`, `ReferenceStencil=0`.

Every preset carries its FNA/XNA `Name` (e.g. `"DepthStencilState.Default"`), as do the presets
of `SamplerState`, `BlendState` and `RasterizerState`.

Preset and shared-state reference semantics are preserved; literal CLR wrapper identity is the
accepted C++ mapping deviation recorded as `SOFTWARE-198` in `CHECKLIST.md`.

## 2. Default graphics states on `GraphicsDevice`

FNA's `GraphicsDevice` constructor assigns all three named presets: `BlendState.Opaque`,
`DepthStencilState.Default`, and `RasterizerState.CullCounterClockwise`. CNA does the same: after
resolving the renderer, it applies all three presets through their ordinary property setters, so
the constructed default reaches the renderer before the first draw, not only the getters. A
renderer without 3D support retains the same public `DepthStencilState.Default` value without
attempting a native depth-state call.

The shared `graphicsdevice_default_state_occlusion_test.cpp` deliberately calls none of the three
state setters: it draws a near quad before a fully overlapping far quad, then reverses draw order
as a positive control. It is registered for EasyGL and SDLGPU.

## 3. Depth testing

`DepthBufferWriteEnable` and all 5 `CompareFunction` values are tested via differential pixel
tests (a "stamp" quad establishes a known depth, then a second quad's write/compare behavior is
read back via a third quad drawn at a depth chosen to give an unambiguous result).

| Property | EasyGL |
|---|---|
| `DepthBufferWriteEnable` | ✅ correct |
| `DepthBufferFunction` (`Less`/`LessEqual`/`Greater`/`Always`/`Never`) | ✅ all correct |

**Clip-space rule for tests**: with an identity World/View/Projection, the vertex Z value becomes
clip-space Z directly. XNA/DirectX use a `[0, +w]` clip-space Z range, not OpenGL's `[-1, +1]` —
a negative Z silently clips away on a `[0, +w]` renderer. Any identity-matrix depth/stencil pixel
test must keep Z within `[0, 1]`.

## 4. Stencil testing

| Property tested | EasyGL |
|---|---|
| `StencilEnable` | ✅ |
| `StencilMask` / `StencilWriteMask` | ✅ |
| `StencilFail` / `StencilDepthBufferFail` / `StencilPass` (front-face ops) | ✅ |
| `TwoSidedStencilMode` / CCW back-face ops | ✅ |
| `ReferenceStencil` propagation via `DepthStencilState` | ✅ |

**Test-design rule**: a test where every check expects the *same* pass/fail outcome cannot
distinguish "the feature works" from "the stencil test is bypassed entirely". Every stencil test
includes a contrast case — e.g. a deliberately wrong read-back reference that a working
implementation must *reject*.

EasyGL requests `SDL_GL_STENCIL_SIZE` 8 at context creation; with zero stencil bits the GL stencil
test trivially passes, which mimics a bypassed test.

**Any stencil pixel test also needs `PresentationParameters.DepthStencilFormat =
DepthFormat::Depth24Stencil8`** (set via `GraphicsDeviceManager` before the device is created) —
the default `DepthFormat::Depth24` has no stencil aspect at all.

## 5. `ReferenceStencil`

FNA's `GraphicsDevice.ReferenceStencil` is a real, independent device property
(`FNA3D_Get/SetReferenceStencil`), analogous to `GraphicsDevice.BlendFactor` — settable without
reassigning the whole `DepthStencilState`, and expected to immediately affect subsequent stencil
compares. (FNA's own source carries a `FIXME: Does this affect the value found in
DepthStencilState?` comment, confirming this is an override, not a read-through proxy.)

- `GraphicsDevice::setDepthStencilStateProperty` propagates the assigned state's
  `ReferenceStencil` into the device field, so `getReferenceStencilProperty()` reflects it.
- `IGraphicsRenderer::SetReferenceStencil` carries the independent property; `GraphicsDevice`
  forwards both whole-state assignment and direct property changes. EasyGL records the active
  stencil functions, read mask and two-sided mode so it can reissue the GL function with the new
  reference for both faces. Its pixel regression checks the ordinary and two-sided paths with
  both windings; the same public test source also runs on SDLGPU and Software.

## 6. Stencil clearing

`GraphicsDevice::Clear` recognizes `ClearOptions::Stencil`, validates that the active surface has
the requested attachment, and dispatches every stencil-only and combined clear shape with the
requested value. EasyGL temporarily opens the stencil write mask and disables scissoring for the
clear, then restores the active state, because native `glClear` otherwise obeys both pieces of GL
state while XNA `Clear` does not.

`easygl_graphicsdevice_clear_stencil_test.cpp` proves four independent cases: stencil-only clear,
combined target/depth/stencil clear with a nonzero value, exact same-frame ordering after a stencil
stamp, and restoration of a restrictive stencil write mask. The shared regression is registered for
EasyGL and SDLGPU.

---

## Summary: what works, per renderer

| Feature | EasyGL |
|---|---|
| `DepthStencilState` API/presets/`Name` | ✅ |
| Default `BlendState`/`DepthStencilState`/`RasterizerState` applied on `GraphicsDevice` construction | ✅ |
| `DepthBufferWriteEnable` | ✅ |
| `DepthBufferFunction` (all `CompareFunction` values) | ✅ |
| `StencilEnable` | ✅ |
| `StencilMask` / `StencilWriteMask` | ✅ |
| Front-face `StencilFail`/`StencilDepthBufferFail`/`StencilPass` | ✅ |
| `TwoSidedStencilMode` / back-face (CCW) ops | ✅ |
| `ReferenceStencil` via `DepthStencilState` assignment | ✅ |
| `ReferenceStencil` independent override (no state reassignment) | ✅ |
| `GraphicsDevice::Clear(ClearOptions::Stencil, ...)` actually clears stencil | ✅ |
| Physical stencil buffer exists on the default window | ✅ |
