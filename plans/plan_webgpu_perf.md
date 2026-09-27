# WebGPU per-draw CPU cost — binding-model redesign

**Status: 0001-0004 and 0007 done (2026-09-27); 0005, 0006 and 0008 open.** Recorded 2026-09-25 at the owner's request;
implementation started 2026-09-27. See *Results* for what changed and what it measured.

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

## Since this was written: the street lost the engine layer (2026-09-27)

`MOD-RETIRE-1` retired CNA's graphics engine layer the day this work began, and cna-street was
downgraded to build without it (cna-street branch `current-cna`): no cascaded shadows, no SSAO
prepass, no HDR target or post chain, no GPU timers. Every object is still drawn; the frame now goes
straight to the back buffer. So the 924 ShaderEffect shadow casters and the timer flushes described
below are gone from the street, and the remaining workload is 1 212 draws a frame, almost all
PbrEffect and SkinnedPbrEffect.

Two things about measuring it changed with that:

* **The street's own `cpuMeanMs` no longer measures a deferred renderer.** It is
  `SceneRenderer::render`'s clock, and Vulkan, SDL_GPU and WebGPU replay their recorded draws at
  `Present()`, after that clock stops. The shadow and post passes used to force the replay inside
  `render()` through their render-target switches; with them gone WebGPU read 12 ms for a frame that
  took 95. The street now also records the wall clock from one `Draw` to the next
  (`frameIntervalMeanMs`/`presentedFps` in the benchmark output, cna-street commit `400cdec`), and
  every number below is that interval.
* **The problem was still there.** After the downgrade, `--benchmark baseline`: OPENGL33 33 ms,
  OPENGL4 29, VULKAN 30, SDL_GPU 30, **WEBGPU 95 ms**. A gdb profile (80 stacks) put 62 of 80 in
  `Present` -> `EnsureFrameRendered`: 29 encoding the draws (`IssuePbrDraw` 22), 26 in
  `wgpuQueueSubmit` -- nearly all of it wgpu's `maintain`/`drop` releasing the previous frame's
  per-draw objects -- 19 in `wgpuQueueWriteBuffer` and 17 in `wgpuDeviceCreateBindGroup`.

