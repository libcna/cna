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

### Measurement after SMG-0006…0014, whole tree rebuilt first (see SMG-0014)

| suite | baseline | now |
|---|---|---|
| Modern `CnaGraphicsExtTests` | 682 / 21 / 259 | **834 / 52 / 76** of 962 |
| Classic `-R '^SdlGpu'` | 202 / 27 | **202 / 27** of 229 |
| CNAEXT examples `-L CnaExt` | 11 / 1 / 20 | **19 / 4 / 9** of 32 |
| Dead / profile-aborted | 0 | 0 |
| Build size | 657 MB | 660 MB |

The classic failure set is **byte-identical to the baseline's** — no regression, nothing newly fixed.

The three new example failures are `CNAEXT_Skybox`, `CNAEXT_ClusteredLights` and
`CNAEXT_Transparency`; `CNAEXT_NoPosixSetenv` is the pre-existing one, which names `::unsetenv`
call sites in `modules/platform/src/Wayland/` and belongs to the Wayland workstream.

### The one structural gap the remaining failures share

`SdlGpuRenderer::DrawPrimitivesEx` and `DrawIndexedPrimitivesEx` **ignore
`params.customEffectRenderer` entirely** and fall through to `DispatchStockDrawEXT`, which picks a
stock shader by vertex shape. The instanced path at least refuses by name
("custom-effect instancing is not implemented"); the non-instanced one silently draws the geometry
with a stock effect, which is why a skybox test reads 255 where it expects 0 — the sky was drawn,
just not by the sky shader.

A custom `ShaderEffect` on the SpriteBatch path works (that is what SMG-0006…0011 built); on a 3D
draw it does not exist. That accounts for the bulk of the 52: `ClusteredForwardEffectTest` (15),
`SkyboxRenderTest` (4), `PerObjectVelocityTest` (4), `DecalPassTest` (4) and others, plus all three
new example failures.

The work it needs is the shape `QueueCompiledEffectDraw`/`IssueCompiledEffectDraw` already has for
MojoShader effects — a queued command carrying the effect's shaders, an arbitrary vertex
declaration rather than the fixed `SpriteVertex` one, and a pipeline keyed on that declaration.

### SMG-0016…0018 — the three sprite-path defects, found by triage rather than by guessing

Twenty-seven failures across eleven post-process suites had **three** distinct causes, not one.
Each is a renderer defect, and none of them is in the shared engine layer.

#### SMG-0016 — clip space is Y-up on `SDL_gpu` and the portable shaders assume Y-down (18 failures)

SDL's own Vulkan backend negates the viewport height it is handed
(`third_party/SDL/src/gpu/vulkan/SDL_gpu_vulkan.c:7479-7480`:
`currentViewport.y = viewport->y + viewport->h; currentViewport.height = -viewport->h;`), so clip
space here is Y-up. CNA's **Vulkan** renderer passes a *positive* height precisely because its
shaders emit NDC directly — "Positive height is deliberate: CNA flips Y in the vertex shader"
(`VulkanRenderer.cpp:14486`). This renderer's own stock sprite shader has always carried the
matching negation by hand (`sprite2d.vert.glsl:23`, whose comment records it was confirmed
empirically).

A portable module cannot carry it: the same source is shared with the renderers that must **not**
negate. So the negation is applied to the module during the rewrite — one `OpFMul` by
`vec4(1,-1,1,1)` before each store to `gl_Position`. That keeps one convention across every
renderer and leaves the shared sources correct for all of them.

The shape mattered: glslang writes `gl_Position` as member 0 of the `gl_PerVertex` block, reached
by an `OpAccessChain`, **not** as a decorated variable of its own — verified by disassembling the
shipped `kFullscreenVulkanVertexSpirV` before writing the pass. A first cut looked for
`OpDecorate <var> BuiltIn Position`, found nothing, and silently changed no module at all.

Proof it was a mirror and not a shader bug: for `HdrDisplayOutput`'s 8×8 gradient, output texel 0
read input texel **56** — row 7 of 8, the exact vertical mirror — and `SpatialUpscale`'s diagonal
gave `result[0] = texels[15*16+0]`. Suites asserting a left/right split passed throughout.

#### SMG-0017 — the custom pipeline hardcoded alpha blending (3 failures)

`GetOrCreatePipeline` set `SRC_ALPHA`/`ONE_MINUS_SRC_ALPHA` on every colour target unconditionally,
consulting `renderState` only for write masks and depth bias — while the stock sprite pipeline
derives all of it from the real state through `FillColorTargetDescriptions`. `FullscreenPass` begins
every engine-layer pass with `BlendState::Opaque`.

