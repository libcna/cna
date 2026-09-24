# cna-street speed on Vulkan, SDL_GPU and WebGPU

Owner brief of 2026-09-24: once cna-street ran on OpenGL4 ([`plan_street_opengl4.md`](plan_street_opengl4.md)),
the owner asked why OpenGL4 was so much faster than the other renderers, and then asked for the
causes to be fixed.

Branch `street-perf` from `street-opengl4` `923afe323` (which carries the `GpuTimer` fix these
measurements need). Task IDs `STREETPERF-0001`, … .

## The measurement that started it

cna-street's `--benchmark baseline` (a fixed camera on the footway, everything on; 1 212 draws a
frame of which 924 are shadow casters), the same Release tree, each renderer selected at runtime,
all in the private compositor. Radeon 780M, Mesa 25.0.7 (radeonsi and RADV). The machine's load
average was 7-30 from other agents, so the absolute numbers are noisy; the order was the same in
both rounds.

| renderer | CPU frame | fps | GPU frame | CPU in the shadow pass (924 draws) |
|---|---|---|---|---|
| OPENGL4 | 33-40 ms | 25-30 | 39-42 ms | 7.8 ms |
| OPENGL33 (EasyGL) | 34-37 ms | 27-29 | 37-38 ms | 8.0 ms |
| VULKAN | 120 ms | 8.3 | 44 ms | 54 ms |
| SDL_GPU | 94-237 ms | 4-11 | (no timer) | 102 ms |
| WEBGPU | 316-367 ms | 3 | (partial) | 140 ms |

The GPU does about the same work on every renderer; the difference is CPU time per draw -- about
9 µs on either GL renderer, 59 µs on Vulkan, 110 µs on SDL_GPU and 150 µs on WebGPU. Sampling the
main thread with gdb (60 stacks each) named a different cause in each:

* **Vulkan** -- 32 of 60 in `VulkanRenderer::ProcessRetiredResources` at `Present`.
* **SDL_GPU** -- 40 of 60 in `SdlGpuRenderer::UploadSceneDrawData`, creating a GPU buffer and a
  transfer buffer for every draw of every frame (kernel VA allocation, `amdgpu_va_range_alloc`).
* **WebGPU** -- most stacks in `malloc`/`memcpy` under `wgpuQueueWriteBuffer`, called once per draw,
  plus a bind group created per draw.

None of the three bring-ups (`plan_street.md`, `plan_street_webgpu.md`, `plan_street_sdlgpu.md`)
measured speed; they compared pictures.

## Status

| ID | Task | Status |
|---|---|---|
| STREETPERF-0001 | Vulkan: the retired-resource queue was drained with one `vector::erase` per entry | ✅ |
| STREETPERF-0002 | SDL_GPU: every draw re-uploaded its buffers' whole contents every frame (~430 MB), and every `SetData` submitted a command buffer of its own | ✅ |
| STREETPERF-0003 | Vulkan: every draw copied its whole vertex buffer and its indices, twice | ✅ |

**Found, not caused** (each A/B-checked: reverted source, rebuilt, still fails):

* `Vulkan_DrawRangeValidation` -- six of its checks expect an invalid draw range to be forwarded,
  and the device now refuses it.
* On Vulkan (`cmake-build-cnaext`), 3 of 2 894 `CnaGraphicsTests` cases:
  `InstancedDrawMultiStreamTest.DuplicateSemanticStreamsRemapToUnusedIndices`,
  `OrdinaryDrawBindingOffsetTest.MultipleStreamsUseOnlyTheGeometryStreamsOwnOffset`,
  `OrdinaryDrawMultiStreamTest.SixteenBindingsCanSupplyAConsumedSemanticFromSlot15`.
* On SDL_GPU (`cmake-build-sdlgpu`): 25 of 238 `-R '^SdlGpu'` ctests and 25 of 2 845
  `CnaGraphicsTests` cases. Two of the latter
  (`SdlGpuIndexedDrawRangeTest.MutatingSourceBuffersAfterQueuingDoesNotChangeQueuedDraws`,
  `…RejectsIndexedRangesOutsideTheBoundBuffers`) rewrite a buffer that is still bound, which the
  device now refuses as XNA does ("The vertex buffer resource is in use").

---

## STREETPERF-0001 — the retired-resource queue was drained quadratically

**Root cause.** `ProcessRetiredResources` walked `retiredResources_` (a `std::vector`) and erased
each bucket old enough to free with `it = erase(it)`, which moves every later bucket down one
place, once per erased bucket. With `CNA_VULKAN_LIFETIME_TRACE=1` the street retires **~940
buckets a frame**, almost all one descriptor set each (a `ShaderEffect`'s set 1, re-made whenever
its textures change, and the old one retired), so each `Present` erased ~940 buckets from the
front of a ~2 800-entry vector. `retiredMrtProxies_` was drained the same way.

