# The eight failing WebGPU ctests

Owner brief of 2026-09-23, after `plan_street_webgpu.md` landed: `plans/plan_webgpu.md` reads as
closed (197 ✅, 1 🟨, 0 ⬜) while `ctest -L WebGPU` had **eight failures**, every one of them on a
row the plan marks done. Diagnose each and fix it.

Branch `webgpu-failing-eight` from `next` `e1c772a6f`. Task IDs `WGF-0001`, … .

## Status

| ID | Test | What it was | Status |
|---|---|---|---|
| WGF-0001 | `Viewport_Cardinality`, `Scissor_Cardinality` | the tests generated rectangles that leave the render target, which `SOFTWARE-226` made illegal | ✅ |
| WGF-0002 | `ContextRecovery` | the shared shadow and IBL bind-group layouts survived a device recreate | ✅ |
| WGF-0003 | `Parity_compressed_cube` | WebGPU had no compressed cube readback at all | ✅ |
| WGF-0004 | `Parity_backbuffer_msaa` | a directly constructed device reported MSAA it never applied | ✅ |
| WGF-0005 | `TextureFilterMipContract` | an out-of-range `MaxMipLevel` aborted the process, and a negative one picked the wrong end of the chain | ✅ |
| WGF-0006 | `SpriteBatch_SortMode` | the test asserted a stability `Array.Sort` does not have | ✅ |
| WGF-0007 | `RealWindowResize` | the window resizes and the drawable size never follows — **not WebGPU's, and not fixed** | 📏 diagnosed |

Seven of the eight pass. `ctest -L WebGPU`: **147/148**, from 140/148.

Three were defects in the renderer (`WGF-0002`, `0003`, `0004`, `0005`), two were tests asserting
behaviour the framework had deliberately changed (`WGF-0001`, `0006`), and one is a shared-layer
finding that reproduces on Vulkan (`WGF-0007`).

---

## WGF-0001 — two cardinality tests built rectangles that leave the target

**Symptom.** Both aborted: `The viewport must fit inside the active render surface and use an
ordered depth range between zero and one`, and the scissor equivalent.

**Root cause.** `SOFTWARE-226` (2026-09-09) restored Microsoft's active-surface bounds check on
`Viewport` and `ScissorRectangle`. Both tests predate it. `Viewport_Cardinality`'s P3 leg derives
32 "distinct" rectangles as `x = i % 32`, `w = 1 + (i % 31)` and so on — **22 of the 32 leave a
32×32 target**. `Scissor_Cardinality` had the same generator plus an S4 leg that deliberately set
two rectangles outside the target and asserted they were "legal".

**Fix.** The generator derives its origin from its own extent (`x = (i % kRT) % (kRT - w + 1)`), so
all 32 stay inside and all 32 stay distinct — which is what those legs measure. S4 keeps the
boundary cases that are still legal (the exact-corner 1×1, and the zero-SIZE rectangle, which
`ScissorRejectsInvalidValuesWithoutChangingState` confirms Microsoft accepts) and now asserts that
the two out-of-bounds ones are **refused** and leave the previous rectangle in place. That is a
stronger statement than "legal", and it keeps the leg's subject: a refusal must disturb the
cardinality no more than an acceptance does, which the counts after it then measure.

---

## WGF-0002 — the shared bind-group layouts outlived their device

**Symptom.** `ContextRecovery` recovered correctly but raised a validation error per family while
it did: *"In wgpuDeviceCreatePipelineLayout, label = 'CNA WebGPU Skinned3D PipelineLayout': Device
... of BindGroupLayout with 'CNA WebGPU Shadow BindGroupLayout' label doesn't match Device"*.

**Root cause.** `ReleaseDeviceOwnedObjectsEXT` calls every family's own `Destroy*Resources()` but
never touched `shadowBindGroupLayout_` or `iblBindGroupLayout_`, the two layouts `WMG-0014` and
`WMG-0022` share across families. `EnsureShadowResourcesEXT`/`EnsureIblResourcesEXT` return early
while their handle is non-null, so a handle left over from a destroyed device is never rebuilt, and
every pipeline layout that names it fails on the new one.

**Fix.** Release and null both with the rest of the device-owned objects. They are device-owned in
exactly the way every family's layout is; being *shared* is what hid them from the teardown.

---

## WGF-0003 — WebGPU could not read a compressed cube face back

**Symptom.** `TextureCube::GetData: the active renderer did not return the complete compressed cube
face region`, thrown and uncaught.

**Root cause.** `ITextureCubeRenderer::GetCompressedDataEXT`'s default refuses, because a converted
RGBA8 readback is not an exact compressed transfer. EasyGL, Software, Vulkan, DirectX 11 and
DirectX 12 all override it. WebGPU did not.

