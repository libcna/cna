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
