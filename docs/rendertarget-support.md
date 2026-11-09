# RenderTarget2D / RenderTargetCube Support Matrix

`RenderTarget2D` and `RenderTargetCube` conformance against FNA and, for target compatibility,
measured Microsoft XNA 4.0. Pixel evidence is on EasyGL unless a section names another renderer.

---

## 1. Constructors, properties and cube binding

Both classes' constructor overloads, `DepthStencilFormat` and `RenderTargetUsage` match FNA. Both
expose `IsContentLost`/`ContentLost`, and `RenderTargetCube::GetTypeName()` reports its own type
rather than `TextureCube`'s.

`RenderTargetCube` inherits `TextureCube`/`Texture` and implements the common
`IRenderTargetBindingSource`; the singular cube-face overload delegates to the same recorded
`SetRenderTargets` path as 2D and plural bindings. Explicit disposal while a face is bound throws
`InvalidOperationException`, matching `RenderTarget2D`; destruction while bound safely detaches the
native destination before releasing it. `EasyGL_BoundResourceDispose`, `EasyGL_HandleRelease` and
the shared bound-target lifetime contract cover these cases.

## 2. Sampling after unbinding

`RenderTarget2D` sampled through `SpriteBatch` after unbinding is pixel-verified on EasyGL.
`RenderTargetCube` is sampled back through `EnvironmentMapEffect`, which reads the cube texture via
`TextureCube::GetRenderer()` (shared with `RenderTargetCube`) and binds it through virtual dispatch
(`EasyGL_RenderTargetCube_SampleAfterUnbind`).

**Cube orientation on GL (REMED-GFX-137).** GL rasterizes a rendered face bottom-up, while an
uploaded cube's first row is sampled as its top. EasyGL therefore mirrors the lookup direction only
for a `RenderTargetCube`, in every stock consumer: `EnvironmentMapEffect` and both PBR IBL cubes
(irradiance and prefiltered specular). Source `ShaderEffect` exposes the same correction as an
opt-in `uRtCubeFlipV` vector, parallel to the 2D `uRtFlipV` contract. Public readback and
`RenderTargetCube::SetData` stay top-down; an upload/readback round trip preserves row order on both
GL profiles.

Because the face-local row flip is discontinuous, GLES's mandatory seamless filtering could cross
into an unrelated neighbour face at an edge. Stock EnvironmentMap/PBR sampling and the documented
source-shader helper keep the bilinear footprint one texel inside the chosen face and give exact
ties to a side face; the focused edge oracle reads `(128,0,128)` instead of red when that inset is
removed. `EasyGL_EnvironmentMap_RenderTargetCubeOrientation` and
`EasyGL_RenderTargetCube_SamplingOrientation` (both IBL terms, a rendered cube with an exact
face-edge sample in a source shader, and switching back to an uploaded cube) run on OPENGLES3 and
OPENGL33.

## 3. Depth buffer

A near quad then a far quad drawn into a `RenderTarget2D` with `DepthFormat::Depth24Stencil8` and
`DepthStencilState::Default` prove the depth test gates the draws (`EasyGL_RenderTarget2D_DepthBuffer`).

EasyGL maps `DepthFormat::None` to no attachment, `Depth16` to `DepthComponent16`, `Depth24` to
`DepthComponent24`, and `Depth24Stencil8` to the combined depth/stencil attachment for both
`RenderTarget2D` and `RenderTargetCube`, including their multisample storage.
`EasyGL_RenderTargetCube_DepthFormat` checks the cube path directly;
`EasyGL_RenderTarget_DepthStencilUsage` exercises real depth and stencil behavior across target
usage and rebind cycles.

## 4. Mipmaps

FNA3D's mechanism (`OPENGL_ResolveTarget`): a render target's full mip chain regenerates from level
0 via `glGenerateMipmap` when it stops being the active target; games never render into non-zero
mip levels directly. CNA follows it:

- `RenderTarget2D`/`RenderTargetCube`'s `LevelCount` reflects `mipMap`.
- `EasyGLRenderTargetRenderer`/`EasyGLRenderTargetCubeRenderer` pre-allocate storage for every mip
  level and regenerate the chain from level 0 on unbind; `EasyGLRenderer` tracks
  `currentRt2D_`/`currentRtCube_` so switching away from a bound target invokes
  `UnbindAsRenderTarget()`.
- `EasyGL_RenderTarget2D_MipComplete` uses the `TextureFilter::Anisotropic`-renders-black-on-an-
  incomplete-mip-chain signature as its completeness probe.

## 5. MSAA

