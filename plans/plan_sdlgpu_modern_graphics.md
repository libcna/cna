# SDL_GPU modern Graphics / CNAEXT workstream

Task IDs `SMG-0001`…. One row per task, each carrying its own evidence. This file is the
evidence ledger for the branch `sdlgpu-modern-graphics`; it is not a design document.

The companion documents that this workstream does **not** duplicate:

- `plans/plan_modern.md` — the CNAEXT engine layer's own backlog (`MOD-*`).
- `plans/plan_vulkan_modern_graphics.md`, `plans/plan_webgpu_modern_graphics.md` — the two
  validated reference implementations.
- `plans/plan_pre_sdlgpu_closeout.md` — `PSG-*`, which measured the baseline this branch starts
  from and built the bounded runner and the dead-test classifier.

---

## Status summary

| | |
|---|---|
| Branch | `sdlgpu-modern-graphics` |
| Baseline | `d6e9ff050` (`origin/next` at 2026-09-23) |
| Modern `CnaGraphicsExtTests` | baseline **682 / 21 / 259** of 962 |
| Classic `-R '^SdlGpu'` | baseline **202 / 27** of 229 |
| CNAEXT examples `-L CnaExt` | baseline **11 / 1 / 20** of 32 |

---

## SMG-0001 — Baseline commit, and why it is not the one the task named

The task named `c3fae8755` as the validated baseline. It is not the head of `origin/next`; it is
an **ancestor of it by eight commits**, all of which landed after that number was written down:

```
git merge-base --is-ancestor c3fae8755 origin/next   -> yes
git rev-list --count c3fae8755..origin/next          -> 8
```

The eight are the WebGPU failing-eight closeout (`WGF-0001`…`WGF-0007`) and the cna-street WebGPU
bring-up (`STREETW-0001`…`STREETW-0005`) — both of which touch generic modern code and the modern
test suite, so building on `c3fae8755` would have meant re-deriving defects that are already fixed.

**Baseline used: `d6e9ff050e32bcfe5e7fd640006619a90b92298c`**, `origin/next` at branch time.
`origin/next` and `next` are identical and neither is modified by this branch.

One piece of pre-existing working-tree dirt was cleaned before branching: the committed test asset
`tests/assets/media/video/video_xnb_object_fixture.xnb` was deleted in the working tree (a test
reads it by name — `ContentManagerVideoXnbTests.cpp:195`), so it was restored with `git restore`.
The untracked `startup-metrics.log` is an unrelated Electron log and is left alone, never staged.

---

## SMG-0002 — Environment, measured rather than assumed

Recorded because "SDL_GPU validated" means nothing without naming what actually ran (Phase 44).

| | |
|---|---|
| OS | Linux 6.12.107+deb13-amd64 (Debian 13) |
| SDL | **3.5.0**, vendored at `third_party/SDL` (not the system SDL) |
| SDL_GPU API | the SDL 3.5 `SDL_gpu.h` surface |
| Physical GPU | **AMD Radeon 780M** (Phoenix1, `c3:00.0`) |
| Driver | Mesa 25.0.7-2+deb13u1, RADV `PHOENIX`, Vulkan 1.4.305 |
| SDL_GPU backend | logged per authoritative run via `SDL_GetGPUDeviceDriver` (see SMG-0003) |
| Shader intake | SPIR-V. `SdlGpuRenderer` requests `SDL_GPU_SHADERFORMAT_SPIRV` and refuses a device that cannot take it |
| Build dir | `cmake-build-sdlgpu` — Release, Ninja, ccache, `CNA_CNAEXT=ON`, `CNA_SHARED_LIBRARY=ON`, `CNA_SDL_GPU_SHADERCROSS=OFF`, `CNA_SDL_GPU_COMPILED_EFFECTS=OFF`, `CNA_TEST_DISPLAY=` (empty) |
| Build size at baseline | 657 MB |

`llvmpipe` is also present as a second Vulkan device; every authoritative run must record which of
the two SDL_GPU actually selected, because a silent llvmpipe run would make "hardware validated"
false.