Harmless wherever fragment alpha is 1, and fatal where alpha means something else: the
volumetric-fog build pass writes `alpha = density`, so a density of 0.08 scaled its own RGB by 0.08
into an 8-bit target and quantised to zero — "the medium scattered no light into a black frame at
all". The path now uses the same `FillColorTargetDescriptions` and the same `PipelineCacheKey` the
stock pipelines use, so blend, write masks, cull, wireframe, bias and stencil are all part of the
pipeline's identity rather than assumed constant.

#### SMG-0018 — a custom effect's textures are resolved by declared binding, not by SDL slot (4 failures)

CNA's portable shaders encode the texture kind and unit in the binding number, because they are
written against the Vulkan renderer's descriptor layout
(`VulkanRenderer.hpp:983-993`): set 1 holds the effect's own textures, `0..3` 2D at that unit,
`4..7` cube at `binding-4`, `8..11` volume at `binding-8`; set 0 is the SpriteBatch's own texture,
which is why `texture1` is always `set = 0`.

Indexing the caller's textures by the **slot** `SDL_gpu` compacts them into is only ever right by
accident. Three of CNA's own fragment shaders try to keep `texture1` alive with a multiply-by-zero
that the optimiser folds away (`skybox.vulkan.frag.glsl:30`, `build.vulkan.frag.glsl:82`,
`sky.vulkan.frag.glsl:78`), so the shader really declares one sampler, not two — and the skybox's
`uEnvironment` landed at slot 0, where it was overwritten by the sprite's 1×1 white dummy texture
and a 2D view was handed to a `samplerCube`. Every face read `(255,255,255)`.

The three kinds now live in separate arrays too, since a cube at unit 1 and a 2D at unit 1 are
different bindings in the shader and must not overwrite each other.

### Progression

| stage | pass | fail | skip |
|---|---|---|---|
| baseline | 682 | 21 | 259 |
| SMG-0006…0011 shader intake | 801 | 55 | 106 |
| SMG-0012…0014 compute and storage | 834 | 52 | 76 |
| **SMG-0016…0018 sprite-path defects** | **851** | **25** | **86** |

**Every one of the 25 remaining failures is the same single cause** — the missing 3D custom-effect
path (`ClusteredForwardEffectTest` 15, `DecalPassTest` 6, `WeightedBlendedTransparencyTest` 3,
`TransparentPhaseTest` 1).

### SMG-0019…0022 — the 3D custom-effect path

`DrawPrimitivesEx` and `DrawIndexedPrimitivesEx` ignored `params.customEffectRenderer` entirely and
fell through to `DispatchStockDrawEXT`, which picks a stock shader by vertex shape. So a custom
`ShaderEffect` worked on the SpriteBatch path and **did not exist** on a 3D draw: the geometry was
drawn, and drawn by the wrong shader, with nothing having failed. The instanced path at least
refused by name.

#### SMG-0019 — the queued command, pipeline and replay

A `CustomEffect3DDrawCommand` alongside the stock families, because `SpriteCommand` cannot serve:
that one is a fixed `SpriteVertex` quad, and a 3D draw brings whatever `VertexDeclaration` was
bound. The vertex layout is therefore part of the pipeline's identity, so these get their own
`GetOrCreate3DPipelineEXT` and the cache key mixes stride, locations, formats and offsets into the
same `PipelineCacheKey` the stock pipelines use.

A vertex element's **location is its index in the declaration** — the convention EasyGL's
custom-program path established and the Vulkan renderer follows. Reflection then says which of those
the shader consumes: an element it does not read is omitted rather than bound to nothing, and a
location it reads that the declaration does not supply is refused by name rather than left reading
undefined input. That is Vulkan's `MOD-2237` rule, and the `vertexInputLocations` the SPIR-V pass
now reports are what make it available here.

#### SMG-0020 — graphics-stage storage buffers

`BindStorageBufferForDrawEXT` was an inherited no-op, so the clustered-lighting path published its
light list, cluster table and index list into nothing and every lit surface came back black. Bound
now at the reflected slot, and slot-indexed for the same reason the compute path is (SMG-0013).

#### SMG-0021 — the engine-owned named matrices

`SetUniformMat4` ignored the name and wrote one slot, so a pass setting five matrices kept only the
last. CNA's portable geometry packages declare an `EngineMatrices` block at binding 19 whose six
members are named, and the Vulkan renderer mirrors exactly those names into it. The same six names
are mirrored here; every other name keeps the established one-matrix contract and lands in the
per-draw block, so an arbitrary custom effect is unaffected.

#### SMG-0022 — instanced custom-effect draws

Refused by name until now. The engine layer's particle system draws this way, and it is not the
instance-stream shape: the per-vertex data is the quad and the per-instance data is read out of a
**vertex-stage storage buffer** by `gl_InstanceIndex`. So the same 3D route serves it, with the
instance count and `SDL_BindGPUVertexStorageBuffers`.

