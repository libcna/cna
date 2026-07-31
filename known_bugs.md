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

## LLGL backend: the OpenGL module clears but draws nothing

**Backend:** LLGL (`CNA_GRAPHICS_BACKEND=LLGL`), OpenGL module only. The Vulkan module is
unaffected and is what the default runtime preference selects.

**Symptom:** With `CNA_LLGL_RENDERER=opengl`, the window is cleared correctly but no sprite ever
appears. `Llgl_2D` fails every pixel check while `Llgl_Smoke` still passes, because the smoke test
only clears and presents.

**What was measured (2026-07-31):** reproduced in a standalone LLGL-only spike with no CNA code
involved, on Mesa llvmpipe, GL 4.5 core. A quad drawn with an identity matrix renders in the right
place — so the position attribute, the pipeline and the render pass are all fine — but `texCoord`
and `color` both arrive as zero in the shader, and the `Scene` uniform block is never fed (with the
real projection matrix the quad collapses to nothing). Adding explicit `layout(location=)` and
`layout(binding=)` qualifiers to the OpenGL shader flavour did not change the result. The same
shaders, pipeline layout and `SetResource` calls work correctly on the Vulkan module.

**Suspected area:** LLGL's OpenGL handling of individually bound (non-heap) resources —
`GLImmediateCommandBuffer::SetResource` and the VAO built from the vertex buffer's attributes. A
`ResourceHeap`-based binding path may behave differently and has not been tried.

**Impact:** on a machine with no usable Vulkan driver the automatic fallback selects OpenGL and the
game renders a blank window instead of failing. Until this is resolved, treat the LLGL backend's
OpenGL module as unsupported and prefer `CNA_LLGL_RENDERER=vulkan`.

**Tracked as:** `plan_llgl.md` task `LLGL-17`.

---

## LLGL backend: `SetBlendFactor` hits an unsupported GL procedure

**Backend:** LLGL, OpenGL module.

**Symptom:** `LLGL::CommandBuffer::SetBlendFactor` threw
`ErrUnsupportedGLProc: illegal use of unsupported OpenGL procedure: glBlendColor` on this
environment's GL context, aborting the frame.

**Status:** worked around, not fixed. The backend now emits `SetBlendFactor` only when the active
blend state genuinely uses a `BlendFactor`/`InverseBlendFactor` term, which is both correct and
cheaper. A game that really uses `Blend::BlendFactor` on such a driver will still fail — loudly,
with LLGL's own error.

**Tracked as:** `plan_llgl.md` task `LLGL-18`.

---
