# EasyGL Renderer — Known Bugs and Limitations

> **Status update, 2026-07-11:** this document is dated Task 227 (2026-06-27, see footer) and has
> not been re-audited against thousands of lines of EasyGL changes since (Tasks 228-955). At least
> 2 rows below are now confirmed stale — the `TextureFilter::Anisotropic` row (fixed, Task 918,
> 2026-07-09) and the `CreateRenderTargetCube` `hasDepth`-always-true row (the function was
> reworked to take a real `depthFormat` parameter, no longer a `hasDepth` bool at all, at some point
> after Task 227) — both corrected in place below with a verified-current note. The remaining rows
> were spot-checked, not exhaustively re-verified against current source; treat any row not marked
> "still confirmed 2026-07-11" with appropriate caution and check the cited file:line yourself
> before relying on it.

This document lists confirmed bugs, incorrect mappings, and incomplete features
in `src/CNA/Internal/Renderers/EasyGL/EasyGLRenderer.cpp` and related files.

Status labels: **bug** (wrong behavior), **missing** (feature absent), **limit** (platform
constraint), **diverges** (differs from XNA/FNA, but may be intentional).

---

## Confirmed pre-existing test failures

| File:line | Status | Description |
|---|---|---|
| `EasyGLRenderer.cpp:1331,1358–1382` | **bug** | `SetRenderTargets()` with `count > 1`: `mrtFboReady_` is reset to `false` by every `SetRenderTarget2D(nullptr)` call. This causes a new FBO to be created on every MRT bind, leaking the previous one. The `set_draw_buffers` call is also suspected to be the direct cause of `EasyGL_MRT_TwoAttachments` failing: `easygl::DrawBuffer` may differ from `metagl::DrawBuffer` in a way that makes the static cast UB. |
| upstream easygl | **bug** (upstream) | `easy-gl-resource-smoke-tests` fails because `BindDrawParams` activates `GL_TEXTURE1` for dual-texture/env-map and restores `GL_TEXTURE0`, but the easygl state-tracker's `g_state.last_active_texture` is left desynchronised if a 3D draw ran before the smoke test. Not a CNA bug; requires an upstream easygl fix. |

---

## GLES3 / platform constraints

| File:line | Status | Description |
|---|---|---|
| `EasyGLRenderer.cpp:1583` | **limit** | `FillMode::WireFrame` is silently ignored. GLES3 has no `glPolygonMode`. XNA wireframe rendering is not possible on this renderer. |
| `EasyGLRenderer.cpp:2610` | **bug** | `glDrawElementsBaseVertex` is used unconditionally. This function requires the `GL_OES_draw_elements_base_vertex` or `GL_EXT_draw_elements_base_vertex` extension on GLES3.0; it is only guaranteed on GLES3.2. No extension check is performed. On devices without the extension, indexed draws with a non-zero base vertex silently render wrong geometry or crash. |
| `EasyGLRenderer.cpp:319–321` | **diverges** | `OcclusionQuery` uses `GL_ANY_SAMPLES_PASSED`. XNA `OcclusionQuery.PixelCount` is meant to return an actual pixel count (int), but GLES3 `ANY_SAMPLES_PASSED` returns 0 or 1 only. `PixelCount()` is therefore always 0 or 1, never the true count. |

---

## Incorrect or divergent behaviour

| File:line | Status | Description |
|---|---|---|
| `EasyGLRenderer.cpp:1211–1213` | **bug** | `Clear()` and `ClearColorAndDepth()` unconditionally call `SDL_GetWindowSize()` and set the GL viewport to the full physical window before clearing. When a render-target FBO is bound, this expands the viewport beyond the RT dimensions. The clear viewport should be the RT size when a RT is active. |
| `EasyGLRenderer.cpp:1591–1593` | **bug** | `SetScissorRect()` flips the Y coordinate using `getPhysicalSize()` (physical window height). Correct for the default framebuffer, but wrong when a render-target FBO is bound — `currentRtHeight_` should be used for the flip in that case. |
| ~~`EasyGLRenderer.cpp:1604–1675`~~ | **fixed, Task 918 (2026-07-09)** | Was: `TextureFilter::Anisotropic` selected but `maxAnisotropy` never passed to the GPU, filtering stuck at driver-default (~1×). Now real: gated on `GL_EXT_texture_filter_anisotropic` being available, reads the live driver cap via `glGetFloatv(MaxTextureMaxAnisotropy)`, clamps the requested value to it, and calls `set_parameter(SamplerParameter::MaxAnisotropy, ...)` (verified in current source, 2026-07-11). |
| ~~`EasyGLRenderer.cpp:1305`~~ | **stale, no longer applicable** | Was: `CreateRenderTargetCube(int size)` always passed `hasDepth=true`, a hardcoded boolean ignoring the caller's intent. The function has since been reworked to `CreateRenderTargetCube(int size, int depthFormat, bool mipMap, int multiSampleCount)` — a real `depthFormat` parameter, not a `hasDepth` bool, forwarded through to `EasyGLRenderTargetCubeRenderer` (verified in current source, 2026-07-11). If a depth-format-fidelity gap remains here it would need fresh verification under a new finding, not this one. |
| fog shaders | **diverges** | Fog is computed on `aPos.z` after the WVP transform (clip-space Z), not on view-space depth. XNA fog is linear in view-space distance. Results diverge at oblique viewing angles. |
| `EasyGLRenderer.cpp:2498–2555` | **diverges** | `DrawColoredPrimitives` and `DrawIndexedColoredPrimitives` always use `prog_colored_` with a hardcoded WVP. They ignore `GpuDrawParams` (alpha-test, fog, diffuse color). Any `BasicEffect` state set before calling these paths is silently lost. |
| `EasyGLRenderer.cpp:1740–1745` | **diverges** | Unknown vertex strides fall back to a position-only layout. Strides not in {16, 20, 24, 32, 52} silently render only position data. Custom vertex types with unlisted strides produce wrong geometry without error. |