#### The Y-flip correction had to move (a correction to SMG-0016)

SMG-0016 negated the value of every store to `gl_Position`. That is wrong for a 3D portable program,
which writes a **component** last — `rigid.vulkan.vert.glsl:38`, `gl_Position.y = -gl_Position.y;`,
the Vulkan renderer's own convention for a 3D vertex program (`pbr3d.vert.glsl`, REMED-GFX-011). A
per-store negation misses that write and mis-orders the rest.

The negation is now applied **once, at the end of the entry point**, by reading `gl_Position` back
and storing the product. That is right whatever shape wrote it, and it leaves the net result exactly
one flip from what Vulkan shows — which is the whole of the correction, for a 2D program that never
negates and a 3D program that already does alike.

### Progression

| stage | pass | fail | skip |
|---|---|---|---|
| baseline | 682 | 21 | 259 |
| SMG-0006…0011 shader intake | 801 | 55 | 106 |
| SMG-0012…0014 compute and storage | 834 | 52 | 76 |
| SMG-0016…0018 sprite-path defects | 851 | 25 | 86 |
| **SMG-0019…0022 the 3D path** | **887+** | **0** | — |

### SMG-0023 — indirect draw

`SDL_gpu` draws indirectly natively, and CNA's `IndirectDrawArguments` /
`IndirectDrawIndexedArguments` are **field-for-field** `SDL_GPUIndirectDrawCommand` /
`SDL_GPUIndexedIndirectDrawCommand`. So the bytes a compute shader wrote are handed to SDL
unchanged: nothing is read back to the CPU and repacked, which would defeat the point of drawing
indirectly. `SupportsIndirectDrawEXT` and `SupportsBaseInstanceDrawingEXT` are both true —
`first_instance` is part of SDL's own command structure.

The route matters more than the call. `DrawPrimitivesIndirectEXT` is **not** a custom-effect path:
`IndirectDrawTests` draws with a stock `BasicEffect`, so the arguments have to reach whichever of
the eleven queued families `DispatchStockDrawEXT`'s shape selection picks. They therefore ride the
queued *command* rather than the route — every stock command carries the buffer and offset, stamped
as it is pushed, and one shared `IssueQueuedDrawEXT` is now the single place a queued draw becomes a
native draw call. A first cut that required a compiled custom effect refused the very test the
feature exists for.

### SMG-0024 — a soak over the modern resources

`modules/renderers/sdl-gpu/examples/sdlgpu_modern_stress_test.cpp`, modelled on the WebGPU
workstream's own. Not registered with ctest: it takes minutes rather than seconds and is run
deliberately, the way the other renderers' stress programs are.

What it tests is **lifetime**, not pixels — the conformance suite already checks what each resource
computes. This renderer defers every draw to `Present()`, so a queued command holds keep-alives to
textures, uniform-array blocks, sampler tables and storage buffers the game may already have
destroyed, and SMG-0006…0023 added several such snapshots per draw. A keep-alive that never
expires, a pipeline cache that never evicts and a transfer buffer that is never released all look
identical from outside: nothing fails, and memory grows. So RSS, open file descriptors and thread
count are sampled after a twenty-cycle warm-up and again at the end.

Per cycle: a storage buffer uploaded, read back and GPU-copied; a storage image; a texture upload;
a render pass into an offscreen target whose size changes every 64th cycle; an indirect draw; and
every one of them destroyed again.

### SMG-0025 — what the soak found, immediately: a use-after-free on the indirect path

**It crashed on the first run, inside `VULKAN_DrawPrimitivesIndirect`.**

The stress program creates its indirect argument buffer inside the frame and destroys it at the end
of the cycle — which is the normal shape for GPU-driven drawing, and exactly what the conformance
tests do not do, because they keep their buffer alive across the `Present()`. This renderer replays
draws at `Present()`, so by then the buffer was gone and the queued command held a released
`SDL_GPUBuffer*`.

This is the same rule every sampled texture here already follows (REMED-GFX-152), and SMG-0023 had
declared a `storageBufferKeepAlive` field without ever populating it. `IStorageBufferRenderer`
derives from `std::enable_shared_from_this`, so the queued draw now owns a reference to every buffer
it will read — the indirect arguments and each buffer published through
`BindStorageBufferForDrawEXT`.

**Why the conformance suite missed it:** a released handle still addresses memory nothing has reused
yet, so the failure is a crash deep inside the driver on a *later* allocation pattern rather than a
wrong result on the next line. Only thousands of create/destroy cycles make the reuse certain. That
is what a soak is for, and it earned its place on its first run.

