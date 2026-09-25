# WebGPU per-draw CPU cost — binding-model redesign

**Status: open, not started.** Recorded 2026-09-25 at the owner's request, for whoever picks it up
(human or AI agent). Nothing below has been implemented; the task IDs are proposals.

Task IDs: `WEBGPUPERF-0001`, … . Predecessor and evidence:
[`plan_street_perf.md`](plan_street_perf.md) (`STREETPERF-0001`..`0004`, branch `street-perf`),
which fixed the geometry copies on Vulkan, SDL_GPU and WebGPU and measured what is left here. The
general WebGPU plan is [`plan_webgpu.md`](plan_webgpu.md); its capability boundary is
`docs/webgpu-renderer.md`.

## The problem in one paragraph

On cna-street's `--benchmark baseline` (1 212 draws a frame, 924 of them shadow casters) WebGPU
spends **~104 ms of CPU a frame (9.6 fps)** where Vulkan spends 29 ms, SDL_GPU 24 ms and OpenGL4
27 ms, for the same GPU work. It is no longer copying geometry (`STREETPERF-0004` removed that:
it was 316-367 ms before). What remains is the renderer's *frame structure*: every draw creates its
bind groups and writes each of its uniform blocks into a fresh transient buffer with
`wgpuQueueWriteBuffer`; every render-target switch finishes and submits an encoder; and every GPU
timer `begin`/`end` flushes and submits the pending draws, plus one more submit to resolve the
query. wgpu's per-call cost for bind-group creation, queue writes and submits (with its tracking and
`maintain`) is what the frame is made of.

## Measurements

All on the Radeon 780M (Mesa 25.0.7 RADV under wgpu-native v29.0.1.1), cna-street's Release tree
`../cna-street/build`, the private compositor, back to back.

| renderer | CPU frame | fps |
|---|---|---|
| OPENGL4 | 26.8 ms | 37.3 |
| OPENGL33 (EasyGL) | 29.7 ms | 33.7 |
| VULKAN | 29.4 ms | 34.0 |
| SDL_GPU | 23.6 ms | 42.4 |
| **WEBGPU** | **104 ms** | **9.6** |

**With GPU timers switched off** (a local experiment -- `SupportsGpuTimerEXT()` forced false, not
committed): WebGPU 83-95 ms. The timers therefore cost ~20 ms; the other ~85 ms is per-draw binding
and per-flush submission.

**Profile** (gdb, 60 main-thread stacks at viewpoint 1, after `STREETPERF-0004`; a stack counts in
every row it passes through):

| where | stacks |
|---|---|
| `RenderPendingDrawsToRenderTarget` (the replay, via `FlushCurrentRenderTarget`) | 44 |
| … reached from `GpuTimer::end` → `WebGPUGpuTimerRenderer::End` → `WriteTimestampEXT` | 28 |
| … reached from `SetRenderTarget2D` | 19 |
| `wgpuDeviceCreateBindGroup` (wgpu `device_create_bind_group`) | 14 |
| `wgpuQueueSubmit` | 13 |
| `wgpuCommandEncoderFinish` (wgpu `encode_render_pass`) | 12 |
| wgpu `Queue::maintain` / `Device::maintain` (per-submit) | 11 |
| `IssuePbrDraw` | 11 |
| `wgpuQueueWriteBuffer` | 6 |
| `malloc` | 7 |

## Where the time goes, with code pointers

Paths are relative to `modules/renderers/webgpu/src/`; line numbers are as of `street-perf`
`f18076dba` and will drift.

