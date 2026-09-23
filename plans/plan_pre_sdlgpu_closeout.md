# Pre-SDL_GPU consolidation

The owner's brief of 2026-09-23: leave `next` clean, fully integrated and reproducibly tested
**before** the SDL_GPU modern-graphics workstream begins. This plan implements no SDL_GPU
functionality and is not an SDL_GPU implementation plan; it measures one, so the next agent starts
from a number rather than a guess.

Task IDs `PSG-0001`, `PSG-0002`, … . Every measurement below was taken on the private compositor
(`tools/platform/run_gpu_tests_private.sh`), never on the live desktop, on the machine's real
Radeon 780M.

## Status

| ID | Task | Status |
|---|---|---|
| PSG-0001 | Integrate `webgpu-modern-graphics` into `next` | 🟩 |
| PSG-0002 | A bounded-memory GoogleTest runner, because the suite outgrew the machine | 🟩 |
| PSG-0003 | The memory measurement that sets the runner's default | 🟩 |
| PSG-0004 | The full `CnaTests` measurement the WebGPU closeout could not take | 🟩 |
| PSG-0005 | The runner on a second renderer, so it is not WebGPU-shaped | 🟩 |
| PSG-0006 | `CNAEXT_LeakLoop`: reproduce, minimise, classify | 🟩 |
| PSG-0007 | SDL_GPU: what exists today | 🟩 |
| PSG-0008 | SDL_GPU: the three formerly dead tests, executed for the first time | 🟩 |
| PSG-0009 | SDL_GPU: modern, classic and CNAEXT-example baselines | 🟩 |
| PSG-0010 | SDL_GPU: backend, build size, warnings, test count | 🟩 |
| PSG-0011 | The handoff matrix, and the questions it cannot answer | 🟩 |
| PSG-0012 | SDL dependency semantics, verified | 🟩 |

---

## PSG-0001 — the WebGPU integration

`origin/next` had not moved since the WebGPU branch was cut, and `next` was an ancestor of it, so
nothing had to be reconciled or rebased. Verified rather than assumed: `git merge-base --is-ancestor`,
and a `git diff` between the merged tree and the validated feature tip that comes back empty.

| | |
|---|---|
| pre-merge `next` | `c576b5d25` |
| WebGPU feature | `7dd39a8cc` (34 commits, WMG-0001..0028) |
| merge commit | `8c8e5bdb6` — `merge(WebGPUModernGraphics): integrate webgpu-modern-graphics into next` |
| post-merge `next` | `8c8e5bdb6`, pushed |

Authorship audited across all 34 commits before the merge: author **and** committer are
`Robert Vokac <robertvokac@robertvokac.com>` on every one, with zero deviations, and a scan for
AI-attribution markers returns nothing. The merge style follows the convention already in `next`'s
history (`merge(VulkanModernGraphics)`, `merge(GpuTestIsolation)`, …): an explicit `--no-ff` commit
that keeps the feature history.

### Pre-merge regression

| suite | result |
|---|---|
| `CnaGraphicsExtTests` WEBGPU | **933 / 0 / 29** |
| `CnaGraphicsExtTests` VULKAN | **930 / 0 / 32** |
| `CnaGraphicsExtTests` OPENGLES3 | **954 / 0 / 8** |
| classic WebGPU, `ctest -R '^WebGPU'` | 12 failures of 201 |
| classic Vulkan, `ctest -R '^Vulkan_'` | 1 failure of 371 |
| CNAEXT examples, `-L CnaExt` | 2 failures of 32 |
| SDL-free Wayland surface (`cna_test_cnaext_ibl`) | 8/8 |
| SDL-free X11 surface (same) | 8/8 |
| `profile_dead_tests.py` | no test died on a profile refusal |

Each of the three modern rows is exactly its pre-closeout figure, and each of the failure counts is
the one `plans/plan_webgpu_modern_graphics.md` already accounts for. No new failure.

### Post-merge

Shards 0 and 1 of the modern WEBGPU suite returned **232** and **231** passes, identical to the same
shards pre-merge, before the machine's low-memory reaper stopped the remaining shards — the very
problem PSG-0002 exists to remove. Combined with the byte-identical tree, that is what the push
rests on, and it is stated here rather than dressed up as a completed rerun. The full post-merge
number is in PSG-0004, taken with the bounded runner.

---