**No display on `:0` / `wayland-0`.** Every run in this ledger goes through
`tools/platform/run_gpu_tests_private.sh`, which starts a private headless Weston plus a rootful
Xwayland with DRI3; the display numbers are recorded per run.

---

## SMG-0003 — Baseline reproduced before any renderer change (Phase 1)

Reproduced on the baseline commit, with no source modified.

### Modern — `CnaGraphicsExtTests`, 962 cases

```bash
tools/platform/run_gpu_tests_private.sh --exec \
    tools/tests/run_gtest_bounded.sh --out <dir> ./cmake-build-sdlgpu/CnaGraphicsExtTests
```

**682 pass / 21 fail / 259 skip / 962 total — exactly the stated baseline.**

Counted per `<testcase>` from the five shard XMLs, not from the `<testsuites>` root, which is the
distinction `PSG-0002` records: GoogleTest writes `GTEST_SKIP` as `result="skipped"` on the case
and puts no skip attribute on the root, so a root-attribute reading turns this into a confident and
false `962 / 0 / 0`.

`CnaGraphicsExtTests` is **not registered with ctest at all** (`cmake/UnitTests.cmake:549` — focused
executables are developer iteration targets, deliberately not a second CI registration). There is
therefore no `ctest -L` that selects the modern suite; the binary is run directly, and the dead-test
classifier has to be run by hand for it (`run_gpu_tests_private.sh` only auto-runs it in ctest mode).

### The 21 failures have ONE root cause

Every one of them fails the same way — the effect its test depends on does not become valid:

```
[ShaderEffect] Compile error: ShaderEffect_vs: error: #version: ES shaders for SPIR-V require version 310 or higher
ShaderEffect_vs:6: error: 'location' : SPIR-V requires location for user input/output
ShaderEffect_vs:8: error: 'non-opaque uniforms outside a block' : not allowed when using GLSL for Vulkan
```

This is `libshaderc` refusing the CNAEXT engine layer's **GLSL ES 3.00** profile. The layer writes
that profile deliberately (`CLAUDE.md`, `MOD-15`), and `ShaderEffect` owns the `#version` line and
the down-level transformations. SDL_GPU never declared a shader language, so the shader package
selected its GLSL ES variant and handed it to a path that compiles for Vulkan SPIR-V, where ES 3.00
without explicit locations and without a uniform block is not a legal input.

The failing suites are exactly the ones whose effects go down that path:
`CRTEffectTest` (7), `DepthEffectTest` (5), `TransparentPhaseTest` (3), `ContactShadowPassTest` (3),
`AerialPerspectiveTest` (2), `SsaoFromRealPrepassTest` (1), `ClusteredForwardEffectTest` (1).

### The 259 skips, by what they are gated on

Tallied from the shard XML skip messages. The count is what makes the size of the opportunity
concrete:

| gate | skips |
|---|---|
| shader-package selection found no usable variant (SSR, clustered, tonemap, DoF, motion blur, sky, resolve, colour grade, volumetric fog, bloom, prepass, light shafts, height fog, film grain, lens flare, decal, CRT, aerial perspective, …) | ~110 |
| `SupportsShadowSamplingEXT()` false — "lit shaders do not sample shadow maps" | 31 |
| compute shaders absent (`SupportsComputeShadersEXT()` false) | 47 |
| `CustomEffects` / `ExecutesShaderEffectSourceEXT()` false | 31 |
| prepass unavailable | 7 |
| `SupportsIndirectDrawEXT()` false | 6 |
| `SupportsGpuTimerEXT()` false | 6 |
| remainder (readback shapes, half-float targets, single-case gates) | ~21 |

The single largest group is not a missing GPU feature — it is the renderer never having declared
which shader language it takes, so every multi-language shader package in the engine layer refuses
to select. That sets the order of work below.

### Classic and examples

Recorded under SMG-0004 once the runs complete; commands are

```bash
tools/platform/run_gpu_tests_private.sh cmake-build-sdlgpu -L CnaExt
tools/platform/run_gpu_tests_private.sh cmake-build-sdlgpu -R '^SdlGpu' -j4
```

Note `-R '^SdlGpu'` (229) and not `-R SdlGpu` (232): the anchored form is what `PSG-0009` recorded,
and the three extra tests the unanchored form picks up are not SDL_GPU renderer tests.

