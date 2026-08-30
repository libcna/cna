# CNA Known Bugs


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

## `CnaTests` gtest fixtures have no device-unavailable guard, and one failure can break every later test in the same process — OPEN

**Backend:** any renderer whose device construction can fail on the test display. First seen
with LLGL's Vulkan module on a DRI3-less Xvfb; that renderer was removed on 2026-08-30
(`docs/removed-renderers.md`), but the defect is in `CnaTests`, not in it, and any
Vulkan-selecting build on a WSI-less display can reproduce it.

**Symptom:** `CnaTests` — the general gtest binary that runs graphics-fixture tests against
whichever renderer is configured — links no skip guard for "this host cannot create a device".
A fixture that constructs a real `GraphicsDevice` (e.g. `Texture3DTest`) throws mid-construction
(`VK_ERROR_SURFACE_LOST_KHR` in the original reproduction). gtest catches it cleanly, so on its
own this is a false `[FAILED]` rather than a crash. But the partially-torn-down
`GraphicsDevice`/X11 client connection appears to corrupt something process-wide: the *next*
test in the same process immediately hits `"X connection to :99 broken (explicit kill or server
shutdown)"` and fails too. The Xvfb server itself is unaffected (confirmed responsive via
`xdpyinfo` immediately after) — only that one process's connection dies. Because
`gtest_discover_tests(... DISCOVERY_MODE PRE_TEST ...)` runs each `TEST()` as its own process,
ordinary `ctest` runs are not expected to cascade; the cascade reproduces only when several such
fixtures are forced into one process by a broad `--gtest_filter`.

**Reproduction:** a renderer whose device cannot initialise on the test display,
`SDL_VIDEODRIVER=x11`, `DISPLAY=:99` (no DRI3):
`./CnaTests --gtest_filter="*Window*"` (or any filter pulling several
`GraphicsDevice`-constructing fixtures into one process) — the first reports `[FAILED]` with the
device exception, the next reports a broken X connection.

**Open question:** whether every `CnaTests` fixture that constructs a real `GraphicsDevice` needs
a device-unavailable guard, or whether `ctest`'s per-test process isolation already makes this
moot in normal CI use. Needs its own investigation before being closed.

---