**Fix.** Both queues are compacted in one pass: an eligible entry is freed (or, for a proxy, its
last share released) and skipped, a kept entry is moved down, and the tail is erased once. The
order of the kept entries and the order in which buckets are freed are both unchanged.

**Result.** `ProcessRetiredResources` is gone from the profile (0 of 60 stacks). The frame went
from 120 ms to 97-104 ms (load average 13-15 during that run) -- less than the profile promised,
because the next cost stands behind it: `QueueCustomEffect3DDrawEXT` now holds 23 of 60 stacks.
The Vulkan renderer's vertex buffers live in host memory only, so every draw copies the *whole*
vertex buffer and its indices into the draw record, and the replay copies them again into the
frame's arena. That is its own task.

**Regression runs** (`cmake-build-cnaext`, VULKAN;OPENGLES3, Wayland):

| suite | result |
|---|---|
| `-R '^Vulkan_'` | 383 / 1 of 384 -- the one is `Vulkan_DrawRangeValidation`, above |
| `CnaGraphicsExtTests` | **937 / 0 / 32** (935 / 0 / 32 before `STREETGL4-0002` added two) |
| `CnaRendererTests` | **231 / 0 / 10** |

---

## STREETPERF-0002 — SDL_GPU uploaded the street's geometry again every frame

**Root cause, in three layers**, each found by sampling after the one before was fixed:

1. `UploadSceneDrawData` created an `SDL_GPUBuffer` *and* an `SDL_GPUTransferBuffer` for every
   draw of every frame and released both after it (40 of 60 stacks, most in the kernel's
   `amdgpu_va_range_alloc`).
2. Behind that, the bytes themselves. SDL_GPU replays draws at `Present()`, and each draw kept
   its geometry by copying its vertex buffer from `vertexStart` to the end, and the whole index
   buffer, into the draw record. Counted: **~5 900 uploads, 420-440 MB a frame** -- the scene
   holds 71 MiB; a mesh drawn into four shadow cascades was copied four times.
3. Behind that, `SetData`: every call created a transfer buffer and acquired, recorded and
   **submitted a command buffer of its own**. `InstancedRendererEXT::setInstances` rewrites ~340
   instance buffers a frame (41 of 60 stacks, 29 in `VULKAN_Submit`).

**Fix.**

* The vertex and index buffers already owned a GPU buffer that `SetData` filled. It becomes an
  `SdlGpuBufferStorageEXT` held by `shared_ptr`, and a draw that reads a buffer's bytes unchanged
  binds that storage at the byte offset of its first vertex (`ResidentGeometryEXT`,
  `uploadedVertexOffset`) instead of copying it. Every stock family's primary stream, Skinned
  (unless its stream had to be normalized), PBR, custom `ShaderEffect` and compiled-effect draws,
  and every index buffer.
* XNA semantics stay: a draw renders what its buffers held when it was issued. `SetData` on a
  buffer whose storage a queued draw still holds moves the buffer to fresh storage, so the queued
  draw keeps the old contents; otherwise the storage is rewritten in place (cycling, as before).
  Normalized skinned streams, extra and per-instance streams and the neutral record are still
  carried by the draw.
* What is still staged goes through pools: scene buffers keyed by usage and power-of-two size,
  bone-palette textures, and 8 MB transfer chunks, each chunk mapped once a frame with cycling.
  Every placement is decided first, then each chunk mapped and filled once, and only then the
  copies recorded, so no copy is recorded from a chunk that is mapped afterwards.
* `SetData` queues its bytes (`QueueBufferUploadEXT`, thread-safe for loading threads); the
  frame's copy pass -- which already runs before any render pass -- writes them first, in order.
  A frame that fails to submit hands them back in front of anything queued since.

**Found on the way, and fixed: the neutral record was read past its end.** It is an
instance-rate stream of one 16-byte record, so instance *i* of an instanced draw read byte
16·*i*. With an exactly-sized buffer that read was out of bounds, and RADV's robust buffer access
happened to answer (0, 0, 0, 1) -- the record's own value. A pooled 256-byte buffer answered with
whatever it held before: the first build of this task differed from the old SDL_GPU by 0.01-0.1 %
at five viewpoints, all thin edges of instanced props; with exact-size pools it was bit-identical
again, which located it. Each instance count now gets one record per instance.

**Measured** (`--benchmark baseline`; load average 5-10 during these runs):

| | CPU frame | fps | staged a frame |
|---|---|---|---|
| as found | 94-237 ms | 4-11 | ~430 MB, ~5 900 uploads |
| pools only | 109-118 ms | 8.5-9.2 | same bytes, no native allocations |
| + resident storage | 48-52 ms | 19-21 | ~0.75 MB, ~440 uploads |
| + queued `SetData` | **24.2-24.4 ms** | **41** | same |
| OPENGL4, same runs | 33.6 ms | 29.8 | -- |

