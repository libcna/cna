# WebGPU and the modern CNA Graphics API

The owner's brief of 2026-09-22: implement, validate and harden the **existing** modern CNA Graphics
API (the CNAEXT engine layer and its portable modern-GPU foundation) on the existing WebGPU renderer,
through the same renderer-neutral suite that EasyGL and Vulkan already pass, without regressing
classic WebGPU, EasyGL or Vulkan.

Task IDs: `WMG-0001`, `WMG-0002`, … . Branch `webgpu-modern-graphics` from `origin/next` `c576b5d25`
(which contains the modern Vulkan workstream `VMG-0001..0019`, the GPU-test isolation work
`GTI-0001..0010` and `STREET-0001..0008`). Not merged, not pushed. This file is the evidence ledger:
every number in it was measured, and says where.

**What "the modern API" is here** is the answer `plans/plan_vulkan_modern_graphics.md` already
established from the repository, not the brief's list: CNA has one modern graphics surface — the
CNAEXT engine layer (`modules/graphics-ext`) and its portable foundation (`plans/plan_modern.md`
Phases 0–22, `docs/adr/0001-modern-gpu-ordering-lifetime.md`). It exposes no command buffers,
barriers, descriptor sets, queues or native handles (ADR 0001, `MOD-2202`), so none are added here.

## Status

| ID | Task | Status |
|---|---|---|
| WMG-0001 | Baseline, WebGPU inventory, build tree; the WebGPU compiled-effect path no longer compiled on `next` | 🟩 |
| WMG-0002 | What the modern API asks of a renderer, and what WebGPU answered at base | 🟩 |
| WMG-0003 | Measured baselines on EasyGL, Vulkan and WebGPU, before any change | 🟩 |
| WMG-0004 | The adapter, named rather than assumed | 🟩 |
| WMG-0005 | Dead tests: six WebGPU rows that aborted on the profile before their contract | 🟩 |
| WMG-0006 | WGSL for every stock shader, generated from the one GLSL source of truth | 🟩 |
| WMG-0007 | A WGSL reflection pass, because automatic bind-group layouts cannot serve an effect | 🟩 |
| WMG-0008 | The modern renderer surface on WebGPU | 🟩 |
| WMG-0009 | The descriptor binding contract, mirrored from Vulkan | 🟩 |
| WMG-0010 | Engine defect: two sites asked the language when they meant the contract | 🟩 |
| WMG-0011 | WebGPU defect: a sprite custom effect was never applied | 🟩 |
| WMG-0012 | Test-side: a test that types GLSL asks whether this renderer runs GLSL | 🟩 |
| WMG-0013 | Indirect draws | 🟩 |
| WMG-0014 | Shadow reception in the stock lit families | 🟩 |
| WMG-0015 | Packages and test language lists that had not caught up with WGSL | 🟩 |
| WMG-0016 | `SupportsTexture3DSamplingEXT` answered false about something the renderer does | 🟩 |
| WMG-0017 | The GPU timer measured its own overhead instead of the work | 🟩 |
| WMG-0018 | The renderer's own 198 tests, and which failures are this workstream's | 🟩 |
| WMG-0019 | The GPU timer measured against a workload that scales | 🟩 |
| WMG-0020 | Debug markers: labels reach the command stream, groups are balanced | 🟩 |
| WMG-0021 | Descriptor-effect instance streams, and the silent wrong-shader draw | 🟩 |
| WMG-0022 | Image-based lighting on both PBR families | 🟩 |
| WMG-0023 | ASan, UBSan and LSan over the modern path | 🟩 |
| WMG-0024 | The SDL-free configuration, on Wayland and on X11 | 🟩 |
| WMG-0025 | Classic: an unbound stock texture reads XNA's opaque black | 🟩 |
| WMG-0026 | The browser route: configure, compile and link under Emscripten | 🟩 |
| WMG-0027 | A soak over the modern resources, and the timer lifetime defect | 🟩 |
| WMG-0028 | Final regression, and the corpus fix it found | 🟩 |

---

## WMG-0001 — baseline and inventory

### Baseline

| | |
|---|---|
| Base | `origin/next` = `c576b5d25f2faa126ebcd65005ce0a57c1e815d3` (VMG merge `29cfe0869` and VMG-0019 `479cb7f73` are ancestors) |
| Branch | `webgpu-modern-graphics`, no upstream |
| Present at base | `tools/platform/run_gpu_tests_private.sh` (GTI-0002), live-desktop policy (`cmake/TestDisplayPolicy.cmake`, GTI-0001/0006), `tools/platform/profile_dead_tests.py` (GTI-0007), `ModernGpuConformanceTests.cpp` (VMG-0012), the modern Vulkan implementation (MOD-2200..2266, VMG), EasyGL's modern implementation |

### The WebGPU provider

