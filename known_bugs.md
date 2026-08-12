# CNA Known Bugs


## glTF import: the eight defects the forensic audit found (`plan_gltf.md` D1–D8)

`plan_gltf.md` `GLTF-012`. Every one was found by the conformance campaign's own oracle ladder
rather than by a bug report, which is the point worth recording: each produced a **model that
rendered**, so none would have arrived as a bug report at all. Each has a corpus fixture that
reproduces it, and each fixture keeps asserting the fixed behaviour so a regression fails a
green test rather than going quiet again.

| ID | What it did | Fixture | Owning task | Status |
|---|---|---|---|---|
| D1 | Every mesh instance was emitted in mesh-local space with an identity bone, so a mesh instanced by two nodes drew twice at the origin | `xf-shared-mesh` | `GLTF-113`/`GLTF-114` | **Fixed** |
| D2 | A parent node's transform never reached its child, so a scaled parent with a translated child placed the child at its own local offset | `xf-parent-child` | `GLTF-113`/`GLTF-114` | **Fixed** |
| D3 | `node.matrix` was discarded entirely — the import data model had nowhere to put it | `xf-matrix-node` | `GLTF-107`/`GLTF-113` | **Fixed** |
| D4 | A sparse **index** accessor decoded to all zeros: `cgltf_accessor_read_index` returns 0 when the accessor is sparse or has no bufferView, with no error channel, and the caller checked neither | `sparse-indices` | `GLTF-063` | **Fixed** |
| D5 | `primitive.mode` was never read, so every topology was flattened into an index list all three loaders divided by three and drew as a triangle list — a strip lost every triangle after the first, a point cloud became one arbitrary triangle, and neither warned | `mode-triangle-strip`, `mode-lines`, `mode-points` | `GLTF-071`/`GLTF-072`/`GLTF-073`/`GLTF-078` | **Fixed** |
| D6 | Rigid (non-joint) node animation was dropped: an unskinned model's clips had nowhere to live | `anim-rigid-node` | `GLTF-294` | **Fixed** |
| D7 | A factor-only metallic-roughness material was downgraded to an untextured white `BasicEffect` — the selection rule asked which texture *maps* were present, so a material with every PBR factor and no map could never select `PbrEffect` | `mat-factor-only-gold` | `GLTF-215`/`GLTF-216` | **Fixed** |
| D8 | `BuildSkeleton` walked parent links only inside the skin's own joint set, so an armature transform above the joints was dropped from the bind pose while the authored `inverseBindMatrices` still contained it | `skin-armature-ancestor` | `GLTF-245`/`GLTF-247`/`GLTF-249` | **Fixed** |

Two further defects were found in the **vendored cgltf** rather than in CNA, and are worked around
CNA-side with the workaround pinned to the vendored behaviour so an upgrade retires both copies:

| What | Where | Worked around by |
|---|---|---|
| Sparse accessor values were read at the base accessor's stride rather than tightly packed, contradicting cgltf's own validator | `third_party/cgltf/cgltf.h` | `ApplySparseOverridesTightly` (`GLTF-062`) |
| §3.6.2.2's `max(c/N, −1)` clamp was omitted for signed normalized components, so −128 decoded to −1.0079 | `third_party/cgltf/cgltf.h` | `ClampNormalizedSigned` (`GLTF-056`) |

One defect is **partially remediated** rather than fixed, and is recorded as such in the corpus's
own ledger so the conformance suite keeps asserting the current behaviour:

| ID | What it does | Fixture | Owning task |
|---|---|---|---|
| `GLTF-241` | A primitive with `COLOR_0` **and** a metallic-roughness material cannot be imported as the file asks: no CNA vertex layout carries a colour alongside a tangent and no PBR shader reads a colour stream, so it imports through the non-PBR path with its colours and without its material. The stride-24 layout it lands on has no normal slot either, so an authored `NORMAL` is discarded and the primitive cannot be lit at all. Both losses are now reported by name rather than silent. | `mat-vertex-color-pbr` | `GLTF-238`/`GLTF-241` |

Two open glTF-side items remain, both **environment-blocked** rather than undiagnosed:

* `EasyGLRenderer::ApplyLayout`'s silent position-only fallback for an unlisted vertex stride
  (`GLTF-157`). The importer's half is fixed — an unlisted stride now throws instead of returning
  an empty buffer — but the renderer file needs sibling `../easy-gl` and `../meta-gl` checkouts to
  compile, and an unverified renderer change is not a fix.
* The glTF viewer's own defect list lives in `openeggbert/cna-gltf-viewer` (`GLTF-421`), a separate
  repository.

## LLGL post-audit disposition (2026-08-09)

This is the authoritative disposition for LLGL entries later in this historical ledger. It does
not rename or absorb them:

- `LLGL-48` is resolved by a collision-free complete blend-state pipeline-key element.
- `LLGL-52` is resolved on the supported LLGL/OpenGL path; its camera and indexed-effect oracles
  are registered and pass.
- `LLGL-53` is resolved for the supported contract by measured narrowing: viewport/depth/render-
  target cases pass, while non-zero depth bias and stencil reject deterministically and their
  capabilities are false.
- `LLGL-54` retains the proven `RenderTarget2D` MRT shape; cube-face and mip-mapped MRT binds reject.
- `LLGL-55`'s OpenGL/Xvfb build/runtime route is green. Vulkan is compile coverage only and an
  explicit Vulkan runtime request rejects, so historical Vulkan-WSI infrastructure gaps do not
  gate this contract.
- `LLGL-56` remains X11-only. LeakSanitizer allocations rooted in pinned LLGL/SDL/Mesa GLX visual
  selection are classified external rather than hidden or patched in CNA.
- `LLGL-57` (new) fixed first-frame swap-chain/back-buffer extent drift after reset/resize.
- `LLGL-58` (new) fixed the CNA-reachable zero-count clear-value pointer passed into pinned LLGL's
  deferred OpenGL command buffer. ASan/UBSan reports no CNA-owned error after the fix.

The original detailed entries remain below as evidence of discovery and prior experiments. Their
old `OPEN` headings are historical where this disposition explicitly supersedes them.

## Multiple SpriteBatch Begin/End in one frame discards all but the last

**Backend:** Vulkan (confirmed), others unknown.

**Symptom:** If `SpriteBatch::Begin()` / `SpriteBatch::End()` is called more than once
within a single `Draw()` frame, only the draws from the **last** Begin/End pair are
visible. All earlier sprite draws are silently discarded.

**Example:**
```cpp
// Frame Draw():
spriteBatch->Begin();
spriteBatch->Draw(background, Vector2::Zero, Color(255,255,255,255));
spriteBatch->End();   // ← this batch is LOST

spriteBatch->Begin();
spriteBatch->Draw(tank, tankPos, Color(255,255,255,255));
spriteBatch->End();   // ← only this batch renders
```

**Workaround:** Merge all sprite draws into a single `Begin()` / `End()` per frame.
If mixing SpriteBatch with PrimitiveBatch (`DrawUserPrimitives`), call
`spriteBatch->End()` first, then draw primitives, then start a new SpriteBatch
only if strictly necessary — but prefer keeping everything in one batch.

**Discovered in:** cna-samples #021 PathDrawing port (2026-06-27).

---

## LLGL backend: the OpenGL module clears but draws nothing — FIXED 2026-07-31

**Kept as a record, not an open bug.** Filed and fixed the same day; the entry stays because the
misdiagnosis is instructive.

**Symptom was:** with `CNA_LLGL_RENDERER=opengl` the window cleared correctly but no sprite ever
appeared. Every symptom pointed at resource binding: `texCoord`, `color` and the uniform block all
read as zero while the position attribute worked.

**Actual cause:** CNA's own shader-language selection, not LLGL and not the driver. LLGL's OpenGL
core profile advertises `ShadingLanguage::SPIRV` (via `GL_ARB_gl_spirv`) alongside GLSL, and the
backend checked SPIR-V first — so the OpenGL module was handed SPIR-V compiled for Vulkan's binding
model. GL accepted it far enough to rasterize geometry from location 0 and silently zero everything
else. Explicit `layout(location=)`/`layout(binding=)` qualifiers and a `ResourceHeap` binding path
both changed nothing, because neither had anything to do with it. What settled it: a fragment
shader hardcoded to output magenta still rendered black, and instrumenting LLGL's own
`GLLegacyShader::CompileShaderSource` showed it was never called.

**Fix:** prefer GLSL wherever a module reports it; SPIR-V is the fallback for a module with no GLSL
(i.e. Vulkan). Both modules are now pixel-verified, and the OpenGL module has its own CTest
registrations (`Llgl_Smoke_OpenGL`, `Llgl_2D_OpenGL`) so it can never again be broken unnoticed by
whatever the default preference happens to select.

**Tracked as:** `plan_llgl.md` task `LLGL-17`.

---

## LLGL backend: `SetBlendFactor` hits an unsupported GL procedure — FIXED 2026-07-31

**Symptom:** `LLGL::CommandBuffer::SetBlendFactor` threw
`ErrUnsupportedGLProc: illegal use of unsupported OpenGL procedure: glBlendColor` on this
environment's GL context, aborting the frame.

**Fix:** the backend requests dynamic blend-factor state, and issues the call, only when the active
blend state genuinely references `Blend::BlendFactor`/`InverseBlendFactor` — correct, cheaper, and
it keeps the overwhelming majority of blend states off a proc some GL tables genuinely lack. A game
that really uses `Blend::BlendFactor` on such a driver still fails loudly, with LLGL's own error.

**Tracked as:** `plan_llgl.md` task `LLGL-18`.

---

## LLGL backend: sprites drawn into a `RenderTarget2D` landed in a tiny corner — FIXED 2026-07-31

**Symptom:** a `SpriteBatch` draw issued while a `RenderTarget2D` was bound produced no visible
content in the sampled region of the target at all — reading the target's colour attachment back
(directly via `GetData()`, or after sampling it onto the screen) returned the clear colour
everywhere the test checked.

**Cause:** every sprite's pixel-space vertex positions are converted to clip space by a single,
frame-global orthographic projection matrix (`spriteProjectionBuffer_`), uploaded once per frame
from the SWAP CHAIN's own resolution. A sprite queued while a much smaller `RenderTarget2D` (e.g.
64x64) was bound still had its vertex positions read through that same swap-chain-sized (e.g.
800x480) projection, collapsing the whole draw into a sliver near one corner of the target's clip
space instead of filling it.

**Fix:** each `LlglRenderTargetBackend` now owns its own fixed pixel-to-clip-space projection
buffer, built once at construction (a render target's resolution never changes after creation,
unlike the swap chain's, which can resize). `QueueSpriteEXT` records which projection buffer a
sprite command needs, and `ReplayFrameCommandsList` binds that one instead of the frame-global
buffer whenever it is set.

**Tracked as:** `plan_llgl.md` task `LLGL-26`.

---

## LLGL backend: destroying a `RenderTarget2D` before `Present()` segfaulted — FIXED 2026-07-31

**Symptom:** a `RenderTarget2D` created, drawn into, and released — all within one `Draw()` call,
with no other reason to touch the render target afterwards — segfaulted inside
`RecordAndSubmitFrame` the next time the frame was actually submitted, dereferencing
`bucket.target->GetResolution()` on a pointer to an already-freed `LLGL::RenderTarget`.

**Cause:** `LlglRenderTargetBackend`'s destructor released its `LLGL::RenderTarget` and textures
immediately, but this backend defers every draw into `frameCommands_`/`FrameCommandBucket` and
only actually replays them at `Present()`/`ReadBackbuffer()` time. A `RenderTarget2D` going out of
scope before that point is a perfectly ordinary pattern (XNA allows it, and nothing about drawing
into a scratch render target implies keeping it alive past the `Draw()` that used it) — the exact
same class of bug already fixed for `VertexBuffer`/`IndexBuffer` earlier in this backend's work.

**Fix:** `LlglRenderTargetBackend` now takes an owning `LlglGraphicsBackend*` and its destructor
calls `ScheduleRenderTargetReleaseEXT()` instead of releasing immediately — deferring the release
of the render target, its colour texture and its own sprite projection buffer until the frame that
may still reference them has actually been submitted, exactly like `ScheduleBufferReleaseEXT`
already does for GPU buffers.

**Tracked as:** `plan_llgl.md` task `LLGL-26`.

---

## LLGL backend: `RenderTarget2D::GetData()` read stale/undefined pixels — FIXED 2026-07-31

**Symptom:** calling `GetData()` on a `RenderTarget2D` immediately after drawing into it — with no
intervening back-buffer read to force a flush — returned `(0,0,0,0)` instead of what was drawn.

**Cause:** `LlglRenderTargetBackend::GetData()` read the colour attachment straight off the GPU
texture via `RenderSystem::ReadTexture()`. Unlike a plain `Texture2D` (populated by an immediate
`WriteTexture()`), a render target's content only exists once its queued `frameCommands_` have
actually been recorded and submitted — which normally happens at `Present()` or
`ReadBackbuffer()`/`CaptureBackbuffer()`, neither of which a direct `GetData()` call triggers.

**Fix:** added `LlglGraphicsBackend::FlushPendingFrameEXT()` -- submits and waits for any queued
frame commands without presenting -- and `LlglRenderTargetBackend::GetData()` calls it first.

**Tracked as:** `plan_llgl.md` task `LLGL-26`.

---

## LLGL backend: `VertexColorEnabled` was silently ignored for every colour-carrying 3D draw — FIXED 2026-07-31

**Symptom:** any `BasicEffect`/`DualTextureEffect` draw using a vertex layout that carries a colour
attribute (`VertexPositionColor`, `VertexPositionColorTexture`, ...) had the vertex colour
multiplied into the tint regardless of `VertexColorEnabled` — setting it `false` had no effect as
long as the buffer itself happened to carry a colour attribute.

**Cause:** none of the four colour-carrying 3D vertex shaders (`colored3d.vert.glsl`,
`colored_textured3d.vert.glsl`, `lit_colored3d.vert.glsl`, `lit_colored_textured3d.vert.glsl`)
ever read `GpuDrawParams::vertexColorEnabled` at all — `FillEffectUniforms()` never wrote it
anywhere, and every one of these shaders computed `vColor = diffuseColor * color` (or the lit
equivalent, `vTint`) unconditionally, purely from whether the VERTEX LAYOUT happened to carry a
colour attribute, never from what the effect actually asked for. Found while implementing
`DualTextureEffect` (`LLGL-25`): its own `VertexColorEnabled=false` check read back the SAME value
as the `=true` case. `examples/llgl_basiceffect_test.cpp`'s own pre-existing Check D was silently
relying on this bug — it drew a `VertexPositionColorTexture` quad and expected the vertex colour to
apply without ever setting `VertexColorEnabled = true` first.

**Fix:** `FillEffectUniforms()` now writes the flag to `uniforms[32]` for unlit draws (safely
overwritten by `worldMatrix` for lit ones) and to `uniforms[51]` (`ambientColorLighting.w`, an
otherwise-unused component) for lit draws. Every colour-carrying vertex shader gates its multiply
on the flag: `vColor = (vertexColorEnabledPad.x > 0.5) ? diffuseColor * color : diffuseColor;` (or
`ambientColorLighting.w` for the lit ones). Because OpenGL requires an identically named/laid-out
uniform block across every shader stage linked into one program, EVERY unlit 3D shader — including
the ones that never read the new field — had to grow its own `Transform` block declaration from
128 to 144 bytes to match, or linking failed with `definitions of uniform block 'Transform' do not
match`. `llgl_basiceffect_test.cpp`'s Check D now explicitly sets `VertexColorEnabled = true` (as
real XNA usage requires) and gained two new checks proving `VertexColorEnabled = false` genuinely
leaves a colour-carrying draw untinted.

**Tracked as:** `plan_llgl.md` task `LLGL-25`.

---

## LLGL backend: `GraphicsDevice.DrawUserPrimitives()`'s typed overloads all threw — FIXED 2026-07-31

**Symptom:** any typed `DrawUserPrimitives()` overload (`VertexPositionColor`,
`VertexPositionTexture`, `VertexPositionColorTexture`, `VertexPositionNormalTexture`, ...) threw
`"this vertex layout is not supported by the colour-only 3D path (stride N with no vertex
declaration). Supply a VertexDeclaration, or use a VertexPositionColor layout."` on this backend.