**The picture:** the 18 captures are **bit-identical to the unmodified SDL_GPU's** (0.000 % at
every viewpoint); against Vulkan the only differences are the views that also differ between two
Vulkan runs (the street's shop dressing and traffic depend on timing).

**Tests.**

* `BufferRewriteWithinFrameTests.cpp` (renderer-neutral, `CnaGraphicsTests`): a draw keeps the
  vertices, and an indexed draw the indices, it was issued with when the buffer is rewritten
  before the frame is flushed; a buffer rewritten after a flush draws its new contents. **A/B:**
  with the move to fresh storage disabled, the first two fail.
* `SdlGpuResidentGeometryTests.cpp` (`CnaRendererTests`): 32 draws from two unchanged buffers
  stage no bytes at all (`GetLastSceneUploadBytesEXT`, test-only).

**Regression runs** (`cmake-build-sdlgpu`, SDL_GPU on its Vulkan backend):

| suite | result |
|---|---|
| `-R '^SdlGpu'` | 213 / 25 of 238 -- the same 25 as the unmodified renderer |
| `-L CnaExt` | 31 / 1 -- `CNAEXT_NoPosixSetenv`, as on every renderer |
| `CnaGraphicsExtTests` | **931 / 0 / 38** |
| `CnaRendererTests` | **224 / 0** |
| `CnaGraphicsTests` | 2 593 / 25 / 227 -- the same 25 as the unmodified renderer |

---

## STREETPERF-0003 — Vulkan copied every draw's geometry twice

**Root cause.** With `STREETPERF-0001` in, `QueueCustomEffect3DDrawEXT` held 23 of 60 stacks and
the record 13 more. This renderer's vertex and index buffers already own a host-visible `VkBuffer`
that `SetData` writes, but no draw ever bound it: every draw copied the buffer's bytes into its
record -- for an indexed draw the *whole* vertex buffer -- and `RecordCommandBuffer` copied them
again into the frame's 3D arena. The same ~430 MB a frame SDL_GPU uploaded, copied twice.

**Fix.** The same rule as SDL_GPU, fitted to this renderer's retirement queue:

* A draw binds its buffers' own `VkBuffer`s (`Pending3DDraw::residentVb/residentIb` and their
  byte offsets: the draw's first vertex, or `startIndex` for indices -- exactly the windows the
  copies used). `BindForDrawEXT` marks the buffer as bound since its last write.
* `SetData` on a bound buffer moves to a fresh `VkBuffer` of the same size and retires the old one
  on the frame fence, so the queued draw -- and any frame still in flight -- keeps what it was
  issued with. A buffer nothing has bound since its last write is rewritten in place, as before;
  the invariant "unbound since written" is exactly "no command reads it". `EnsureByteCapacity`
  retires a bound buffer instead of destroying it, for the same reason.
* Covered: the stock `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` routes, custom `ShaderEffect`
  draws (ordinary and instanced, the index source now passed through), and the instanced core's
  single stream. Packed multi-stream draws and the indirect route keep their snapshots; the
  indirect route builds its own from a folded first record, so it clears the capture's in-place
  bindings -- the first build missed that, and `Vulkan_IndirectDraw` said so (31/256 red pixels).

**Measured** (`--benchmark baseline`, load average 3-4):

| | CPU frame | fps | GPU frame |
|---|---|---|---|
| as found | 120 ms | 8.3 | 43.5 ms |
| after `STREETPERF-0001` | 97-104 ms | 9.7-10.3 | 43.2 ms |
| after this task | **27.0-28.0 ms** | **35.7-37.0** | 41.6 ms |
| OPENGL4, same runs | 38.0 ms | 26.4 | 49.6 ms |

**The picture:** the 18 captures are **bit-identical to the pre-change Vulkan capture** at every
viewpoint. Under `VK_LAYER_KHRONOS_validation` for 40 frames: no errors -- ten
`AllocateDescriptorSets-WrongType` warnings, about descriptor pools this task does not touch.

**Tests.** `BufferRewriteWithinFrameTests` (from `STREETPERF-0002`) passes on Vulkan, and with the
move to a fresh buffer disabled its first two cases fail here too. `VulkanResidentGeometryTests`:
32 draws from two unchanged buffers copy no bytes into the frame arena
(`GetLastArenaGeometryBytesEXT`, test-only).

**Regression runs** (`cmake-build-cnaext`, VULKAN;OPENGLES3, Wayland):

| suite | result |
|---|---|
| `-R '^Vulkan_'` | 383 / 1 of 384 -- `Vulkan_DrawRangeValidation`, as before |
| `CnaGraphicsExtTests` | **937 / 0 / 32** |
| `CnaRendererTests` | **231 / 0 / 10** before the new test; the new test passes |
| `CnaGraphicsTests` | 2 691 / 3 / 200 -- the three above, which fail without this change |