---

## SMG-0004 — What SDL_GPU can and cannot be asked to do

Read from the vendored `third_party/SDL/include/SDL3/SDL_gpu.h`, so it describes the SDL this
build actually links, not SDL_GPU in general.

**Present, and therefore in scope:**

| feature | SDL entry points |
|---|---|
| compute pipelines | `SDL_CreateGPUComputePipeline`, `SDL_BeginGPUComputePass`, `SDL_BindGPUComputePipeline`, `SDL_DispatchGPUCompute`, `SDL_EndGPUComputePass` |
| compute uniforms | `SDL_PushGPUComputeUniformData` |
| storage buffers | `SDL_BindGPUComputeStorageBuffers`, `SDL_BindGPUVertexStorageBuffers`, `SDL_BindGPUFragmentStorageBuffers` |
| storage textures | `SDL_BindGPUComputeStorageTextures`, `SDL_BindGPUVertexStorageTextures`, `SDL_BindGPUFragmentStorageTextures` |
| indirect draw | `SDL_DrawGPUPrimitivesIndirect`, `SDL_DrawGPUIndexedPrimitivesIndirect` |
| indirect dispatch | `SDL_DispatchGPUComputeIndirect` |
| readback | `SDL_DownloadFromGPUBuffer`, `SDL_DownloadFromGPUTexture` |
| debug markers | `SDL_InsertGPUDebugLabel`, `SDL_PushGPUDebugGroup`, `SDL_PopGPUDebugGroup` |

**Absent, and therefore a truthful permanent `false` rather than work:**

- **GPU timers.** SDL_GPU 3.5 exposes no timestamp query, no query object and no occlusion query of
  any kind — `grep -niE 'timestamp|SDL_GPUQuery|occlusion' SDL_gpu.h` returns nothing.
  `SupportsGpuTimerEXT()` therefore stays false and `CreateGpuTimerEXT()` stays null. The
  `IGpuTimerRenderer` contract explicitly forbids substituting a CPU clock, so there is no
  honest partial implementation available here. The six GPU-timer skips are permanent and correct.
- **Occlusion queries** — same absence; the existing
  `sdlgpu_occlusionquery_limitation_test.cpp` already records this.

---

## SMG-0005 — Modern surface inventory and classification (Phase 4)

The modern surface is 93 `…EXT` virtuals in `IGraphicsRenderer.hpp` (65 of them on
`IGraphicsRenderer` itself) plus five wholly-modern interfaces — `IGpuTimerRenderer`,
`IStorageBufferRenderer`, `IComputeShaderRenderer`, `ITexture2DArrayRenderer`,
`IStorageTexture2DRenderer`. Every EXT default is a refusal (`false` / `nullptr` / `0` / `{}` /
`Defer`) or an identity pass-through, except the indirect-draw pair, which throws.

SDL_GPU overrides **none** of the modern capability surface at baseline. Verified by name:

```
SupportsComputeShadersEXT SupportsIndirectDrawEXT SupportsGpuTimerEXT
SupportsImageBasedLightingEXT SupportsShadowSamplingEXT ExecutesShaderEffectSourceEXT
SupportsComputeImageBindingEXT SupportsTexture3DSamplingEXT SupportsBaseInstanceDrawingEXT
SupportsShaderLanguageEXT GetShaderDialectEXT GetMaxVertexShaderStorageBlocksEXT
CreateStorageBufferEXT CreateStorageTexture2DEXT CreateTexture2DArrayEXT
CreateGpuTimerEXT MemoryBarrierEXT BindStorageBufferForDrawEXT
```

— all absent from `SdlGpuRenderer.hpp`. `CreateRenderTarget2DEXT` and `CreateRenderTargetCubeEXT`
are the only two present, and they are classic-creation extensions rather than engine-layer members.

For scale, the two reference renderers override 51 (Vulkan) and 47 (WebGPU) modern members;
WebGPU factors its modern work into four dedicated files totalling ~3 500 lines.

### Classification