## PSG-0002 — `tools/tests/run_gtest_bounded.sh`

The WebGPU closeout could not measure `CnaTests` on WEBGPU at all. Every attempt — four shards, then
twelve — was killed by this machine's low-memory reaper, and the closeout recorded the number as
owed rather than answered. This is the tool that pays it.

**The lever is how many tests share one process, not how many processes run at once.** That is a
measurement, not a preference: a two-shard run of the 962-test engine-layer suite was killed at test
351, while a five-shard run of the same suite on the same binary completed. A CNA test builds a
`GraphicsDevice`, and the WebGPU provider keeps per-device state that is not returned when the
device is destroyed, so cost accumulates *inside* a process and raising parallelism alone makes it
worse.

The runner therefore bounds both, and `--tests-per-shard` is the one with the load-bearing default:

```
tools/tests/run_gtest_bounded.sh [options] <gtest-binary> [-- <extra gtest args>]
    --tests-per-shard N   how many test cases share one process   (default 200)
    --shards N            exact shard count instead
    --max-parallel N      how many shards run at once             (default 1)
    --filter F            gtest filter, applied before sharding
    --out DIR             logs, XML and per-shard state
    --resume              skip shards that already completed
    --stop-on-fail        stop launching after a failure
    --allow-live-display  permit :0 / wayland-0
```

It creates no display. It composes with the existing private compositor, so **one** compositor
serves every shard rather than one per shard:

```
tools/platform/run_gpu_tests_private.sh --exec \
    tools/tests/run_gtest_bounded.sh --max-parallel 2 ./cmake-build-webgpu/CnaTests
```

Properties that are there because their absence is a way to report a wrong number:

* **A signal-killed shard fails the run and is named.** The reaper's kill is exactly the case where
  a naive aggregator reports a confident partial pass; here the summary says `KILLED` and calls the
  counts a lower bound.
* **Skips are counted per testcase, not from the XML root.** GoogleTest records a `GTEST_SKIP` as
  `result="skipped"` on the case and puts no `skipped` attribute on `<testsuites>` at all. The first
  version of this aggregator trusted the root and turned a true **933 / 0 / 29** into a confident
  **962 / 0 / 0** — every skip silently promoted to a pass. Caught by comparing against the
  text-mode run, which is why that comparison is recorded here.
* **XML first, text second.** The text summary is the fallback for a shard that died before gtest
  could write its XML — precisely the shard worth not losing.
* **Its own `TMPDIR` per shard**, so two concurrent shards cannot collide in scratch files.
* **It refuses `:0` and `wayland-0` by name** unless `--allow-live-display` is passed, because the
  runner opens no display and the only way it reaches the owner's desktop is by inheriting one.
* `--resume` re-aggregates from existing XML and skips completed shards.

Renderer-agnostic by construction: it reads nothing about the renderer and passes the environment
through. PSG-0005 proves that on a second one.

## PSG-0003 — the measurement behind the default

`CnaGraphicsExtTests`, same binary, private compositor:

| | WEBGPU | VULKAN |
|---|---|---|
| peak RSS over the same ~200 tests | **1115 MB** | **167 MB** |

and the full 962-test suite through the bounded runner at 200 tests per shard, one at a time:

| | |
|---|---|
| shards | 5 |
| peak summed RSS of test processes | **1144 MB** |
| wall clock | 253 s |
| result | **933 passed / 0 failed / 29 skipped**, matching the text-mode sharded run exactly |

So 200 tests per process holds WEBGPU — the most expensive renderer measured — to about 1.1 GB,
against the >30 GB an unsharded run reaches. The default is deliberately the *safe* number rather
than the fastest one, and `--tests-per-shard` raises it for a renderer that costs less.

## PSG-0004 — the full `CnaTests` measurement, taken

WMG-0003's baseline row was `10564 / 9998 / 63 / 503`, and the WebGPU closeout could not re-take it.
Through the bounded runner, 53 shards of 200 tests, one at a time, one private compositor:

| | baseline | now |
|---|---|---|
| total | 10564 | **10565** |
| passed | 9998 | **10246** |
| failed | **63** | **31** |
| skipped | 503 | **288** |
| shards killed | — | **0** |
| peak RSS | >30 GB, reaped | **716 MB** |
| wall clock | never finished | 1597 s |

