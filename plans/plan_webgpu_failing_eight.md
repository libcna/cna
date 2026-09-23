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
| WGF-0007 | `RealWindowResize` | the test assumed a presentation mode that stopped being the default | ✅ |

All eight pass. `ctest -L WebGPU`: **148/148**, from 140/148.

Four were defects in the renderer (`WGF-0002`, `0003`, `0004`, `0005`) and three were tests
asserting behaviour the framework had deliberately changed (`WGF-0001`, `0006`, `0007`). That split
is the lesson: a plan row being green says the work was done, not that the test still describes the
framework it was written against.

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

## WGF-0007 — the test assumed a presentation mode that stopped being the default

**Symptom.** `Timed out waiting for the real window resize to propagate (the platform resize never
arrived)`, plus `Viewport width changed after a real window resize` failing.

**The first diagnosis was wrong, and measuring is what corrected it.** The message named the
platform, so the platform is where the investigation started; a temporary probe in
`GraphicsDevice::UpdateViewportFromWindow` then showed the drawable size arriving correctly:

```
[diag-uvfw] #0 drawable=800x480    #3 drawable=1600x680    #300 drawable=1600x680
```

The window resized, the platform reported it, and `OnSurfaceChanged` received it. A second probe,
inside `GetViewportSize`, named the real cause:

```
[diag-gvs] #300 drawable=1600x680 mode=0 virtual=800x480 -> 800x480
```

`mode=0` is `CnaPresentationMode::Letterbox`, under which the LOGICAL viewport is pinned to the
virtual resolution in both axes **by design** — the window's new shape reaches the device as a
different physical rectangle instead, and it did: `233,0 1133x680`, correctly letterboxed inside
1600×680. Nothing was broken.

**Root cause.** The test's header calls `FixedHeightDynamicWidth` "the default". It stopped being
one; `GraphicsRendererCreateArgs` defaults to `Letterbox`. A width-only oracle can never pass under
Letterbox. The reference test (`easygl_real_window_resize_test.cpp`) had already met this and says
so in its constructor — *"This test predates Letterbox becoming CNA's default ... state that
prerequisite instead of making the test silently depend on whichever presentation mode the
framework defaults to"* — and asks for the mode it needs. The WebGPU copy never did.

**Fix.** State the same prerequisite in the same place, so the two tests measure the same thing in
the same mode. A seventh check came out of the investigation and is kept: the PHYSICAL viewport
rectangle followed the resize too (`0,0 1600x680`, was `0,0 800x480`), which is what a renderer
actually programs and what distinguishes "the device recomputed its viewport" from "the logical
number happened to move". The timeout arms now say which half timed out and print all four numbers.

**Note on `Vulkan_RealWindowResize`**, which fails in this environment and is NOT this row's: it
runs the EasyGL source, which already asks for the mode, and fails differently — *"X11/Xvfb resize
event never arrived"*, with `ClientSizeChanged` never firing. That test provokes the resize through
the windowing library's own native call, which is exactly the path the WebGPU test's header
explains it avoids. An environment limit of the private rootful Xwayland, not a renderer defect,
and not something this row changes.