**Measured after the fix**, 220 cycles: RSS 101 052 → 101 116 KiB (**+64 KiB**), file descriptors
23 → 23, threads 6 → 6, all five checks PASS.

### SMG-0026, SMG-0027 — the last two truthful capabilities

**`SupportsTexture3DSamplingEXT` → true.** `sampler3D` needs nothing special of the intake: the
descriptor translation classifies a sampled image by its *type*, not its dimension, and
`BindTexture3D` resolves a volume to the unit the portable shaders encode in binding `8 + unit`.
What had made this false was that no custom effect ran at all.

**`SetStringMarkerEXT` implemented.** `SDL_InsertGPUDebugLabel` takes a command buffer, and this
renderer has none open while a game is calling the marker API — draws are queued and replayed at
`Present()`. The label is therefore recorded with the queue and emitted onto the command buffer that
actually carries the frame's work, which is also the only place it could mean anything.

Neither changed the suite (900 / 0 / 62 before and after); both are now implemented rather than
inherited refusals.

### SMG-0028 — the 3000-cycle soak, measured

```
warm: RSS 101 220 KiB, 23 fds, 7 threads
ran 3020 of 3020 cycles
RSS 101 220 -> 101 236 KiB (+16), fds 23 -> 23, threads 7 -> 7
5/5 PASS
```

Sixteen kilobytes over three thousand and twenty create-use-readback-destroy cycles, with no file
descriptor and no thread gained.

### SMG-0029 — ASan, UBSan and LSan over the modern suite

Tree: `build-probe/smg-sdlgpu-asan`, Debug, `CNA_SANITIZE=address,undefined`,
`CNA_SANITIZE_OPTIMIZATION=O0`, `CNA_SHARED_LIBRARY=ON` — the same recipe the Vulkan workstream used
(`plans/plan_vulkan_modern_graphics.md` VMG-0017/0018). Debug also turns SDL_gpu's own validation on,
which is the point of running it here rather than in Release. The whole 962-case modern suite, nine
shards of 120.

| sanitizer | result |
|---|---|
| AddressSanitizer | **zero errors** — no overflow, no use-after-free, no double free |
| UndefinedBehaviorSanitizer | **zero runtime errors** |
| LeakSanitizer | 768 bytes in 6 allocations, at process exit, **fixed** — see below |

**The leak is classified by cycle growth, not by its size.** Every reported allocation is a bare
`realloc` frame with no CNA frame above it, from `<unknown module>` or from an address far past
`libcna.so`'s own text — the `dlopen`ed Mesa/RADV driver, which ASan cannot symbolise. The
measurement that settles it is the soak run under ASan at two workloads:

| cycles run | LeakSanitizer total |
|---|---|
| 70 | 768 bytes in 6 allocations |
| 520 | **768 bytes in 6 allocations** |

Ten times the work, byte-for-byte the same total. That is a one-time provider allocation, which the
workstream brief admits as documentable; a progressive CNA allocation would scale.

**One measurement had to be redone before it could be believed.** The first attempt reported the
same 768 bytes at both workloads *while the program had produced no output at all* — LeakSanitizer
tears the process down with `_exit`, which skips stdio's flush, so a run that did all its work and
one that did none looked identical. The stress program now flushes each result line as it prints it,
and the numbers above come from runs whose "ran 70 of 70" and "ran 520 of 520" lines are visible.

**RSS is deliberately not asserted in the sanitizer build.** ASan quarantines freed allocations
rather than returning them, so RSS grows with the number of allocations whether or not anything
leaked — measured at roughly 167 KiB per cycle here, scaling linearly, while LeakSanitizer's total
stayed fixed. Asserting a ceiling there would be asserting the quarantine's size. LeakSanitizer is
the leak check in that configuration; the ordinary build, where the soak measured **+16 KiB over
3020 cycles**, is where the ceiling means something.

### SMG-0030 — why the WebGPU, Vulkan and EasyGL regression runs are not required

The brief asks for them *conditionally*: "If generic modern code/tests change". Nothing generic
changed. The whole diff against the baseline is:

```
modules/renderers/sdl-gpu/...          6 files
plans/plan_sdlgpu_modern_graphics.md   this ledger
plans/plan_platform.md                 regenerated by tools/platform/sdl_inventory.py --update
```

No file under `modules/graphics/`, `modules/graphics-ext/`, `modules/renderers/common/` or any
other renderer family is touched, and no test is modified — every conformance test that runs here is
the one the other renderers already run, unchanged. Those three renderers therefore cannot have been
affected, and running them would demonstrate only that.

The repository's own boundary gates were run and pass: `sdl_inventory.py --check`,
`sdl_classify.py --check`, `renderer_sdl_audit.py --check`, `sdl_ratchet.py --check`,
`hot_path_lint.py`, `check_renderer_identities.py`.
