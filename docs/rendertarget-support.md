# RenderTarget2D / RenderTargetCube Support Matrix

Phase 39 (graphics Tasks 331–340) audited and pixel-verified `RenderTarget2D` and
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

The architecture-blocked cube-binding finding from this phase was subsequently closed.
`RenderTargetCube` now inherits `TextureCube`/`Texture`, implements the common
`IRenderTargetBindingSource`, and the singular cube-face overload delegates to the same recorded
`SetRenderTargets` path as 2D and plural bindings. Explicit disposal while a face is bound throws
`InvalidOperationException`, matching `RenderTarget2D`; destruction while bound safely detaches the
native destination before releasing it. `EasyGL_BoundResourceDispose`, `EasyGL_HandleRelease` and
the shared bound-target lifetime contract keep these cases active.

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

Task 877 subsequently fixed the exact-format gap. EasyGL maps `DepthFormat::None` to no attachment,
`Depth16` to `DepthComponent16`, `Depth24` to `DepthComponent24`, and `Depth24Stencil8` to the
combined depth/stencil attachment for both `RenderTarget2D` and `RenderTargetCube`, including their
multisample storage. `EasyGL_RenderTargetCube_DepthFormat` checks the cube path directly;
`EasyGL_RenderTarget_DepthStencilUsage` exercises real depth and stencil behavior across target
usage and rebind cycles.

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

Task 880 subsequently connected `GraphicsDevice.Viewport` to the renderer. EasyGL converts XNA's
top-left rectangle against the active target's own height, programs the GL viewport and depth
range, and preserves the selected viewport until an actual target transition resets it. The
subregion, SpriteBatch target and deferred-viewport regressions distinguish this from the former
full-target hardcoding. `SOFTWARE-222` later pinned the complementary XNA rule: rebinding the
identical target set is a no-op and therefore does not reset a custom viewport or scissor.

## 7. Multiple render targets and compatibility validation (Task 339)

The phase's FNA-only conclusion that target compatibility had no public validation was superseded
by measured Microsoft XNA behavior in `SOFTWARE-220/221`. CNA now rejects duplicate target
resources (including two faces of the same cube), foreign-device resources, mismatched dimensions,
mismatched applied sample counts and different pixel sizes before changing the active set. Exact
`SurfaceFormat` equality is not required: formats with the same pixel size are compatible.
`SOFTWARE-222` makes an identical ordered resource/face set a true no-op.

The old `EasyGL_MRT_TwoAttachments` failure was a test-premise defect, not evidence that the second
attachment was inactive. It used `BasicEffect`, whose fragment shader writes only output location
0, while expecting attachment 1's pre-bind contents to survive even though its default
`RenderTargetUsage::DiscardContents` permits them to be discarded. The replacement uses an
explicit multi-output `ShaderEffect` and verifies ordered one-through-four-target writes,
per-attachment color masks, slot-zero depth ownership, MSAA resolve, transitions, usage, readback
and immediate sampling. That complete EasyGL regression passes.

## 8. MRT limits per renderer (Task 340 / Task 881)

| Layer | Simultaneous color attachments | Enforcement |
|---|---|---|
| XNA/CNA `Reach` profile | 1 | Shared profile validation rejects a larger set before renderer work. |
| XNA/CNA `HiDef` profile | 4 (`MAX_RENDERTARGET_BINDINGS`) | Shared `GraphicsDevice::SetRenderTargets` throws above four. |
| EasyGL hardware boundary | `min(4, GL_MAX_DRAW_BUFFERS, GL_MAX_COLOR_ATTACHMENTS)` | The renderer reports and enforces a lower native limit if the context supplies one. |

Task 881 added the shared four-target ceiling, so a renderer's larger native allowance can never
expand the XNA API. EasyGL no longer hardcodes eight or silently truncates a set. Its binding path
uses four explicit slots and validates the native limit before replacing the active destinations.

---

## Summary: what actually works today, per renderer

| Feature | EasyGL |
|---|---|
| `RenderTarget2D`/`RenderTargetCube` constructors, properties, `IsContentLost`/`ContentLost` | ✅ |
| `RenderTarget2D` sampling after unbind (`SpriteBatch`) | ✅ |
| `RenderTargetCube` sampling after unbind (`EnvironmentMapEffect`, PBR IBL, opt-in source `ShaderEffect`) | ✅ |
| Depth buffer functionality (does depth testing work inside an RT) | ✅ |
| Depth/stencil format fidelity (exact `DepthFormat` honored) | ✅ (fixed, Task 877) |
| Mipmap generation (`LevelCount`, real GPU mips) | ✅ (fixed, Task 336) |
| MSAA (`MultiSampleCount`, real resolve) | ✅ (fixed, Task 337) |
| `SetRenderTarget`/`SetRenderTargets` Viewport/ScissorRectangle reset on a real binding change; identical sets are no-ops | ✅ (fixed, Task 338 / SOFTWARE-222) |
| `Viewport`'s actual GPU effect (subregion, target switch and depth range) | ✅ (fixed, Task 880) |
| MRT (ordered 2–4 targets, 2D/cube faces, MSAA and per-slot masks) | ✅ |
| MRT compatibility and device-ownership validation | ✅ (`SOFTWARE-220/221/222`) |
| MRT count cap matches XNA's `MAX_RENDERTARGET_BINDINGS=4` | ✅ (fixed, Task 881) |

Legend: ✅ verified working · ❌ confirmed broken/gap.

## Follow-up status

- Tasks 877, 880 and 881 are closed by the exact depth-format mapping, real viewport wiring and
  shared XNA MRT ceiling described above.
- Task 145's failing test premise was replaced by the discriminating multi-output MRT contract;
  there is no surviving EasyGL two-attachment defect.
- The former Task 863 downstream gap is closed: cube faces participate in recorded singular and
  plural bindings, explicit bound disposal is rejected, and destruction cannot leave a dangling
  native target.
- Current repository-wide renderer defects and intentional capability limits live in
  `handoff.md` §5; none of the historical open items on this page remains one of them.

This closes Phase 39 (graphics Tasks 331–340) in full.