Fewer failures than the baseline, and part of that is a real improvement in the *measurement*
rather than in the renderer: every shard is a fresh process, so a test no longer inherits whatever
the previous five hundred left behind. Stated here because it is the honest reading, not the
flattering one.

**No failure among the 31 is a regression from the integrated branch**, and that is measured rather
than argued. The two plausible candidates — `InstancedVertexColorTest.EffectWorldComposesAfterTheInstanceWorld`
and `OrdinaryDrawMultiStreamTest.SixteenBindingsCanSupplyAConsumedSemanticFromSlot15`, which sit
exactly where WMG-0021 changed instancing — were rebuilt and run at the pre-branch commit
`c576b5d25` in a worktree and fail there identically. Neither test file is touched by the branch.

## PSG-0005 — the runner on a second renderer

Run unchanged against VULKAN and OPENGLES3 in PSG-0009's regression below, at the same settings.
It reads nothing about the renderer, so "not WebGPU-shaped" is a property of its construction
rather than a claim; the second and third renderers are the evidence.

## PSG-0006 — `CNAEXT_LeakLoop`, classified and fixed

Reproduced at current `next` (WEBGPU only; VULKAN and OPENGLES3 pass), and already established
pre-existing at `63e208bed` by the WebGPU closeout.

It is **CNA's defect, not the provider's**, and the classification is measured:

```
gdb   WebGPUProgramLayoutEXT::PipelineLayout (this=0x0)
      from IssueDescriptorSpriteEXT
      p effect->valid_         -> true
      p (int)effect->contract_ -> 1445236928     <- a two-value enum
      p effect->programLayout_ -> empty
```

`contract_` cannot hold that value, so the object had been freed. ASan then named it exactly, and
only without the fix:

```
heap-use-after-free  WebGPURenderer.cpp:7088
    in WebGPURenderer::IssueSpriteWithCustomEffect
```

**Minimised**: it needs no cycles at all. A queued sprite or custom-effect draw holds its
`ShaderEffect` as a raw pointer, and this renderer replays a frame later, so disposing the effect
before the flush is the whole reproduction. `CNAEXT_LeakLoop` builds and destroys a `RenderPipeline`
per cycle and therefore walks into it on its **first** frame — it never was the thousand-frame soak
its name suggests.

**Fix**: `ForgetEffectEXT`, from the effect's own destructor, drops every queued draw that still
names it and counts the drop. Dropping rather than keeping alive, because REMED-GFX-167's
`keepAlive` route works for textures only because a samplable is shared-owned, while an
`IEffectRenderer` is owned uniquely by its XNA `Effect`; widening that is an interface change across
every renderer. A draw whose effect no longer exists cannot be honoured.

| | |
|---|---|
| `CNAEXT_LeakLoop` | **PASS** on WEBGPU, VULKAN and OPENGLES3 |
| ASan with the fix | 0 errors |
| ASan without the fix | `heap-use-after-free`, as quoted |
| regression | `WebGPU_EffectOutlivedByDraw`, 4/4, deterministic — no frame counts |

---

# The SDL_GPU handoff

Measured, not implemented. No SDL_GPU renderer behaviour was changed by this workstream.

## PSG-0010 — the configuration these numbers belong to

| | |
|---|---|
| build dir | `cmake-build-sdlgpu`, Release, `CNA_SHARED_LIBRARY=ON` |
| renderer / platform | `CNA_GRAPHICS_RENDERER=SDL_GPU`, `CNA_PLATFORM=SDL3` |
| pinned | `CNA_SDL_GPU_SHADERCROSS=OFF`, `CNA_SDL_GPU_COMPILED_EFFECTS=OFF` |
| **ShaderEffect** | **enabled** — configure found `/usr/lib/x86_64-linux-gnu/libshaderc.so.1`, so `CustomEffects` reports **true** here. Without libshaderc it reports false and a different set of tests skips; pin this before comparing runs |
| SDL_GPU driver | **Vulkan** |
| device | **AMD Radeon 780M (RADV PHOENIX)**, radv Mesa 25.0.7-2+deb13u1, Vulkan conformance 1.4.0.0 |
| shader format | SPIR-V only (`SDL_GPU_SHADERFORMAT_SPIRV`); shaders are hand-written GLSL compiled by the module's own `compile_shaders.py`, sharing nothing with `tools/shader_package/` |
| build size | **657 MB** |
| build | 0 errors, 2 warnings |
| ctest registrations | 384 total, **229** matching `^SdlGpu`, 32 `CnaExt`-labelled |
| display | private Weston + private rootful Xwayland throughout; never `:0` or `wayland-0` |