| group | verdict | why |
|---|---|---|
| shader language / dialect declaration | **required, representable** | SDL_GPU takes SPIR-V; the engine layer's packages ship SPIR-V |
| compute pipelines, dispatch, compute uniforms | **required, representable** | full SDL 3.5 compute pass API |
| storage buffers (read, write, readback, ranges, copy) | **required, representable** | storage-buffer binds on all three stages + download |
| storage textures | **required, representable** | compute storage read/write usage flags |
| `Texture2DArray` | **required, representable** | layered `SDL_GPUTexture` |
| device limits | **required, partly representable** | SDL_GPU publishes few limits; the rest must be answered conservatively rather than invented |
| indirect draw / indirect dispatch | **required, representable** | `SDL_DrawGPUPrimitivesIndirect` and friends |
| debug markers | **required, representable** | `SDL_InsertGPUDebugLabel` / push / pop |
| IBL, shadow sampling | **required, representable** | needs the stock lit shaders to gain the samplers |
| GPU timers | **legitimately unsupported** | no query API of any kind in SDL_GPU 3.5 |
| occlusion queries | **legitimately unsupported** | same |

---

## Work log

Rows are appended as work lands; each carries its own measurement.

### SMG-0006 — the SPIR-V descriptor mapping layer

`modules/renderers/sdl-gpu/{include/…,src/}SdlGpuSpirvBindings.{hpp,cpp}`.

`SDL_gpu` does not let a shader choose its descriptor sets; `SDL_CreateGPUShader` documents a fixed
table and every module handed to it must already obey it. CNA's portable packages do not, and are
not wrong to: their SPIR-V is compiled from the `*.vulkan.*.glsl` sources, written against **CNA's
own Vulkan renderer's** convention (fragment samplers at `set = 1`, storage buffers at `set = 2`,
per-draw parameters in a `push_constant` block). Two SPIR-V renderers, two layouts.

The layer reflects a module and rewrites two things, and nothing else:

1. **Descriptor sets and bindings**, into `SDL_gpu`'s table — vertex `set 0`/`set 1`, fragment
   `set 2`/`set 3`, compute `set 0` read-only / `set 1` read-write / `set 2` uniforms — renumbering
   bindings contiguously within each set in the category order SDL requires, and reporting every
   resource's original `(set, binding)` back so the runtime binds what the shader asked for.
2. **`push_constant` → uniform buffer.** `SDL_gpu` has no push constants; its per-draw parameters
   arrive as uniform buffers. Measured before relying on it: CNA's Vulkan post-process block is
   `vec2 viewportSize; mat4 uMatrix; vec4 uVector; float uScalar`, whose compiler-assigned offsets
   read from the shipped SPIR-V are **0, 16, 80, 96** — byte for byte the 128-byte block
   `SdlGpuEffectRenderer` has always pushed, and each of those offsets is std140-legal on its own,
   so the block is a valid uniform buffer without repacking. A block carrying no `Block`
   decoration is refused by name rather than reinterpreted.

It needs no shader compiler, so the route works in builds that deliberately have no `libshaderc`
(Windows, Apple, Emscripten), where `ShaderEffect` was previously unavailable outright.

### SMG-0007 — truthful SPIR-V intake, declared

`GetShaderDialectEXT()` returns `SpirV`; `SupportsShaderLanguageEXT` returns true for SPIR-V at
vertex and fragment (compute defers to `SupportsComputeShadersEXT()`, which is still false);
`ExecutesShaderEffectSourceEXT()` returns true.

This is deliberately **not** the flag-flip the workstream brief warned against. The capability
became true only once `CompileSpirvProgramEXT` existed: a caller's SPIR-V is reflected, translated,
created as real `SDL_GPUShader`s with the resource counts the module actually declares, and a
payload that cannot be is refused by name. GLSL is still **not** claimed even where the optional
`libshaderc` route exists, because that route compiles for Vulkan's rules, in which the engine
layer's GLSL ES 3.00 profile is not a legal input — claiming it would advertise a path that fails
on the layer's own shaders.

### SMG-0008 — a custom effect may sample more than one texture

`IEffectRenderer::BindTexture`/`BindTextureCube`/`BindTexture3D` were inherited as no-ops, because
the GLSL route's effects sample exactly one texture: the sprite's. A portable package routinely
declares several — a post-process pass reads scene colour, depth, sometimes history.