1. **Bind groups per draw.** 25 `wgpuDeviceCreateBindGroup` call sites in `WebGPURenderer.cpp`, 4
   in `WebGPURendererModern.cpp`, 1 in `WebGPUModernEffect.cpp`. Each stock `Issue*Draw`
   (`IssueColoredDraw`, `IssueTexturedDraw`, `IssueLitTexturedDraw`, `IssueAlphaTestDraw`,
   `IssueDualTextureDraw`, `IssueEnvMapDraw`, `IssueInstancedDraw`, `IssuePbrDraw`,
   `IssueSkinnedDraw`, `IssueSkinnedPbrDraw`, `IssueCustomEffectDraw`,
   `IssueDescriptorEffectDrawEXT`, `IssueCompiledEffectDraw`) builds its groups from scratch and
   pushes them to `pendingBindGroupReleases_`. `IssuePbrDraw` (≈line 14 827) alone creates four: a
   UBO group over three per-draw uniform buffers, a texture/sampler group, a shadow group
   (`CreateShadowBindGroupEXT`, `WebGPURendererModern.cpp` ≈1312, which also writes its own
   transient uniform) and an IBL group (`CreateIblBindGroupEXT`, ≈1432).
2. **Uniform writes per draw.** Every uniform block of every draw is
   `AcquireTransientBuffer(...)` + `wgpuQueueWriteBuffer(...)`. wgpu allocates staging memory and
   records a copy for each write. The transient pool (`AcquireTransientBuffer` /
   `RecycleTransientBuffer`, ≈line 3546/3597) removed the buffer *creation* long ago; the write and
   the bind group that names the buffer remain per draw.
3. **The ShaderEffect route.** The street's 924 shadow casters are custom `ShaderEffect` draws:
   `IssueDescriptorEffectDrawEXT` → `BuildDescriptorEffectBindGroupsEXT`
   (`WebGPUModernEffect.cpp` ≈384) writes one transient buffer per declared block from the draw's
   snapshot and builds up to four bind groups per draw from the reflected layout.
4. **A submit per flush.** `FlushCurrentRenderTarget` (≈11 899) → `RenderPendingDrawsToRenderTarget`
   (≈11 695) encodes the pending draws, finishes the encoder and submits it. It runs on every
   render-target switch (shadow cascades, prepass, scene, every post pass) and from
   `FlushPendingDrawsForModernEXT` (`WebGPURendererModern.cpp` ≈465).
5. **Timers flush.** `WriteTimestampEXT` (`WebGPURendererModern.cpp` ≈986, `WMG-0017`) calls
   `FlushPendingDrawsForModernEXT()` at both ends of a range, because this renderer's timestamps
   ride on the passes' `timestampWrites`: the passes inside a range must close while it is open.
   Each range end then submits one more encoder to resolve the query set. The street times five
   stages and every post pass, so this is a few dozen extra encoders and submits a frame.

## Constraints a fix must keep

* **XNA semantics.** A draw renders what its resources held when it was issued, even if the game
  rewrites them before the frame is presented (`BufferRewriteWithinFrameTests`, renderer-neutral).
  `wgpuQueueWriteBuffer`/`wgpuQueueWriteTexture` execute before the *next* `wgpuQueueSubmit`, not in
  command-buffer order. Anything that defers or batches submits must still put every queue write
  made *outside* the replay (buffer/texture/storage `SetData`) after the command buffers already
  encoded before it -- e.g. submit what is encoded before such a write.
* **Resident geometry (`STREETPERF-0004`).** A draw binds its vertex/index buffer's own
  `WebGPUBufferStorageEXT`; `SetData` moves to fresh storage when `shared_ptr::use_count() > 1`,
  i.e. while a *queued* command holds it. A command that has been encoded into a command buffer
  that is not yet submitted is no longer in the queue -- if submission is deferred, that case needs
  the same protection (keep the storage referenced until the submit, or submit first).
* **The transient recycler.** `pendingBufferReleases_` → `RecycleTransientBuffer` pools by
  power-of-two size class. A buffer may not be reused until the submit that reads it; resident
  storage must never be handed to it (a pooled resident buffer is overwritten by the next user).
* **Timers.** `WMG-0017`'s semantics (passes between `begin` and `end` carry the timestamp writes;
  one timer owns a pass), and `CNA::Graphics::GpuTimer` keeps up to four ranges in flight
  (`STREETGL4-0002`). `docs/cnaext-engine-layer.md` documents the per-pass timing contract.
* **Portability.** The renderer also builds for the browser (Emscripten). Dynamic uniform offsets
  are core WebGPU; native-only features (e.g. timestamp writes inside encoders) need a fallback.
  Respect `minUniformBufferOffsetAlignment` (typically 256) and
  `maxDynamicUniformBuffersPerPipelineLayout` (≥ 8 in core).