## PSG-0009 — the three baselines

| suite | pass | fail | skip |
|---|---|---|---|
| **modern** `CnaGraphicsExtTests` | **682** | **21** | **259** (962 total) |
| **classic** `ctest -R '^SdlGpu'` | **202** | **27** | — (229 total) |
| **CNAEXT examples** `-L CnaExt` | **11** | **1** | **20** (32 total) |

For scale, the same modern suite on the renderers that implement it: WEBGPU 933/0/29,
VULKAN 930/0/32, OPENGLES3 954/0/8.

The single CNAEXT example failure is `CNAEXT_NoPosixSetenv`, which names `::unsetenv` call sites in
`modules/platform/src/Wayland/` and belongs to the Wayland workstream — not SDL_GPU's.

The 27 classic failures, for the next workstream to own:

```
SdlGpu_2D_HeadlessDriver          SdlGpu_BackbufferFormat
SdlGpu_ConstructorExceptionSafety SdlGpu_Deferred_Scissor
SdlGpu_DepthBias                  SdlGpu_DepthlessCube_SpriteBatchCompatibility
SdlGpu_DrawLineTopology           SdlGpu_EasyGLOracle_depthstencilstate_stencil_twosided
SdlGpu_EasyGLOracle_draw_range_validation   SdlGpu_EasyGLOracle_surface_format_throws
SdlGpu_OcclusionQuery_Limitation  SdlGpu_Parity_compressed_cube
SdlGpu_Parity_compressed_cube_fallback      SdlGpu_Parity_dual_texture_terms
SdlGpu_RenderState                SdlGpu_RenderTargetCube_Formats
SdlGpu_RenderTargetCube_MsaaMip   SdlGpu_RenderTarget_SetData
SdlGpu_Smoke                      SdlGpu_SwapchainRecovery
SdlGpu_Texture2D_FormatMatrix     SdlGpu_Texture2D_FormatMatrixFallback
SdlGpu_TextureCube_FormatMatrix   SdlGpu_TextureCube_FormatMatrixFallback
SdlGpu_TextureFilterMipContract
SdlGpuIndexedDrawRangeTest.MutatingSourceBuffersAfterQueuingDoesNotChangeQueuedDraws
SdlGpuIndexedDrawRangeTest.RejectsIndexedRangesOutsideTheBoundBuffers
```

`SdlGpu_Parity_dual_texture_terms` is worth naming: WebGPU's twin was the XNA opaque-black
null-texture rule (WMG-0025), and SDL_GPU may well be the next renderer with that same gap.

## PSG-0011 — the modern gap matrix

Every modern `IGraphicsRenderer` member, checked in the source rather than inferred from a test:
**SDL_GPU overrides none of them.** It takes the base class's answer everywhere, which is uniformly
`false` / `nullptr` / no-op / `0`.

| group | SDL_GPU today |
|---|---|
| compute shaders (`CreateComputeShader`, `DispatchEXT`, `MemoryBarrierEXT`) | not implemented |
| storage buffers (`CreateStorageBufferEXT`, byte read/write/copy) | not implemented |
| storage textures (`CreateStorageTexture2DEXT`) | not implemented |
| `Texture2DArray` (`CreateTexture2DArrayEXT`) | not implemented |
| GPU timers (`CreateGpuTimerEXT`, `SupportsGpuTimerEXT`) | not implemented |
| modern limit queries (every `GetMax*EXT` / `GetMin*EXT`) | not implemented, all return the base constant |
| indirect draw (`SupportsIndirectDrawEXT`, both entry points) | not implemented |
| shadow sampling / IBL / debug markers | not implemented |
| descriptor or bind-group contract | **does not exist** — unlike Vulkan and WebGPU there is nothing to extend |
| graphics pipelines, render targets, MRT, samplers, bindings | **classic, implemented and broad** |
| instancing | classic stock families only; custom-effect instancing refuses by name |

That is a clean floor: nothing over-claims, so no false positive has to be unwound first.

### Two apparent contradictions that are not contradictions

Both were candidates until the contract was read. **Neither should be "fixed" without re-reading it**,
and this workstream deliberately did not touch them.