---

## Missing features

| File:line | Status | Description |
|---|---|---|
| `EasyGLRenderer.cpp:1299` | **missing — still confirmed 2026-07-11** | `preserveContents` parameter in `CreateRenderTarget2D` is ignored (the parameter is explicitly unused, `bool /*preserveContents*/`, in current source). XNA `RenderTargetUsage::PreserveContents` should skip the automatic `DiscardContents` clear. Currently all RTs behave as `DiscardContents`. |
| `EasyGLRenderer.cpp:341, 392` | **missing — still confirmed 2026-07-11** | `EasyGLTextureRenderer::UpdatePixelsLevel` (regular `Texture2D`/`SetData` uploads) never calls `generate_mipmap()`/`glGenerateMipmap`. Samplers using `LinearMipmapLinear` on a texture with only level 0 get undefined results. Note: render targets (`EasyGLRenderTargetRenderer::UnbindAsRenderTarget`) do call `generate_mipmap()` on unbind — that is a separate, unrelated code path added since this row was written; it does not cover ordinary `Texture2D` uploads. |
| `EasyGLRenderer.cpp:67, 107` | **missing** | `CreateTexture3D` and `CreateTextureCube` ignore `surfaceFormat` and always allocate `RGBA8`. Non-RGBA surface formats (`Alpha8`, `HalfVector4`, etc.) are not supported for 3D/cube textures. |
| `EasyGLRenderer.cpp:67, 107` | **missing** | `mipMap=true` is accepted by `CreateTexture3D`/`CreateTextureCube` but no mip storage is allocated and `glGenerateMipmap` is never called. |
| `EasyGLTexture3DRenderer` | **missing** | Volume textures are not registered with the `ResourceRegistry`; context loss still orphans their handles. Cube textures were fixed on 2026-09-04: every mip retains recovery data when enabled, registers with the EasyGL registry and restores its GL texture after loss. |
| `EasyGLRenderer.cpp:233–236` | **missing** | `EasyGLEffectRenderer::Unbind()` is a no-op. Subsequent operations that rely on a specific program being unbound after effect teardown silently see the previously active effect. |
| blend state | **missing** | `ApplyBlendState` does not call `glColorMask`. XNA `BlendState.ColorWriteChannels` (restricting R/G/B/A writes) is silently ignored — CNA always writes all four channels. |
| stencil state | **missing** | Two-sided stencil mode uses the front-face stencil read mask for the back-face stencil function. XNA supports separate front/back stencil read masks; only the write mask is per-face here. |
| `EasyGLEffectRenderer` | **missing** | `SpriteBatch` has a 65 535-index limit (`uint16_t pending_indices_`). Batches larger than 16 383 quads silently wrap/corrupt indices. XNA `SpriteBatch` supports larger batches. |
| `EasyGLRenderer.cpp:975–982` | **limit** | `MultiSampleCount` is applied only at renderer construction via `GraphicsRendererCreateArgs::multiSampleCount`. There is no `IGraphicsRenderer::SetMultiSampleCount()`. Changing `GDM::PreferMultiSampling` or `PresentationParameters::MultiSampleCount` after the device is created updates the PP field but does NOT change the active MSAA sample count — that would require recreating the renderer. |

---

## Missing VertexBuffer / IndexBuffer readback

| | Status | Description |
|---|---|---|
| `VertexBuffer::GetData<T>` | **missing** | Not declared or implemented in CNA. FNA exposes three overloads that read vertex data back from the GPU via `FNA3D_GetVertexBufferData`. In CNA neither `IVertexBufferRenderer` nor `IIndexBufferRenderer` has a readback method, so `GetData` cannot be added without an `IGraphicsRenderer` interface change. |
| `IndexBuffer::GetData<T>` | **missing** | Same as above — no `FNA3D_GetIndexBufferData` equivalent in the renderer interface. |
| `VertexBuffer::SetData` GPU offsetInBytes | **missing** | The `(int offsetInBytes, T[], int startIndex, int elementCount, int vertexStride)` overload (and the `DynamicVertexBuffer` variant with `SetDataOptions`) writes to a specific byte offset inside the GPU buffer. `IVertexBufferRenderer::SetData` only supports writing at offset 0. Implementing this correctly requires adding an `offsetInBytes` parameter to the renderer interface. |
| `IndexBuffer::SetData` GPU offsetInBytes | **missing** | Same — `IIndexBufferRenderer` has no offsetInBytes variant. |

---

## Viewport / coordinate system notes

- **`GetViewportSize` returns the LOGICAL size, not the physical framebuffer size.**
  In `FixedHeightDynamicWidth` mode: `height = virtualHeight_`, `width = physW × virtualHeight_ / physH`.
  If the physical window does not honor `SDL_SetWindowSize` (e.g. some virtual X11 drivers), the
  logical width will not equal the requested `BackBufferWidth`. The PP field is always updated
  correctly; only the viewport width is adaptive.

- **`UpdateViewportFromWindow` skips update if dimensions are unchanged.** A resize that keeps
  the same logical size (e.g. same aspect ratio on a larger display) will not fire a viewport
  change even though the physical framebuffer may have grown.

---

*Last updated: Task 227 (2026-06-27). Audit covers EasyGLRenderer.cpp as of commit `a491501`.
Partially spot-checked against current source 2026-07-11 (see status banner at top) — not a full
re-audit.*