## Measurements (2026-09-25, before the downgrade)

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
| WEBGPUPERF-0001 | Test-only counters per frame (bind groups created, queue writes and bytes, submits) and a baseline recorded here | ✅ `GetBindGroupCreateCountEXT`, `GetQueueWriteCountEXT`, `GetQueueWriteByteCountEXT`, `GetBindingCacheSizeEXT` (submits were already `GetQueueSubmitCountEXT`); every draw-path `wgpuDeviceCreateBindGroup`/`wgpuQueueWriteBuffer` goes through `CreateBindGroupEXT`/`QueueWriteBufferEXT`. Baseline in *Results* |
| WEBGPUPERF-0002 | Stock families: one uniform arena per flush, bound with dynamic offsets; one UBO bind group per (arena, layout) per flush; one `wgpuQueueWriteBuffer` of the arena before the submit. PBR and skinned PBR first -- they are the street's opaque pass | ✅ for PBR and skinned PBR, and every family's per-instance stream (a vertex arena). The classic families are WEBGPUPERF-0007 |
| WEBGPUPERF-0003 | Cache texture/sampler bind groups keyed by (layout, views, samplers); evict when a texture/view dies (hook into the texture renderers' release) and by frame age | ✅ PBR texture groups. No hook was needed: an entry holds each view's own `WebGPUSampledResourceEXT`, so a view's address cannot be reused while it is keyed, and the per-present sweep drops an entry once it holds a texture's last reference, or after 120 idle frames |
| WEBGPUPERF-0004 | Shadow and IBL groups: uniforms into the arena, texture parts cached | ✅ for every family that binds them (PBR, skinned PBR, LitTextured, Skinned) |
| WEBGPUPERF-0005 | ShaderEffect descriptor route: reflected uniform blocks into the arena with dynamic offsets (the reflected layout marks them dynamic); cache the non-uniform groups. This is the street's 924 shadow casters | ⬜ The street's casters went with the engine layer; it now draws one ShaderEffect a frame (the sky). Still worth doing for games that draw many. Measured in the code on 2026-09-27: `BuildDescriptorEffectBindGroupsEXT` builds every declared group per draw and writes one transient buffer per declared block -- the scalars, the engine matrices, and four array blocks padded to `kDescriptorArrayCapacity` elements each (std140), several KiB a draw, mostly zeros. The layouts come from `WebGPUProgramLayoutEXT` (WGSL reflection, `WebGPUModern.cpp`), which the compute route shares, and a group can mix arena-able blocks with application storage buffers -- so this is its own change, not a copy of 0002 |
| WEBGPUPERF-0006 | Fewer submits: resolve timer queries in the flush that is already happening instead of a separate encoder; evaluate one encoder per frame, submitted early only when a queue write, readback or present needs ordering | ⬜ The timer half no longer reaches a game: `CNA::Graphics::GpuTimer` was retired and `CreateGpuTimerEXT` is only reachable through the internal renderer interface. The encoder-per-frame half stands |
| WEBGPUPERF-0008 | The compiled-effect route (XNA `.fx` effects through MojoShader, `IssueCompiledEffectDraw`): per draw it builds four groups (group 0 empty), writes the VS and PS register files and the LOD-bias block, **and copies every vertex stream into a transient buffer of its own** -- the per-draw geometry copy `STREETPERF-0004` removed from the stock families is still live here. Give it resident geometry first, then the arena and the cache | ⬜ Found 2026-09-27 while scoping 0005; not measured on a workload yet |
| WEBGPUPERF-0007 | The classic stock families (Colored, Textured, LitTextured, AlphaTest, DualTexture, EnvMap, Instanced, Skinned, sprites): group 0 into the uniform arena, texture groups cached -- the same treatment 0002/0003 gave PBR | ✅ The shared `coloredBindGroupLayout_` (six families), the lit, skinned and environment-map group 0s and the stock sprite group's sampler block are dynamic-offset bindings over the uniform arena; their texture groups come from `AcquireSampledBindingEXT`, which keys a sampler-and-view group straight from its descriptor. A sprite used to write its own 16-byte block and build and release its own group; the block is now appended once per distinct LOD bias per flush, and a run of sprites sharing a group binds it once. The custom-effect sprite and draw routes are unchanged (0005) |

Each task: the street's 18 captures identical before/after; `-L WebGPU` 148/0 in
`cmake-build-webgpu`; `CnaRendererTests` and `CnaGraphicsTests` keep exactly the failures that
exist without the change (2 and 20 on `street-perf`, listed in `plan_street_perf.md`);
`CnaGraphicsExtTests`; the numbers recorded here.

**Target:** WebGPU's CPU frame on `--benchmark baseline` within about 1.5× of Vulkan's (≈45 ms or
better) with identical captures.

## Results (2026-09-27)

**What changed** (`modules/renderers/webgpu`):

* A **uniform arena** and a **vertex arena** per flush (`StreamArenaEXT`): persistent 1 MiB chunk
  buffers with CPU staging. A draw appends its blocks (aligned to `minUniformBufferOffsetAlignment`)
  and `ReplayOrderedSegments` writes every used chunk with one `wgpuQueueWriteBuffer`, after its last
  pass and before its caller submits. Chunks are never replaced, so a group over one stays valid.
* PBR and skinned-PBR group 0, and the uniform entry of the shared shadow and IBL groups, are
  **dynamic-offset** bindings over the chunk. Five dynamic uniform buffers for PBR, six for skinned
  PBR -- under core WebGPU's floor of eight.
* A **binding cache** (`AcquireCachedBindGroupEXT`) for the arena groups, the PBR texture groups and
  the shadow and IBL groups, keyed by layout, arena chunk, samplers and views. The neutral fallback
  textures now hand out one reference object each (a fresh one per call cost two allocations per
  draw and read to the sweep as a dead texture).
* Per-instance streams go into the vertex arena for every family.

**Counters** (`WebGPU_BindingCost`, PbrEffect draws with 16 distinct uniform sets, per steady frame):

| | bind groups | queue writes |
|---|---|---|
| before, 16 draws | 64 | 80 |
| before, 64 draws | 256 | 320 |
| after, 16 draws | **0** | **1** |
| after, 64 draws | **0** | **1** |

**The street** (`--benchmark baseline`, frame interval, back to back and interleaved because the
machine is shared -- the load average moved between 7 and 13 during these runs):

| | before | after |
|---|---|---|
| WEBGPU | 90.7 / 94.6 ms | **40.3 / 28.1 ms** |
| VULKAN (reference) | 35.9 ms | 38.8 ms |

WebGPU is now on a par with Vulkan on the same GPU, inside the 1.5x target. All 18 viewpoints
captured on WEBGPU before and after are pixel-identical (worst difference 0); two runs of the
unchanged build differ in views 01, 07, 11, 13 and 18 by up to 0.08 % of pixels, as they always did.

**The classic families (WEBGPUPERF-0007)**, same test, 16 lit textured BasicEffect draws and 64
SpriteBatch sprites per steady frame: 0 bind groups and 2 queue writes (the arena and SpriteBatch's
own vertex upload), and the same 0 and 2 at four times as many of each. Before, every one of those
draws and sprites created at least one group and wrote at least one block.

**Regressions:** `ctest -L WebGPU` in `cmake-build-webgpu` (RelWithDebInfo, `WEBGPU;VULKAN`) 147/147
with the new `WebGPU_BindingCost`, after both steps. 0007 changed one test's premise:
`WebGPU_BufferPoolStress` asserted the transient pool's reuse count kept climbing, and the pool had
served that scene's per-draw uniform blocks and nothing else -- its geometry is resident since
`STREETPERF-0004` -- so once the blocks moved to the arena the scene stopped touching the pool at all.
Its Check B now asserts what keeps that scene churn-free instead: no bind group and a fixed few queue
writes per steady frame (18 over 10 frames, against 320+ before). The street after 0007: 18 captures
identical to the unmodified renderer's, 25.8 / 28.7 ms a frame. `CnaRendererTests` 269 pass, 2 fail -- the two
`SharedBackendConformanceContract` compiled-effect tests, failing before this work. `CnaGraphicsTests`
20 fail; the same 20 were run on the unmodified source and fail there too. `WebGPU_BindingCost`'s
checks were proven live by mutation: zeroing the dynamic offsets fails A and E, and keying the
texture group without its views fails E.

## How to measure

Build (cna-street carries all five renderers; see `plan_street_perf.md`):

```sh
export CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv
cd ../cna-street
cmake --build build -j8 --target cna-street compare-images

R=../cna/tools/platform/run_gpu_tests_private.sh          # never the live desktop
CNA_GRAPHICS_RENDERER=WEBGPU $R --exec ./build/bin/cna-street --no-audio --benchmark baseline
# read frameIntervalMeanMs, not cpuMeanMs -- see "Since this was written"
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