* **Device loss** (`WEBGPU-182`): anything new that owns device objects registers as a
  `IWebGPUDeviceResourceEXT` and is released/recreated with the rest.
* **Pictures do not change.** Every task below is a performance change: the street's 18 captures
  must stay identical (A/B, see *How to measure*).

## Proposed tasks

| ID | Task | Status |
|---|---|---|
| WEBGPUPERF-0001 | Test-only counters per frame (bind groups created, queue writes and bytes, submits) and a baseline recorded here | ⬜ |
| WEBGPUPERF-0002 | Stock families: one uniform arena per flush, bound with dynamic offsets; one UBO bind group per (arena, layout) per flush; one `wgpuQueueWriteBuffer` of the arena before the submit. PBR and skinned PBR first -- they are the street's opaque pass | ⬜ |
| WEBGPUPERF-0003 | Cache texture/sampler bind groups keyed by (layout, views, samplers); evict when a texture/view dies (hook into the texture renderers' release) and by frame age | ⬜ |
| WEBGPUPERF-0004 | Shadow and IBL groups: uniforms into the arena, texture parts cached | ⬜ |
| WEBGPUPERF-0005 | ShaderEffect descriptor route: reflected uniform blocks into the arena with dynamic offsets (the reflected layout marks them dynamic); cache the non-uniform groups. This is the street's 924 shadow casters | ⬜ |
| WEBGPUPERF-0006 | Fewer submits: resolve timer queries in the flush that is already happening instead of a separate encoder; evaluate one encoder per frame, submitted early only when a queue write, readback or present needs ordering | ⬜ |

Each task: the street's 18 captures identical before/after; `-L WebGPU` 148/0 in
`cmake-build-webgpu`; `CnaRendererTests` and `CnaGraphicsTests` keep exactly the failures that
exist without the change (2 and 20 on `street-perf`, listed in `plan_street_perf.md`);
`CnaGraphicsExtTests`; the numbers recorded here.

**Target:** WebGPU's CPU frame on `--benchmark baseline` within about 1.5× of Vulkan's (≈45 ms or
better) with identical captures.

## How to measure

Build (cna-street carries all five renderers; see `plan_street_perf.md`):

```sh
export CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv
cd ../cna-street
cmake --build build -j8 --target cna-street compare-images

R=../cna/tools/platform/run_gpu_tests_private.sh          # never the live desktop
CNA_GRAPHICS_RENDERER=WEBGPU $R --exec ./build/bin/cna-street --no-audio --benchmark baseline
CNA_GRAPHICS_RENDERER=WEBGPU $R --exec ./build/bin/cna-street --no-audio --no-overlay --capture build-probe/webgpu-perf/new
```

For the A/B of the pictures, capture once with the unmodified source and once with the change and
compare every file (`./build/bin/compare-images a.png b.png`). Views 01 and 11 (and at most ~0.01 %
at 18) differ between two runs of *any* renderer -- the street's shop dressing and traffic depend on
timing -- so judge those against a second run of the same build, not as regressions.

Profiling: there is no `perf` on this machine (`perf_event_paranoid` 3), but gdb can attach
(`ptrace_scope` 0). A poor man's sampler that produced every profile above:

```sh
# sample.sh RENDERER N OUT -- run the street at viewpoint 1 and take N main-thread stacks
r=$1; n=$2; out=$3
CNA_GRAPHICS_RENDERER=$r ../cna/tools/platform/run_gpu_tests_private.sh --exec \
    ./build/bin/cna-street --no-audio --viewpoint 1 --frames 4000 > $out.log 2>&1 &
until grep -q 'shadow cascades' $out.log 2>/dev/null; do sleep 1; done; sleep 8
pid=$(pgrep -x cna-street | head -1)
for i in $(seq 1 $n); do
  gdb -p $pid -batch -nx -ex 'set pagination off' -ex 'thread 1' -ex 'bt 30' 2>/dev/null \
      | grep -E '^#' >> $out
  echo '----' >> $out; sleep 0.3
done
kill $pid; wait
```

Count, per function name, in how many of the `----`-separated samples it appears (inclusive), and
which libcna frame is nearest the top (self). Build trees: `cmake-build-webgpu` (Debug,
`WEBGPU;VULKAN;OPENGLES3`, Wayland, compiled effects ON) for the suites; build only the targets
you run (`CLAUDE.md`, and the ~8-core cap in the owner's standing instructions).

---

## Appendix — the analysis as reported to the owner (2026-09-24)

Copied verbatim from the report that ended the `street-perf` work, at the owner's request:

> Vulkan and SDL_GPU are fixed and now run as fast as the GL renderers or faster. WebGPU improved
> about 3×, but it's still roughly 3× slower than the rest, and closing that gap needs a larger
> redesign I haven't started. Final `--benchmark baseline`, all five renderers back to back on the
> same build:
>
> | Renderer | Before | Now |
> |---|---|---|
> | OpenGL4 | 14.7 fps | **37.3 fps** |
> | EasyGL | 32 fps | 33.7 fps |
> | Vulkan | 8.3 fps | **34.0 fps** |
> | SDL_GPU | 4–11 fps | **42.4 fps** |
> | WebGPU | 3 fps | **9.6 fps** |
>
> The street renders the same as before. On each renderer I compared 18-viewpoint captures from
> the old and new code: pixel-identical except a few views that also differ between two runs of the
> same renderer (the street's shop interiors and traffic depend on timing). Vulkan ran 40 frames
> under the validation layer with no errors.
>
> **The main cause:** Vulkan, SDL_GPU and WebGPU record draws and replay them at `Present()`. To
> keep each draw's data they copied its entire vertex buffer and its indices every frame: about
> 430 MB per frame for a scene holding 71 MiB. Now a draw uses the buffer's own GPU copy directly.
> If the game rewrites a buffer that an already-issued draw still uses, the buffer moves to fresh
> storage, so that draw still sees its original data as XNA requires.
>
> **What else turned up along the way:**
> - **Vulkan:** freeing retired resources erased entries one at a time from a vector. That alone
>   was half the frame; it's now a single pass.
> - **SDL_GPU:**
>   - Every draw created and released its own GPU buffers.
>   - Every `SetData` submitted its own command buffer (~340 per frame from the instancing code).
>     They're now pooled and batched into the frame's upload pass.
> - **SDL_GPU, found by comparing images:** a small per-instance helper record was one entry long,
>   so every instance after the first read past its end. It only rendered correctly because this
>   driver happened to return the right value for out-of-range reads.
> - **Two bugs my first versions introduced, caught by tests and fixed:** Vulkan indirect draws and
>   WebGPU's wireframe route rewrite geometry after capture and were still pointing at the original
>   buffer.
>
> **Testing:** a new renderer-neutral test checks that a draw keeps the data it was issued with even
> if the buffer is rewritten before the frame is presented. It passes on all five renderers and
> fails on SDL_GPU and Vulkan when the copy-on-rewrite step is disabled. Each renderer also has a
> small test pinning that static geometry is no longer re-uploaded or copied. On all three renderers
> the regression suites show no new failures. Every failure that remains also fails on the
> unmodified code; I checked each one and listed them in the ledger. Several are stale tests that
> rewrite a buffer while it's still bound, which the device now refuses.
>
> **Why WebGPU is still slow:** GPU timers cost about 20 ms, because every timer start and end
> forces a GPU submission. The rest comes from creating bind groups and writing uniforms for every
> draw, plus one submission per render-target switch. The street's 924 shadow draws go through a
> path that builds four bind groups each. Fixing this means redesigning how WebGPU binds resources:
> one uniform buffer per submission with dynamic offsets, cached bind groups, and fewer
> submissions. That's bigger and riskier than what I described before you said yes, so I stopped
> for your decision.
>
> Everything is on branch `street-perf`, which sits on top of `street-opengl4`: eight commits,
> nothing merged or pushed. The ledger is `plans/plan_street_perf.md`. I deleted the temporary
> captures.
