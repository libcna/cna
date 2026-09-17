# CNA Known Bugs

> **Retired renderers.** Bug entries that belonged to a renderer retired on 2026-09-17
> (`docs/removed-renderers.md`) were removed from this list with that renderer -- six `LLGL`
> entries. They are in git history; nothing in them applies to a renderer CNA still has.

## FNA3D resource renderers that outlive their device free through a dangling FNA3D_Device

**Backend:** FNA3D (found on the SDL_GPU/Vulkan driver; the ownership bug is driver-independent).

**Status:** OPEN. Found 2026-08-14 by the plans/plan_fx.md FX-054 full-suite regression run; not caused
by the compiled-effect work, which never touches these types.

A `Fna3dRenderTargetCubeRenderer` (and, by the same pattern, the other `Fna3dResources.cpp`
renderers) keeps a raw `FNA3D_Device*` and guards its destructor only with `device_ == nullptr`.
When the resource outlives its `GraphicsDevice` -- which is exactly what
`MetalResourceHealth.RenderTargetCubeRendererEscapesThroughTextureCubeBaseMove` deliberately
arranges by moving a renderer out through the `TextureCube` base and holding the `shared_ptr` past
device destruction -- that pointer is dangling rather than null, so the destructor calls
`FNA3D_AddDisposeTexture` on freed memory.

Effect: running the whole `CnaTests` binary under the FNA3D renderer ends in a segmentation fault
after the last test, so the run produces no gtest summary even though every test passed. The suite
passes when run standalone, which is why it went unnoticed.

AddressSanitizer, from a full-suite run of `cmake-build-fna3d-asan`:

```
ERROR: AddressSanitizer: heap-use-after-free
    #0 FNA3D_AddDisposeTexture FNA3D.c:754
    #1 Fna3dRenderTargetCubeRenderer::~Fna3dRenderTargetCubeRenderer Fna3dResources.cpp:452
   ...
    #10 MetalResourceHealth_RenderTargetCubeRendererEscapesThroughTextureCubeBaseMove_Test::TestBody
        MetalResourceHealthTests.cpp:240
freed by:
    #1 SDLGPU_DestroyDevice FNA3D_Driver_SDL.c:4263
    #2 FNA3D_DestroyDevice FNA3D.c:247
    #3 Fna3dRenderer::~Fna3dRenderer Fna3dRenderer.cpp:428
```

**Fix direction:** give the FNA3D renderer a shared liveness token that its destructor
invalidates, and have every `Fna3dResources.cpp` renderer hold a weak reference to it and skip
native disposal once the device is gone -- the discipline several other renderers already apply,
and the discipline the neighbouring `MetalResourceHealth.*RejectAfterDeviceDeath` cases exist to
enforce.

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