FNA's algorithm is `MathHelper.ClosestMSAAPower` + `FNA3D_GetMaxMultiSampleCount`:

- `MultiSampleCount` reflects the device-clamped value (`glGetIntegerv(GL_MAX_SAMPLES, ...)`), not
  a raw pass-through. Unlike `LevelCount`, it is legitimately device-dependent in FNA too.
- The target renderers draw into a multisampled color (+ depth) renderbuffer and resolve it via
  `glBlitFramebuffer` on unbind — resolve first, then regenerate mips.
- `EasyGL_RenderTarget2D_MsaaResolve` is differential: a diagonal-edged triangle at
  `MultiSampleCount=0` gives a hard binary edge, at `=8` intermediate (blended) pixels. A solid-fill
  test cannot distinguish a working resolve from a copy.

## 6. Viewport and scissor on target changes

FNA's `SetRenderTargets` resets `Viewport` and `ScissorRectangle` to `(0, 0, newWidth, newHeight)`
on every render-target switch (the new target's size when binding, the backbuffer's when
unbinding). `GraphicsDevice::ResetViewportAndScissorForRenderTarget` does this in all 3
`SetRenderTarget*`/`SetRenderTargets` overloads (`EasyGL_RenderTarget_ViewportScissorReset`).
Rebinding the identical ordered target set is a no-op and does not reset a custom viewport or
scissor (`SOFTWARE-222`, measured XNA).

`GraphicsDevice.Viewport` reaches the renderer: EasyGL converts XNA's top-left rectangle against the
active target's own height, programs the GL viewport and depth range, and keeps the selected viewport
until an actual target transition resets it (`EasyGL_Viewport_Subregion`, the SpriteBatch-target
and deferred-viewport regressions).

## 7. Multiple render targets and compatibility validation

Measured Microsoft XNA behavior (`SOFTWARE-220/221`): CNA rejects duplicate target resources
(including two faces of the same cube), foreign-device resources, mismatched dimensions, mismatched
applied sample counts and different pixel sizes before changing the active set. Exact
`SurfaceFormat` equality is not required: formats with the same pixel size are compatible.

The EasyGL MRT regression (`EasyGL_MRT_TwoAttachments`) uses an explicit multi-output
`ShaderEffect` — `BasicEffect` writes only output location 0 — and verifies ordered
one-through-four-target writes, per-attachment color masks, slot-zero depth ownership, MSAA resolve,
transitions, usage, readback and immediate sampling.

## 8. MRT limits per renderer

| Layer | Simultaneous color attachments | Enforcement |
|---|---|---|
| XNA/CNA `Reach` profile | 1 | Shared profile validation rejects a larger set before renderer work. |
| XNA/CNA `HiDef` profile | 4 (`MAX_RENDERTARGET_BINDINGS`) | Shared `GraphicsDevice::SetRenderTargets` throws above four. |
| EasyGL hardware boundary | `min(4, GL_MAX_DRAW_BUFFERS, GL_MAX_COLOR_ATTACHMENTS)` | The renderer reports and enforces a lower native limit if the context supplies one. |

The shared four-target ceiling means a renderer's larger native allowance never expands the XNA API.
EasyGL's binding path uses four explicit slots and validates the native limit before replacing the
active destinations; it never truncates a set.

---

## Summary: what works, per renderer

| Feature | EasyGL |
|---|---|
| `RenderTarget2D`/`RenderTargetCube` constructors, properties, `IsContentLost`/`ContentLost` | ✅ |
| `RenderTarget2D` sampling after unbind (`SpriteBatch`) | ✅ |
| `RenderTargetCube` sampling after unbind (`EnvironmentMapEffect`, PBR IBL, opt-in source `ShaderEffect`) | ✅ |
| Depth buffer functionality (does depth testing work inside an RT) | ✅ |
| Depth/stencil format fidelity (exact `DepthFormat` honored) | ✅ |
| Mipmap generation (`LevelCount`, real GPU mips) | ✅ |
| MSAA (`MultiSampleCount`, real resolve) | ✅ |
| `SetRenderTarget`/`SetRenderTargets` Viewport/ScissorRectangle reset on a real binding change; identical sets are no-ops | ✅ |
| `Viewport`'s actual GPU effect (subregion, target switch and depth range) | ✅ |
| MRT (ordered 2–4 targets, 2D/cube faces, MSAA and per-slot masks) | ✅ |
| MRT compatibility and device-ownership validation | ✅ |
| MRT count cap matches XNA's `MAX_RENDERTARGET_BINDINGS=4` | ✅ |
