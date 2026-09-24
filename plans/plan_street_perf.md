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

**Found, not caused:** `Vulkan_DrawRangeValidation` fails on `street-opengl4` without any change
from this plan (A/B: reverted source, rebuilt, still fails) -- six of its checks expect an invalid
draw range to be forwarded, and the device now refuses it.

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
