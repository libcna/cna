# CNA Known Bugs

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

## LLGL backend: OpenGL module renders nothing for a Y-offset scissor/viewport against the backbuffer — OPEN

**Status:** open, discovered while verifying the per-draw-`Viewport` fix above (previous entry)
under `CNA_LLGL_RENDERER=opengl`. Root cause narrowed to LLGL's own OpenGL screen-origin handling;
not fixed here (third-party pinned dependency, not this project's code).

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

**Tracked as:** `plan_llgl.md` Phase LLGL-7, `LLGL-39` (no `_OpenGL` CTest variant registered for
`spritebatch_custom_viewport`/`spritebatch_viewport_switch`/`rendertargetcube_plural_binding` --
see `cmake/Tests/LlglTests.cmake`'s own comment there for the full explanation).

---

## LLGL backend: untextured+unlit `BasicEffect` with no vertex-colour attribute throws — OPEN

**Status:** open, discovered while wiring `plan_llgl.md`'s Phase LLGL-7 (LLGL-39). Root cause
identified with confidence; not fixed here.

**Symptom:** `examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp` crashes
(`std::runtime_error`, uncaught) the moment it draws a `BasicEffect` with `TextureEnabled=false`,
`VertexColorEnabled=false`, `PreferPerPixelLighting`'s default (lighting disabled, `BasicEffect`'s
own default), and a `VertexPositionNormalTexture` (stride-32, no colour attribute at all) vertex
layout -- an entirely ordinary, real-XNA-legal way to draw flat-`DiffuseColor`-only geometry (every
`ModelMeshPart` drawn via a `BasicEffect` with no texture and no per-vertex colour hits exactly this
combination). The thrown message is explicit: `"LLGL backend: an untextured draw needs vertex
colours, and this vertex layout has none"`.

**Root cause:** `AcquirePrimitiveVertexShader()`'s own shader-variant selection
(`LlglGraphicsBackend.cpp`) has exactly one branch for the fully-untextured, fully-unlit case:
`else if (hasColor) { ... kColored3dVertGlsl ... } else { throw ... }` -- there is no shader variant
at all for "untextured, unlit, AND no vertex-colour attribute in the layout." Every other
combination this backend supports (textured, lit, coloured) has its own dedicated compiled shader;
this one specific combination -- a flat, constant-`DiffuseColor`-only draw -- was never given one.

Notably, `kColored3dVertGlsl` itself does not actually NEED the colour attribute's VALUE when
`VertexColorEnabled=false` (its own `vColor = (vertexColorEnabledPad.x > 0.5) ? diffuseColor *
color : diffuseColor;` line already ignores `color` entirely in that case) -- the crash is purely
because that shader unconditionally DECLARES a `layout(location = 1) in vec4 color` input, and
handing it a pipeline whose `vertex.inputAttribs` has no location-1 entry (because the bound vertex
buffer's own layout has no colour attribute to describe) was judged too risky to attempt blind
(undefined behaviour on Vulkan at best, a validation-layer rejection at worst) rather than actually
tested.

**Fix shape (not implemented):** a new, minimal `flat3d.vert.glsl`/`.gl.vert.glsl` +
`flat3d.frag.glsl`/`.gl.frag.glsl` shader pair (mirroring `untextured3d.frag.glsl`'s own fragment
stage, matching `SkinnedEffect.VertexColorEnabled`'s own LLGL-37 precedent for "add a whole new
shader variant rather than risk an unbound-attribute declaration") that declares ONLY `position`
(location 0) as input and outputs `diffuseColor` unconditionally, selected instead of throwing when
`!textured && !lit && !hasColor`.

**Tracked as:** `plan_llgl.md` Phase LLGL-7 (blocks `LLGL-39`'s
`Llgl_RasterizerState_CullMode_IndexedBasicEffect` registration).

---

## LLGL backend: `Orthographic` + `CreateLookAt` scenario reports geometry off-screen — OPEN

**Status:** open, discovered while wiring `plan_llgl.md`'s Phase LLGL-7 (LLGL-39). Root cause NOT
identified; not fixed here.

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

**What was ruled out:** this is NOT the per-draw-`Viewport` bug documented in this file's own
previous entry -- scenario (b)'s own draw calls (`RunScenario`'s `findOne`/`renderBoth` lambdas)
never change `GraphicsDevice.Viewport` at all, unlike the three files that entry covers. `Matrix::
CreateOrthographic`/`CreateLookAt` are shared, backend-agnostic CPU-side math (`Matrix.cpp`),
identical for every backend, so the WVP matrix and resulting NDC coordinates should be bit-for-bit
identical across backends -- yet only LLGL fails to find the geometry it places. No LLGL-specific
depth-range remapping code was found in `LlglGraphicsBackend.cpp` (`grep` for
`glDepthRange`/`DepthRange`/`ClipControl`-style handling returned nothing), so a Z-range convention
mismatch (OpenGL's traditional `[-1,1]` vs Vulkan/D3D's `[0,1]`) was considered but not confirmed --
LLGL itself is expected to normalize this internally across its own modules, the same way it does
for every other passing scenario in this same file.

**Next step for whoever picks this up:** instrument (temporarily) to print the actual clip-space
`x/y/z/w` for triangle A and B under scenario (b)'s specific WVP matrix, and compare against the
same values computed independently (e.g. in Python) from the identical `Matrix::CreateOrthographic
(400, 400, 10, 10000)` / `Matrix::CreateLookAt(eye=(1000,500,0), target=(0,150,0), up=(0,1,0))`
inputs, to determine whether the NDC coordinates are genuinely outside `[-1,1]`/`[0,1]` (a real math
or matrix-construction bug reachable only through this specific matrix shape) or whether they are
in-range and something in this backend's own rasterization of that specific geometry is still
wrong.

**Tracked as:** `plan_llgl.md` Phase LLGL-7 (blocks `LLGL-39`'s
`Llgl_RasterizerState_CullMode_Camera` registration).

---
