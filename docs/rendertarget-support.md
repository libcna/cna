# RenderTarget2D / RenderTargetCube Support Matrix

Phase 39 (`plans/plan_graphics.md` Tasks 331–340) audited and pixel-verified `RenderTarget2D` and
`RenderTargetCube` conformance against FNA. This document summarizes the findings and closes the
phase.

---

## 1. Constructor/property audits (Tasks 331–332)

`RenderTarget2D` (Task 331) and `RenderTargetCube` (Task 332) were each audited line-by-line
against FNA's C# source. Both classes' constructor overloads, `DepthStencilFormat`, and
`RenderTargetUsage` already matched FNA. Two real, fixed findings:

- `RenderTarget2D` was missing `IsContentLost`/`ContentLost` entirely (`RenderTargetCube` already
  had both) — fixed by adding them, mirroring `RenderTargetCube`'s exact pattern.
- `RenderTargetCube::GetTypeName()` was never overridden, so it incorrectly inherited
  `TextureCube`'s `"...TextureCube"` string instead of reporting its own type — fixed.

One architecture-blocked finding, **not fixed** (out of scope for a design-pass-free task):
`RenderTarget2D::Dispose(bool)` has a "still bound" guard (throws if disposed while bound);
`RenderTargetCube` cannot have the same guard added without an architecture change, because
`RenderTargetBinding` only stores `Texture*`, and `RenderTargetCube` inherits `GraphicsResource`
directly, not `Texture` (Task 863's already-tracked gap). Confirmed `GraphicsDevice::SetRenderTarget
(RenderTargetCube*, CubeMapFace)` never records the binding at all for the same reason — a direct
downstream consequence of Task 863, not a new independent bug.

## 2. Sampling after unbinding (Tasks 333–334)

**`RenderTarget2D`**: already pixel-verified working on EasyGL (Task 87) before this phase —
reconfirmed, no new work needed.

**`RenderTargetCube`**: no existing test covered sampling a cube render target back out via
`EnvironmentMapEffect` after unbinding. On EasyGL it works: `EnvironmentMapEffect` reads the cube
texture via `TextureCube::GetRenderer()` (shared with `RenderTargetCube`) and calls `BindGL()`
through virtual dispatch — both the plain cube-texture and render-target-cube renderers override it
correctly.

The later REMED-GFX-137 orientation audit found that binding alone was insufficient on EasyGL:
GL rasterizes a rendered face bottom-up while an uploaded cube's first row is sampled as its top.
The lookup direction is now mirrored only for a `RenderTargetCube` in every stock consumer:
`EnvironmentMapEffect` and both independent PBR IBL cubes (irradiance and prefiltered specular).
Source `ShaderEffect` exposes the same correction as an opt-in `uRtCubeFlipV` vector, parallel to
the existing 2D `uRtFlipV` contract. The shared nine-check GPU witness covers both IBL terms, a
rendered cube (including an exact face-edge sample) in a source shader, and switching that shader
back to an uploaded cube; it passes on
OPENGLES3 and OPENGL33, and a specular-only mutation fails exactly its two checks. Public readback
remains top-down and unchanged. The inherited `RenderTargetCube::SetData` path had already been
normalized in `23c5557f7`; the complete readback contract's W1 check confirms a 64/64 same-order
upload/readback round trip on both GL profiles. The final GLES-only edge artifact came from the
necessarily discontinuous face-local row flip: mandatory seamless filtering crossed into an
unrelated transformed neighbour. Stock EnvironmentMap/PBR sampling and the documented source-shader
helper now keep the bilinear footprint one texel inside the chosen face and give exact ties to a
side face. The focused edge oracle changes from red to `(128,0,128)` when that inset is removed;
restored, it passes on OPENGLES3 and OPENGL33, and the museum's reflection sphere has no face outline.
No public row-orientation or rendered-cube edge gap remains.

## 3. Depth buffer functionality (Task 335)

Task 331's property test only proved `getDepthStencilFormatProperty()` round-trips the constructor
argument — a stored value never connected to any GPU resource. Task 335 proved the FUNCTIONAL
behavior with a real pixel test: draws a near quad then a far quad into a `RenderTarget2D` with
`DepthFormat::Depth24Stencil8` and `DepthStencilState::Default`, and confirms the depth test
genuinely gates the draws on EasyGL — not just a property.

Found (not fixed, tracked as **Task 877**) a real, scoped format-fidelity gap distinct from "does
it work at all": EasyGL does not honor the *exact* requested `DepthStencilFormat` — it always
allocates depth-only `DepthComponent24` (zero stencil bits, ever).

## 4. Mipmap support (Task 336) — real fix on EasyGL

Following FNA3D's actual native mechanism (`OPENGL_ResolveTarget`: a render target's full mip chain
auto-regenerates from level 0 via `glGenerateMipmap` when it stops being the active target — games
never render into non-zero mip levels directly), Task 336 **fixed** render target mipmaps on
EasyGL:

- `RenderTarget2D`/`RenderTargetCube`'s `LevelCount` now correctly reflects `mipMap` (previously a
  `mipMap ? 1 : 1` no-op).
- `EasyGLRenderTargetRenderer`/`EasyGLRenderTargetCubeRenderer` pre-allocate GPU storage for every
  mip level and regenerate the chain from level 0 on unbind.
- Discovered `IRenderTargetRenderer`/`IRenderTargetCubeRenderer::UnbindAsRenderTarget()` were
  **completely dead code** — never called by anything in the project. Fixed by adding
  `currentRt2D_`/`currentRtCube_` tracking to `EasyGLRenderer` so switching away from a
  bound target actually invokes it.
- Pixel-verified with a mip-completeness probe reusing the established
  `TextureFilter::Anisotropic`-renders-solid-black-on-incomplete-mip-chains signature (Task 867/299).

## 5. MSAA support (Task 337) — real fix on EasyGL

Reusing Task 336's exact resolve-on-unbind mechanism, Task 337 **fixed** MSAA render targets on
EasyGL, following FNA's real algorithm (`MathHelper.ClosestMSAAPower` + `FNA3D_GetMaxMultiSampleCount`):

- `MultiSampleCount` now reflects the renderer's real, device-capability-clamped value (queried via
  `glGetIntegerv(GL_MAX_SAMPLES, ...)`), not a raw pass-through.
- `EasyGLRenderTargetRenderer`/`EasyGLRenderTargetCubeRenderer` render into a real multisampled color
  (+ depth) renderbuffer and resolve it via `glBlitFramebuffer` on unbind (same call site as
  Task 336's mip regeneration, correctly ordered: resolve first, then regenerate mips).
- Pixel-verified with a genuine anti-aliasing **differential** proof (a solid-fill-only test cannot
  distinguish "resolve preserves solid colors" from "resolve genuinely averages sub-pixel
  coverage"): a diagonal-edged triangle rendered at `MultiSampleCount=0` produces a hard binary
  edge; at `=8` it produces genuinely intermediate (blended) pixel values.

Key design distinction from `LevelCount`: `MultiSampleCount` is legitimately
renderer/device-capability-dependent even in real FNA.

## 6. `SetRenderTarget(nullptr)` returns to backbuffer (Task 338) — real fix, shared code

The core routing (does unbinding restore drawing to the actual backbuffer?) was already
extensively proven by dozens of existing tests. Auditing FNA's actual `SetRenderTargets` source
found a real, previously-undiscovered gap: FNA *always* resets `Viewport`/`ScissorRectangle` to
`(0, 0, newWidth, newHeight)` on every render-target switch (the new target's size when binding, or
the backbuffer's size when unbinding) — CNA never touched either property at all. **Fixed**: added
`GraphicsDevice::ResetViewportAndScissorForRenderTarget`, wired into all 3
`SetRenderTarget*`/`SetRenderTargets` overloads. Pixel-verified on EasyGL with a real GPU-level
proof (`ScissorRectangle` is wired to the renderer's scissor test).

Found and deliberately deferred a much larger, separate gap discovered along the way:
`GraphicsDevice.Viewport` had **zero GPU wiring** on EasyGL, which hardcoded the full
render-target/window size (Task 880).

## 7. Multiple render targets with mixed formats (Task 339) — audit only, no bug

Read FNA's actual `SetRenderTargets` source: it performs **zero explicit validation** of
format/size/count mismatches between bound targets — it computes dimensions from the first target
only and delegates everything to the native driver. "Reject invalid combinations" is not an
XNA-level behavior. Confirmed EasyGL behaves the same way (no CNA-level validation), consistent
with FNA — no bug.

Found one real, minor divergence: FNA's actual MRT cap is `MAX_RENDERTARGET_BINDINGS = 4`
(implicitly enforced via a fixed-size array that would throw past 4) — see §8 for EasyGL's own
cap. Tracked as **Task 881**.

Deliberately did not touch the already-tracked, off-limits `EasyGL_MRT_TwoAttachments` bug
(Task 145) — even the basic same-size 2-target MRT case is already known-broken on EasyGL, which
blocks any meaningful deeper mismatched-format verification.

## 8. MRT limits per renderer (Task 340)

| Renderer | Simultaneous color attachments | Enforcement |
|---|---|---|
| EasyGL | 8 (hardcoded `constexpr int kMaxMRT = 8`) | Silently truncates anything beyond 8 — no error. |
| **Real FNA/XNA** | **4** (`MAX_RENDERTARGET_BINDINGS`) | Implicit — a fixed-size internal array throws if a game passes more than 4. |

EasyGL's own cap does not match FNA's real 4-target limit (Task 881); shared
`GraphicsDevice::SetRenderTargets` enforces 4. EasyGL additionally assumes every bound target
shares the first target's dimensions/format, with zero CNA-level cross-target validation — matching
FNA's own delegate-to-native-driver behavior (§7), not a divergence.

---

## Summary: what actually works today, per renderer

| Feature | EasyGL |
|---|---|
| `RenderTarget2D`/`RenderTargetCube` constructors, properties, `IsContentLost`/`ContentLost` | ✅ |
| `RenderTarget2D` sampling after unbind (`SpriteBatch`) | ✅ |
| `RenderTargetCube` sampling after unbind (`EnvironmentMapEffect`, PBR IBL, opt-in source `ShaderEffect`) | ✅ |
| Depth buffer functionality (does depth testing work inside an RT) | ✅ |
| Depth/stencil format fidelity (exact `DepthFormat` honored) | ❌ always `DepthComponent24`, no stencil (Task 877) |
| Mipmap generation (`LevelCount`, real GPU mips) | ✅ (fixed, Task 336) |
| MSAA (`MultiSampleCount`, real resolve) | ✅ (fixed, Task 337) |
| `SetRenderTarget`/`SetRenderTargets` Viewport/ScissorRectangle reset on a real binding change; identical sets are no-ops | ✅ (fixed, Task 338 / SOFTWARE-222) |
| `Viewport`'s actual GPU effect (any sub-region viewport) | ❌ zero wiring (Task 880) |
| MRT (2+ same-size/format targets) | ❌ `EasyGL_MRT_TwoAttachments`, attachment 1 stays black (Task 145) |
| MRT count cap matches FNA's `MAX_RENDERTARGET_BINDINGS=4` | ❌ caps at 8 instead (Task 881) |

Legend: ✅ verified working · ❌ confirmed broken/gap.

## Open, tracked follow-up work

Phase 39 opened several new tracked tasks while auditing/fixing `RenderTarget2D`/`RenderTargetCube`:

- **Task 877** — EasyGL does not honor the exact requested `DepthStencilFormat`.
- **Task 880** — `GraphicsDevice.Viewport` still has zero GPU wiring on EasyGL (large,
  pre-existing, broader than render targets specifically).
- **Task 881** — `SetRenderTargets`'s per-renderer MRT cap doesn't match FNA's real limit of 4.

Pre-existing, not opened this phase, still blocking deeper MRT work: `EasyGL_MRT_TwoAttachments`
(Task 145) — even a basic same-size 2-target MRT setup doesn't render correctly on EasyGL. Needs
its own dedicated root-cause investigation before Task 881 or any other MRT-adjacent work can be
meaningfully extended.

This closes Phase 39 (`plans/plan_graphics.md` Tasks 331–340) in full.