**Cause:** every typed `DrawUserPrimitives()` overload packs its own GPU-format struct
(`GraphicsDevice.cpp`'s `GpuVPC`/`GpuVPT`/`GpuVPCT`/`GpuVPNT`) and uploads it through
`backend_->CreateVertexBuffer(int)` (count-only) + a raw byte `SetData()` — never through a real
`VertexDeclaration`. `LlglVertexBufferBackend::ResolveVertexAttributes()` already had a
stride-based fallback for exactly this no-declaration case, but it only recognised stride 16
(`VertexPositionColor`) — strides 20/24/32 (`VertexPositionTexture`/`VertexPositionColorTexture`/
`VertexPositionNormalTexture`) fell through to an empty attribute list and the draw path refused
them by name. Found while implementing `DualTextureEffect` (`LLGL-25`): its own cross-backend test,
`examples/dualtextureeffect_vertexcolor_test.cpp`, uses `VertexPositionColorTexture` (stride 24)
through `DrawUserPrimitives()` and hit this exact refusal.

**Fix:** extended the stride-based fallback to recognise all four GPU-packed stream sizes this
backend's shader family already supports (16/20/24/32 bytes), each a distinct, unambiguous size —
the same "infer the vertex format from the upload stride" technique the Vulkan backend's own
`MakeExt3DKey()` already relies on for these exact stream sizes. Skinned/tangent streams (48/52/68
bytes) are deliberately still unrecognised: `SkinnedEffect` is not implemented on this backend at
all yet, so there is no shader to feed them to.

**Tracked as:** `plan_llgl.md` task `LLGL-32`.

---

## LLGL backend: `EnvironmentMapEffect` rendered fully black whenever fog was disabled — FIXED 2026-07-31

**Symptom:** every `EnvironmentMapEffect` draw with `FogEnabled=false` (the default) rendered its
whole surface in the same colour as `FogColor` (black, since fog was never configured) instead of
the real lit/reflected colour — the cube map, lighting, and diffuse texture all had zero visible
effect.

**Cause:** `env_map3d.frag.glsl`/`.gl.frag.glsl`'s fog blend was written as
`mix(fogColor.rgb, rgb, vFogFactor)`, copied verbatim from the plain Vulkan-standalone backend's
own `env_map3d.frag.glsl` — but that backend's `vFogFactor` uses the OPPOSITE convention (0 = fully
fogged) from this backend's own established one (`lit_textured3d.vert.glsl`/`.frag.glsl`: 0 = no
fog, matching `mix(original, fogColor, vFogFactor)`). This backend's `env_map3d.vert.glsl` computes
`vFogFactor` using its OWN (0-means-no-fog) convention — correctly copied from
`lit_textured3d.vert.glsl` — so the two halves of the formula disagreed: with fog disabled
(`vFogFactor=0`, `fogColor` left at its zeroed default), `mix()` returned its FIRST argument
(`fogColor.rgb` = black) unconditionally, discarding the real shading result entirely.

**Fix:** swapped the `mix()` argument order to `mix(rgb, fogColor.rgb, vFogFactor)` in both shader
flavours, matching this backend's own established fog convention instead of the source line it was
transliterated from. Found while adding `Llgl_EnvironmentMapEffect_AlphaScaledLerp` (reusing the
pre-existing, cross-backend `examples/environmentmapeffect_alphascaledlerp_test.cpp`): both its
checks read back solid black instead of the expected cube-map colour, isolated via a series of
temporary debug-shader edits (hardcoded output colour, then real cube sampling with a fixed
direction, then real `N`/`E`/`reflDir`, then individual field readouts) that progressively narrowed
the divergence down to the final `mix()` call.

**Tracked as:** `plan_llgl.md` task `LLGL-25 (EnvironmentMapEffect)`.

---

## LLGL backend: drawing into a real MSAA `RenderTarget2D` produced zero antialiasing — FIXED 2026-08-01

**Symptom:** requesting a genuine `MultiSampleCount` for a `RenderTarget2D` and drawing a diagonal
edge into it produced a hard, unblended step at the edge — identical to a non-multisampled render
target — even though `RenderTarget2D.MultiSampleCount`/`GetMultiSampleCount()` correctly reported a
real, backend-applied sample count greater than 1. No crash, no validation error, no exception —
just no actual antialiasing.

**Cause:** every graphics pipeline this backend builds (3D primitives, `SpriteBatch`, custom
`ShaderEffect`s) hardcoded `pipelineDesc.rasterizer.multiSampleEnabled = (swapChain_->GetSamples() >
1)` and, for 3D primitives, `pipelineDesc.renderPass = swapChain_->GetRenderPass()` — both keyed off
the SWAP CHAIN's own sample count/render pass regardless of what target was actually bound at draw
time. This was invisible before real per-render-target MSAA existed, since every render target was
previously single-sample, matching the swap chain's own usual default (0/no MSAA), so the two never
disagreed. Once `CreateRenderTarget2D` could apply a real sample count higher than the swap chain's,
a pipeline built with `multiSampleEnabled=false` was drawn into a genuinely multisampled framebuffer.
LLGL's Vulkan module (lavapipe, a software rasterizer, on this project's own test environment)
accepted this silently instead of raising a validation error, rasterizing single-sample coverage
into every sample of the MS image — the resolve step then averaged identical values back down,
producing a hard edge with no blend, the exact symptom observed.

**Fix:** two new accessors mirror the already-existing `GetPrimaryRenderPassEXT()`/
`GetActiveColorAttachmentCountEXT()` "ask the currently bound target, not the swap chain" pattern:
`LlglBoundRenderTarget::GetSampleCount()` (1/no-MSAA by default; overridden by
`LlglRenderTargetBackend` to return its own real, device-clamped sample count; MRT binds and
`RenderTargetCube` faces do not support MSAA yet, so their default of 1 is correct) and
`LlglGraphicsBackend::GetPrimarySampleCountEXT()` (the bound target's own sample count, or the swap
chain's when nothing is bound). Every `multiSampleEnabled`/`renderPass` assignment across
`AcquirePrimitivePipeline` and the shared `FillCurrentBlendAndRasterStateEXT` (used by both the
sprite pipeline and every custom `ShaderEffect` pipeline) now calls these instead of reading the
swap chain directly, and `MakeBlendPipelineKey` folds the sample count into its cache key so a
pipeline built for one sample count is never handed back for a draw that needs another. Diagnosed
by adding a temporary debug printf of the raw scanned edge-pixel values before and after the fix
(confirmed the same hard 255→0 step with `appliedSampleCount=4` before the fix, and a genuine
mid-tone blended pixel at the same scan position after it) rather than guessing at the cause.

**Tracked as:** `plan_llgl.md` task `LLGL-26` (MSAA render targets follow-up).

---

## LLGL backend: per-slot `ColorWriteChannels1..3` silently did nothing under MRT — FIXED 2026-08-01

**Symptom:** setting `BlendState.ColorWriteChannels1 = ColorWriteChannels::None` (or any other
non-default per-slot mask) while drawing into a multi-render-target set had no effect at all —
slot 1 still received the same, fully unmasked write as slot 0, as if the property had never been
set.

**Cause:** `FillCurrentBlendAndRasterStateEXT` correctly filled `pipelineDesc.blend.targets[slot]`
with each slot's own colour write mask, but LLGL only actually READS `blend.targets[i]` per
attachment when `GraphicsPipelineDescriptor::blend.independentBlendEnabled` is explicitly `true` —
confirmed by reading `VKGraphicsPSO.cpp`'s own `CreateColorBlendState`
(`desc.targets[desc.independentBlendEnabled ? i : 0]`) and `GLBlendState.cpp`'s identical
`if (desc.independentBlendEnabled)` branch. This backend never set that flag, so every attachment
silently reused `targets[0]`'s own mask (and blend factors/functions, though those are always
identical across slots for XNA anyway, so only the colour mask divergence was ever visible)
regardless of what slots 1..3 were actually configured with.

**Fix:** `pipelineDesc.blend.independentBlendEnabled = (clampedCount > 1)` in
`FillCurrentBlendAndRasterStateEXT`, where `clampedCount` is the number of active colour
attachments (1 outside an MRT bind, so this is a no-op for every pre-existing caller).

**Found by:** extending `examples/llgl_mrt_test.cpp` with a real 2-draw masking test (an unmasked
baseline draw establishing distinguishable per-slot content, then a masked draw attempting to
overwrite both slots with white) and observing slot 1 read back the masked draw's own value instead
of the baseline's — then reading LLGL's Vulkan/OpenGL source directly rather than guessing at the
cause. A first version of the test (a single masked draw against a sentinel colour written in an
EARLIER, separate MRT bind cycle) produced a false failure signal of its own: this backend's render
passes use `Undefined`/`DONT_CARE` load semantics (see `LlglRenderTargetBackend`'s own doc
comment), so content is only ever guaranteed to survive WITHIN one bind cycle, never across a later,
separate rebind — the sentinel was legitimately gone before the masked draw even ran, for a reason
unrelated to masking. Fixed in the TEST by keeping both draws inside one bind cycle.