This is not a missing feature but a crash, and it is how it was found. SDL writes a descriptor for
every sampler the shader declares, so a declared-but-unbound sampler reaches the driver as a null
image: the first test to exercise the new route died in `VULKAN_INTERNAL_BindGraphicsDescriptorSets`
inside `libvulkan_radeon`, with the reflection correctly reporting two samplers and the draw binding
one. Slot 0 stays the sprite's own texture (FNA's `SpriteBatch.DrawPrimitives` assigns
`Textures[0]` after the effect's pass), the rest come from what the effect bound, and a slot the
caller left empty gets the 1×1 opaque white texture rather than null.

### SMG-0010 — std140 uniform arrays

The engine layer hands a `ShaderEffect` whole arrays (`SetUniformFloatArray("uAerialScalars", …)`),
which do not fit the fixed 128-byte block and live in uniform blocks of their own. CNA's portable
shaders name those by a fixed binding — 12 `FloatArray`, 13 `Vec2Array`, 14 `Vec3Array`,
15 `Mat4Array` — so the reflected **original** binding is what says which array feeds which slot.
The four setters were inherited as no-ops, so every array-driven pass rendered its input unchanged.
Elements are padded to four floats (sixteen for `mat4`), which is what std140 does to an array of
`float`, `vec2` or `vec3` alike.

### SMG-0011 — the SpriteBatch path never applied the effect

**A genuine renderer defect, pre-existing, and not mine.** `SdlGpuSpriteBatchRenderer::Draw`
resolved the custom effect's renderer but never called `customEffect_->Apply()`. Seven other
renderers do (`VulkanRenderer.cpp:2399`, `EasyGLRenderer.cpp:5668`, `D3D11SpriteBatch.cpp:329`,
`D3D12SpriteBatch.cpp:304`, `D3D9SpriteBatch.cpp:288`, `MetalRenderer.mm:2429`,
`OpenGL4Renderer.cpp:2493`).

An `Effect` publishes its parameters from `OnApply()` — that is what the XNA lifecycle is for, and
where every CNAEXT stock effect writes them (`CRTEffect::OnApply` sets `uCrtParams`). Without the
call such an effect was queued with its uniforms as they stood before it was ever applied: all
zeros. Every CRT test therefore rendered its input unchanged, which is exactly what
`DisabledParametersCopyTheSourceExactly` asserts, so that one test passed *because* of the defect.

**Why previous coverage missed it:** invisible while the renderer could not run a custom effect's
source at all. The draw was skipped, so the uniforms it would have used never mattered.

**Measured:** with the fix, `CRTEffectTest` went 2/7 to 7/7 and `DepthEffectTest` 0/5 to 4/5.

### Progression after the shader-intake tranche

| stage | pass | fail | skip | of |
|---|---|---|---|---|
| baseline | 682 | 21 | 259 | 962 |
| **SMG-0006…0011** | **801** | **55** | **106** | 962 |

153 skips became real executions; 119 of them pass. The 34 new failures are not a regression in the
usual sense — they are passes-or-failures that did not previously exist as either, because the
effect they exercise could not run. They are the remaining work, and they cluster:
`ClusteredForwardEffectTest` (14, the lit path, which needs graphics-stage storage buffers),
`DepthOfFieldTest` (5), `SkyboxRenderTest` (4), `DecalPassTest` (4), `PerObjectVelocityTest` (4),
`VolumetricFogTest` (3), `WeightedBlendedTransparencyTest` (3), and singles elsewhere.

### SMG-0012 — storage buffers, storage images and compute

`modules/renderers/sdl-gpu/{include/…,src/}SdlGpuModern.{hpp,cpp}`, kept out of the
twelve-thousand-line `SdlGpuRenderer.cpp` the way WebGPU keeps `WebGPUModern.*` out of its own.

