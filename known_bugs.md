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

## LLGL backend: `FixedHeightDynamicWidth`'s logical width ignores the requested backbuffer width — OPEN

**Status:** open, discovered while wiring `plan_llgl.md`'s Phase LLGL-7 (`LLGL-40`,
`backbuffer_first_read_test.cpp`; also reproduced by `LLGL-41`'s `bound_target_lifetime_test.cpp` and
`LLGL-43`'s `deferred_source_lifetime_test.cpp`). Root cause identified with confidence; not fixed
here (needs comparison against how other backends implement the same `CnaPresentationMode`, out of
scope for a test-wiring task).

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

**Why this needs more than a test-wiring fix:** `presentationParameters_.getBackBufferWidthProperty()`
already correctly reports the game's own requested width (63, not 28) -- `GraphicsDevice::
GetBackBufferData` sizes its read from that, which is why every affected leg still returns the FULL
requested element count with none of it left unwritten (poison-free), just with wrong CONTENT past
the derived boundary. Whether `FixedHeightDynamicWidth` is SUPPOSED to let the logical width diverge
from the requested width like this (an intentional "cinematic" width-follows-window-aspect feature)
or whether other backends implementing the same named mode instead keep the logical extent pinned to
the requested size and apply the aspect mismatch purely as a physical-fit letterbox/scale was not
established here -- fixing it correctly requires settling that question first, not just patching
this one formula.

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

**Tracked as:** `plan_llgl.md` Phase LLGL-7, `LLGL-40`/`LLGL-41`/`LLGL-43` (no CTest registration for
`backbuffer_first_read_test.cpp`, `bound_target_lifetime_test.cpp`, or
`deferred_source_lifetime_test.cpp` until this is resolved -- see `cmake/Tests/LlglTests.cmake`'s own
comments there for the full per-leg breakdown).

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

## LLGL backend: a custom `ShaderEffect` using multiple Vulkan descriptor sets crashes the driver — OPEN

**Status:** open, discovered while wiring `plan_llgl.md`'s Phase LLGL-7 (`LLGL-41`,
`rendertarget_effect_source_test.cpp`'s own C1 check). Root cause identified with confidence; not
fixed here (a real fix needs SPIR-V reflection to validate shader/layout compatibility before
attempting pipeline creation, a nontrivial addition, and the narrower option -- rejecting multi-set
shaders with a clear error -- still needs that same reflection to detect the case at all).

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

**Why this needs more than a test-wiring fix:** this backend's custom-effect pipeline layout is a
genuine, narrower capability than what this shared test fixture assumes (single descriptor set
only) -- a real fix is either (a) extending `AcquireCustomEffectLayoutEXT`/`CompileProgram` to
support multiple descriptor sets (a real feature addition, not a bug fix), or (b) adding SPIR-V
reflection to `CompileProgram` so a shader whose resources do not fit this backend's single-set
layout is rejected with a clear compile error (matching the `custom.IsEffectValid()` boundary this
test already has a path for) instead of reaching pipeline creation and crashing. Neither is a
test-wiring change.

**Tracked as:** `plan_llgl.md` Phase LLGL-7, `LLGL-41` (no CTest registration for
`rendertarget_effect_source_test.cpp` until this is resolved).

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

## LLGL backend: lit+textured `BasicEffect` with no normal attribute throws — OPEN

**Status:** open, discovered while wiring `plan_llgl.md`'s Phase LLGL-7 (`LLGL-41`,
`rendertarget_sampling_orientation_test.cpp`). Root cause identified with confidence, the
same general class as the already-documented untextured+unlit finding above; not fixed here.

**Symptom:** `rendertarget_sampling_orientation_test.cpp`'s CD4 check ("BasicEffect lit +
textured") crashes the whole process (`std::runtime_error`, uncaught -- this shared fixture has
no try/catch around its `Leg3D` legs) the moment it draws a `BasicEffect` with
`TextureEnabled=true`, `LightingEnabled=true` (`EnableDefaultLighting()`), and a plain
`VertexPositionTexture` (stride 20, no normal attribute) vertex layout -- 10/10 checks before it
pass cleanly (`S1`/`S2`, both `AB1` orientation checks, `CD1`-`CD3`). The thrown message:
`"LLGL backend: lighting needs a vertex layout with normals, and this one has none"`.

**Root cause:** the same shape as the untextured+unlit `flat3d` gap above --
`AcquirePrimitiveVertexShader()`'s shader-variant selection has no branch for "textured, LIT, and
no normal attribute in the layout," only throws. Every other backend this shared fixture already
runs against apparently tolerates a normal-less lit draw (most plausibly by defaulting to some
fixed normal, e.g. `(0,0,1)`, rather than refusing it outright) -- this fixture's own CD4 check
does not assert a specific lit colour, only that TEXTURE SAMPLING ORIENTATION survives lighting
being turned on, so an approximate/default-normal lighting result would satisfy it exactly as
well as a physically meaningful one.

**Fix shape (not implemented):** a new shader variant (or a uniform-supplied default normal
substituted into the existing lit-textured shader when the vertex layout has no normal attribute)
that computes lighting against a fixed `(0,0,1)` (or similar) normal instead of refusing to bind
when the attribute is absent -- the same "declare only what the layout actually provides, pick the
matching shader variant" approach `SkinnedEffect.VertexColorEnabled` (LLGL-37) and the proposed
`flat3d` shader above already establish as this backend's convention.

**Tracked as:** `plan_llgl.md` Phase LLGL-7, `LLGL-41` (no CTest registration for
`rendertarget_sampling_orientation_test.cpp` until this is resolved).

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

## LLGL backend: every texture slot beyond slot 0 shares slot 0's own sampler state — OPEN

**Status:** open, discovered while wiring `plan_llgl.md`'s Phase LLGL-7 (`LLGL-44`,
`stock_effect_sampler_contract_test.cpp`). Root cause identified with certainty from the source
itself -- the limitation is already self-documented in a code comment, just not previously surfaced
as a public finding since no test had exercised two texture slots with genuinely DIFFERENT sampler
states before this file. Not fixed here (a real fix needs per-slot sampler state tracking, out of
scope for a test-wiring task).

**Symptom:** `stock_effect_sampler_contract_test.cpp`'s M leg (64/65 checks otherwise pass) sets
`DualTextureEffect`'s slot 0 to `TextureFilter.Point` and slot 1 to `TextureFilter.Linear`, then
draws. M1 (swapping the two slots' filters changes the rendered image) and M2 (both slots
`PointClamp` renders block-uniform, i.e. genuinely point-sampled) both pass. M3 -- slot 1's `Linear`
filter alone should break block uniformity, since a linearly-interpolated slot should blend across
texel boundaries -- FAILS: the rendered image stays block-uniform, as if slot 1 were ALSO sampled
with `Point` despite the game requesting `Linear` for it.

**Root cause:** `LlglGraphicsBackend::ApplySamplerState(int slot, ...)` only ever writes into the
single global `samplerFilter_`/`samplerAddressU_`/`samplerAddressV_`/`samplerMaxAnisotropy_` member
variables regardless of which `slot` the game names -- there is no per-slot storage at all. Every
`QueuePrimitives()` call that builds a multi-texture-unit command (`DualTextureEffect`'s own
`command.sampler2`, and `PbrEffect`'s 5 texture units, which already carry a code comment
acknowledging this exact limitation: "All 5 texture units share this backend's single global sampler
state (`ApplySamplerState` only ever tracks slot 0)") calls `AcquireSampler()` with those SAME global
values for every slot, so slot 1+ always ends up with whatever slot 0's own sampler state currently
is, never its own.

**Why this needs more than a test-wiring fix:** correcting it needs `ApplySamplerState` to track an
array of per-slot filter/address/anisotropy values (mirroring how `colorWriteChannels_` is already an
array of 4 for MRT), plus every multi-texture-unit `QueuePrimitives()` call site (`DualTextureEffect`,
`PbrEffect`'s 5 units, `SkinnedPbrEffect`) reading its OWN slot's entry instead of the shared globals.
Given this session's own recent, closely-related attempt at a `VertexBuffer`/`IndexBuffer` fix in this
exact same hot path introduced a new crash (see the entry above) and had to be reverted, a second
speculative change to adjacent, equally sensitive draw-command-building code was judged too risky to
attempt blind at the same point in the same session.

**Tracked as:** `plan_llgl.md` Phase LLGL-7, `LLGL-44` (no CTest registration for
`stock_effect_sampler_contract_test.cpp` until this is resolved).

---