| | |
|---|---|
| Implementation | **wgpu-native v29.0.1.1** (`cmake/ThirdPartyWebGPU.cmake`, `CNA_WEBGPU_VERSION`), official Linux x86_64 release, SHA-256 pinned (`WEBGPU-1`); used here from `~/deps/wgpu-native-v29.0.1.1` via `CNA_WEBGPU_ROOT` |
| Header | the unified `webgpu/webgpu.h` + `webgpu/wgpu.h` (native extensions) |
| Browser | the same renderer source under Emscripten's `emdawnwebgpu` port (`WEBGPU-126..`) |
| Shader input | WGSL (every stock shader, `ShaderEffect` source); SPIR-V only on the native compiled-effect route (`WGPUShaderSourceSPIRV`, naga's SPIR-V frontend) |

### The build tree

`cmake-build-webgpu` — the name `docs/webgpu-renderer.md` already uses. `CNA_GRAPHICS_RENDERER=WEBGPU`
(so the classic `WebGPU_*` registrations exist: they are gated on the *default* renderer),
`CNA_GRAPHICS_RENDERERS="WEBGPU;VULKAN;OPENGLES3"` so the renderer-neutral suites run all three
renderers from the **same binaries** through runtime selection, `CNA_PLATFORM=WAYLAND` (CNA's native
Wayland backend), `CNA_CNAEXT=ON`, `CNA_WEBGPU_COMPILED_EFFECTS=ON` (the classic compiled-effect
surface), `CNA_SHARED_LIBRARY=ON`, Debug, ccache launchers. `CNA_ENABLE_SDL=AUTO`: MojoShader, which
the compiled-effect route needs, resolves SDL3 for its stdlib, so this tree links the prebuilt SDL3 —
the SDL-free configuration is measured separately (WMG task below). Only the targets that run were
built (`cmake-build-webgpu/wmg_baseline_targets.txt`), never the whole tree.

### A baseline that did not compile

With `CNA_WEBGPU_COMPILED_EFFECTS=ON`, `next` did not build: `SOFTWARE-255` (`e55945cd7`) made
`EffectPassCollection::operator[]`/`EffectTechniqueCollection::operator[]` return pointers (XNA's null
semantics) and updated every renderer except WebGPU. `WebGPURenderer.cpp:9146`,
`WebGPUCompiledEffectTests.cpp` (four sites) and the Emscripten-only
`webgpu_browser_compiled_effect_test.cpp` (two sites) still used `.`. There was no WebGPU build tree
on the machine, which is how it went unnoticed. Fixed mechanically (`->`). The same stale spelling is
in `D3D9SpriteBatch.cpp:388`, `D3D11SpriteBatch.cpp:469` and `D3D12SpriteBatch.cpp:579` — Windows
renderers, outside this workstream, recorded here and not touched.

---

## WMG-0002 — what the modern API asks of a renderer, and what WebGPU answered at base

The modern surface a renderer must satisfy is not a document; it is the set of `IGraphicsRenderer`
members the engine layer calls. Taken from the interface itself: compute shaders (`CreateComputeShader`,
`DispatchEXT`, `MemoryBarrierEXT`), storage buffers (`CreateStorageBufferEXT`, byte read/write/copy),
`Texture2DArray`, storage textures, GPU timers, the modern limit queries (`GetMax*EXT`/`GetMin*EXT`),
surface-format usage support, the four honesty queries (`ExecutesShaderEffectSourceEXT`,
`SupportsShadowSamplingEXT`, `SupportsImageBasedLightingEXT`, `SupportsComputeShadersEXT`),
`SupportsShaderLanguageEXT` per stage, `WaitForCompletionEXT`/`ProcessEventsEXT`, and the descriptor
binding contract an engine-layer `ShaderEffect` draws through (`plans/plan_vulkan_modern_graphics.md`
VMG-0002 states it for Vulkan; WebGPU mirrors it, see WMG-0009).

At base WebGPU implemented none of it: every one of those members took `IGraphicsRenderer`'s default
body, which is the honest "no". That is why the engine-layer suite skipped 245 of 961 tests there.

## WMG-0003 — measured baselines, before any change

Same binaries, runtime renderer selection, private compositor only
(`tools/platform/run_gpu_tests_private.sh --exec`), never the live desktop.

| Suite / renderer | total | pass | fail | skip |
|---|---|---|---|---|
| `CnaGraphicsExtTests` — OPENGLES3 (EasyGL) | 961 | 953 | 0 | 8 |
| `CnaGraphicsExtTests` — VULKAN | 961 | 929 | 0 | 32 |
| `CnaGraphicsExtTests` — **WEBGPU** | 961 | **685** | **31** | **245** |
| `CnaTests` (classic) — WEBGPU | 10564 | 9998 | 63 | 503 |
| `CnaWebGPURendererTests` | 56 | 51 | 3 | 2 |

The 245 skips are the measurement that matters: on this renderer a third of the engine-layer suite
was not failing, it was declining to run.

## WMG-0004 — the adapter, named rather than assumed

```
Adapter: AMD Radeon 780M (RADV PHOENIX) (Mesa 25.0.7-2+deb13u1), Vulkan backend,
         integrated GPU, vendor 0x1002 device 0x15bf
```

Requested with `WGPUPowerPreference_HighPerformance` and logged at device creation, so a run that
silently landed on a software adapter is visible in its own log rather than inferred. No run in this
ledger used `WGPUBackendType_Null` or a CPU adapter.

## WMG-0005 — dead tests: six WebGPU contract rows that aborted before their contract

`tools/platform/profile_dead_tests.py` classifies a test that exits early as not having run. Six rows
asked for a WebGPU contract and then aborted on the profile, so they reported success without reaching
the assertion: `backbuffer_pass_order_test.cpp`, `graphicsdevice_ordered_clear_test.cpp`,
`texturecube_texture3d_getdata_contract_test.cpp` and `texturecube_texture3d_setdata_contract_test.cpp`
(their WebGPU rows now set `wantHiDefProfile = true`), and `webgpu_mrt_test.cpp` /
`webgpu_texture3d_test.cpp` (which now ask for `GraphicsProfile::HiDef` explicitly). Counted as fixed
tests, not as new passes.

## WMG-0006 — WGSL for every stock shader, generated from the one GLSL source of truth

WebGPU ingests WGSL. CNA's stock engine-layer shaders are Vulkan GLSL, compiled to SPIR-V by
`tools/shader_package/generate_shader_package.py` and checked in. Rather than hand-write a second
shader per pass — two sources that drift — the generator gained a `wgsl` output produced from the
*same* GLSL by naga (`naga-cli 28.0.0`, recorded in every generated header as `kWgslTranslator`,
`kWgslTranslatorVersion`, `kWgslTranslatorSha256`).

Three source rewrites are needed before naga will accept Vulkan GLSL, applied by
`webgpu_transform()` and recorded as `kWgslSourceTransform = "cna-webgpu-glsl/1"`:

1. `layout(push_constant)` → a `set = 3, binding = 0` std140 uniform block. WebGPU has no push
   constants in core; the scalar block becomes an ordinary uniform buffer at a reserved group.
2. A combined `sampler2D` is split into a texture and a sampler (`NAME_cnaTexture`,
   `NAME_cnaSampler` at `binding + 32`) with a `#define` restoring the original spelling, because
   WebGPU has no combined image samplers.
3. A std140 uniform block whose members are all arrays of 4- or 8-byte elements becomes a `std430
   readonly buffer`. WGSL's uniform address space requires a 16-byte array stride, which a
   `float[]` or `vec2[]` uniform array does not have. A block that mixes array and non-array members
   is refused **by name** rather than rewritten.

The gate that makes this reproducible rather than hopeful: every payload is round-tripped
(`spv → wgsl → spv`) and the generator **fails** if any uniform or storage block member's offset or
array stride differs between the two SPIR-V modules. A translation that silently relaid out a block
cannot be committed.

19 package manifests and their 19 generated headers now carry WGSL alongside GLSL ES, desktop GLSL
and SPIR-V.

## WMG-0007 — a WGSL reflection module, because auto-layout is not usable here

`modules/renderers/webgpu/{include,src}/…/WebGPUWgslReflection.{hpp,cpp}` parses a WGSL module's
declarations: resources with their `@group`/`@binding` and kind, entry points with stage and
workgroup size, vertex inputs (both parameter and struct-member forms), and WGSL's own memory layout
rules (`vec3` aligns 16 and sizes 12, `matCxR`, array stride, `@align`/`@size`). It refuses by name
what it cannot answer for — `var<immediate>`, `texture_external`, a resource with no `@group`.

It exists because `wgpuDeviceCreateRenderPipeline` with an automatic layout derives a *different*
bind-group layout per pipeline, and an engine-layer effect compiles many pipelines that must share
one layout. Explicit layouts built from reflection are the only route that holds.

`WebGPUWgslReflectionTests.cpp` pins the parse: 9 CPU-only tests covering the generated naga shapes,
compute resources and workgroup size, every handle type, the layout rules (a struct whose members
land at 0, 12, 16, 32, 80, 144, 148, 192 with size 208), the legacy `WEBGPU-76` module, and each
refusal.

## WMG-0008 — the modern renderer surface on WebGPU

`WebGPUModern.{hpp,cpp}`, `WebGPURendererModern.cpp` and `WebGPUModernEffect.cpp` implement the
`IGraphicsRenderer` modern members listed in WMG-0002: compute shaders validated against their own
WGSL reflection, storage buffers, `Texture2DArray`, storage textures (with the sampled view alongside),
GPU timers on a two-entry timestamp query set, every `GetMax*EXT`/`GetMin*EXT` limit read from the
device's actual limits rather than a constant, surface-format usage support, and the event/completion
pumping the modern API's readbacks need.

Three WebGPU constraints the implementation had to answer rather than ignore:

- `wgpuQueueWriteBuffer` and buffer-to-buffer copies are 4-byte aligned, while the modern API's
  storage-buffer API is byte-granular. A byte-copy compute kernel does the unaligned head and tail.
- `copyTextureToBuffer` requires 256-byte row alignment, so readback stages through a padded buffer.
- Core WebGPU writes timestamps only at pass boundaries, so a GPU timer brackets its work with two
  empty compute passes rather than writing mid-pass.

## WMG-0009 — the descriptor binding contract, mirrored from Vulkan

An engine-layer `ShaderEffect` binds through a fixed contract, so that one shader package works on
every renderer that implements it. WebGPU's mirror of Vulkan's:

| | |
|---|---|
| group 0 | binding 0 the draw texture, binding 32 its sampler |
| group 1 | bindings 0–3 2D, 4–7 cube, 8–11 3D, 12–15 uniform arrays (capacity 72), 16–18 2D arrays, 19 the engine matrices; each sampler at `binding + 32` |
| group 2 | storage buffers |
| group 3 | binding 0 the 128-byte scalar block: `vpSize[0..1]`, `uMatrix[4..19]`, `uColor[20..23]`, `uFloats[24..31]` |

Which contract a compiled package uses is read from the shader itself, not from its language: legacy
iff a uniform sits at group 0 binding 0, descriptor iff any resource is at group ≥ 1 or binding ≥ 32.

## WMG-0010 — an engine defect: two sites asked the language when they meant the contract

`ClusteredForwardEffect::begin` and `ClusteredLightBuffer::upload` each branched on
`… == CNA::ShaderLanguageEXT::SpirV` to decide whether to take the packaged descriptor route —
uniform arrays in the scalar block and lights in storage buffers — or the hand-written GLSL route,
which binds the same data as three extra sampler units. WGSL packages are generated from the same
GLSL by the same generator and use the same binding contract, so a WGSL renderer was sent down the
GLSL route and then asked for sampler unit 4, which the contract does not have.

This is generic engine code, not renderer code, so the fix is renderer-neutral:
`CNA::UsesDescriptorBindingContractEXT(language)` in `modules/graphics/include/CNA/ShaderLanguageEXT.hpp`
names the question — *is this one of the languages CNA ships as a generated package built on the
descriptor contract* — and both sites ask it.

Measured: `CnaGraphicsExtTests` on WEBGPU 845 → **861** passes, 50 → **34** failures; the entire
16-test `ClusteredForwardEffectTest` class went from failing to passing. Unchanged on VULKAN and
OPENGLES3 (`UsesDescriptorBindingContractEXT` is true for `SpirV` exactly as before, and false for
both GLSL dialects).

## WMG-0011 — a WebGPU defect: a sprite custom effect was never applied

`Effect::Apply()` is what makes an effect's own parameters reach its renderer — `CRTEffect::OnApply`
is the only place `uCrtParams` is ever set. EasyGL (`EasyGLRenderer.cpp:5668`) and Vulkan
(`VulkanRenderer.cpp:2399`) call it as the sprite batch flushes. WebGPU never called it at all, so
every `SpriteBatch.Begin(..., effect)` post-process ran with its whole parameter block at zero. For
CRT and `DepthEffect` a zeroed block is an exact copy of the input, so the passes reported success
and did nothing; that is the `MOD-1699` failure mode the honesty queries exist to prevent, arriving
by another door.

WebGPU copies a sprite's uniforms when the sprite is *queued* (WEBGPU-142, so a later `Begin` cannot
change an already-queued sprite), so `Apply()` is called there — one step earlier than the other two
renderers and per sprite rather than per batch, which is the stricter reading of the same rule.