* `SupportsCapability(Texture3D)` is **true** while `SupportsTexture3DSamplingEXT()` is **false**.
  `IGraphicsRenderer.hpp:3153` makes the two deliberately distinct — the capability promises only
  "a volume can be uploaded and read back", the EXT query asks whether a bound volume "is actually
  SAMPLED by the shader", and it *"Defaults to false, the conservative answer... one that has not
  been measured under-claims rather than over-claims"*. A renderer that uploads volumes but has not
  been measured sampling them is supposed to look exactly like this.
* The renderer consumes SPIR-V everywhere, yet `SupportsShaderLanguageEXT(SpirV, *)` is **false**.
  The contract at `IGraphicsRenderer.hpp:2687` says "True only when the **implemented shader
  intake** consumes that exact pair". SDL_GPU's SPIR-V is its own baked-in stock shaders, not an
  intake; its only runtime intake is GLSL through `libshaderc`. `false` is the truthful answer for
  the question actually being asked.

The first job of the SDL_GPU workstream is to decide, for each, whether it is a capability-reporting
bug, a missing implementation, or an intentional distinction — with the contract open.

### Architectural questions the next agent must answer first

* **Does SDL_GPU expose compute at all?** SDL 3.4's GPU API has compute pipelines; nothing in CNA
  uses them, and no capability claims them. This gates storage buffers, storage textures, indirect
  culling and GPU-driven rendering — most of the 259 modern skips.
* **Which shader path?** The modern engine layer ships generated packages
  (`tools/shader_package/generate_shader_package.py`) in GLSL ES / desktop GLSL / SPIR-V / WGSL.
  SDL_GPU consumes SPIR-V but through its own module-local pipeline, and with a **different set
  convention** (vertex textures set 0, vertex UBO 1, fragment textures 2, fragment UBO 3) than the
  plain Vulkan convention CNA's Vulkan shaders use. Teaching SDL_GPU to consume the generated
  packages, or duplicating them, is the first real design decision.
* **What binding contract?** Vulkan and WebGPU each have an explicit descriptor/bind-group layer
  that an engine-layer `ShaderEffect` draws through. SDL_GPU has none; it starts from nothing on
  that axis.
* **Which capabilities can be represented exactly, and which must stay false?** The honesty queries
  (`ExecutesShaderEffectSourceEXT`, `SupportsShadowSamplingEXT`, `SupportsImageBasedLightingEXT`,
  `SupportsComputeShadersEXT`) exist so a pass reports a truthful refusal instead of drawing
  nothing; `MOD-1699` is the failure mode they prevent.

## PSG-0012 — SDL dependency semantics

Verified, not assumed, and already correct: `cmake/SdlAvailability.cmake:68-86` lists SDL_GPU among
the renderers that genuinely require SDL, and `CNA_ENABLE_SDL=OFF` with SDL_GPU selected is a
configure-time `FATAL_ERROR` that **names the renderer** and states that nothing is substituted. No
SDL-free native renderer gains an SDL dependency from SDL_GPU merely existing in the tree — the
WebGPU SDL-free trees of WMG-0024 are the evidence, and they still carry no SDL.

## PSG-0013 — regression after the infrastructure changes

This workstream changed generic test infrastructure (`cmake/UnitTests.cmake`, four shared example
fixtures, the bounded runner) and one renderer (`ForgetEffectEXT`). Re-measured through the bounded
runner afterwards:

| renderer | result | reference |
|---|---|---|
| WEBGPU | **933 / 0 / 29** | unchanged |
| VULKAN | **930 / 0 / 32** | unchanged |
| OPENGLES3 | **954 / 0 / 8** | unchanged |

No regression, and the three renderers are also PSG-0005's evidence that the runner is not
WebGPU-shaped.

---

## What is left for the SDL_GPU workstream

* **27 classic SDL_GPU failures**, listed in PSG-0009. Real renderer results, none of them a dead
  test any more.
* **21 modern failures and 259 modern skips**, which is what "no modern member is implemented"
  measures as.
* **The two contract questions** in PSG-0011, to be settled with the contract open rather than by
  making two booleans agree.
* **`CNAEXT_NoPosixSetenv`**, which is the Wayland workstream's and not SDL_GPU's.

Nothing in this plan implements SDL_GPU. That is the next workstream's first commit.