**Fix.** `WebGPUTextureCubeRenderer::GetCompressedDataEXT`, reading the block store the class
already keeps — `compressedLevels_`, which `SetCompressedDataEXT` fills and the sibling `GetData`
already decodes from, because a compressed level cannot be recovered from the GPU as blocks on
every backend. The rules are `VulkanTextureCubeRenderer`'s, because this is one shared contract
rather than two: block-aligned origin, block-aligned extent unless it reaches the mip edge, tightly
packed block rows out, and a refusal rather than a zero fill when the face was never uploaded.

---

## WGF-0004 — a directly constructed device reported MSAA it never applied

**Symptom.** `parity_backbuffer_msaa` — whose header says a backbuffer must be *genuinely*
multisampled, "not merely echo the requested integer" — read sample count 4 and found **zero**
intermediate coverage pixels along an opaque diagonal.

**Root cause.** Two halves, and both were needed:
* `WebGPURenderer`'s constructor ignored `GraphicsRendererCreateArgs::multiSampleCount`. Only the
  `Reset()` path calls `ApplyMultiSampleCount`, so a `GraphicsDevice` built directly from
  `PresentationParameters` kept `sampleCount_` at 1 — no multisampled colour texture, no resolve.
  EasyGL takes the count at construction, which is why the same fixture passes there.
* `GetAppliedMultiSampleCountEXT`'s interface default **echoes the request back**, and WebGPU did
  not override it, so `GraphicsDevice` wrote 4 into `PresentationParameters` regardless.

**Fix.** Apply the requested count at construction, after `ConfigureSurface`, and answer
`GetAppliedMultiSampleCountEXT` from the applied state the way EasyGL does.

---

## WGF-0005 — `MaxMipLevel` could abort the process, and a negative one aimed the wrong way

**Symptom.** A `panic in a function that cannot unwind` inside wgpu-native, aborting the test
binary. The first error in the chain: *"Invalid lodMaxClamp: 32. Must be greater or equal to
lodMinClamp (which is 99)"*.

**Root cause, twice over.**
* `FillWGPUSamplerDescriptor` set `lodMinClamp = max(0, maxMipLevel)` against a fixed
  `lodMaxClamp = 32`. `MaxMipLevel = 99` is legal in XNA — it simply names a level past the chain —
  but it produces a sampler WebGPU rejects. An invalid sampler invalidates the bind group, which
  invalidates the draw, which makes `wgpuQueueSubmit` panic in a non-unwinding frame.
* `max(0, ...)` also folds a NEGATIVE `MaxMipLevel` onto level 0. XNA writes that field to
  Direct3D 9's **unsigned** `MaxMipLevel`, so −1 arrives as a very large level and names the LAST
  one — the opposite end of the chain from what the clamp produced. `L3` and `L9` measure exactly
  that.

**Fix.** Clamp `lodMinClamp` to `lodMaxClamp`, and map a negative straight to the upper clamp.
**And the cache key with it**, which is what made the first attempt look like it had not compiled:
`GetOrCreateSlotSampler` keyed on `max(0, maxMipLevel)`, so the sampler asking for the last level
collided with the one asking for the most detailed and the cache returned whichever was built
first — the descriptor's clamp could never be reached. 99/99 checks now pass.

---

## WGF-0006 — the sort-mode test asserted a stability XNA does not have

**Symptom.** `Check F: Texture mode (one texture group) preserves submission order (blue on top)`.

**Root cause.** `SpriteBatch::flushBatch` sorts through `XnaArraySort`, a faithful reproduction of
.NET's `Array.Sort` — an introsort, and **not stable**. CNA's own renderer-neutral
`SpriteBatchSortModeTest.EqualDepthsMatchXnaArraySortOrdering` pins the consequence: three sprites
of equal key come out in REVERSE submission order. Two sprites of one texture come out reversed for
the same reason, so the top sprite is the one submitted FIRST.

**Fix.** The check asserts the contract instead of stability, and prints the colour it measured.
Measured: `255,0,0` — red, the first-submitted sprite, exactly as `Array.Sort` predicts.

---

## WGF-0007 — the window resizes, the drawable size does not follow

**Not fixed, and not WebGPU's.** `Vulkan_RealWindowResize` fails identically in the same private
compositor, so the cause is in the shared platform/device path rather than in either renderer.

**What was measured**, which is the value this row adds — the old failure message named the wrong
half:

```
window 1600x680, target 1600x680, renderer logical 800x480, viewport 800x480
```

The platform window **did** reach the requested size. What never changed is the size the renderer
reports (`GetViewportSize`), and therefore the device's `Viewport`. `GraphicsDevice::Present()`
calls `UpdateViewportFromWindow()` every frame, which re-queries `IPlatformWindow::GetPixelSize()`
and hands it to `OnSurfaceChanged`; 300 frames of that changed nothing, so the drawable size the
platform reports is what stayed behind — not the compositor, which honoured the resize, and not
the renderer, which was never told.

The test's timeout now says which half timed out and prints all four numbers, because the message
it used to print — "the platform resize never arrived" — is false here and points the next reader
at the compositor.

Chasing the drawable-size path further belongs with the platform layer and both affected renderers,
not in a WebGPU row; it wants a task of its own in `plans/plan_platform.md`.