## WMG-0012 — two test-side questions that had not caught up with a second source language

Both were written when every renderer that ran shader source ran GLSL.

`RunsShaderSource` answers whether the text handed to a `ShaderEffect` determines the pixels, and
says nothing about which language that text is in. Vulkan answers no to both at once because it takes
SPIR-V, which is why sixteen tests that *type GLSL into a `ShaderEffect`* skip there and looked
settled. WebGPU runs the source it is given and requires WGSL, so those tests were handing it a
shader it cannot compile and reading the refusal as a renderer defect. `RunsGlslShaderSource` and
`CNA_SKIP_WITHOUT_GLSL_SHADER_SOURCE` ask the narrower question; a test that hands over a shader
*package* still asks `RunsShaderSource`, unchanged.

`SsrPassTest.SupportAsksTheTwoPartQuestion` enumerated the languages CNA ships in a package to
predict `isSupported()`, and the list had not gained WGSL — so it read a working renderer as an
unsupported one. WGSL joins SPIR-V in the list.

Both renderer-neutral: on VULKAN and OPENGLES3 every one of these tests keeps the status it had.

## WMG-0013 — where the engine-layer suite stands on WebGPU

`CnaGraphicsExtTests`, same binary, runtime renderer selection, private compositor:

| | total | pass | fail | skip |
|---|---|---|---|---|
| WEBGPU at base | 961 | 685 | 31 | 245 |
| WEBGPU after WMG-0010..0012 | 961 | 879 | 0 | 82 |
| WEBGPU after WMG-0013 (indirect) | 961 | 890 | 0 | 71 |
| **WEBGPU final** (WMG-0014..0017) | 961 | **932** | **0** | **29** |
| VULKAN (re-measured, unchanged) | 961 | 929 | 0 | 32 |
| OPENGLES3 (re-measured, unchanged) | 961 | 953 | 0 | 8 |

The final WEBGPU row is **ahead of Vulkan** on this suite, on a renderer that began the workstream
with 31 failures and a third of the suite declining to run. Vulkan and EasyGL were re-measured after
every change to shared engine code and are byte-for-byte on their own baselines.

**No failures**, and the 29 remaining skips are *fewer* than Vulkan's 32. They are the shape Vulkan's
are: tests that type GLSL into a `ShaderEffect` (WMG-0012), tests whose negative case needs a
renderer *without* a capability this one now has, and the handful of shared capability skips. What
the workstream removed from this column, in order: 31 shadow-reception skips (WMG-0014), 12 indirect
(WMG-0013), 5 compute-package, 4 volumetric-fog and 3 language-list skips (WMG-0015).