**Also found, module-dependent, not a CNA defect:** once the real bug was fixed, the Vulkan module
(lavapipe, on this project's own test environment) genuinely masks a non-zero slot; the OpenGL
module (llvmpipe via GLX) does not — slot 1 still reads back the unmasked value, meaning
`glColorMaski`'s own per-draw-buffer masking is not honoured by this environment's GL driver. The
test detects this and reports `[SKIP]` for that one check on the OpenGL module rather than failing,
matching the existing `WireFrame`/back-buffer-MSAA precedent for a real, environment-specific
driver constraint outside this backend's control.

---

## LLGL backend: 3D pipeline cache ignores `ColorWriteChannels`/blend factors for `DrawPrimitives` — OPEN

**Status:** open, discovered during LLGL-33, not fixed. `BlendState.MultiSampleMask` itself
(the actual LLGL-33 feature) is unaffected and shipped normally — see `docs/llgl-backend.md`.

**Symptom:** `AcquirePrimitivePipeline` (the 3D/`DrawPrimitives` pipeline cache used by
`BasicEffect` and friends, as opposed to `AcquireSpritePipeline`/`LlglEffectBackend::AcquirePipeline`
used by `SpriteBatch`/custom effects) folds only the **low 16 bits** of
`MakeBlendPipelineKey()`'s ~48-bit result into its own cache key. That truncation silently discards
`colorSrcBlend_`/`colorDstBlend_`/`alphaSrcBlend_`/`alphaDstBlend_`/`colorBlendFunc_`/
`alphaBlendFunc_`/`colorWriteChannels_[0]` entirely (they occupy the sub-key's higher-order bits).
Two 3D draws differing ONLY in one of those fields can be assigned the SAME cached
`LLGL::PipelineState*`, so the second draw silently keeps the FIRST draw's blend/write-mask state
baked in instead of its own.

**Found by:** registering the shared, cross-backend `examples/gfx077_colorwritechannels_3d_test.cpp`
(already used by `SdlGpu`/`WebGpu`) against this backend — its very first check (differential
`ColorWriteChannels::None` vs `::All` baselines through `BasicEffect`+`DrawPrimitives`) failed, both
baselines reading back identically, proving `ColorWriteChannels` had no effect on that path. This
test is **not** currently registered as an LLGL CTest (see `cmake/Tests/LlglTests.cmake`) precisely
because it fails against this open bug; do not register it again until the underlying issue is
fixed.

**Attempted fixes, both abandoned:** widening the outer truncation to include the missing fields —
first via an XOR + FNV-prime multiply (matching `MakeVertexLayoutKey`'s own `mix` style), then via a
plain positional multiply-add fold (matching this function's own established style, avoiding the XOR
approach entirely) — fixed the `ColorWriteChannels_3D` test in isolation but broke an unrelated,
previously-passing test: `Llgl_BasicEffect`'s alpha-blend check
(`"Alpha blends the white texel halfway to the background"`) started rendering
`(64,64,64,191)` instead of the correct `(128,128,128,128)`, with BOTH fold strategies, even though
instrumented tracing showed the pipeline built for that draw is provably correctly configured
(`src=SourceAlpha, dst=InverseSourceAlpha`, its own uniquely-computed key, its own freshly-built
`LLGL::PipelineState*`, no cache collision with any other draw).

**The genuinely confusing part:** reverting to the OLD, truncated key — the one that provably causes
the alpha-blend draw to be assigned the SAME cached `PipelineState*` as an EARLIER, unrelated Opaque
draw (identical pointer, identical key, confirmed via instrumented tracing) — renders the alpha-blend
draw CORRECTLY. Reusing a pipeline object whose baked-in blend state is `Opaque`
(`blendEnabled=false, srcColor=One, dstColor=Zero`) for a draw that needs real
`SourceAlpha`/`InverseSourceAlpha` blending should, under ordinary GPU fixed-function blend
semantics, produce an unblended result — it does not. No explanation was found before this
investigation was shelved in favour of shipping the safe, unaffected parts of LLGL-33. Hypotheses
considered but not confirmed or ruled out: (a) LLGL/lavapipe pipeline object interning/de-duplication
keyed on something coarser than full descriptor content (e.g. shader/layout identity or
`debugName`), causing distinct `LLGL::PipelineState*` C++ wrapper objects to alias the same
underlying GPU pipeline; (b) some sensitivity to the total number of distinct
`GraphicsPipelineDescriptor`s created across the process's lifetime; (c) an incorrect assumption
elsewhere in this reasoning about how this backend actually applies blend state, not yet identified.

**Current state:** `AcquirePrimitivePipeline` keeps the original, known-buggy `& 0xFFFFu`
truncation (see its own doc comment in `LlglGraphicsBackend.cpp`) rather than shipping either
attempted fix. `BlendState.MultiSampleMask` is NOT affected by this limitation:
`MakeBlendPipelineKey` folds `multiSampleMask_` in LAST, so it occupies the sub-key's lowest 4 bits
and survives the `& 0xFFFFu` truncation intact — confirmed by `Llgl_MultiSampleMask`/`_OpenGL`
passing (4/4 PASS on the Vulkan module, 3 PASS + a correctly-detected `[SKIP]` on the OpenGL
module, which never applies a sample mask at all — see `LlglGraphicsBackend.cpp`'s own
`FillCurrentBlendAndRasterStateEXT` doc comment).

**Next step for whoever picks this up:** do not retry the same two fold strategies without new
information. Consider instrumenting at the LLGL/Vulkan API call level (not just this backend's own
descriptor construction) to see whether two DIFFERENT `LLGL::PipelineState*` C++ objects might be
returned pointing at the same underlying `VkPipeline` handle, or whether `vkCmdBindPipeline` is
actually being issued with the value this backend thinks it queued.

**Tracked as:** `plan_llgl.md` task `LLGL-21`.

---

## LLGL backend: per-draw `Viewport` is not captured/replayed, only per-render-pass-bucket — FIXED 2026-08-02

**Symptom:** a game that sets a DIFFERENT `GraphicsDevice.Viewport` before each of several draws
into the SAME target within one unflushed frame (no `GetData()`/`Present()` between them) got
every one of those draws rasterized with whichever viewport was set LAST, not its own. Confirmed by
three independent, unrelated test files all failing the same way:

- `examples/spritebatch_viewport_switch_test.cpp` (2/6 PASS before the fix): two `SpriteBatch`
  batches, each `Begin()`'d after its own distinct `Viewport`, both drawing into the back buffer in
  one frame -- the SECOND batch's sprite landed correctly, the FIRST batch's sprite was read back
  at the SECOND viewport's own footprint instead of its own.
- `examples/spritebatch_custom_viewport_test.cpp` (7/13 PASS before the fix): Check C1/C2
  specifically (a transformed sprite drawn under a custom sub-region `Viewport`) failed; checks
  that only ever used the default full-target viewport (B1/B2/D1-D3) passed.
- `examples/rendertargetcube_plural_binding_test.cpp` (9/14 PASS before the fix): its own
  `SampleFaces()` helper draws 6 `EnvironmentMapEffect` quads into 6 DIFFERENT sub-rectangles of
  the back buffer (one per cube face, via `device.setViewportProperty(Viewport(x0,y0,...))`
  immediately before each `DrawUserPrimitives()` call) in one unflushed frame, then reads the whole
  back buffer back at the end -- 4 of the 6 faces came back wrong (stable, reproducible wrong
  values, not garbage), 2 coincidentally still correct. This looked at first like a genuine
  cube-face-index/array-layer mapping bug, but was not: the cube-face WRITE side of this test uses
  the plain, already-thoroughly-verified singular `SetRenderTarget(cube, face)` API (LLGL-36:
  56/56 PASS on the dedicated `GetData` contract oracle); only the READ-back side (`SampleFaces`'s
  own multi-viewport back-buffer probe) was affected, confirming the same root cause as the two
  `SpriteBatch` files above, not a new one.

**Root cause, and why it was actually TWO bugs:**

1. `RecordAndSubmitFrame()`/`CaptureBackbuffer()` called
   `commands_->SetViewport(LLGL::Viewport{0, 0, resolution.width, resolution.height})` exactly
   ONCE per `FrameCommandBucket` -- i.e. once per DISTINCT TARGET IDENTITY the frame's queued
   commands group into (see `GroupFrameCommandsByTargetEXT()`), always sized to the WHOLE target,
   never to whatever `GraphicsDevice.Viewport` sub-rectangle was active when a given command was
   queued. This is the bug that actually needed a GPU-level `SetViewport()` fix: 3D primitives
   resolve their clip-space position to the screen through the GPU's own rasterizer viewport
   transform, so a stale/wrong GPU viewport genuinely misplaces them.
2. Sprites, however, turned out to work differently: `QueueSpriteEXT()` bakes sprite geometry
   straight into window/target PIXELS at queue time (a deliberate design choice -- see its own
   comment -- so the GPU viewport can stay at the whole target and the projection can stay constant
   for the whole frame), and it never added a custom `Viewport`'s X/Y offset to that geometry at
   all -- only the SCISSOR was narrowed to the viewport rectangle (`ComputeEffectiveScissor()`), so
   a sprite drawn under a sub-region `Viewport` still baked its position as if the viewport were
   the whole target, then got clipped (not repositioned) by the scissor. Per FNA's own contract
   (`SpriteBatch.cs PrepRenderState`: `CreateOrthographicOffCenter(0, Viewport.Width,
   Viewport.Height, 0, 0, 1)`), sprite destination coordinates are VIEWPORT-LOCAL and the rasterizer
   viewport transform is what actually positions them on screen -- so the FIX for sprites is a pure
   translation by `Viewport.X`/`Viewport.Y`, not a GPU viewport change at all.

**Fix:** `CaptureFrameCommandViewportEXT()` fills a new `FrameCommand::viewport[4]` physical-pixel
rectangle for every queued command, exactly like `command.scissor`/`command.scissorEnabled` already
were -- the whole target by default, narrowed only for `Primitives` commands when a custom
`Viewport` is active (mirroring `ComputeEffectiveScissor`'s own viewport-narrowing branch).
`ReplayFrameCommandsList()` now issues `commands_->SetViewport()` per `Clear`/`Primitives`/`Sprite`
command instead of `RecordAndSubmitFrame()`/`CaptureBackbuffer()` issuing it once per bucket before
the replay loop -- this is what actually fixes 3D primitives (`rendertargetcube_plural_binding`'s
own `EnvironmentMapEffect` draws). Separately, `QueueSpriteEXT()` now adds `viewportRect_[0]`/`[1]`
(when `viewportSet_`) to sprite geometry before the existing letterbox scale is applied -- this is
what actually fixes the two `SpriteBatch` files.

**A third, unrelated bug found and fixed along the way:** `LlglSpriteBatchBackend::Begin()`
unconditionally reset its own `transform_` member to identity -- but `SpriteBatch::Begin()` always
calls `backend_->SetTransformMatrix(transformMatrix_)` BEFORE `backend_->Begin()`, so any custom
`transformMatrix` passed to `SpriteBatch.Begin()` was silently discarded the instant `Begin()` ran.
This is what `spritebatch_custom_viewport_test.cpp`'s Check C1/C2 (a transformed sprite) was
actually hitting once the viewport-offset fix above was in place -- fixed by simply not resetting
`transform_` in `Begin()` (nothing needs resetting: it's always freshly set immediately before).

**Verified:** all three files now PASS in full under the default/`auto` (Vulkan) module: 14/14
(`rendertargetcube_plural_binding`), 13/13 (`spritebatch_custom_viewport`), 6/6
(`spritebatch_viewport_switch`). See the next entry for a newly-found, separate OpenGL-module-only
limitation these files still hit under `CNA_LLGL_RENDERER=opengl`.

**Tracked as:** `plan_llgl.md` Phase LLGL-7, `LLGL-39`.

---

## LLGL backend: OpenGL module renders nothing for the backbuffer after an intervening render-target bind — FIXED (LLGL-51, 2026-08-04)

**Root cause, confirmed via live instrumentation of the vendored LLGL OpenGL module (temporary,
fully reverted -- `~/deps/LLGL` is pristine again):** `GraphicsDevice.GetBackBufferData()` /
`LlglGraphicsBackend::CaptureBackbuffer()` ends with LLGL's own `CommandBuffer::
CopyTextureFromFramebuffer()`, which internally does two GL operations: `glCopyTexSubImage2D`
(framebuffer -> an intermediate texture, confirmed via direct `glReadPixels`/`glGetTexImage`-style
probes to be UNAFFECTED by `GL_SCISSOR_TEST`, matching the OpenGL spec) followed by
`glBlitFramebuffer` (intermediate texture -> the destination staging texture, confirmed to BE
scissor-clipped, also per spec). Nothing in `CaptureBackbuffer()`'s own command sequence ever
resets `GL_SCISSOR_TEST`/`GL_SCISSOR_BOX` before this call -- and this project's own
`ComputeEffectiveScissor()` (see its own doc comment) deliberately computes an "effective scissor"
rectangle for EVERY `Primitives` draw whose viewport is smaller than its target, as the mechanism
that clips XNA-style sub-viewport rendering. Whichever draw ran LAST in the capture's own render
pass leaves that draw's own (correct, intentional) scissor rectangle active at the GL level, and
the later blit silently clips the ENTIRE backbuffer readback down to that one small rectangle --
every pixel outside it reads back as the destination staging texture's own zero-initialised
(black, alpha-zero) memory, never touched by the blit at all. This is not a Y-flip/coordinate bug
(the original 2026-08-02 hypothesis, see below, was investigating the wrong mechanism) and not
scissor/viewport application to the DRAWS themselves (both were independently confirmed correct at
every step via live pixel probes) -- it is a driver-state-hygiene gap around one specific,
non-draw, internal LLGL operation.

**Confirmed empirically at every step**, not reasoned from source alone: added temporary
`fprintf`/`glReadPixels` probes to `~/deps/LLGL`'s `GLCommandExecutor.cpp` (after each draw) and
`GLFramebufferCapture.cpp` (before/after the copy, before/after the blit) -- draws land correctly
on FBO 0, `glCopyTexSubImage2D` correctly captures them into the intermediate texture, and the
blit's destination is black ONLY outside whatever `GL_SCISSOR_BOX` happened to be active
(`(64,0,32,72)` for one failing leg, `(0,0,32,72)` for another -- both exactly matching that leg's
own last draw's sub-viewport). Verified the fix two ways before implementing it for real: first by
`glDisable(GL_SCISSOR_TEST)` right before the blit (temporary, vendored-side experiment), then by
only widening `GL_SCISSOR_BOX` to the full destination (matching what CNA's own code can actually
do via the public `LLGL::CommandBuffer::SetScissor()` API, since there is no public way to reach
into this internal call and disable the test itself) -- both fixed every affected check.

**Fix** (`src/CNA/Internal/Backends/Llgl/LlglGraphicsBackend.cpp`, `CaptureBackbuffer()`): issue
`commands_->SetScissor(LLGL::Scissor{0, 0, bucketResolution.width, bucketResolution.height})`
immediately before `commands_->CopyTextureFromFramebuffer(...)`. This does not need to disable the
scissor test itself -- widening the box to cover the whole destination means nothing inside it is
ever clipped, achieving the same effect through the public API alone. No change to the vendored
LLGL dependency (would diverge from the pinned `Release-v0.04b` tag).

**Verified, `CNA_LLGL_RENDERER=opengl`, Xvfb `:99`** (`git stash` pre/post comparison against the
pre-fix binary): `deferred_scissor_capture_test.cpp` 43/47 -> **47/47 (full pass)**;
`deferred_viewport_capture_test.cpp` 35/39 -> 37/39 (its own F2/F3, the only two checks this
mechanism could explain, both now pass; the remaining two, E1/E2, are a genuinely separate,
already-declared "this rasterizer has no viewport depth remap" limitation, unaffected by this fix);
`spritebatch_custom_viewport_test.cpp` and `spritebatch_viewport_switch_test.cpp` -- this ticket's
own ORIGINAL tracked symptom, described below -- now **13/13 and 6/6, full passes**. A full
69-binary regression sweep (both Vulkan-default-on-DRI3-less-Xvfb and OpenGL-forced) shows zero
new regressions elsewhere. `rendertargetcube_plural_binding_test.cpp` still cannot be verified on
this sandbox's OpenGL module -- blocked by the separate, pre-existing `hasCubeTextures` gap, not
by anything this fix touches.

**`_OpenGL` ctest lanes added** for all four files named in `LLGL-51`'s own acceptance gate
(`Llgl_SpriteBatch_CustomViewport_OpenGL`, `Llgl_SpriteBatch_ViewportSwitch_OpenGL`,
`Llgl_Deferred_Scissor_OpenGL` -- full passes -- and `Llgl_Deferred_Viewport_OpenGL`, kept
registered despite E1/E2 as a real regression trip-wire, matching this project's own established
partial-pass-suite precedent). `rendertargetcube_plural_binding` still has no `_OpenGL` lane (same
`hasCubeTextures` blocker as above).

---

<details>
<summary>Original investigation history (2026-08-02/03), superseded by the confirmed root cause above</summary>

**Status:** re-investigated for `plan_llgl.md`'s `LLGL-51` via LIVE instrumentation of the
vendored LLGL source (temporary `fprintf` added to `~/deps/LLGL`'s own `GLStateManager.cpp`, run
against this backend's real Xvfb/llvmpipe environment, then fully reverted via `git checkout --` --
`~/deps/LLGL` is a pristine, unmodified pinned checkout again). This narrows out the ORIGINAL
"Y-conversion" hypothesis below almost entirely; the real mechanism is still not confirmed.

**The original "Y-offset" framing does not match the actual failing test code.** Re-reading
`deferred_viewport_capture_test.cpp`'s own F2/F3 legs (the two that fail) shows every `SetViewport()`
call in both legs uses `y = 0` and the FULL canvas height -- only X and width ever vary (horizontal
bands, not vertical ones). There is no non-zero-Y viewport/scissor rectangle anywhere in either
failing leg. The title/symptom below (inherited from `spritebatch_custom_viewport_test.cpp`'s own,
separate, still-plausible Y-offset finding) does not explain F2/F3's own failure at all.

**Live instrumentation directly contradicts every part of the original root-cause narrative below:**
- `flipViewportYPos_` and `framebufferHeight_` (the two fields `AdjustViewport`/`AdjustScissor` use)
  ARE correctly restored to the swap chain's own correct values (`flip=1`, `fbH=`the real logical
  canvas height) every single time `GLStateManager::BindRenderTarget()` switches back to the swap
  chain from an off-screen target -- both `GLSwapChain::MakeCurrent()` (resyncs `framebufferHeight_`)
  and the immediately following `BindGLRenderTarget(nullptr)` (calls `SetClipControl(GL_LOWER_LEFT,
  ...)`, which unconditionally resyncs `flipViewportYPos_`) fire on every swap-chain rebind, observed
  directly via `fprintf` at every one of dozens of rebinds across a full test run -- never once stale.
- The ACTUAL `SetViewport()` calls for F2's own green/blue backbuffer draws (`(0,0,32,72)` and
  `(64,0,32,72)`, logical canvas 96x72) produce the CORRECT, unchanged output (`flip=1` but `y=0` so
  the flip is a no-op either way) -- there is no visible viewport-Y miscomputation for this leg at
  the point `glViewport` is actually issued.

**What remains genuinely unexplained:** the pixels are not merely repositioned, they are ABSENT
(read back as the clear colour, as if the draw never happened) for both F2's backbuffer portion
(read AFTER an off-screen target was bound and returned from) and F3 (a backbuffer-only leg, no
render target involved at all, that fails only because it runs immediately after F2 in the same
process/GL context -- consistent with some form of state corruption from F2 leaking forward, not an
independent Y-flip bug of its own). Since viewport application itself now measures correct, the
likely remaining suspects are the FRAMEBUFFER-COPY/readback path (`CommandBuffer::
CopyTextureFromFramebuffer`, used by `CaptureBackbuffer()`) or scissor-test/other draw state left
over from the off-screen target's own bind, neither of which this pass instrumented. Whoever
continues this should instrument THOSE paths next (same reversible `~/deps/LLGL` technique used
here) rather than the viewport-flip math this pass already ruled out.

**Original hypothesis (2026-08-02, narrowed but not confirmed at the time; superseded by the above
for F2/F3 specifically, but may still explain `spritebatch_custom_viewport_test.cpp`'s own separate,
genuine non-zero-Y findings -- not re-investigated this pass):

**Symptom:** `spritebatch_custom_viewport_test.cpp` and `spritebatch_viewport_switch_test.cpp` PASS
completely (13/13, 6/6) under the default/`auto` (Vulkan) module but read back ZERO matching pixels
-- not wrong position, entirely empty -- for every check whose effective scissor/viewport rectangle
against the SWAP CHAIN has a NON-ZERO Y offset, specifically under `CNA_LLGL_RENDERER=opengl`
(GLX/llvmpipe software rasterizer). A rectangle with a non-zero X offset but Y=0 (e.g.
`rendertarget_viewport_scissor_reset_test.cpp`'s own right-half scissor, already registered and
passing on both modules) renders correctly on OpenGL too -- only the Y axis is affected.

**What was ruled out:** this is not a regression from the fix above, and not a coordinate-math bug
in this backend's own code -- `ComputeEffectiveScissor()`/`CaptureFrameCommandViewportEXT()` compute
byte-identical rectangles for both modules (confirmed via temporary debug instrumentation), and
those exact numbers produce correct, byte-exact results on Vulkan. `rendertargetcube_plural_binding
_test.cpp` cannot be used to confirm this finding independently: it already hits the separate,
pre-existing `LLGL::RenderingFeatures::hasCubeTextures not supported` gap on this same OpenGL module
(see `docs/llgl-backend.md`'s "SkinnedEffect"/environment-map sections), so it never reaches a draw.

**Root cause (narrowed, not confirmed):** LLGL's OpenGL command buffer passes `Viewport`/`Scissor`
x/y straight to `glViewport`/`glScissor` without any flip of its own
(`GLImmediateCommandBuffer::SetViewport`/`SetScissor`); the upper-left-origin normalization this
project's own code relies on (see `UploadFrameResources()`'s own comment: "LLGL normalizes
[viewport and scissor rectangles] to upper-left everywhere") is instead applied one layer down, in
`GLStateManager::AdjustViewport`/`AdjustScissor`, gated on `flipViewportYPos_` -- itself set from
whether `glClipControl(GL_UPPER_LEFT, ...)` is genuinely honoured by the driver
(`GLStateManager::SetClipControl`). For the swap chain/default framebuffer specifically,
`GLStateManager::BindRenderTarget` requests `GL_LOWER_LEFT` (OpenGL's native origin) and relies on
LLGL's own CPU-side flip to compensate, rather than asking the driver to remap the origin -- a path
that requires `ARB_clip_control` to be genuinely available and consistently applied to both the
vertex-shader-visible clip space and the viewport/scissor rectangle. A software GLX/llvmpipe driver
not exposing (or inconsistently emulating) `ARB_clip_control` is the most likely explanation for a
mismatch that only appears on a Y-offset sub-region against the backbuffer specifically -- but this
was not independently confirmed by reading Mesa/llvmpipe's own capability reporting, only inferred
from LLGL's own source.

**Why this project can't easily work around it:** the affected rectangles are computed identically
for both modules and are provably correct on Vulkan; adding a module-specific manual Y-flip in
CNA's own code would require either (a) discovering the EXACT compensation LLGL's own emulation
path is failing to apply (risking a second, harder-to-diagnose mismatch if the real cause turns out
to be something else in this specific environment), or (b) bypassing LLGL's screen-origin
abstraction entirely for the GL module, which the project's own architecture (a single call site per
draw kind, backend-agnostic) is not set up for. Filed as an environment/module limitation rather
than attempted blind.

**Tracked as:** `plan_llgl.md` Phase LLGL-8, `LLGL-51` (still open; also blocks `LLGL-7`'s own
`LLGL-39` from registering `_OpenGL` variants of `spritebatch_custom_viewport`/
`spritebatch_viewport_switch`/`rendertargetcube_plural_binding` -- see
`cmake/Tests/LlglTests.cmake`'s own comment there). `Llgl_Deferred_Viewport`/`Llgl_Deferred_Scissor`
remain at 37/39 / 43/47 on the OpenGL module.

</details>

---

## LLGL backend: the swap chain's own render pass replayed wherever it FIRST appeared, not last — FIXED 2026-08-02

**Symptom:** discovered while wiring `plan_llgl.md`'s Phase LLGL-7 (`LLGL-40`,
`backbuffer_pass_order_test.cpp`). The ordinary XNA pattern "render to a texture, then composite it
onto the backbuffer in the SAME unflushed frame" (`RenderTarget2D t; SetRenderTarget(&t); fill red;
SetRenderTarget(null); draw t onto the backbuffer`) sampled `t` as pure zero/transparent-black
content instead of red, whenever ANY earlier command in that same frame had already touched the
backbuffer (e.g. an initial `GraphicsDevice.Clear()` before the render-target work) -- checks A1/A2
of `backbuffer_pass_order_test.cpp` failed with `(0,0,0,0)`, not merely a stale/wrong colour.

**Root cause:** `GroupFrameCommandsByTargetEXT()` built `FrameCommandBucket`s in FIRST-APPEARANCE
order across every distinct target INCLUDING the swap chain (`target == nullptr`) -- so a frame
whose very first queued command was a backbuffer `Clear()` put the swap chain's own bucket FIRST in
replay order, before the render-target bucket that hadn't been touched yet even ran. A backbuffer
draw that samples a render target as a texture therefore executed before that target had ever been
written to at all, reading whatever a freshly-created texture starts with (not "the target's content
as of an earlier cycle", which is what the file's own documented "backbuffer trails everything"
architecture -- see `docs/llgl-backend.md`'s "One render pass per distinct target" bullet -- was
already supposed to guarantee).

**Fix:** the swap chain's own commands are now pulled into a separate bucket as they're found
(instead of taking a slot in `buckets` at their own first-appearance position) and that bucket is
unconditionally appended LAST, after every render-target bucket, regardless of when the frame first
touched the backbuffer. This matches what `docs/llgl-backend.md` already documented as the intended
design and is the same "trailing pass" shape Vulkan/SdlGpu had before their own REMED-GFX-143 fixes
-- `orderedBackbufferSegments` in the new `backbuffer_pass_order_test.cpp` `CNA_BACKEND_LLGL`
Contract branch stays declared `false` (this backend still does not give each backbuffer cycle its
own segment), but the COLLAPSED result the `false` declaration predicts is now what actually happens
(both consumers see the target's FINAL content), rather than an undefined/garbage read.

**Verified:** `backbuffer_pass_order_test.cpp` 30/30 PASS on the default (Vulkan) module (29/30 run
cleanly under `CNA_LLGL_RENDERER=opengl` before hitting the separate, pre-existing
`hasCubeTextures not supported` gap on check M2). Full `Llgl` (67/67) + `CnaTests` sweep run clean
afterward with zero regressions.

**Tracked as:** `plan_llgl.md` Phase LLGL-7, `LLGL-40`.

---

## LLGL backend: a `Texture2D`/`TextureCube`/`Texture3D` destroyed before `Present()` segfaulted — FIXED 2026-08-02

**Symptom:** discovered while wiring `plan_llgl.md`'s Phase LLGL-7 (`LLGL-40`,
`backbuffer_readback_dimension_test.cpp`/`backbuffer_first_read_test.cpp`). A completely ordinary
pattern -- create a `Texture2D`, draw it via `SpriteBatch` inside a helper function, let it go out of
scope when that function returns, THEN call `GraphicsDevice.GetBackBufferData()` later in the same
`Draw()` -- crashed with `SIGSEGV` inside LLGL's own `VKDescriptorCache::EmplaceDescriptor`, reached
through `ReplayFrameCommandsList()`'s `Sprite` case dereferencing a `command.texture` that no longer
pointed at a live `LLGL::Texture`. Confirmed with `gdb`: `LlglTextureBackend::~LlglTextureBackend()`
released the underlying `LLGL::Texture` IMMEDIATELY and unconditionally, the instant the C++
`Texture2D` wrapper went out of scope -- but this backend defers replaying queued sprite/primitive
commands until `Present()`/`GetBackBufferData()` actually flushes the frame, so any `FrameCommand`
that had already captured a pointer to that texture (`command.texture`, `envMapTexture`,
`pbrNormalTexture`, etc.) was left dangling well before the frame that referenced it ever replayed.
`LlglTextureCubeBackend`/`LlglTexture3DBackend` had the exact same immediate-release destructor and
the identical bug, just not yet independently triggered by a test.

**Fix:** all three texture backend classes now take an owning `LlglGraphicsBackend*` at construction
(mirroring `LlglVertexBufferBackend`'s own `ScheduleBufferReleaseEXT` precedent) and their
destructors call a new `ScheduleTextureReleaseEXT()` instead of releasing immediately -- it releases
right away only when no frame is currently queued (nothing could be referencing the texture), and
otherwise defers into the already-existing `pendingTextureReleases_` pool (previously only fed by
`RenderTargetCube`'s own colour/depth textures), drained by the already-existing
`ReleasePendingBuffers()` after every frame submission's `queue_->WaitIdle()`.

**Verified:** the crash reproduced on `backbuffer_readback_dimension_test.cpp`'s very first leg
(A1, a plain rectangle-less read after one ordinary `SpriteBatch` draw) and `backbuffer_first_read
_test.cpp` (12/13 legs crashed) before the fix; both run to completion with zero crashes after it
(`backbuffer_readback_dimension_test.cpp`: 8/8 PASS; `backbuffer_first_read_test.cpp`: 9/13 PASS,
the other 4 blocked on a separate, unrelated finding -- see the next entry). Full `Llgl` + `CnaTests`
sweep run clean afterward with zero regressions.

**Tracked as:** `plan_llgl.md` Phase LLGL-7, `LLGL-40`.

---

## LLGL backend: `FixedHeightDynamicWidth`'s logical width ignores the requested backbuffer width — FIXED

**Status:** fixed by `plan_llgl.md` `LLGL-50` (2026-08-03). The question this entry's own earlier
"why this needs more than a test-wiring fix" section left open -- whether the derived width should
ever be allowed to fall below the requested one at all -- is answered: no. `ComputePresentationRect()`
now treats the aspect-derived width as a FLOOR, not a hard override:
`logicalWidth = virtualWidth_ > 0 ? std::max(derivedWidth, virtualWidth_) : derivedWidth`. A window
WIDER (relative to its own height) than the requested aspect is unaffected -- `derivedWidth` already
exceeds `virtualWidth_` there, exactly matching this mode's own already-tested "a wider window shows
more content" contract (`llgl_presentation_test.cpp` Check E, still 6/6 PASS, unchanged). Only a
window NARROWER than the requested aspect (this project's own fixed ~800x480 headless test window
combined with a short/tall requested backbuffer) now keeps the full requested width addressable,
letterboxing on the other axis instead of silently shrinking the addressable space.

**Symptom:** `backbuffer_first_read_test.cpp`'s D63/D64/D65 legs (backbuffers 63x17/64x17/65x17,
deliberately probing GPU row-pitch alignment boundaries) and E1 (64x32) all fail with columns near
and past a specific boundary reading back as pure `(0,0,0,alpha)` -- the frame's own `Clear()`
colour, not the drawn pattern -- while every other leg using a size below that boundary (`A1`/`A3`/
`A4`/`A6`/`B1`-`B4`/`C1`, and separately every size `backbuffer_readback_dimension_test.cpp` probes:
37x23, 41x29, 50x40, 30x20) passes cleanly.

**Root cause:** `ComputePresentationRect()`'s `CnaPresentationMode::FixedHeightDynamicWidth` branch
(this backend's own default presentation mode) computes `logicalWidth = round(physicalWidth *
logicalHeight / physicalHeight)` -- deriving the logical (virtual-resolution) coordinate space's own
WIDTH purely from the PHYSICAL window's aspect ratio, discarding `virtualWidth_` (the game's own
requested `PreferredBackBufferWidth`, correctly captured via `SetVirtualResolution()`) entirely. In
this project's own headless test environment the physical window consistently ends up ~800x480
regardless of what a game requests (confirmed via `SDL_GetWindowSizeInPixels` immediately after
`SDL_CreateWindow`, independent of any CNA/LLGL code -- an environment/SDL/X11 characteristic, not
something this backend controls), so a request for a very SHORT backbuffer (height 17 or 32) derives
a narrow logical width (round(800*17/480)=28, round(800*32/480)=53) that is SMALLER than what a wide
aspect ratio actually requested (63-65, or 64) -- every column at or past the derived boundary falls
outside this backend's own internal coordinate space (used for BOTH `QueueSpriteEXT`'s geometry
baking and `ReadBackbuffer`'s own sampling), landing on whatever the frame's `Clear()` left there
instead of the drawn content. `backbuffer_readback_dimension_test.cpp`'s own probed sizes all happen
to keep the requested width under their own derived boundary, which is why that file did not surface
this on its own, and why it is safe to keep registered as-is.

**Why a floor, not a pin:** `presentationParameters_.getBackBufferWidthProperty()` already correctly
reports the game's own requested width (63, not 28) -- `GraphicsDevice::GetBackBufferData` sizes its
read from that, which is why every affected leg still returns the FULL requested element count with
none of it left unwritten (poison-free), just with wrong CONTENT past the derived boundary. The fix
does not pin the logical extent to the requested size unconditionally (which would silently discard
`FixedHeightDynamicWidth`'s own intentional "width follows window aspect" feature, already tested and
relied on by `llgl_presentation_test.cpp` Check E) -- it only raises the derived value up to the
requested one when the derivation would otherwise fall short, preserving both contracts at once.

**Third reproduction, a different symptom variant:** `bound_target_lifetime_test.cpp` requests a
72x36 backbuffer (`kBBW`/`kBBH`). Empirically confirmed via a temporary debug print (added and
removed, not left in the source): `physical=800x480 virtual=72x36`, so `logicalWidth =
round(800*36/480) = 60`, thirteen columns short of the requested 72 -- the SAME formula, the same
fixed-physical-window environment characteristic. The observable symptom differs from D63/D64/D65
above: rather than reading back the frame's `Clear()` colour, the out-of-bounds column (7 of 8) reads
back the PREVIOUS valid column's content (`PatternColor(6)` instead of `PatternColor(7)`) --
consistent with the same root cause surfacing through a different code path (a clamped sample rather
than an unwritten region), not a second, independent bug. This affects nearly every leg's own
`RequireBackbufferExact` check (15 of 18), since it is unconditional in this fixture with no
per-backend declaration to route around it. The two exceptions (G1, G2) route their backbuffer check
through a leg that Present()s and lets the physical window settle first, and J1 has no backbuffer
check at all -- those three legs pass in full. Critically, **0 of 18 legs crashed**: the actual
REMED-GFX-168 defect this fixture exists to catch (a SIGSEGV when a bound render target is destroyed
mid-cycle) does not reproduce on LLGL at all -- every leg's own destroy-while-bound-specific
assertions (the NEXT target reads correctly, a live sibling in an MRT set still resolves and survives,
`Present()` refuses identically for a live or a destroyed bound target, 120 create/destroy-while-bound
rounds complete cleanly) pass everywhere they are not entangled with the unrelated backbuffer-column
finding above.

**Fourth reproduction, the same story again:** `deferred_source_lifetime_test.cpp` (`LLGL-43`) also
requests a 72x36 backbuffer and hits the identical column-7-reads-column-6 symptom on 9 of its 17
legs. Critically, **0 of 17 legs crashed** here either: the REMED-GFX-167 defect this fixture exists
to catch (a heap-use-after-free when a deferred draw's SOURCE dies before the frame that queued it
replays) does not reproduce on LLGL at all. The 8 legs whose own assertions never touch the backbuffer
(B1, B2, C1, E1, E2, I1, K1, L1) pass in full, including a `RenderTargetCube` destroyed before
`Present()`, 120-round handle-reuse safety, and a source released while still in-flight across two
more frames.

**Verified fixed** (`CNA_LLGL_RENDERER=opengl`, Xvfb, 2026-08-03): `backbuffer_first_read_test.cpp`
now passes all 13 legs (supervisor: 13/13, 0 crashed) -- D63/D64/D65 and E1 all read back their own
exact requested content now, registered as `Llgl_BackBuffer_FirstRead`. `bound_target_lifetime_test.cpp`
went from 3/18 to 17/18 legs passing (the one remaining failure, F1, is the SEPARATE, PRE-EXISTING
OpenGL-module `hasCubeTextures` limitation documented elsewhere in this file -- confirmed unrelated
by reproducing it identically against the pre-`LLGL-50` binary too), the other 17 registered
individually as `Llgl_BoundTargetLifetime_<leg>`. `deferred_source_lifetime_test.cpp` went from
8/17 (this entry's own earlier count of "legs that pass in full" turned out to already include E1/E2
inaccurately -- they hit the same cube limitation as `bound_target_lifetime_test.cpp`'s F1 both
before and after this fix) to 15/17, the other 15 registered as `Llgl_DeferredSourceLifetime_<leg>`.
`llgl_presentation_test.cpp` Check E (the mode's own intentional "wider window shows more content"
behavior) remains 6/6 PASS, unaffected.

**Tracked as:** `plan_llgl.md` Phase LLGL-8, `LLGL-50` -- fixed and verified (see above); all three
files are now registered (see `cmake/Tests/LlglTests.cmake`'s own comments there for the full
per-leg breakdown of what remains excluded and why).

---

## LLGL backend: `backbuffer_pass_order_test.cpp`'s own Contract was stale after LLGL-45; correcting it exposes a new, real, narrower gap (V1/V2) — OPEN

**Status:** discovered 2026-08-03 while running a broad regression sweep after `LLGL-45`/`LLGL-46`/
`LLGL-49`/`LLGL-50`. `backbuffer_pass_order_test.cpp`'s own `CNA_BACKEND_LLGL` `Contract` branch
still declared `orderedBackbufferSegments = false` (the pre-`LLGL-45` bucket-by-identity behavior)
after `LLGL-45` had already fixed the underlying replay engine -- a stale test assumption, not a code
regression. Corrected the declaration to `true` and re-ran: **dozens of checks that were previously
silently `skip()`ped** ("this backend replays all backbuffer work in one trailing pass") **now
genuinely evaluate and PASS** (A3-A6, C1, C3, C5, C6, O1-O4, U1, U2, M1, and A1 itself, which had
started FAILING once the stale `false` value made its own COLLAPSED-result assertion wrong) --
strong, broad, independent confirmation that `LLGL-45`'s own fix is correct. This is registered
here as its own entry (not folded into the `LLGL-45` entry above) because it also surfaces something
`LLGL-45` did NOT fix.

**The new, real, open finding:** V1 ("viewport per cycle") and V2 ("scissor per cycle") now FAIL for
real, for the first time ever measured -- previously masked by the stale `false` contract, which
made these checks silently skip too. Both drive the SAME shape: three backbuffer cycles
(A: sub-Viewport/scissor X, draw; B: a DIFFERENT target bound in between; A again: a DIFFERENT
sub-Viewport/scissor, a SECOND draw only covering part of the first cycle's own area) and expect the
backbuffer to show BOTH the first cycle's surviving (non-overdrawn) content AND the third cycle's
new content, each still clipped to its OWN viewport/scissor rectangle from when it was actually
queued. V1 fails its combined position assertion outright (`ok == false`, no per-pixel detail
captured); V2 fails with 18/? probes wrong, first probe reading the CLEAR colour instead of the
expected red -- i.e. the FIRST cycle's own draw appears to be MISSING from the final image, not
merely at the wrong scissor position. Not yet root-caused: this is a narrower, more specific defect
than the general ordering `LLGL-45` fixed (segment placement itself is now empirically correct, per
every OTHER check in this same file passing), so the cause is more likely in how per-command
viewport/scissor state interacts with a target being revisited (e.g. something about how the
SWAP CHAIN specifically -- as opposed to an off-screen `RenderTarget2D`, which is what
`deferred_viewport_capture_test.cpp`'s own already-passing K2 leg exercises for a similar
same-target-two-cycles shape -- carries its own per-segment viewport captures across a Load-reload).

**Not yet verified whether this also affects the Vulkan module**, the actual default target this
file's own registered `Llgl_BackBuffer_PassOrder` CTest runs against -- this sandbox's Vulkan module
cannot present (no DRI3), the same infrastructure gap affecting several other findings this session.
The file also independently crashes (`ValidateGLTextureType: hasCubeTextures not supported`) on a
LATER, cube-texture-dependent leg under the OpenGL module specifically -- confirmed PRE-EXISTING
(reproduces identically against the pre-`LLGL-45` binary too), the same OpenGL-module capability gap
already documented elsewhere in this file, unrelated to this entry.

**Tracked as:** `plan_llgl.md` Phase LLGL-8 (a follow-up correction to `LLGL-45`, not its own
numbered ticket). `Llgl_BackBuffer_PassOrder` remains registered (unchanged from before this
session) since this sandbox cannot re-verify it against Vulkan either way; whoever next has real
Vulkan or a DRI3-capable Xvfb should re-run it and, if V1/V2 fail there too, open a dedicated
follow-up ticket for the per-cycle viewport/scissor-on-backbuffer gap specifically.

---

## LLGL backend: a target revisited after depending on another target replays out of public order — FIXED (4/5 reproductions), U2 unverified

**Status:** fixed by `plan_llgl.md` `LLGL-45` (2026-08-03): `GroupFrameCommandsByTargetEXT()` now
segments `frameCommands_` in TRUE public order (a new segment starts only when the target actually
changes from the immediately preceding command, instead of merging every command sharing one target
into a single first-appearance bucket), and every segment's own `BeginRenderPass()` uses a real
`AttachmentLoadOp::Load` render pass (`AcquireLoadRenderPassEXT()`) so a target's real prior content
genuinely survives being revisited later in the same frame -- real `RenderTargetUsage.PreserveContents`
across a same-frame rebind, not merely an accident of bucket merging. The former "swap chain always
trails every other bucket" special case (the `LLGL-40` fix) is gone: the swap chain now gets its own
ordered segment(s) like any other target, appended at the end only when the frame's own true order
never touched it at all.

**Verified fixed, 2026-08-03 (fresh runs after the fix, `CNA_LLGL_RENDERER=opengl`, Xvfb):**
`rendertarget_producer_consumer_test.cpp` D5 and I2 (41/41 checks, up from 39/41),
`rendertarget_effect_source_test.cpp` F1 (32/32, now 19/20 legs passing and registered, up from
18/20), `rendertarget_backbuffer_consumer_test.cpp` G1 (86/86 checks, all registered). All three
files' own `CNA_BACKEND_LLGL` Contract branches needed no changes -- they already correctly declared
what SHOULD happen; only the replay engine was wrong.

**Not yet verified: `rendertarget_depthstencil_usage_test.cpp`'s U2** (two `RenderTargetCube` faces
sharing one physical depth buffer, replayed out of public order). The fix's own render-pass helper
(`AcquireLoadRenderPassEXT()`) is generic over any `LLGL::RenderTarget` -- keyed only by colour-
attachment count, depth/stencil presence and sample count, all read directly off the target at replay
time -- so it applies uniformly to a cube face exactly like a plain `RenderTarget2D`, with no
per-target-kind special-casing; U2's own scenario is expected to be fixed by the same ordering +
real-Load mechanism. This is NOT confirmed empirically, though: this sandbox's OpenGL module reports
`LLGL::RenderingFeatures::hasCubeTextures == false` (confirmed via `rendertarget_pass_boundary_test.cpp`
crashing identically on both the pre- and post-fix binary, i.e. a pre-existing, unrelated environment
limitation), and its Vulkan module cannot present under this sandbox's Xvfb (no DRI3,
`VK_ERROR_SURFACE_LOST_KHR`) -- so U2 cannot be run at all here. `rendertarget_depthstencil_usage_test.cpp`
is not yet even wired up as a `cna_llgl_test()` build target (LLGL-38's real-hardware pass, or a
DRI3-capable Xvfb per LLGL-55, should verify and register it).

**Original symptom, five independent reproductions of the same root cause across four files
(historical, for context on what was broken):**
- `rendertarget_depthstencil_usage_test.cpp`'s U2 check (28/29 checks otherwise pass): clear face A
  and face B of a `RenderTargetCube` to depth 1.0 each, then draw into face A at depth 0.25, then
  draw into face B (depth-tested) at depth 0.50 -- expects face B's draw to be REJECTED (0.50 is
  farther than the 0.25 face A's draw already wrote into the depth buffer they share, per FNA's own
  one-`glDepthStencilBuffer`-per-cube convention this backend already implements for storage).
  Instead face B's draw is ACCEPTED, as if the shared depth buffer still read 1.0.
- `rendertarget_effect_source_test.cpp`'s F1 check ("A -> B -> A round trip"): produce a pattern
  into target A, consume A into target B (an ordinary 3D draw sampling A as a texture), then consume
  B back into A again, then read A -- expects A to end up holding the pattern (relayed through B).
  Instead A reads back as `(0,0,0,0)` -- the consume-B-into-A draw sampled B before B had ever been
  produced into at all.
- `rendertarget_producer_consumer_test.cpp`'s D5 check ("A -> B -> A"): produce a pattern into A,
  consume A into B, then produce an ALT pattern into A again (a second, later bind cycle) -- expects
  B to hold the FIRST cycle's content (what it actually sampled) and A, read last, to hold the
  SECOND cycle's content. Instead B reads back the alt pattern too: A's bucket drains both of its own
  cycles as one unit before B's bucket (positioned later, since B first appeared after A's first
  cycle) ever runs, so B's own draw -- despite being queued between A's two cycles -- samples A only
  after A's SECOND cycle has already overwritten it.
- `rendertarget_producer_consumer_test.cpp`'s I2 check: produce target `u` (cycle 1), sample `u` onto
  the BACKBUFFER (cycle 2), reproduce `u` with different content (cycle 3), then -- after an unrelated
  target is produced and read mid-frame -- read the backbuffer back. Expects the backbuffer draw to
  show cycle 1's content (what it actually sampled, mid-frame). Instead it shows cycle 3's content:
  `u`'s bucket (cycles 1 and 3) drains fully at its own first-appearance position, and the swap-chain
  bucket -- forced to always trail every other bucket by the `LLGL-40` fix -- ends up sampling `u`
  only after cycle 3 has already overwritten it, even though cycle 2's own draw was queued BETWEEN
  `u`'s two cycles. The very fix that resolved the swap-chain's own FIRST-appearance-order bug
  (`LLGL-40`) is what makes this manifestation possible: forcing the swap chain to a fixed trailing
  position is exactly wrong when some earlier bucket gets revisited after the swap chain's own read
  was supposed to happen.
- `rendertarget_backbuffer_consumer_test.cpp`'s G1 check (88/90 checks otherwise pass): produce
  target A (cycle 1), sample A on the BACKBUFFER (consumer 1, expecting cycle 1's content), reproduce
  A with alt content (cycle 2), then sample A on the BACKBUFFER again (consumer 2, expecting cycle
  2's content). G2 (consumer 2) passes -- it correctly sees cycle 2, the content actually left in A's
  bucket once it finally drains. G1 (consumer 1) fails, reading cycle 2's content instead of cycle 1's
  -- the same swap-chain-always-trails-last interaction as `rendertarget_producer_consumer_test.cpp`'s
  I2 above, just with the two backbuffer consumers merged into one batch's readback instead of a
  separate mid-frame target read in between.

**Root cause:** `GroupFrameCommandsByTargetEXT()` buckets commands by target IDENTITY and replays
each bucket FULLY, one at a time, in the bucket's own first-appearance order -- correct only when
buckets never depend on each other's content. Both symptoms break that assumption a different way:
- U2: two DIFFERENT `LLGL::RenderTarget` objects (one per cube face) alias the SAME physical depth
  `LLGL::Texture`. The public sequence "clear A; clear B; draw A; draw B" replays as [face-A bucket:
  clear, draw] then [face-B bucket: clear, draw] -- face B's own EARLIER "clear to 1.0" (queued
  before face A's draw, but living in face B's bucket) ends up replayed AFTER face A's draw, wiping
  the shared depth value face A had just written.
- F1: target A is bound TWICE -- once to be produced, once again (later, after B depends on A) to
  consume B. Because A's bucket already exists (and already appeared first), the SECOND bind's
  commands are simply APPENDED to A's existing bucket rather than getting their own later position
  -- so A's bucket (draining fully before B's bucket even starts) replays its own later "consume
  B" command before B's bucket has produced anything into B at all.

Both are the same underlying model failure: a bucket, once it has any commands, drains its ENTIRE
accumulated command list as one contiguous unit at its OWN first-appearance position, regardless of
whether some of that bucket's LATER commands should, in true public order, run interleaved with (or
after) some OTHER bucket's commands that appeared in between. This is the same general shape as the
(now-fixed) `LLGL-40` swap-chain-bucket-ordering bug, generalized: that fix special-cased ONE target
(the swap chain) to always trail every other bucket, which works because the backbuffer never
PRODUCES something another bucket depends on. Two ordinary render targets (or two faces sharing one
resource) can depend on each other in either direction, so no single "always goes last" rule can fix
this case -- a general fix needs either true interleaved replay across buckets, or replacing the
whole "one bucket per target identity" model with "one native pass per public bind cycle,
positioned in true public order" (the real form of `segmentsBindCycles`/`orderedBackbufferSegments`
these test files' own Contracts describe as the ideal, currently-undeclared-as-true shape).

**The fix (LLGL-45, option (b) above):** `GroupFrameCommandsByTargetEXT()` now builds one segment per
contiguous run of same-target commands in TRUE public order -- a target revisited after another
target's (or the swap chain's) commands appeared in between gets its OWN new segment in its own
original position, rather than being merged into whichever segment first used that target. Every
segment's own `BeginRenderPass()` uses `AcquireLoadRenderPassEXT()`'s `AttachmentLoadOp::Load` pass
(a small, backend-lifetime cache keyed by colour-attachment count/depth-stencil presence/sample
count, since every target and the swap chain in this backend always share the same colour and
depth-stencil FORMAT), so a revisited target's real prior content is genuinely reloaded rather than
begun from undefined memory -- safe even for a target's first-ever segment, since a `DiscardContents`
bind already queues its own explicit `Clear()` as that segment's first command. This is option (b),
not (a): no cross-bucket interleaving was needed once buckets stopped being merged by identity in the
first place.

**Tracked as:** `plan_llgl.md` Phase LLGL-8, `LLGL-45` -- fixed and verified for D5/F1/G1/I2 (see
above); `rendertarget_producer_consumer_test.cpp` and `rendertarget_backbuffer_consumer_test.cpp` are
now fully registered (`Llgl_RenderTarget_ProducerConsumer`, `Llgl_RenderTarget_BackbufferConsumer`),
and `rendertarget_effect_source_test.cpp`'s F1 leg is registered alongside its other passing legs.
U2/`rendertarget_depthstencil_usage_test.cpp` remains unregistered pending real-hardware or
DRI3-capable-Xvfb verification (see above).

---

## LLGL backend: a custom `ShaderEffect` using multiple Vulkan descriptor sets crashes the driver — FIXED (implementation verified in isolation; end-to-end Vulkan run still needed)

**Status:** fixed by `plan_llgl.md` `LLGL-46`/`LLGL-47` (2026-08-03) via option (b) from this entry's
own original "why this needs more than a test-wiring fix" analysis: `LlglEffectBackend::CompileProgram()`
now scans the SPIR-V shaderc just compiled for any `OpDecorate .../DescriptorSet` value other than 0
(`SpirvUsesOnlyDescriptorSetZero()`, a minimal targeted binary scan -- not a full reflection library,
matching this class's own established "no SPIRV-Cross dependency" precedent) and fails compilation
(`compileError_` set, `valid_` stays false, no `LLGL::Shader`/pipeline object ever created) before
reaching the crash-prone path, instead of letting a mismatched shader through to
`LLGL::VKGraphicsPSO::CreateVkPipeline`. `rendertarget_effect_source_test.cpp`'s own C1 leg already
has a graceful escape hatch for exactly this outcome (`if (!custom.IsEffectValid()) { boundary(...);
return; }`), so the fix does not need to make a multi-descriptor-set shader WORK, only fail safely.

**Verified so far (2026-08-03):** a standalone scratch program (not part of the project, `libshaderc`
linked directly) compiled the EXACT shader source text from both
`examples/llgl_shadereffect_test.cpp` (the currently-passing custom-effect test, which omits `set=`
entirely, defaulting to set 0) and `rendertarget_effect_source_test.cpp`'s own C1 shaders (`set = 1`/
`set = 2`/`set = 3`) through the real `shaderc_compile_into_spv`, then ran the EXACT
`SpirvUsesOnlyDescriptorSetZero()` logic now in `LlglGraphicsBackend.cpp` against the real compiled
bytes: the working shader's SPIR-V is correctly `allowed`, and C1's own vertex AND fragment SPIR-V
are both correctly `rejected`. A broad OpenGL-module regression sweep (including
`Llgl_ShaderEffect`, unaffected since that path never reaches shaderc at all on this module) shows no
regressions.

**Not yet verified end-to-end:** this sandbox's Vulkan module cannot present under its own Xvfb (no
DRI3), so `rendertarget_effect_source_test.cpp`'s C1 leg itself cannot actually be run through the
real backend here to confirm the graceful `boundary()` path is reached in practice (as opposed to
the isolated logic check above) -- the same infrastructure gap `LLGL-45`'s own U2 and `LLGL-46`'s
Vulkan-module verification hit. `Llgl_RenderTarget_EffectSource_C1` is not registered as a CTest
until `LLGL-38`'s real-hardware pass or a DRI3-capable Xvfb (`LLGL-55`) can confirm it there.

**Symptom:** `rendertarget_effect_source_test.cpp`'s C1 check crashes the whole process
(`SIGSEGV`) partway through (15/20 legs otherwise pass, 1 crashed) the moment it compiles and first
uses a custom `ShaderEffect` whose GLSL source declares resources across THREE different Vulkan
descriptor sets (`layout(set = 1, binding = 0)`, `set = 2`, `set = 3`). `custom.IsEffectValid()`
reports true (shaderc compiles the GLSL to SPIR-V without error), so the test proceeds to draw with
it; the crash happens later, inside `LLGL::VKGraphicsPSO::CreateVkPipeline` -> deep inside the
Vulkan driver itself (`libvulkan_lvp.so`, the lavapipe software rasterizer this project's own test
environment runs on), confirmed via `gdb`.

**Root cause:** `LlglGraphicsBackend::AcquireCustomEffectLayoutEXT()` builds ONE `VkDescriptorSetLayout`
(Vulkan descriptor set 0) with three bindings (`PC` at binding 1, `colorMap` at binding 2,
`samplerState` at binding 3) -- matching this project's OWN `llgl_shadereffect_test.cpp` fixture,
whose shader correctly omits `set = ...` entirely (`layout(binding = 2) uniform sampler2D
colorMap;`, defaulting to set 0 in GLSL). `rendertarget_effect_source_test.cpp`'s shared,
cross-backend fixture shader instead explicitly spreads its resources across sets 1/2/3 -- a
convention other backends' own custom-effect infrastructure apparently tolerates, but this
backend's single-descriptor-set `AcquireCustomEffectLayoutEXT()` does not. Creating a
`VkPipeline` from a SPIR-V module that references descriptor sets the bound `VkPipelineLayout`
never declared corresponding `VkDescriptorSetLayout`s for is undefined per the Vulkan spec; this
project's test environment has no validation layers enabled to turn that into a clean
`VK_ERROR_*` instead of a driver crash.

**The fix took option (b)** from this entry's own original analysis (reject with a clear error
rather than extend the pipeline layout to genuinely support multiple sets) -- see this entry's
opening paragraphs for the implementation and how far it has been verified.

**Tracked as:** `plan_llgl.md` Phase LLGL-8, `LLGL-47` -- fixed and verified in isolation (see
above); `Llgl_RenderTarget_EffectSource_C1` still needs an end-to-end Vulkan run (`LLGL-38`/`LLGL-55`)
before it can be registered.

---

## LLGL backend: untextured+unlit `BasicEffect` with no vertex-colour attribute throws — FIXED

**Status:** fixed by `plan_llgl.md` `LLGL-52` (2026-08-03). Confirmed: a fresh, isolated run of the
`untextured+unlit+no-colour` combination no longer throws and renders `DiffuseColor` flat, as
expected.

**Symptom (as originally filed):** a `BasicEffect` with `TextureEnabled=false`,
`VertexColorEnabled=false`, lighting disabled, and a `VertexPositionNormalTexture` (stride-32, no
colour attribute at all) vertex layout threw `"LLGL backend: an untextured draw needs vertex
colours, and this vertex layout has none"` -- an entirely ordinary, real-XNA-legal way to draw
flat-`DiffuseColor`-only geometry (every `ModelMeshPart` drawn via a `BasicEffect` with no texture
and no per-vertex colour hits exactly this combination).

**Correction:** this entry originally cited `examples/rasterizerstate_cullmode_indexed_
basiceffect_test.cpp` as the reproducer for THIS specific (unlit) combination. That was imprecise --
live instrumentation during `LLGL-52` showed that test's own `BasicEffect` actually calls
`EnableDefaultLighting()` (`lit=true`), so its crash was really the SEPARATE lit+untextured+
no-vertex-colour gap documented in its own entry below, not this unlit one. The unlit,
no-vertex-colour combination this entry describes is still a real, distinct gap in its own right
(reachable from any `ModelMeshPart` with a colourless layout, no lighting, no texture); it just
was not what that particular test file was hitting.

**Root cause:** `AcquirePrimitiveVertexShader()`'s own shader-variant selection
(`LlglGraphicsBackend.cpp`) had exactly one branch for the fully-untextured, fully-unlit case --
`else if (hasColor) { ... kColored3dVertGlsl ... } else { throw ... }` -- with no shader variant at
all for "untextured, unlit, AND no vertex-colour attribute in the layout."

**Fix:** a new, minimal `flat3d.vert.glsl`/`.gl.vert.glsl` shader pair (mirroring
`untextured3d.frag.glsl`'s own existing fragment stage unchanged, matching `SkinnedEffect.
VertexColorEnabled`'s own LLGL-37 precedent of adding a whole new shader variant rather than
risking an unbound-attribute declaration) that declares ONLY `position` (location 0) as input and
outputs `diffuseColor` unconditionally, selected instead of throwing when `!textured && !lit &&
!hasColor`.

**Tracked as:** `plan_llgl.md` Phase LLGL-7/LLGL-8, `LLGL-39`/`LLGL-52`.

---

## LLGL backend: lit+textured `BasicEffect` with no normal attribute throws — FIXED

**Status:** fixed by `plan_llgl.md` `LLGL-52` (2026-08-03). Confirmed by a fresh
`rendertarget_sampling_orientation_test.cpp` run: 61/61 checks pass (up from crashing at CD4),
including the previously-uncaught CD4 check.

**Symptom:** `rendertarget_sampling_orientation_test.cpp`'s CD4 check ("BasicEffect lit +
textured") crashed the whole process (`std::runtime_error`, uncaught -- this shared fixture has
no try/catch around its `Leg3D` legs) the moment it drew a `BasicEffect` with
`TextureEnabled=true`, `LightingEnabled=true` (`EnableDefaultLighting()`), and a plain
`VertexPositionTexture` (stride 20, no normal attribute) vertex layout. The thrown message:
`"LLGL backend: lighting needs a vertex layout with normals, and this one has none"`.

**Root cause:** `AcquirePrimitiveVertexShader()`'s shader-variant selection had no branch for
"textured, LIT, and no normal attribute in the layout," only threw. The fixture's own CD4 check
does not assert a specific lit colour, only that TEXTURE SAMPLING ORIENTATION survives lighting
being turned on, so a fixed-normal lighting approximation satisfies it exactly as well as a
physically meaningful one.

**Fix:** a new `lit_textured3d_flatnormal.vert.glsl`/`.gl.vert.glsl` shader variant that substitutes
a fixed `(0,0,1)` object-space normal (transformed by the world normal matrix, same as every other
lit shader) instead of reading a normal attribute the layout does not supply, selected when
`lit && textured && !hasNormal` (regardless of `hasColor`). Pairs with the existing
`lit_textured3d.frag.glsl`/`.gl.frag.glsl` fragment shader unchanged.

**Tracked as:** `plan_llgl.md` Phase LLGL-7/LLGL-8, `LLGL-41`/`LLGL-52`.

---

## LLGL backend: lit+untextured `BasicEffect` with no vertex-colour attribute throws — FIXED

**Status:** fixed by `plan_llgl.md` `LLGL-52` (2026-08-03). Confirmed by a fresh
`rasterizerstate_cullmode_indexed_basiceffect_test.cpp` run: 6/6 PASS (up from an uncaught crash),
plus `llgl_lighting_test.cpp`'s own Check H now lighting for real instead of asserting a throw
(10/10 PASS).

**Symptom:** `examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp` crashed
(`terminate called after throwing an instance of 'std::runtime_error'`, GLSL link error: "definitions
of uniform block `Transform' do not match") drawing a `BasicEffect` with `TextureEnabled=false`,
`VertexColorEnabled=false`, and `EnableDefaultLighting()` (lighting ENABLED, not disabled -- an
earlier pass at documenting this test's own trigger assumed lighting was off, see the correction in
the untextured+unlit entry above) from a `VertexPositionNormalTexture` layout (no vertex-colour
attribute).

**Root cause:** discovered by adding temporary `fprintf` instrumentation directly in
`AcquirePrimitiveVertexShader()` (not guessed from the ticket's own summary, which was imprecise) --
the real combination hitting the throw was `lit=true, textured=false, hasColor=false`, for which
`AcquirePrimitiveVertexShader()`'s if/else chain had NO branch at all: it checked `lit && textured`,
then `lit && hasColor`, then fell through to `textured`, then `hasColor`, then a final unconditional
`else` (the UNLIT `flat3d` branch, described above). A lit-but-colourless-and-untextured draw
therefore silently got the UNLIT `flat3d` vertex shader -- but the SEPARATE fragment-shader-selection
logic in `AcquirePrimitivePipeline()` (`lit && textured ? ... : lit ? primitiveLitUntexturedFragmentShader_
: ...`) always pairs `lit && !textured` with the LIT fragment shader regardless of which vertex
shader got selected -- so the pipeline linked an UNLIT vertex shader's `Transform` uniform block
(no world matrix, no light uniforms) against a LIT fragment shader expecting the full block, a
genuine byte-layout mismatch between the two actually-linked stages.

**Fix:** a new `lit_flat3d.vert.glsl`/`.gl.vert.glsl` shader variant (identical to the existing
`lit_colored3d.vert.glsl` lit-untextured shader minus the `color` attribute and its multiply,
`vTint = diffuseColor` unconditionally) selected via a new `else if (lit) { ... }` branch inserted
between the existing `lit && hasColor` branch and the `textured` branch. Pairs with the existing
`lit_untextured3d.frag.glsl`/`.gl.frag.glsl` fragment shader unchanged -- its inputs
(`vNormal`/`vWorldPos`/`vTint`/`vFogFactor`) are exactly what this new shader produces.

**Tracked as:** `plan_llgl.md` Phase LLGL-8, `LLGL-52`.

---

## LLGL backend: `Orthographic` + `CreateLookAt` scenario reports geometry off-screen — OPEN

**Status:** open, discovered while wiring `plan_llgl.md`'s Phase LLGL-7 (LLGL-39); narrowed
considerably during `LLGL-52` (2026-08-03) by following this entry's own previously-suggested next
step, and narrowed FURTHER on 2026-08-04 on real hardware (this machine's own physical desktop,
`DISPLAY=:0`, has a real AMD Radeon 780M with a working RADV Vulkan driver -- confirmed via
`vulkaninfo`/`glxinfo`, not assumed). Root cause STILL not identified, but two decisive new facts
are now established (see the 2026-08-04 findings below) and one earlier claim in this entry (no
LLGL-specific depth-range remap exists) is corrected below. Not fixed here.

**2026-08-04 findings (real Vulkan + real hardware, `DISPLAY=:0 CNA_LLGL_RENDERER=vulkan`):**
- **The bug is ARCHITECTURAL, not OpenGL-module-specific.** `cna_test_llgl_rasterizerstate_
  cullmode_camera` run against the REAL Vulkan module (not Xvfb -- this sandbox's Xvfb `:99`/`:101`
  still lack DRI3, but the machine's own real desktop display does not) reproduces scenario (b)'s
  failure IDENTICALLY: `geometry off-screen (A found=1, B found=0)`, same as the OpenGL module.
  This rules out anything specific to either renderer module (`GLStateManager`, `glDepthRangef`,
  GL's own clip-space convention, `VkViewport` specifics) -- the defect must be in code SHARED
  between both modules (`QueuePrimitives`, `AcquirePrimitivePipeline`, the deferred FrameCommand
  capture/replay machinery, or CNA's own CPU-side matrix/vertex handling upstream of either
  backend). It also independently confirms the `QueuePrimitives` GL-only Z-remap (see the
  correction below) cannot be the cause: Vulkan's `clippingRange` is `ZeroToOne`, so that remap
  code never even RUNS on the Vulkan module, yet the failure is identical.
- **The failure tracks POSITION, not winding.** Swapping which triangle (`worldTriA`/`worldTriB`)
  is built at `centerA` (`target + camRight*-80`, world Z=+80) vs `centerB` (`target +
  camRight*+80`, world Z=-80) while keeping each variable's OWN winding function
  (`MakeCwBasis`/`MakeCcwBasis`) attached to its own NAME moved the failure WITH the position, not
  with the label or the winding: whichever triangle sits at `target + camRight*80` (world Z=-80,
  this camera's screen-right side) fails to render, regardless of whether it is called `triA` or
  `triB` and regardless of whether it is wound CW or CCW in local space. This rules out any
  winding-dependent explanation (a stray cull, a front/back stencil-slot-style swap like the one
  documented on the Vulkan backend for an unrelated stencil bug) and confirms this is purely about
  WHERE the geometry sits, specifically along the camera's own right/screen-horizontal axis, under
  Orthographic projection only.

**Symptom:** `examples/rasterizerstate_cullmode_camera_test.cpp` (no `CNA_BACKEND_` conditional
branches at all -- meant to be universal, backend-agnostic math, already registered and passing on
several other backends) fails its own internal `[FATAL]` scenario-setup check for exactly ONE of
its five scenarios: `(b) Orthographic + CreateLookAt`. The test's own probe (`FindPixel`, a
full-framebuffer scan for a matching colour) cannot find triangle B anywhere on screen at all
(`geometry off-screen (A found=1, B found=0)`), so that scenario is skipped rather than judged.
Scenarios (a) Identity, (c) Perspective+CreateLookAt (the IDENTICAL `eye`/`target`/`up` camera as
(b), just with a perspective instead of an orthographic projection), (d) and (e) (both also
Perspective+CreateLookAt, with a rotated/mirrored World matrix) all pass cleanly -- 24/24 individual
cull-mode checks PASS across those four scenarios.

**Correction to this entry's own earlier claim:** a depth-range remap DOES exist in
`LlglGraphicsBackend.cpp`, in `QueuePrimitives()` -- keyed on `renderer_->GetRenderingCaps().
clippingRange == LLGL::ClippingRange::MinusOneToOne`, it folds XNA's D3D-convention `[0,1]` depth
into GL's `[-1,1]` convention (`z' = 2*z - w`) directly into the combined `world*view*projection`
matrix, once per draw, before the textured/lit/dualTexture/envMap/skinned/pbr branch. The earlier
"no remap code found" claim searched for the wrong strings (`glDepthRange`/`DepthRange`/
`ClipControl`); grepping for `ClippingRange` specifically finds it. This is now ruled out as the
asymmetry's cause, though: this remap operates on the shared WVP matrix, identically for both
triangles in the same draw call (same `world`/`view`/`projection` arguments, only vertex
POSITIONS differ between the two `DrawUserPrimitives` calls) -- it cannot by itself explain why one
triangle renders and the other does not.

**What was ruled out this session, with live instrumentation (not guessed):**
- **Not a math/matrix-construction bug.** Printing `Vector4::Transform(vertex, wvp)` directly (CNA's
  own real pipeline, the identical call `NdcSignedArea()` already used) for every vertex of both
  triangles in scenario (b) shows: `W = 1.0` exactly for all six vertices (a true orthographic
  projection, no perspective skew introduced anywhere upstream); triangle A's NDC X/Y in
  `[-0.6,-0.2]`/`[-0.3,0.3]`, triangle B's in `[0.2,0.6]`/`[-0.3,0.3]` -- a clean mirror image, both
  comfortably inside `[-1,1]`; `Z = 0.1051` identical for BOTH triangles (they sit at the same
  camera-space depth, differing only along `camRight`), comfortably inside `[0,1]`. Nothing here is
  even close to a clip-volume boundary.
- **Not cull-mode-related.** `CullMode::None` is explicitly set for the probe draw and cannot cull
  by winding at all; scenario (c) draws the exact same CCW-in-local-basis triangle B (same
  `MakeCcwBasis` construction, same camera, just a different projection) and it DOES render under
  `CullMode::None` -- so `CullMode::None` is not silently falling back to a default cull state in
  this backend in general.
- **Not draw-order/leftover-state.** Swapping which triangle's `findOne()` call runs first (B then
  A, instead of A then B) reproduces identically: triangle B never appears, regardless of which
  Clear+Draw+readback cycle runs first or second.
- **Not a miscoloured or mislocated triangle.** A full 128x128 framebuffer scan for ANY non-black
  pixel (not just near triangle B's predicted screen location) after triangle B's own isolated
  Clear+Draw+GetBackBufferData cycle finds ZERO non-black pixels anywhere. This is a genuine
  non-render, not a wrong-colour or wrong-position one.
- **Not the per-draw-`Viewport` bug documented elsewhere in this file** -- scenario (b)'s own draw
  calls (`RunScenario`'s `findOne`/`renderBoth` lambdas) never change `GraphicsDevice.Viewport` at
  all.
- **Not fog/large-coordinate precision** -- scenarios (c)/(d)/(e) use the identical world-scale
  vertex positions (hundreds of units from the origin) and the identical `BasicEffect`/
  `VertexColorEnabled` setup, and both triangles render correctly there.
- **Not a `Matrix::CreateOrthographic` construction bug** -- read directly (`Matrix.cpp`): a
  textbook-symmetric orthographic matrix (`M11 = 2/width`, `M22 = 2/height`, both applied
  identically regardless of the sign of the transformed X/Y), no sign-asymmetric or
  absolute-value logic anywhere in it that could treat a negative-X and positive-X vertex
  differently.
- **Not OpenGL-module-specific** and **not winding-dependent** -- see the 2026-08-04 findings above.

**What remains unexplained:** the one variable that reproduces the failure is Orthographic
projection specifically, combined with a non-identity `CreateLookAt` view, drawing whichever
triangle sits on the POSITIVE side of the camera's own right-axis offset (world Z=-80 for this
specific camera; NDC X in `[0.2,0.6]`, the right half of the screen). Perspective with the exact
same view/camera/triangle construction does not reproduce it; Identity (no real Orthographic
matrix at all) does not reproduce it; the CPU-side clip-space values for the FAILING vertex are
just as unremarkable and correct as the succeeding one's (confirmed identical `W=1.0`, symmetric
NDC X, identical `Z=0.1051`). Since the defect (a) reproduces byte-for-byte identically on two
independently-implemented renderer modules that share almost no code below `QueuePrimitives`/
`AcquirePrimitivePipeline`, and (b) is provably NOT explained by anything in the CPU-side
matrix/vertex math these two modules are FED, the most likely remaining location is somewhere in
the SHARED deferred-command capture/replay path itself (`QueuePrimitives`'s own uniform-buffer
pooling, `FrameCommand` capture, or `AcquirePrimitivePipeline`'s pipeline-cache key/reuse) rather
than in either module's own native rendering calls.

**2026-08-04 follow-up: the previously-suggested next lead is now RULED OUT too.** Followed this
entry's own recommendation and instrumented `QueuePrimitives()` directly (temporary `fprintf`,
fully removed before committing -- confirmed via `git diff` showing no residual change) to compare
the actual captured `FrameCommand` for triA's (succeeding) and triB's (failing) draws in scenario
(b): vertex buffer pointer, cached pipeline pointer, WVP matrix values, `elementCount`/
`vertexStart`/`startIndex`/`baseVertex`. Every one of these is either byte-identical between the
two draws (matrix -- correctly so, since world/view/projection are the same camera for both
triangles by the test's own design; pipeline -- correctly so, since none of the state
`AcquirePrimitivePipeline` keys on differs between the two draws; `elementCount=3`/all offsets 0)
or a REUSED-BUT-EXPECTED pointer value (the vertex buffer address is identical for both draws, but
this is ordinary allocator address reuse across two temporally-separate, fully-flushed frames --
`DrawUserPrimitives`'s own per-call temp buffer is torn down via `ScheduleBufferReleaseEXT()`,
which was independently verified by reading its own source to correctly DEFER the actual release
until `frameCommands_` is next flushed, not release-then-dangle immediately -- so this is not the
same-shape bug as the pipeline-cache-key-overflow finding this entry originally suspected: there is
no aliasing here, both draws' commands are captured, transported, and would replay with fully
correct, distinct data). **This rules out the entire capture/replay-machinery layer as the cause**:
whatever is happening must be at or below the actual GPU rasterization of triB's own vertex data,
which the CPU-side `Vector4::Transform` check already showed is unremarkable (same as triA's own
passing check, just mirrored in X). Not yet checked: the ACTUAL BYTES uploaded into the vertex
buffer via `DrawUserPrimitives`'s `Pack()`/`PositionColorStream` conversion (confirmed only that
the SOURCE `Tri` vertices are correct pre-pack, not that the packed GPU-visible bytes match).

**Next step for whoever picks this up:** read back the vertex buffer's own GPU-side content right
after `SetData()` (e.g. via `LLGL::RenderSystem::ReadTexture`'s buffer-readback analogue, or by
mapping the buffer if the module supports it) for triB's own draw specifically, to rule out (or
confirm) a `Pack()`/stride/format bug that only manifests for this specific vertex data -- this is
the one remaining untested layer between "CPU math is correct" (confirmed) and "GPU draws nothing"
(confirmed). If that also comes back clean, the defect is genuinely inside actual GPU rasterization
of this specific geometry under Orthographic + this specific view matrix, which would need either
a native GPU debugger (RenderDoc, apitrace) or a from-scratch minimal repro outside this whole
engine to isolate further.

**Tracked as:** `plan_llgl.md` Phase LLGL-7 / `LLGL-52` (blocks `LLGL-39`'s
`Llgl_RasterizerState_CullMode_Camera` registration).

---

## LLGL backend: a `VertexBuffer`/`IndexBuffer` reused within one frame silently loses the earlier draw's content — FIXED

**Status:** fixed by `plan_llgl.md` `LLGL-46` (2026-08-03). Confirmed by a fresh 127/127 run of
`frontface_winding_test.cpp` (up from 115/127; now registered as `Llgl_FrontFaceWinding`), plus a new
dedicated regression, `examples/llgl_vertexindexbuffer_grow_test.cpp` (registered as
`Llgl_VertexIndexBuffer_Grow`, 6/6), that specifically targets the GROW case this entry's own earlier
candidate-fix attempt never got to (see below) -- confirmed to genuinely reproduce the pre-fix defect
by running it against the pre-fix binary (a stashed diff), where it fails exactly as expected
(the pre-grow draw's own geometry area reads back as the clear colour instead of its own content,
rather than a hard crash on this particular software rasterizer -- still a real correctness
violation, just not the harder crash this same defect produced elsewhere before, see below).

**The fix:** `LlglVertexBufferBackend::SetData()`/`LlglIndexBufferBackend::Upload()` now call
`LlglGraphicsBackend::FlushPendingFrameEXT()` (a no-op when `frameCommands_` is empty) BEFORE either
writing in place or reallocating, whenever `buffer_ != nullptr` (i.e. this is not the buffer's very
first upload). Flushing submits and waits on every currently-queued draw -- including any draw that
still references this buffer's CURRENT (about-to-change) content by raw `LLGL::Buffer*` -- so by the
time the write or reallocation actually happens, nothing can still be reading the old content: safe
to write in place, and safe to `Release()` the old buffer immediately when growing (no deferred-
release bookkeeping needed at all). A buffer whose `buffer_` is still null (every
`GraphicsDevice::DrawUserPrimitives()`/`DrawUserIndexedPrimitives()` overload's own per-draw temp
buffer, always freshly constructed) never reaches this branch, so the fix cannot interact with that
unrelated internal mechanism -- which is exactly the interaction that broke the reverted attempt
below.

**A candidate fix was attempted and reverted (historical, kept for context):** mark a
`VertexBuffer`/`IndexBuffer` as "referenced by a pending frame command" when `QueuePrimitives()`
captures it, and have `SetData()` force a fresh `LLGL::Buffer` allocation (deferring the OLD one's
release) instead of writing in place whenever that mark is set -- clearing the mark once the frame
that queued the draw is actually submitted. This built and looked correct by inspection, but running
`frontface_winding_test.cpp` against it produced a NEW crash (`free(): invalid pointer` deep inside
`libvulkan_lvp.so`) on `Entry::UserIndexed16`, an entry point that does not reuse a persistent
`VertexBuffer` the way `Entry::BufferPrimitives` does -- meaning that fix's own tracking touched this
project's internal "user primitives" scratch-buffer mechanism in a way not understood before the
attempt was reverted. The flush-first fix above sidesteps this entirely: it adds no new tracking
state and no address-keyed bookkeeping (the exact shape of bug that a stale/reused-address tracking
map would produce), and never even reaches its own new code path for a buffer whose `buffer_` is
still null.

**Symptom:** `frontface_winding_test.cpp`'s W3 leg draws two triangles (a clockwise-wound one, then a
counter-clockwise-wound one) into one bind cycle via `Entry::BufferPrimitives`/
`BufferPrimitivesRange`/`BufferIndexed`/`BufferIndexedRange` -- each calls `triVb_->SetData(...)` (or
`wideVb_`/`triIb_`/`wideIb_`), `dev.SetVertexBuffer(...)`, then `dev.DrawPrimitives(...)`/
`DrawIndexedPrimitives(...)`, reusing the SAME persistent `VertexBuffer`/`IndexBuffer` member object
for BOTH triangles within one frame (no intervening `Present()`/readback). The FIRST (clockwise) triangle
never appears on screen under ANY `RasterizerState.CullMode`, including `CullNone` (which should show
both) -- 12 checks across those four entry points fail identically. Every OTHER entry point in the
same file (`DrawUserPrimitives`/`DrawUserIndexedPrimitives`/`TriangleStrip`, 115/127 checks) passes,
including the exact same winding/cull-mode logic -- this is not a culling bug, despite the symptom
initially reading like one.

**Root cause:** `LlglVertexBufferBackend::SetData()`/`LlglIndexBufferBackend::Upload()` write into the
existing `LLGL::Buffer` object IMMEDIATELY (`renderSystem_->WriteBuffer(...)`), while `QueuePrimitives()`
only stores a REFERENCE to that buffer object (`command.vertexBuffer = vertexBuffer.GetLlglBuffer()`)
in a `FrameCommand` that does not actually replay until frame end (`RecordAndSubmitFrame()`). When a
game calls `SetData()` + draw TWICE on the same buffer object within one frame, the second `SetData()`
overwrites the buffer's GPU content before either queued draw command has replayed -- so BOTH commands
end up rendering the buffer's FINAL content (the second triangle) when replay finally happens, and the
first triangle's draw is not "culled", it simply never had its own geometry to render. This is the
same general class of bug this whole project's `deferred_viewport_capture_test.cpp`/
`deferred_scissor_capture_test.cpp`/`deferred_source_lifetime_test.cpp` family already probes for
OTHER mutable state (viewport, scissor, texture source lifetime) -- just for `VertexBuffer`/
`IndexBuffer` CONTENT specifically, which none of those files exercise. This project's OWN existing
`transformBuffers_`/`customEffectUniformBuffers_` (internal, backend-owned per-draw uniform buffers)
already avoid the identical trap by never reusing one buffer object across two draws in a frame --
`SetData()` on a PUBLIC, game-owned `VertexBuffer`/`IndexBuffer` has no such guarantee, since the game
decides when to call it.

**A first candidate fix was attempted and reverted before the working one above was found** -- see
this entry's own opening paragraphs for the full account of what it tried, why it crashed, and why
the eventual fix (flush-before-write, no new tracking state at all) sidesteps that failure mode
entirely rather than patching around it.

**Tracked as:** `plan_llgl.md` Phase LLGL-8, `LLGL-46` -- fixed and verified (see above);
`frontface_winding_test.cpp` is now registered (`Llgl_FrontFaceWinding`), and a new dedicated
grow-capacity regression is registered alongside it (`Llgl_VertexIndexBuffer_Grow`).

---

## LLGL backend: every texture slot beyond slot 0 shares slot 0's own sampler state — FIXED

**Status:** fixed by `plan_llgl.md` `LLGL-49` (2026-08-03). `ApplySamplerState(int slot, ...)` now
writes into `samplerFilter_[slot]`/`samplerAddressU_[slot]`/`samplerAddressV_[slot]`/
`samplerMaxAnisotropy_[slot]` (5-element arrays, `kTrackedSamplerSlotCount`) instead of a single
global set of scalars, for the exact 5 slots this backend's stock effects ever consume (confirmed
against the Vulkan backend's own reference convention, `slotSamplers_[]`/`PbrSlotSamplersRawEXT()`:
slot 0 = every family's base/primary texture, slot 1 = `DualTextureEffect`'s second texture OR
`EnvironmentMapEffect`'s cube map -- the two are mutually exclusive per draw -- slots 2/3/4 =
`PbrEffect`'s metallic-roughness/emissive/occlusion maps). Every multi-texture-unit
`QueuePrimitives()` call site now acquires its OWN slot's sampler instead of reusing slot 0's;
`FrameCommand` grew 4 new `pbr*Sampler` fields (previously PBR's 4 extra maps had no sampler field
of their own at all, and `ReplayFrameCommandsList` bound `command.sampler` -- slot 0's -- at all 5
texture units, a limitation this file's own OLD code comment already self-documented).

**Verified fixed** (`CNA_LLGL_RENDERER=opengl`, Xvfb, 2026-08-03): `stock_effect_sampler_contract_test.cpp`
now passes with zero failures (M3 -- "slot 1's Linear filter alone breaks block uniformity" --
explicitly confirmed passing), now registered as `Llgl_StockEffectSampler`. Confirmed the pre-fix
binary reproduces the exact documented M3 failure and nothing else (same stash-and-rebuild technique
used for the other fixes this session). A regression sweep of `Llgl_DualTexture`,
`Llgl_DualTextureEffect_VertexColor`, `Llgl_PbrEffect_HandDerived`, `Llgl_BasicEffect`,
`Llgl_Lighting`, `Llgl_2D` and `Llgl_Smoke` shows no regressions.

**Symptom:** `stock_effect_sampler_contract_test.cpp`'s M leg (64/65 checks otherwise pass) sets
`DualTextureEffect`'s slot 0 to `TextureFilter.Point` and slot 1 to `TextureFilter.Linear`, then
draws. M1 (swapping the two slots' filters changes the rendered image) and M2 (both slots
`PointClamp` renders block-uniform, i.e. genuinely point-sampled) both pass. M3 -- slot 1's `Linear`
filter alone should break block uniformity, since a linearly-interpolated slot should blend across
texel boundaries -- FAILS: the rendered image stays block-uniform, as if slot 1 were ALSO sampled
with `Point` despite the game requesting `Linear` for it.

**The fix took exactly the shape this entry's own earlier analysis anticipated:** an array of
per-slot filter/address/anisotropy values (mirroring how `colorWriteChannels_` is already an array
of 4 for MRT), plus every multi-texture-unit `QueuePrimitives()` call site reading its own slot's
entry instead of a shared global. See this entry's opening paragraphs for the implementation and
verification.

**Tracked as:** `plan_llgl.md` Phase LLGL-8, `LLGL-49` -- fixed and verified (see above);
`stock_effect_sampler_contract_test.cpp` is now registered as `Llgl_StockEffectSampler`.

---

## LLGL backend: pipeline-cache key folding overflows and silently discards new fields — FIXED (process finding, LLGL-53)

**Status:** fixed by `plan_llgl.md` `LLGL-53` (2026-08-04) as part of wiring `RasterizerState.
DepthBias`/`SlopeScaleDepthBias` and the full stencil state into `AcquirePrimitivePipeline()`.
Recorded here as its own entry because it is a REUSABLE lesson about this function's own key
scheme, not just a one-off depth-bias bug -- the next person widening this key should read this
first.

**Symptom:** after wiring `depthBias_`/`slopeScaleDepthBias_`/8 stencil fields into
`AcquirePrimitivePipeline()`'s existing single-`uint64_t` cache key (via more multiply-add steps,
matching every other field's own established style), `rasterizerstate_depthbias_test.cpp`'s A1
check (`DepthBias=3000000` vs `DepthBias=0`, otherwise identical draw state) computed the
IDENTICAL key for both -- confirmed directly by printing the key: `key=9223372034707292159` for
BOTH the zero-bias and the 3,000,000-bias draw. The biased draw silently reused the zero-bias
cached pipeline, so its bias never took visual effect (behaved exactly as if depth bias were
still unwired).

**Two distinct bad techniques were tried and rejected, in order:**
1. An XOR + FNV-prime mix (matching the technique `MakeBlendPipelineKey()`'s own comment already
   documents as "tried and produced WRONG RENDERED PIXELS despite every logged descriptor field
   being provably correct" for a DIFFERENT field, `LLGL-33`) reproduced the exact same unexplained
   failure class here too, but differently: with it in place, `rasterizerstate_depthbias_test.cpp`'s
   H0/I0 checks (baseline, zero bias) went from a correct green pixel to a completely BLACK one
   (neither triangle drawn at all). A second, independent data point for that unexplained defect
   class -- still not understood for either technique, and still worth avoiding blind.
2. A plain multiply-add using a 32-bit multiplier (`key = key * 0x100000000ull + bits`) to fold
   each float's full bit pattern "cleanly" LOOKS like the same safe small-multiplier style every
   other field in this function already uses -- it is not. For a 64-bit `key` that already carries
   entropy from every earlier-folded field, multiplying by 2^32 discards the key's entire previous
   upper 32 bits via unsigned overflow (`(key * 2^32) mod 2^64 == (key mod 2^32) * 2^32`); doing
   this 4 times in a row (two bias floats, two stencil masks) collapsed almost every earlier field
   into irrelevance. This made H0/I0 pass again (coincidentally -- their real, separate bug is
   documented below), but silently broke every bias-flip check that had been working
   (A1/B1/C1/D1/E1/G0/H1/I1 all failed): the biased draw's key collided with an EARLIER,
   already-cached unbiased pipeline instead of creating its own.

**Root cause (general):** this whole key scheme folds every field via `key = key * N + value`
using SMALL per-field multipliers (2, 4, 8, 64, 65536...), which is safe PROVIDED the field being
folded needs to SURVIVE to the end -- but `std::uint64_t` overflow means any field's contribution
gets multiplied away to nothing once the PRODUCT of every multiplier applied AFTER it exceeds
2^64. The existing key already relied on this being "good enough" for its own fields (an accepted,
documented risk, see `LLGL-48`); adding ~10 more fields' worth of multiply-add steps pushed the
cumulative multiplier for early fields (like `depthBias_`) far past 2^64.

**Fix:** `primitivePipelineCache_`'s key type was widened from a single `std::uint64_t` to a
4-element `std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>` (`std::map` gets lexicographic
ordering on tuples for free, no custom comparator needed). Element 1 is the ORIGINAL key,
unchanged. Element 2 packs both depth-bias floats losslessly (32+32 bits, zero collision risk).
Element 3 packs both stencil masks losslessly (32+32 bits). Element 4 packs the 8 stencil
op/function fields plus 2 stencil bools using the same small-multiplier style, safe here because
nothing is folded in after it within its own dedicated 64-bit word. **Verified**: A1's key
computation now differs correctly between bias=0 and bias=3000000, and
`rasterizerstate_depthbias_test.cpp` returned to 12/17 PASS (all constant-DepthBias flip checks:
A/B/C/E/G) -- see the separate entry below for the file's own remaining 5 failures, none of which
are this key issue.

**Lesson for whoever widens this key next (LLGL-48's own eventual scope):** do not add MORE
multiply-add steps to the single `key` variable past a handful of small (<256) multipliers without
checking whether an EARLIER field's contribution still survives. A tuple/composite key sidesteps
the whole class of bug and costs nothing extra at `std::map`'s scale.

**Tracked as:** `plan_llgl.md` Phase LLGL-8, `LLGL-53`.

---

## LLGL backend: `RasterizerState.SlopeScaleDepthBias`, custom `Viewport.MinDepth`/`MaxDepth`, and `RenderTarget2D` depth testing each have a genuine, unexplained defect — OPEN (LLGL-53)

**Status:** open, discovered while wiring `plan_llgl.md`'s `LLGL-53`. Root cause NOT identified for
any of the three; not fixed here. Distinct from the pipeline-cache-key entry above -- confirmed via
isolation (disabling the depth-bias/stencil descriptor writes entirely, with the cache key already
fixed, does NOT make any of these three pass), so none of these are a side effect of this ticket's
own new descriptor/key code; they are real, separately-rooted defects that this ticket's own new
test coverage (`rasterizerstate_depthbias_test.cpp`, reused from Software) happened to be the first
thing to exercise them on this backend.

**D1 -- `SlopeScaleDepthBias` alone has no effect.** `rasterizerstate_depthbias_test.cpp`'s D0
(SlopeScale=0, shallow-sloped quad correctly wins over a flat one by ordinary depth) passes; D1
(the SAME geometry, SlopeScale=150 expected to push the sloped quad's far edge behind the flat
one) still reports the shallow quad winning, as if the slope factor were never applied. `D2`
(a FLAT quad under the same high SlopeScale, expected to get ZERO slope offset since a flat
polygon's screen-space depth slope is 0) correctly passes, which is at least consistent with "no
slope offset is being applied to sloped geometry either" rather than some fixed non-zero offset
firing regardless of slope. `pipelineDesc.rasterizer.depthBias.slopeFactor = slopeScaleDepthBias_;`
is wired identically to `constantFactor` (LLGL's own `GLRasterizerState.cpp` reads both the same
way, see `IsPolygonOffsetEnabled`/`Bind()`), and the CONSTANT factor demonstrably works (every
`A1`/`B1`/`C1`/`E1`/`G0` check in the same file passes) -- so this is specifically about the SLOPE
term, not depth bias wiring in general.

**H0/H1 -- ANY draw under a non-default `Viewport.MinDepth`/`MaxDepth` renders nothing at all.**
`rasterizerstate_depthbias_test.cpp`'s H0 (custom depth range `[0.2, 0.8]`, DepthBias=0 -- a pure
baseline, no bias involved at all) expects the later of two coplanar quads to win by ordinary
depth-test semantics, same as every other baseline check in the file. Instead, a full-framebuffer
readback shows the centre pixel stays pure background black -- NEITHER quad drew. Confirmed via a
`git stash`-based comparison against the exact pre-`LLGL-53` binary: this same scenario correctly
rendered green (H0 passed) BEFORE any of `LLGL-53`'s changes, so it is a genuine new regression
somewhere in this ticket's own work, most likely `SetViewport()`'s newly-wired
`minDepth`/`maxDepth` plumbing (`CaptureFrameCommandViewportEXT()`'s own `viewportMinDepth`/
`viewportMaxDepth` fields, and the `LLGL::Viewport`'s 6-argument constructor now used at replay).
Debug instrumentation confirmed the CAPTURED values are correct at replay time
(`viewport=(0,0,640,480) depth=(0.200,0.800)`, exactly matching what the test requested) -- so the
defect is not in this backend's own capture/plumbing, but somewhere between that correct
`LLGL::Viewport` call and what actually reaches the screen (LLGL's own OpenGL module's
`GLImmediateCommandBuffer::SetViewport()`/`GLStateManager::SetDepthRange()` were read and look
correct on their own too, `GLProfile::DepthRange(minDepth, maxDepth)`; not yet traced further).

**I0/I1 -- a bound `RenderTarget2D` with depth testing renders nothing, even with DepthBias=0 and
the DEFAULT `[0,1]` depth range.** Same "baseline renders nothing" symptom as H0, but reached via a
completely different path (`SetVp(dev, 0, 0, rtW, rtH)` on a bound `RenderTarget2D`, no custom
depth range at all). Also confirmed via `git stash` comparison to be a genuine NEW regression from
this ticket's own work (I0 rendered green correctly before `LLGL-53`). By `Viewport(x,y,w,h)`'s own
constructor defaulting `MinDepth`/`MaxDepth` to `[0,1]`, and H1's own block explicitly resetting the
viewport (`SetVp`) before I's block runs, I0/I1 do NOT inherit H0/H1's non-default depth range --
so despite the superficially similar symptom, **H0/H1's own root cause below (confirmed 2026-08-04)
does not apply to I0/I1**, which remain a genuinely separate, still fully unexplained defect.

**2026-08-04: H0/H1's root cause CONFIRMED, narrowed to one specific interaction -- not fixed
(the correct behavior IS what triggers it).** Live-instrumented the vendored LLGL OpenGL module
(temporary, fully reverted, `~/deps/LLGL` pristine again) end to end: `SetViewport()`'s captured
`viewport`/`depthRange` reach `glViewport`/`glDepthRangef` correctly (`GL_VIEWPORT=(0,0,640,480)`,
`GL_DEPTH_RANGE=(0.200,0.800)`, `depthTest=1`, `depthFunc=GL_LEQUAL`, `writeMask=1` -- all queried
directly via `glGetIntegerv`/`glIsEnabled`, all exactly as requested) for BOTH the red and green
draws in H0. The scissor rect computed by `ComputeEffectiveScissor()` is IDENTICAL between H0
(fails) and G0 (passes, same file, immediately before H0) -- `(0,0,640,480)` in both, since
`viewportRect_`'s own width (96) already differs from the logical width (120) for EVERY check in
this file, independent of depth range -- ruling out `viewportSet_`'s LLGL-53-widened condition
(the very first hypothesis this investigation reached for) as the cause; it was already true before
LLGL-53 touched anything.

**The actual mechanism:** `GraphicsDevice::Clear(Color)` uses `Viewport.MaxDepth` -- not a
hardcoded `1.0` -- as the depth-buffer clear value, matching FNA's own exact behavior (Task 928,
predates this ticket entirely). For H0, that is `0.8` (`Viewport.MaxDepth` from the just-applied
custom range). Confirmed via direct `glGetFloatv(GL_DEPTH_CLEAR_VALUE, ...)`: the backend correctly
requests and the GL module correctly applies a raw clear value of `0.8000`. **This combination --
clearing the depth buffer to a non-`1.0` raw value, then drawing under a narrowed
`glDepthRangef(0.2, 0.8)` -- causes the LEQUAL depth test to reject every subsequent fragment**,
even though the fragment's own computed window-depth (`~0.5`, per the test's own worked example)
is arithmetically `<= 0.8` and should pass. **Proven, not inferred:** temporarily forcing the GL
clear-depth call to always use `1.0` (ignoring the correctly-computed `0.8`, a deliberately
XNA-INCORRECT experiment, reverted immediately after) makes **H0 pass** -- conclusively isolating
the mechanism to this one interaction. H1 (which also clears under the same non-default range)
still fails even with that same experimental override, meaning it depends on something further
(most likely `DepthBias`'s OWN interaction with a non-default range, not yet isolated).

**Why this is not being "fixed" here:** `Clear()`'s use of `Viewport.MaxDepth` is CORRECT,
intentional, XNA/FNA-faithful behavior (see Task 928's own comment) -- reverting it to a hardcoded
`1.0` would violate XNA fidelity for any other legitimate use of a custom `MaxDepth`, not just
paper over this specific case. The remaining question -- WHY a raw depth-buffer clear value
combined with a narrowed `glDepthRangef` causes total fragment rejection, when the values involved
are all within their documented valid ranges -- was not resolved: candidates not yet distinguished
are a genuine `llvmpipe`/Mesa software-rasterizer quirk in how `glClear(GL_DEPTH_BUFFER_BIT)`
interprets its clear value relative to the CURRENT depth range (non-standard per the GL spec, which
says `glClear` writes the raw depth-buffer value unaffected by `glDepthRangef` -- but this is a
SOFTWARE rasterizer, not a spec-perfect reference implementation), versus something LLGL's own GL
module does differently for a narrowed range that a real GPU driver would not. Needs either a
native GPU debugger (RenderDoc/apitrace, unavailable in this sandbox) or a real, non-software GL
driver to distinguish a genuine environment limitation from an LLGL/CNA-side bug.

**Also found while adding `llgl_stencil_test.cpp` (new, LLGL-53): the stencil test does not
actually GATE.** Writing a reference value via `StencilOperation::Replace` and then reading it
back via a `CompareFunction::Equal` test against the SAME reference passes correctly (Check A), but
a mismatched reference (`CompareFunction::Equal` against a DIFFERENT value than what was written)
is NOT rejected -- the gated draw lands anyway, as if the compare function were always
`CompareFunction::Always` regardless of what was actually requested (Checks B and C's second half).
Root cause not identified; candidates not yet ruled out: the swap chain's own depth-stencil
attachment format may lack a real stencil channel despite `LLGL::SwapChainDescriptor::stencilBits`
defaulting to 8 (not yet confirmed via `swapChain_->GetDepthStencilFormat()`'s actual reported
value on this module), or `pipelineDesc.stencil.front.compareOp`/`readMask` may not be reaching the
GPU the way `pipelineDesc.depth.compareOp` demonstrably does (ordinary depth testing works
correctly elsewhere in this same backend).

**2026-08-04 cross-check on real Vulkan (`DISPLAY=:0 CNA_LLGL_RENDERER=vulkan`, this machine's own
physical AMD Radeon 780M/RADV -- see `feedback_real_display_vulkan_available` in memory for how):**
- **The stencil-doesn't-gate defect is confirmed ARCHITECTURAL**, not OpenGL-specific:
  `llgl_stencil_test.cpp` fails Checks B and C's second half IDENTICALLY under Vulkan (same exact
  output). Rules out anything GL-module-specific (`GLStateManager`'s own stencil state handling,
  the swap chain's GL depth-stencil renderbuffer format) as the sole cause -- whatever is wrong is
  in code shared between both renderer modules, most likely `AcquirePrimitivePipeline()`'s own
  `pipelineDesc.stencil` construction or how `stencilFunction_`/`stencilMask_`/etc. get captured,
  not either module's native stencil-state translation.
- **NEW, separate finding, plausible but NOT fully confirmed: constant `DepthBias` -- the ONE
  mechanism already verified WORKING on the OpenGL module (`A1`/`B1`/`C1`/`E1`/`G0` all pass, on
  Xvfb) -- appears to NOT work on the Vulkan module.** Under `CNA_LLGL_RENDERER=vulkan` on the real
  display, `rasterizerstate_depthbias_test.cpp`'s `A1`/`B1`/`C1`/`D1` (every bias-effect check)
  FAIL while every baseline (`A0`/`B0`/`C0`/`D0`/`D2`) PASSES -- a clean, internally-consistent
  split (bias-off always right, bias-on always wrong), not scattered/random failures, which is why
  this is treated as a plausible real finding rather than noise. **Two things were confirmed, not
  guessed:** (1) distinct `LLGL::PipelineState*` pointers ARE created for `DepthBias=0` vs
  `DepthBias=3000000` (`0x...b358` vs `0x...2778`, printed directly) -- this is NOT a repeat of the
  pipeline-cache-key-overflow bug fixed elsewhere in this ticket, the cache is behaving correctly.
  (2) LLGL's own `VKGraphicsPSO.cpp` (`CreateRasterizerState()`) was read directly and looks
  correct on its face: `depthBiasEnable` is computed from the same three fields, all three are
  copied verbatim into the static `VkPipelineRasterizationStateCreateInfo`, and
  `VK_DYNAMIC_STATE_DEPTH_BIAS` is correctly absent from the module's own dynamic-state list
  (meaning depth bias is INTENDED to be baked statically at creation time here, which the code does
  do) -- no LLGL-side dynamic-state override was found that could be clobbering it afterward.
  **However, a real confound was discovered during this same investigation that means this finding
  needs independent re-verification before being trusted fully:** running the SAME test suite
  against the OpenGL module on the real display (`CNA_LLGL_RENDERER=opengl DISPLAY=:0`, this
  machine's own physical desktop, GNOME/Mutter compositor) produced SCATTERED, internally
  INCONSISTENT results (`A0`/`B0`/`C0`/`D0` baselines FAIL -- unlike their reliable PASS on Xvfb --
  while `D1`, a bias-EFFECT check, unexpectedly PASSES) -- a pattern that looks like genuine
  window/compositor interference with this test's pixel-exact small-window readback assumptions
  (window decorations, DPI scaling, vsync/compositor double-buffering catching a mid-transition
  frame), NOT a real rendering defect. Since the Vulkan run was ALSO taken on this same real,
  composited display (not Xvfb), the same class of interference cannot be fully ruled out there
  either, even though its own result pattern is much cleaner/more consistent than GL's scattered
  one. **Root cause not identified either way.** Next step: re-run this exact comparison once a
  DRI3-capable Xvfb is available (`LLGL-55`'s own scope) to eliminate the real-desktop-compositor
  variable entirely and get a clean Vulkan-vs-OpenGL comparison with NEITHER run touching a real,
  composited window.
- `D1` (`SlopeScaleDepthBias`) also fails identically under Vulkan (consistent with -- not new
  information beyond -- its OpenGL failure already documented above).
- `H0`/`H1`/`I0`/`I1` were not re-tested under Vulkan this pass (the file aborts earlier under
  Vulkan on an unrelated, pre-existing, already-known limitation: `E0`'s `FillMode::WireFrame`
  throws `"not yet implemented"` on the Vulkan renderer module by design, per
  `AcquirePrimitivePipeline()`'s own `SupportsWireFrameEXT()` guard -- this is documented,
  intentional behaviour, not a new finding).

**Tracked as:** `plan_llgl.md` Phase LLGL-8, `LLGL-53` -- left OPEN, now FOUR/FIVE distinct
defects, none root-caused (the Vulkan-specific `DepthBias` failure is a NEW fifth one, found only
once real Vulkan testing became possible on this machine). `Llgl_RasterizerState_DepthBias`
(12/17 on OpenGL) and `Llgl_Stencil` (2/4, same on both modules) are both registered anyway as real
regression trip-wires for whoever continues this, matching this session's own established
`Llgl_Deferred_Viewport`-style precedent for partial-pass suites.

---

## `CnaTests` gtest fixtures (not the `examples/` LLGL test binaries) have no Vulkan-WSI-unavailable guard at all, and one failure can break every later test in the same process — OPEN (LLGL-55-adjacent)

**Backend:** LLGL (Vulkan module), discovered incidentally while verifying the new
`LlglSdlSurfaceConstructor` unit test (`LLGL-56`) against a wider `--gtest_filter`.

**Symptom:** `LLGL-55`'s Vulkan-WSI-unavailable skip guard
(`examples/common/LlglVulkanWsiSkipGuard.cpp`) is linked only into the `examples/*_test.cpp`
binaries via `cna_llgl_test()` (`cmake/Tests/LlglTests.cmake`). It is **not** linked into
`CnaTests`, the general gtest binary that also builds and runs graphics-fixture tests against
whichever `CNA_GRAPHICS_BACKEND` is configured -- on this DRI3-less Xvfb with LLGL selected, a
fixture that constructs a real `GraphicsDevice` (e.g. `Texture3DTest`) throws the same
`VK_ERROR_SURFACE_LOST_KHR` mid-construction. gtest's own exception handling catches this cleanly
(reported as `[FAILED]`, not a process abort), so on its own this is "only" a false FAIL rather
than a crash. But the failing fixture's partially-torn-down `GraphicsDevice`/X11 client connection
appears to corrupt something process-wide: the *next* test run in the same process
(`TextureCubeTest` in the reproduction) immediately hits `"X connection to :99 broken (explicit
kill or server shutdown)"` and fails too. The shared Xvfb `:99` server itself is unaffected
(confirmed responsive via `xdpyinfo` immediately after) -- only that one process's own connection
dies, but since `ctest`'s own `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST ...)` runs each
`TEST()` as its own separate process, ordinary `ctest` runs are NOT expected to cascade this way;
the cascade only reproduces when multiple such tests are forced into one process (e.g. a broad
`--gtest_filter` covering several graphics fixtures, as happened here).

**Reproduction:** `CNA_LLGL_RENDERER` unset (default, Vulkan-preferring), `SDL_VIDEODRIVER=x11`,
`DISPLAY=:99` (no DRI3): `./CnaTests --gtest_filter="*Window*"` (or any filter that pulls in
several `GraphicsDevice`-constructing fixtures into one process) -- the first such fixture reports
`[FAILED]` with the Vulkan swap-chain exception, and the next reports a broken X connection.

**Not investigated further this session** (out of scope for the `LLGL-56` unit test being added at
the time): whether every `CnaTests` fixture that constructs a real `GraphicsDevice` needs the same
kind of guard `LLGL-55` gave the `examples/` binaries, or whether `ctest`'s per-test process
isolation already makes this moot in normal CI use. Needs its own investigation before being
folded into `LLGL-55`'s own scope or closed as "ctest isolation already covers it."

**Tracked as:** new, not yet in `plan_llgl.md`'s task table -- follow-up for whoever continues
`LLGL-55`/`LLGL-56`.

---
