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
| WEBGPU now | 961 | **879** | **0** | **82** |
| VULKAN (unchanged) | 961 | 929 | 0 | 32 |
| OPENGLES3 (unchanged) | 961 | 953 | 0 | 8 |

**No failures.** The 82 skips are what is left to implement, and every one names itself:

| count | what it skips for |
|---|---|
| 31 | `SupportsShadowSamplingEXT` is false — the stock WGSL effects do not sample a shadow map yet |
| 12 | no indirect draw route (`IndirectDrawTest` 6, `GpuInstanceCullerTest` 6) |
| 5 | `ComputeShaderTests` packages that offer only SPIR-V compute |
| 4 | `VolumetricFogTest` cannot select both of its packages |
| 3 | `EffectPassTest` / `ShaderPackageSelectionEXTTest` language-list expectations |
| 27 | skipped on Vulkan too (the inline-GLSL probes of WMG-0012 and the shared capability skips) |

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