## WMG-0013 — indirect draws

`SupportsIndirectDrawEXT` was false, so twelve tests declined to run: six in `IndirectDrawTest` and
six in `GpuInstanceCullerTest`, whose whole point is a draw whose counts were produced by compute
and never came back to the CPU.

The route is the one Vulkan takes, and it is short here because every `Queue*Draw` already snapshots
the **complete** vertex and index window rather than the primitive count's worth. So the draw is
queued through the ordinary path with a legal zero primitive count — purely to capture this call's
effect, declaration, pipeline state, viewport and scissor — and the argument buffer is then attached
to the single command that produced. At replay the family's own draw call becomes
`drawIndirect`/`drawIndexedIndirect`, and the command's CPU counts are not read at all.

What that shifts, and where it is handled: a zero vertex count is every family's "nothing to draw"
signal, so each of those ten guards now asks whether this is an indirect draw before believing it.
The argument buffer's `StorageBuffer` record is retained by `shared_ptr` for the life of the command,
so a `Dispose()` between the public call and the flush cannot leave a freed `WGPUBuffer` in a queued
draw. Refused by name: a compiled (FX) effect, a wireframe draw (its route rewrites a draw's own
triangles into line segments at queue time, and an indirect draw's triangles are not known then), a
buffer not declared with `IndirectArguments` usage, and a buffer belonging to another device.

`SupportsIndirectDrawEXT` answers from the device: `drawIndirect` is core WebGPU, but a non-zero
`firstInstance` in an argument block needs the optional `IndirectFirstInstance` feature, which the
modern API's argument structs carry, so the answer is whether the device was created with it.

Measured: `CnaGraphicsExtTests` on WEBGPU 879 → **890** passes, 82 → **71** skips, 0 failures. The
twelfth test, `ARendererWithoutTheCapabilityRefusesByName`, correctly skips on a renderer that now
has the capability, exactly as it does on Vulkan and EasyGL.

## WMG-0014 — shadow reception in the stock lit families

`SupportsShadowSamplingEXT` was false, and 31 tests skipped on it. The four families Vulkan answers
for now answer here: `BasicEffect`, `SkinnedEffect`, `PbrEffect` and `SkinnedPbrEffect`.

`webgpu_shaders::kShadowSampling` is the WGSL twin of the Vulkan renderer's `shadow_sampling.glsl`,
function for function and constant for constant, at **group 2** — which every stock family had free,
and which is deliberately identical across families exactly as Vulkan's set 1 is. WGSL module-scope
declarations may appear in any order, so it is appended to a shader's source rather than spliced
into it. The uniform block is the same 132 floats the Vulkan renderer fills, filled by the same
arithmetic, so the two renderers answer a shadow query from identical numbers rather than from two
readings of one description.

Three things this needed that are particular to WebGPU:

- **`textureSampleLevel`, not `textureSample`.** Every tap sits inside a loop with a `continue` and
  behind early returns — non-uniform control flow, where WGSL forbids an implicit-derivative sample.
  The atlas has one level, so level 0 is the same tap without the restriction.
- **The atlas is read `v`-flipped, as on Vulkan and unlike EasyGL.** The caster writes
  `gl_Position.y = -lightSpace.y` for Vulkan's clip space, and naga's SPIR-V frontend negates `y`
  again in the entry point it generates — which is exactly how a Vulkan-authored shader lands
  correctly in WebGPU's opposite clip space. Two negations leave the rasterised atlas in Vulkan's
  orientation, so it is read Vulkan's way.
- **A draw with a shadow map takes the per-pixel path** whatever `PreferPerPixelLighting` says. The
  Gouraud sibling computes its lighting in the vertex stage, where a per-pixel shadow lookup has
  nowhere to go. Vulkan has this rule already (`VulkanRenderer.cpp`, `d.preferVertexLit`); without
  it here, `ShadowsSurviveTheDefaultPerVertexLighting` and eleven others failed with no shadow at
  all — the most useful single failure of this task, because it named the defect exactly.

A 1×1 white texture stands in for an absent map, because a pipeline statically uses every binding
its layout declares; white reads as "nothing occludes", which is what the parameter block already
says when the shadow is off.

Measured: `CnaGraphicsExtTests` on WEBGPU 890 → **921** passes, 71 → **40** skips, 0 failures. All
17 `ShadowVisibilityTest`, 7 `CascadedShadowVisibilityTest` and 7 `PunctualShadowVisibilityTest`
cases pass.

## WMG-0015 — packages and language lists that had not caught up with WGSL

Four more places named the languages that existed when they were written:

- `VolumetricFogPass`'s package helper took a SPIR-V fragment and no WGSL one, so a WGSL renderer
  could select the vertex stage and not the fragment — the package was unusable and
  `isSupported()` was false, skipping four tests.
- `PortableTintShaderPackage` and `TransparencyExampleShaderPackage` offered GLSL ES and SPIR-V only.
  Both also indexed `kPayloads` by position, and regenerating the packages had shifted those
  positions — the labels were reading the wrong provenance row. Fixed with the WGSL variants.
- `ComputeShaderTests`' two packages offered GLSL ES and SPIR-V compute only (five tests), and
  `EffectPassTest` / `ShaderPackageSelectionEXTTest` enumerated languages to decide whether a
  renderer had anything to run (three tests).

All renderer-neutral: nothing an existing renderer selects changes.

## WMG-0016 — a query that answered "no" about something the renderer does

`SupportsTexture3DSamplingEXT()` was left at the interface default, `false`, while this renderer
binds volumes at group 1 bindings 8..11 with their samplers at `binding + 32`, declares them as
`texture_3d<f32>` in the generated WGSL, and fills them from `BindTexture3D`.

`ShaderPackageEXT::selectFor` judges any package carrying a `SampledTexture3D` binding requirement
unusable when that feature is absent, so `ColorGradePass`'s volume-LUT effect was never created, the
pass copied its input through, and it reported success. That is the `MOD-1699` failure mode reached
from the other direction: not a renderer claiming something it cannot do, but one denying something
it can. The full-suite log named it on every `ColorGradePass` construction — *"ColorGradePass
(volume): its shader did not compile on the WEBGPU renderer"* — which is how it was found, in the
tail of an OOM-killed run rather than in a failing assertion.

All ten `ColorGradePassTest` cases pass and the line is gone from the log.

## WMG-0017 — the GPU timer measured its own overhead

`GpuTimerTest.MoreWorkTakesMoreGpuTime` was the one failure left in the engine-layer suite, and it
was right: four full-screen draws read **0.2365 ms** and forty read **0.2006 ms** — ten times the
fill coming back as no longer at all.

A timestamp is only legal at a pass boundary in core WebGPU, so `WriteTimestampEXT` wrote one at the
beginning of an *empty compute pass* on each side of the range. Nothing orders an empty compute pass
against a graphics submission it shares no resource with, so the pair measured a fixed overhead
rather than the work between them. This is exactly what the ledger had already recorded as "not
measured, so not claimed" — the measurement, once made, disagreed with the implementation.

The fix is the shape the Vulkan renderer already uses (`pendingTimestamps_`, ordered into the
command stream): the timestamps ride the **real passes**. While a timer is open, every render pass
carries its writes — the first pass after `Begin` writes index 0 at its beginning, and every pass
writes index 1 at its end, so the last pass before `End` is the one that survives. A timer opened
around no drawing still gets a real, near-zero pair from one empty pass rather than reading whatever
the query set last held. One timer at a time, because a `WGPURenderPassDescriptor` carries exactly
one set of timestamp writes; a second concurrent `Begin` keeps the empty-pass behaviour.

Measured after: **4 draws 0.0219 ms, 40 draws 0.1570 ms** — 7.2x for ten times the fill.

The same commit repairs what `WMG-0014` broke in `ValidateAllShadersEXT`: it compiled the seven
shadow-receiving families *without* the appended shadow block, so it validated a program this
renderer never builds and failed on the very functions that block defines.

## WMG-0018 — the renderer's own 198 tests, and which failures are this workstream's

`ctest -R '^WebGPU'` is 198 tests (142 `WebGPU_` example programs, 47 renderer gtests, 9 of them the
WGSL-reflection tests added here). Eleven fail, and **none of the eleven is this workstream's** —
each was measured failing on the base commit `c576b5d25` itself:

| what | how it was established |
|---|---|
| `WebGPU_PointSamplingContract`, `WebGPU_DescriptorCapacityContract` | `plans/plan_webgpu.md` names them repeatedly as "the two long-standing XNA-pixel-centre-convention failures, A/B-proven unrelated during Wave 1" |
| `WebGPUCompiledEffectTest.SharedBackendConformanceContract`, `WebGPUCompiledEffectDrawTest.AddressWSelectsADifferentVolumeSliceForEachMode`, `WebGPUCompiledEffectWgslDrawTest.SharedBackendConformanceContract` | the three failures in this workstream's own **pre-work baseline** of `CnaWebGPURendererTests` (56 tests, 3 failures) |
| `WebGPU_ContextRecovery`, `WebGPU_RealWindowResize`, `WebGPU_SpriteBatch_SortMode`, `WebGPU_Viewport_Cardinality`, `WebGPU_Scissor_Cardinality`, `WebGPU_TextureFilterMipContract` | **measured**: the base commit's `modules/` and `tools/` were checked out in place, `cmake-build-webgpu` rebuilt, and all six failed there too — 0 of 6 passed |

Two failures *were* this workstream's, and both are fixed rather than explained: `WebGPU_ShaderValidation`
(see `WMG-0017`) and the six profile rows of `WMG-0005`, which stopped dying early and then ran on
to a Reach refusal until the profile was requested where it can still be heard.

Three parity fixtures also fail, and they are not this workstream's either:

* `WebGPU_Parity_dual_texture_terms` fails only its **null-texture** claims — *"null Texture samples
  XNA opaque black: mean=(200,180,120) expected=(0,0,0)"*. Microsoft XNA reads an unbound
  stock-effect texture as opaque black, and the Vulkan renderer implements exactly that
  (`VulkanRenderer.cpp`, "default opaque black 2D"). **WebGPU has no such handling at all** — the
  string does not appear in its source — so the fixture has failed since that rule reached the
  fixture (`e05b3d0f0`, 2026-09-13, an ancestor of this workstream's base). It is a real WebGPU
  gap, recorded here rather than claimed, and it is a *classic*-API gap, outside this workstream.
* `WebGPU_Parity_compressed_cube` is the fixture whose oracle is a BC cube against an RGBA8 cube
  within one renderer, and `TextureCubeTest.SetDataCompressedBytesUploadsRequestedFaceMip` is a
  failure in this workstream's own pre-work classic baseline — the same compressed-cube gap.
* `WebGPU_Parity_backbuffer_msaa` was measured the same way, and so were the other two: the base
  commit's `modules/` and `tools/` checked out in place, `cmake-build-webgpu` rebuilt, **0 of 3
  passed there**.

So **all fourteen** failures in `ctest -R '^WebGPU'` are pre-existing, each established by
measurement or by the project's own record, and none by assertion.

---

# The closeout, WMG-0019..0028

The owner's brief of 2026-09-22: finish the acceptance items the workstream had recorded as *not
implemented* or *not measured*, and leave the branch merge-ready without reopening the renderer as
a feature project. Everything below was measured on the private compositor
(`tools/platform/run_gpu_tests_private.sh`), never on the live desktop, on the adapter WMG-0004
names — AMD Radeon 780M (RADV PHOENIX), Mesa 25.0.7, Vulkan backend.

## WMG-0019 — the GPU timer, measured against a workload that scales

WMG-0017 fixed the timer and proved it with one ratio. One ratio is a weak claim: two samples drawn
from noise around a constant satisfy it about half the time.
`GpuTimerTest.TheNumberTracksTheWorkloadAcrossThreeSizes` asks whether the number *follows* the
work across three full-screen-draw workloads an order of magnitude apart, and asserts four separate
things — every reading finite and non-negative, the curve rising at every step, forty times the
fill taking at least four times as long, and every reading smaller than the CPU wall clock that
encloses its own submission and read-back. That last one is the bound a tick-to-nanosecond error
breaks: WebGPU timestamps are nanoseconds by specification and need no period, while Vulkan
multiplies by `timestampPeriod`.

Renderer-neutral, and each assertion fails on the behaviour WMG-0017 removed (0.2365 → 0.2006 ms):

| renderer | 4 draws | 40 draws | 160 draws | 40 draws, three repeats |
|---|---|---|---|---|
| WEBGPU | 0.0348 ms | 0.1877 ms | 0.6017 ms | 0.1551 / 0.1971 / 0.1729 |
| VULKAN | 0.0915 ms | 0.6090 ms | 2.3711 ms | 0.5933 / 0.5939 / 0.5921 |
| OPENGLES3 | 0.0565 ms | 0.4850 ms | 1.9445 ms | 0.4749 / 0.4752 / 0.4754 |

## WMG-0020 — debug markers

`SetStringMarkerEXT` took `IGraphicsRenderer`'s no-op body. It is the **only** debug-label entry
point CNA has — there is no public push/pop group API on the interface, and none was added.

A label is queued into the ordered stream rather than emitted at the call, for the reason every
public graphics command in this renderer is: nothing is recorded until the bind cycle flushes. That
makes it a third `OrderedKind` beside REMED-GFX-156's `Clear`, differing in the one way that
matters to `BuildPassSegments` — a `Clear` is observable in the pixels and therefore a pass
boundary, a label is not, so it extends the segment it landed in and
`wgpuRenderPassEncoderInsertDebugMarker` emits it inline. It does not advance
`nativeDrawIssueCount_`, because WEBGPU-115 measures a refused draw's "nothing reached the GPU"
with that counter and a label is not work.

Debug groups around the renderer's own render and compute passes come with it — the role
`VulkanRenderer`'s `beginDebugRegion`/`endDebugRegion` lambdas play. Counts are exposed as
`GetRecordedDebugMarkerCountEXT` and its two region siblings, the shape `Vulkan_GpuTimerDebug`
already reads Vulkan's.

`WebGPU_DebugMarker`, 7/7. Every check is a **difference** between two measurements rather than an
absolute, so a renderer that ignored the labels would leave the difference at zero: one label
emitted once; three labels around three draws, none coalesced; the render pass opened a group and
every group opened was closed; a compute dispatch opens exactly one and closes it; a null and an
empty label insert nothing; a label with no pass behind it emits nothing and is safe.

## WMG-0021 — instance streams for a ShaderEffect, and what measuring found

WMG-0018 recorded this as a refusal — "a split per-instance stream is refused by name". Measuring
it found something worse: **it was not refused at all.** `DrawInstancedPrimitivesEx` never looked at
`params.customEffectRenderer`, unlike `DrawPrimitivesEx` and `DrawIndexedPrimitivesEx` which have
branched on it since WEBGPU-76, so an instanced draw through a `ShaderEffect` fell into the stock
`instanced3d` family and was rendered with CNA's own shader instead of the game's. A silent
wrong-shader result: the MOD-1699 failure mode again, the draw succeeding and drawing the wrong
thing.

The contract is XNA's own and there is only one of it — `SetVertexBuffers` with an
`InstanceFrequency` above zero, then `DrawInstancedPrimitives` — and nothing in it is particular to
a stock effect. `CNA::Graphics::InstancedRendererEXT` documents that an effect wanting its tint
stream *"must be a `ShaderEffect` whose vertex input declares it"*; `CNA::Graphics::ParticleSystem`
is a `ShaderEffect` with an instance **count** and no instance stream at all; Vulkan and EasyGL both
implement it. So it is implemented rather than refused.

The route the other two renderers take, followed: the custom-effect branch moves ahead of the
instance-stream search, because a `ShaderEffect` draw with no per-instance stream is still an
instanced draw and the old fallback flattened `ParticleSystem` to a single instance; instance
attribute locations continue after the per-vertex declaration's element count and then across the
instance streams in order, which is EasyGL's `PerVertexLocationCount` convention and the one Vulkan
copied at VULKAN-168; the `InstanceFrequency` divisor is expanded into one record per instance at
queue time, because wgpu-native v29.0.1.1's `WGPUVertexBufferLayout` carries a step **mode** and no
step **rate**; each stream's stride and attributes join the pipeline cache key, or an instanced
draw and an otherwise identical non-instanced one would share a cached pipeline; and MOD-2237's
supplied-location rule now counts an instance attribute as supplied, which it could not before
because there was no instance half to supply it from.

Both contracts take it — the descriptor route filters attributes through its WGSL reflection, the
legacy route has none and offers every declared attribute. The per-**vertex** split is still refused
by name (`RequireSingleStreamRouteEXT`), unchanged: a different gap, untouched.

`WebGPU_ShaderEffectInstanced`, 6/6, on four instances differing **only** in their per-instance
record, so a draw that ignored the stream, re-read one record, or ran the stock shader each fails a
different check: four distinct quadrants; each quadrant the colour its own record named; a second
per-instance stream reaching its own locations (the tint shape); an identical second draw reusing
the pipeline and agreeing; an instanced draw with no instance stream running the game's shader;
`InstanceFrequency` 2 advancing the record every second instance.
`InstancedRendererEXT`, `ParticleSystem`, `GpuInstanceCuller` and the instanced-draw tests: 28/28.

## WMG-0022 — image-based lighting

`SupportsImageBasedLightingEXT()` was the interface default, `false`, which was truthful: an
irradiance cube, a prefiltered specular cube and a BRDF table set on a `PbrEffect` were accepted and
ignored. Vulkan and EasyGL answered true.

`webgpu_shaders::kIblSampling` is the WGSL twin of `VulkanRenderer`'s `CnaIblAmbient` and EasyGL's
`cnaIblAmbient` — the same split-sum equation term for term, so the three renderers answer an IBL
query from one equation rather than three readings of one description. The mip for a given
roughness is `roughness * (mipCount - 1)`, which is
`CNA::Graphics::EnvironmentProcessor::mipForRoughness` and what `iblPrefilteredMipCount` documents.

At **group 3**, appended to both PBR families exactly as WMG-0014 appends `kShadowSampling` at
group 2. The three resources are read through sampler slots 10, 11 and 12, where Vulkan's
`PbrSlotSamplersRawEXT()` and EasyGL's texture units read them (MOD-1225).

Three things particular to WebGPU: `textureSampleLevel` throughout, because the prefiltered tap
needs an explicit LOD and every tap sits behind the function's early return, which is non-uniform
control flow; a draw with no environment still binds all seven bindings, because a pipeline
statically uses every binding its layout declares; and the state is resolved to values at the public
draw call (REMED-GFX-167), so an environment set before the flush cannot change an already-queued
draw. The ambient line is a **sum**, not a branch, because the engine already zeroed `ambientColor`
when a valid bundle is bound (MOD-1226), and occlusion multiplies the ambient/IBL term only
(MOD-1227). `ValidateAllShadersEXT` compiles the four PBR variants with the block appended, for the
reason WMG-0017 had to state about the shadow block.

`CNAEXT_ImageBasedLighting` 8/8 — ahead of Vulkan's 7/7 and level with EasyGL — and
`CNAEXT_GltfPbr` and `CNAEXT_Showcase` both un-SKIP and pass. The check worth naming is the white
furnace: a white environment on a white non-metal returns close to the energy it received, at four
roughness points.

## WMG-0023 — ASan, UBSan and LSan

Built in the sanctioned reusable `build-asan/` and `build-ubsan/`, configured
`CNA_GRAPHICS_RENDERER=WEBGPU`, `CNA_PLATFORM=WAYLAND`, `CNA_ENABLE_SDL=OFF`, `CNA_CNAEXT=ON`,
Debug, ccache launchers.

| | suite | result |
|---|---|---|
| **ASan** | 90 modern engine tests (compute, storage buffers and textures, indirect, GPU timer, device loss, multi-device, ShaderEffect, shader packages, particles) | **0 defects** |
| **ASan** | `cna_test_webgpu_modern_stress`, 310 cycles | **0 defects** |
| **UBSan** | the same 90 tests, `-fno-sanitize-recover=all` | **0 runtime errors** |
| **UBSan** | the same stress run, 300 cycles | **0 runtime errors**, 6/6 |
| **LSan** | the stress run at 100 and at 400 cycles | see below |

LeakSanitizer reports **1 873 223 bytes in 1 110 allocations**, and the number that settles what
they are is that it is *the same number at both scales*: byte for byte, allocation for allocation,
at one hundred cycles and at four hundred. A resource-lifetime leak would have scaled with four
times the create/dispose traffic. The stacks agree — they run through
`wgpu_core::hub::Hub::new`, `wgpu_core::track::TrackerIndexAllocators::new` and
`amdgpu_va_range_alloc2`, with CNA frames only on the device-**construction** path. One-time
provider and driver allocations never freed at process exit; **0 CNA leaks**. LSan is demonstrably
working here, since it reported them.

## WMG-0024 — the SDL-free configuration, and two defects it found

`CNA_ENABLE_SDL=OFF` with `CNA_GRAPHICS_RENDERER=WEBGPU` had never been configured. Configuring it
found two things, both latent since the code was written.

The configure failed outright: two WebGPU example targets borrow an EasyGL source that reaches for
SDL3 itself and linked `SDL3::SDL3` unconditionally. Then the link succeeded and the **run** failed,
which is worse: `IssueDescriptorEffectDrawEXT` and `IssueDescriptorSpriteEXT` had drifted inside the
`#if defined(CNA_WEBGPU_COMPILED_EFFECTS)` block while their only call sites stayed outside it, so
`libcna.so` carried two undefined symbols that the dynamic linker did not resolve until the first
`ShaderEffect` draw reached them. They are descriptor-contract draws (WMG-0008/0009/0011), not
compiled-effect ones; the block now begins after them.

`libcna.so`'s direct `NEEDED` entries, which is the evidence that matters:

| tree | direct dependencies |
|---|---|
| WAYLAND | `libzstd`, FFmpeg (`libavcodec`/`avformat`/`avutil`/`swresample`, optional video), **`libwayland-client`**, **`libxkbcommon`**, **`libwgpu_native`**, `libstdc++`, `libm`, `libgcc_s`, `libc`, `ld-linux` |
| X11 | the same, with `libX11`/`libXext`/`libXi`/`libXrandr`/`libXcursor`/`libXau`/`libXss` in place of the two Wayland ones |

No `libSDL2`, `libSDL3` or `libSDL3_mixer` in either, nor anywhere in the `ldd` closure of
`libcna.so`, `CnaGraphicsExtTests`, `cna_test_cnaext_ibl` or `cna_test_webgpu_modern_stress`. The
X11 entries in the *Wayland* tree's transitive `ldd` come from FFmpeg's `libva-x11`, not from CNA —
a provider dependency, classified separately.

Measured on both, real hardware adapter logged at device creation, never a software one:

| | Wayland (private Weston) | X11 (private rootful Xwayland, DRI3) |
|---|---|---|
| `CNAEXT_ImageBasedLighting` | 8/8 | 8/8 |
| modern stress, 1010 cycles | 6/6, RSS +16 KiB | 6/6, RSS +24 KiB |
| `CnaGraphicsExtTests` | — | **933 pass / 0 fail / 29 skip** |

## WMG-0025 — the classic gap WMG-0018 recorded

Microsoft XNA 4.0 reads an unbound stock-effect texture as opaque black, and this renderer had no
null-texture handling at all: every classic family bound the neutral-white `tex * colour` identity,
which is glTF's default material (GLTF-474) rather than XNA's rule. GSC-0004 corrected DirectX11,
DirectX12 and EasyGL; VKPAR-0004 corrected Vulkan; WebGPU was the renderer nobody had reached.

Eight bind sites take the new 1×1 opaque-black 2D and cube: `QueueLitTexturedDraw`,
`QueueAlphaTestDraw`, `QueueTexturedDraw`, `QueueSkinnedDraw`, both `DualTextureEffect` layers and
both `EnvironmentMapEffect` slots. White stays where white is right — the PBR family's glTF
fallbacks, a `ShaderEffect`'s own sampler slots, the compiled-effect route. Bound
*unconditionally*, which is Vulkan's reading rather than EasyGL's: the WGSL stock programs already
carry the identity where it belongs, as
`select(vec4f(1.0), textureSampleBias(...), textureEnabled > 0.5)`.

`StockEffectNullTextureTest` — five renderer-neutral cases that had skipped WebGPU by name — 5/5,
and `WebGPU_Parity_dual_texture_terms` passes, having failed on its null-texture claims since
`e05b3d0f0` (2026-09-13), an ancestor of this branch's base. Small and isolated, so fixed here
rather than deferred as `WEBGPU-CLASSIC-F1`.

## WMG-0026 — the browser route

emsdk 6.0.9, the repository's own supported path (`cmake/ThirdPartyWebGPU.cmake` selects
Emscripten's `--use-port=emdawnwebgpu` when `EMSCRIPTEN`), build directory
`cmake-build-wasm-webgpu` as `scripts/run-webgpu-browser-test.sh` names it.

```
emcmake cmake -S . -B cmake-build-wasm-webgpu -G Ninja -DCMAKE_BUILD_TYPE=Release \
              -DCNA_GRAPHICS_RENDERER=WEBGPU -DCNA_CNAEXT=ON -DCNA_BUILD_EXAMPLES=ON
cmake --build cmake-build-wasm-webgpu --target cna_demo_2d -j6
```

Configure ✅, compile ✅, link ✅ — `libcna_renderer_webgpu.a` and `libcna_graphics_ext.a` both
compile to WebAssembly, and `cna_demo_2d.{html,js,wasm,data}` links (7.5 MB wasm). The wasm carries
43 WGSL entry-point markers, so the stock shader packages WMG-0006 generated reach the browser
build, and the JS references `navigator.gpu`/`requestAdapter`.

**Runtime was not tested**, and nothing here claims it was. A compile smoke says the code builds for
the browser, not that it renders in one.

## WMG-0027 — the soak, and the defect it found

`WebGPU_ModernStress`. `webgpu_resource_lifetime_stress_test` (WEBGPU-191) does this for the classic
path; it predates every modern resource, so compute shaders, storage buffers, storage textures,
indirect arguments and GPU timers had never been stressed. The oracle is the renderer's own
uncaptured-error count rather than a crash or a pixel, because a test that only checked for a crash
would pass on a renderer quietly submitting invalid work.

It found a defect on its second run. WMG-0017 made every render pass carry the open timer's query
set until the range closes, and the renderer held that query set as a **raw handle**. A `GpuTimer`
destroyed while its range is open — which is what any exception between `begin()` and `end()` does —
released the query set and left the renderer naming it, and the next pass built after that
referenced freed memory. wgpu-native reports that as a *non-unwinding panic* inside
`wgpuCommandEncoderBeginRenderPass`: a recoverable error turned into an abort at the next `Present`,
with the original error never reported. That is exactly how it surfaced — the stress test's own
first failure was invisible behind it. `ForgetOpenGpuTimerEXT` closes it, from the timer renderer's
destructor, and only for the timer that owns the open range.

3010 cycles, each creating, using, reading back and destroying every modern resource, with a
render-target resize every 64th:

| | |
|---|---|
| RSS | 157 832 → 157 852 KiB (**+20 KiB**) |
| file descriptors | 22 → 22 |
| threads | 22 → 22 |
| uncaptured provider errors | 0 → 0 |
| device lost | no |
| checks | 6/6, and the last cycle's readback holds what its own dispatch wrote |

Registered at 512 cycles so an ordinary ctest run can afford it.

## WMG-0028 — the final regression, and the corpus fix it found

Building `cmake-build-vulkan` for the classic Vulkan run found that WMG-0007's WGSL reflection suite
had joined **every** renderer's `CnaTests` through `cmake/UnitTests.cmake`'s recursive glob, and it
includes a header whose include root arrives with the WebGPU renderer target. Any non-WebGPU
configure produced a `CnaTests` that could not compile. Excluded exactly as `plans/plan_fna3d.md`'s
suite already is, and for the reason that entry states in the same file. Nothing had configured a
non-WebGPU tree since WMG-0007 landed.

### Modern conformance, final

`CnaGraphicsExtTests`, same binary, runtime renderer selection, private compositor:

| renderer | total | pass | fail | skip | against the pre-closeout row |
|---|---|---|---|---|---|
| **WEBGPU** | 962 | **933** | **0** | **29** | 932/0/29 of 961, +1 new test |
| VULKAN | 962 | 930 | 0 | 32 | 929/0/32 of 961, +1 |
| OPENGLES3 | 962 | 954 | 0 | 8 | 953/0/8 of 961, +1 |

The one added test is WMG-0019's. No renderer gained a failure and no renderer's skip count moved.

**The WEBGPU row is measured in four gtest shards.** In one process the run is OOM-killed at about
five hundred tests on this 30 GB machine, and the reason is measured rather than guessed: over the
same ~200 tests, peak RSS is **1 115 MB on WEBGPU against 167 MB on VULKAN** from the same binary.
The suite builds a `GraphicsDevice` per test and each one costs a wgpu-native hub; it is a harness
property, not a renderer result. Every test passes when the run is sharded, and WMG-0027 shows no
growth *within* a device — +20 KiB over 3010 cycles.

### Classic WebGPU

`ctest -R '^WebGPU'` is now **201** tests (198 plus WMG-0020/0021/0027's three, all passing) with
**13** failures, against WMG-0018's 14 of 198. The difference is exactly
`WebGPU_Parity_dual_texture_terms`, fixed by WMG-0025. **No new failure.** The remaining thirteen
are the ones WMG-0018 established pre-existing, by measurement or by the project's own record.

### The rest

| suite | result |
|---|---|
| Classic Vulkan, `ctest -R '^Vulkan_'` in `cmake-build-vulkan` | **370 / 371**; `Vulkan_DrawRangeValidation` fails identically with `modules/graphics` and `modules/graphics-ext` checked out at `origin/next`, so it is pre-existing and measured, not assumed |
| CNAEXT examples (`-L CnaExt`) | 32 registered, **30 pass**, 2 pre-existing failures |

The two CNAEXT failures, each established rather than asserted:

* `CNAEXT_LeakLoop` segfaults on WEBGPU and passes on VULKAN and OPENGLES3. Building it from this
  branch's own pre-closeout commit `63e208bed` reproduces the segfault exactly, so it is not this
  closeout's. It is a **genuine pre-existing WebGPU defect** and the one item below that is real
  renderer debt rather than environment.
* `CNAEXT_NoPosixSetenv` names `::unsetenv` call sites in `modules/platform/src/Wayland/` and
  `modules/platform/tests/`, none of which this branch touches (`git log origin/next..HEAD` over
  those paths is empty). It belongs to the Wayland workstream.

### The private-display policy, proven rather than asserted

Every run above went through `tools/platform/run_gpu_tests_private.sh`, which prints what it built:

```
run_gpu_tests_private: DISPLAY=:2 (private Xwayland), WAYLAND_DISPLAY=cna-weston-<pid> (private Weston)
[WebGPU] Adapter: AMD Radeon 780M (RADV PHOENIX) (Mesa 25.0.7-2+deb13u1), Vulkan backend,
         integrated GPU, vendor 0x1002 device 0x15bf
```

The display number is one the X server chose itself (`-displayfd`) and the Wayland socket is named
after the runner's pid, so neither can be `:0` or `wayland-0`. `CNA_TEST_DISPLAY` is empty in every
tree used here, which is what the runner refuses to proceed without. The compositor, its shell
client and Xwayland are all torn down when the runner returns.

### Build sizes

| tree | size |
|---|---|
| `cmake-build-webgpu` (the working tree, unchanged by this closeout) | 6.7 GB |
| `cmake-build-webgpu-nosdl` (Wayland, SDL-free, Release) | 172 MB |
| `cmake-build-webgpu-x11` (X11, SDL-free, Release) | 171 MB |

Well inside the 15 GB the brief asks for. The two SDL-free trees are small because they are Release
and share one `libcna.so`.

---

## What is left, and what kind of thing it is

Classified rather than listed, so a reader can tell debt from environment.

**Genuine renderer debt (classic WebGPU).** `CNAEXT_LeakLoop` segfaults on WEBGPU and on no other
renderer, established pre-existing by building it at `63e208bed`. The thirteen `ctest -R '^WebGPU'`
failures WMG-0018 enumerated, minus the one WMG-0025 fixed.

**Provider limitation.** LSan's 1 873 223 bytes are wgpu-native's and the AMD driver's one-time
allocations, proven by being identical at one hundred and at four hundred cycles. A `GraphicsDevice`
costs roughly 1.1 GB of peak RSS across a 200-test run on WEBGPU against Vulkan's 167 MB, which is
what forces the engine-layer suite to be sharded on a 30 GB machine.

**Browser runtime validation.** Not done, and not claimed. WMG-0026 is a compile smoke: configure,
compile and link under `emdawnwebgpu`. Running it in a browser is its own task.

**Test debt.** The `ShaderEffect`-with-instance-stream coverage added by WMG-0021 is a WebGPU
example, because the tree has no renderer-neutral suite for that combination — Vulkan's and
EasyGL's are renderer-specific example binaries too. A renderer-neutral conformance case would need
a multi-language shader package.

**Environment.** `Vulkan_MrtMipFinalization` and `Vulkan_CubeFaceReadbackDependency` still `#error`
unless `CNA_RENDERER_VULKAN` is the *default* renderer, so they do not build in a multi-renderer
tree; a Vulkan-examples CMake question, recorded by WMG-0018 and untouched here.
`CNAEXT_NoPosixSetenv` belongs to the Wayland workstream.

**Not reopened, deliberately.** The per-**vertex** multi-stream split on the custom `ShaderEffect`
route is still refused by name (`RequireSingleStreamRouteEXT`). It is a different gap from
WMG-0021's, it was refused before this closeout and it is refused after, honestly and by name.