**The ordering rule this renderer needs and the reference renderers do not.** `SdlGpuRenderer`
defers every draw to `Present()`, so "issued" and "submitted" are different states here. A dispatch
that reads a render target, or a `GetData` on a buffer a dispatch wrote, must observe the state
*after* that work — so compute and readback both call `FlushPendingGpuWorkEXT()` first, which is
what the existing readback path already did before `GetData`. With that, SDL's own submission order
gives `render -> compute -> render` and `compute write -> graphics read`, and no barrier is left to
reason about. `MemoryBarrierEXT` is therefore a documented no-op rather than an unimplemented one:
SDL_gpu inserts the barriers its resource-state tracking implies within a command buffer, and
between command buffers submission order *is* execution order. There is no API to express one
through and nothing for one to fix.

**What `SDL_gpu` has no resource for.** There is no uniform *buffer* usage at all —
`SDL_GPU_BUFFERUSAGE_*` is vertex, index, indirect and the four storage combinations, and uniform
data reaches a shader only through `SDL_PushGPU*UniformData`. A `StorageBufferUsage::Constant`
buffer is therefore pushed from the CPU shadow copy every buffer already keeps. That is SDL's model
for a constant buffer, not an emulation of one.

Also landed here:

- **`GetSurfaceFormatUsageSupportEXT`** was never overridden, so the engine layer's
  `StorageTexture2D` descriptor check could not find a single known-and-supported usage and refused
  every storage image by name. It now answers from real `SDL_GPUTextureSupportsFormat` queries, per
  usage, so a driver that supports reading but not writing a format is reported as exactly that.
- **An integer sampler in a compute module is refused** before pipeline creation. The driver would
  happily create a pipeline whose descriptor aliases a float image to an integer one and deliver
  the wrong texels with nothing having failed. CNA's textures are float-sampled everywhere.
- **Every sampler a compute module declares is bound**, falling back to the 1×1 white texture — the
  same rule and the same reason as SMG-0008, and it was found the same way, as a segfault inside
  the Vulkan driver the first time a compute shader sampled a texture.

### Progression

| stage | pass | fail | skip | of |
|---|---|---|---|---|
| baseline | 682 | 21 | 259 | 962 |
| SMG-0006…0011 shader intake | 801 | 55 | 106 | 962 |
| **SMG-0012 compute and storage** | **829** | **57** | **76** | 962 |

Classic `-R '^SdlGpu'` stayed at 202 / 27 of 229 across the first tranche, with a byte-identical
failure set.

### SMG-0013 — compute bindings are indexed by slot, not appended

A defect in SMG-0012's own first cut, found by `ClusteredLightComputeTest` reporting
"cluster 1576: CPU has 1 lights, GPU has 0".

`SDL_BindGPUComputeStorageBuffers` takes a **contiguous array starting at a first-slot index**, so
the array's position *is* the binding. The first cut built that array by appending every resource
it found bound and skipping every one it did not — so a shader with an unbound resource, or one
whose declaration order differed from its binding order, had every later binding shifted down one.
Nothing reports an error when that happens: the shader reads a real buffer, just not the one it
asked for.

Now every binding array is sized from the reflected count and written at `resource.slot`, leaving a
hole where the caller bound nothing. Applied to all four kinds — read-only and read-write storage
buffers, storage images, and samplers.

**Measured:** `ClusteredLightComputeTest` 5 failures to **10/10 passing**.

### SMG-0014 — a methodology finding worth more than the bug it hid

Two classic tests, `SdlGpu_MinimizedRetry` and `SdlGpu_PresentationSurface`, appeared to regress
after SMG-0012 and reproduced on every rerun. They were not a regression.

This configuration is `CNA_SHARED_LIBRARY=ON`: the renderer lives in `libcna.so`, and the example
test binaries link against it. Adding virtual members to `SdlGpuRenderer` changes its vtable
layout, so an example binary built before that change calls through a slot that has since moved —
`GetSwapIntervalEXT()` returned something that was never a swap interval. Rebuilding only
`cna_renderer_sdl_gpu` and `CnaGraphicsExtTests`, as the iteration loop had been doing, leaves
every `cna_test_sdlgpu_*` executable stale.

**Rule for every measurement run in this ledger from here on: `cmake --build cmake-build-sdlgpu`
with no `--target`, then measure.** A targeted build is for compile-checking only. After the full
build both tests pass, and the classic failure set is the baseline's again.
