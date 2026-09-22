# Vulkan and the modern CNA Graphics API, on real hardware

Workstream B of the owner's 2026-09-22 brief: implement the **existing** modern CNA Graphics API for
Vulkan, on the physical AMD Radeon 780M / RADV, through CNA's native X11 and Wayland platforms, without
regressing the classic Vulkan renderer. Workstream A (test isolation) is
`plans/plan_gpu_test_isolation.md` GTI-0006..0010.

Task IDs: `VMG-0001`, `VMG-0002`, … . Branch `vulkan-modern-graphics` from `origin/next` `7791f8fc7`
(which contains the classic closeout, `VKPAR-0018..0029`). Not merged, not pushed.

**What "the modern API" is here — the repository's answer, not the brief's list.** CNA has exactly one
modern graphics surface: the CNAEXT engine layer and its portable modern-GPU foundation,
`plans/plan_modern.md` (Phases 0–22) and `misc/CNAEXT.md`. Phase 22 (`MOD-2200`–`MOD-2266`) is where the
Vulkan implementation of that surface already lives, and it is marked ✅ through `MOD-2254`. This plan
does not re-implement it and does not add a second API. What it owns is the question the Phase 22
evidence left open: most of it was measured on llvmpipe under Xvfb, several rows say outright that RADV
"was deliberately not run", and all of it predates the `sw` merge into `next` (2026-09-13) that began
enforcing XNA's Reach profile. Measured again here on the Radeon, through private compositors, the
answer was: **implemented, but a large part of its own test coverage had silently stopped running**,
and behind that sat one real cross-layer defect (VMG-0006), three renderer defects exposed by synchronization
validation, the new conformance suite and the sanitizers (VMG-0013, VMG-0014, VMG-0017), and several
engine/example defects.

## Status

| ID | Task | Status |
|---|---|---|
| VMG-0001 | Inventory the existing modern API; re-baseline EasyGL and Vulkan on RADV | ✅ |
| VMG-0002 | One modern build tree: `cmake-build-cnaext` (Vulkan + EasyGL, CNAEXT, native Wayland, SDL-free, `libcna.so`) | ✅ |
| VMG-0003 | The Vulkan example/oracle suite builds and runs without SDL, on the native platforms | ✅ |
| VMG-0004 | 29 CNAEXT example programs skipped or died on the Reach profile on every renderer | ✅ |
| VMG-0005 | The engine layer's own gtest suite (951 cases) ran on a Reach device | ✅ |
| VMG-0006 | XNA's float-filtering rule (SOFTWARE-217) broke every HDR engine pass on every renderer | ✅ |
| VMG-0007 | `spirv-val` over every checked-in SPIR-V module | ✅ |
| VMG-0008 | `CNAEXT_RenderPipeline` / `CNAEXT_GltfPbr`: stale source-execution gates skipped work Vulkan does | ✅ |
| VMG-0009 | `InstancedRendererEXT` left its instance streams bound; every second frame threw | ✅ |
| VMG-0010 | `CNAEXT_GpuTiming` asserted llvmpipe's GPU/CPU ratio | ✅ |
| VMG-0011 | `CNAEXT_Showcase`: stale gate, a tangent-less PBR record, a shadow pass sampling itself, a vacuous check | ✅ |
| VMG-0012 | Renderer-neutral conformance: the gaps the shared suite leaves on Vulkan | ✅ |
| VMG-0013 | Synchronization validation over the modern suite; one renderer hazard class fixed | ✅ |
| VMG-0014 | A compute dispatch inside a render-target bind cycle erased every draw issued before it | ✅ |
| VMG-0015 | Modern stress on RADV: RSS, FDs, threads, validation, device loss | ✅ |
| VMG-0016 | The skinned shadow fixtures drew with no texture (was VMG-F1) | ✅ |
| VMG-0017 | What ASan and UBSan found: zero-byte copies on the indirect path, four test overreads | ✅ |
| VMG-0018 | Native X11 (SDL-free) and ASan/UBSan/LSan over the modern and classic Vulkan suites; one test reference cycle fixed | ✅ |
| VMG-0019 | Final classic, EasyGL and platform regression; result | ✅ |

---

## VMG-0001 — inventory and baseline

### The modern surface, and where Vulkan stands on it

Taken from the headers (`modules/graphics-ext/include/CNA/Graphics`, `modules/graphics/include/CNA`,
the CNAEXT members of `GraphicsDevice`/`ShaderEffect`), `docs/modern-gpu-baseline.md` and the live
capability profile the checked-in `cna_probe_modern_gpu_capabilities` prints on this machine. The
"Vulkan" column is the state **after** this workstream; its evidence is the row named.

| Group | Entry points | Vulkan (RADV) | Evidence |
|---|---|---|---|
| Capability discovery | `GraphicsCapability` 0–18, `RendererFeature` (32), `RendererLimit` (22), `RendererFormatUsage` (13 × 27 formats), `GetRendererCapabilityProfileEXT`, `…FeatureSupportEXT`, `…LimitEXT`, `…SurfaceFormatSupportEXT`, English report | implemented, device-derived | MOD-2203/2220–2222; `Vulkan_FormatLimitQueries`, `Vulkan_CapabilitySnapshot`, `Vulkan_ModernFeatureDiscovery` pass on RADV |
| Shader payloads | `ShaderLanguageEXT`, `ShaderStageEXT`, `SupportsShaderLanguageEXT`, `ShaderCodeEXT`, `ShaderPackageEXT`, `selectFor`, `ShaderDiagnosticEXT`, `ShaderCompilationExceptionEXT` | implemented: SPIR-V vertex/fragment/compute; every GLSL/HLSL/MSL/WGSL source language **refused** (truthfully — Vulkan runs no source) | MOD-2210–2217; VMG-0007 validates all 194 shipped modules |
| `ShaderEffect` (CNAEXT) | package/code constructors, uniforms, `SetTextureArrayEXT`, `SetStorageTextureEXT`, graphics SSBO reads | implemented for SPIR-V; source constructors accepted but not executed (`ExecutesShaderEffectSourceEXT() == false`) | `Vulkan_ShaderEffect_*` |
| Compute | `ComputeShader` (package/code), `dispatch`, scalar uniforms, `bindStorageBuffer`, `bindConstantBuffer`, `bindTexture`, `bindImage`, `bindStorageTexture`, `barrier` (compatibility only) | implemented | MOD-2241/2242/2244/2247–2251; `Vulkan_ComputeStorageBuffer`, `ComputeTest.Portable*` |
| Buffers | `StorageBufferDescriptor` (6 roles × CPU access), `StorageBuffer`, `StorageBufferT<T>`, `ConstantBufferT<T>`, ranged transfers, `copyTo` | implemented | MOD-2229–2231; `StorageBufferTest.*`, `ConstantBufferTest.*` |
| Textures | `Texture2DArray` (+ transfers, sampled binding), `StorageTexture2D` (15 exact formats), float/HDR `RenderTarget2D`/`RenderTargetCube` (9 formats), `Texture3D` sampling | implemented, per-format from device queries | MOD-2223/2225–2228/2234/2243/2244 |
| Draws | `DrawPrimitivesIndirectEXT`, `DrawIndexedPrimitivesIndirectEXT`, `DrawInstancedPrimitivesBaseInstanceEXT`, `IndirectDrawArguments(Indexed)` | implemented | MOD-2232/2245/2250; `Vulkan_IndirectDraw` |
| Timing / debug | `GpuTimer`, debug-utils labels/markers, structured validation logging | implemented | MOD-2246; `Vulkan_GpuTimerDebug`, `GpuTimerTest` |
| Ordering / sync / lifetime | ADR 0001: one device order, internal usage tracking, fence-retired lifetime, no routine global stalls | implemented | MOD-2247–2254; `Vulkan_ModernResourceLifetime`, `…AllocatorStress`, `…Recovery` |
| Display output | `DisplayColorSpace`, `SetDisplayColorSpaceEXT` | sRGB only — scRGB/HDR10 swap chains **refused** (truthful) | MOD-2239c |
| Engine layer on top | `RenderPipeline`, every post-process pass, shadows (directional/cascaded/point/spot), skybox/atmosphere, IBL, prepass, clustered forward, GPU culling, particles, auto-exposure, WBOIT, instancing/LOD | runs on Vulkan through generated SPIR-V packages (MOD-2218–2239z) | `CnaGraphicsExtTests` 928 / 0 fail / 32 skip of 960, CNAEXT examples 35/35 (VMG-0019) |
| Not applicable | a command-buffer, barrier, descriptor, queue or native-handle surface | CNA deliberately exposes none (ADR 0001, MOD-2202) | — |

Explicitly **not** in the modern API, so not implemented here: mesh/task shaders, bindless, VRS, sparse
resources, ray tracing, asynchronous compute (MOD-2266); nothing was added because Vulkan could.

### Environment

| | |
|---|---|
| GPU | AMD Radeon 780M, RADV PHOENIX (every Vulkan test log: `[Vulkan] GPU: AMD Radeon 780M (RADV PHOENIX)`) |
| Driver | Mesa 25.0.7-2+deb13u1 |
| Vulkan | loader 1.4.305; instance created at 1.1 (`VulkanRenderer.cpp`) |
| Validation | `VK_LAYER_KHRONOS_validation` (system package), loaded by every Debug `VulkanRenderer` |
| EasyGL reference | OpenGL ES 3.2 Mesa 25.0.7 (radeonsi), EGL on native Wayland |
| Displays | `tools/platform/run_gpu_tests_private.sh`: private headless Weston + private rootful Xwayland with DRI3. Never `:0`, never `wayland-0`, never Xvfb for Vulkan |
| SPIR-V tools | SPIRV-Tools 2025.1 (`spirv-val`), unpacked into `~/deps/spirv-tools` without sudo |

### Baselines — before any change of this workstream

Same binaries, runtime renderer selection (`CNA_GRAPHICS_RENDERER`), private runner, RADV.

| Suite | EasyGL (GLES 3.2) | Vulkan (RADV) |
|---|---|---|
| `CnaGraphicsExtTests` (the engine layer's 951 shared cases) | 905 pass / **20 fail** / 26 skip | 886 pass / **19 fail** / 46 skip |
| CNAEXT example oracles (35 registrations) | not measured separately — they skip on the same line of code for every renderer | 11 pass / **20 skip** / **4 fail** |
| `Vulkan_*` (381 incl. 12 CNAEXT-only, native Wayland, SDL-free) | — | **380 / 1** (`Vulkan_DrawRangeValidation`, known) |

Almost every red and skipped entry above had one of two causes, neither of them Vulkan:

1. **The profile.** The standalone `GraphicsDevice()` the suite uses and every example's
   `GraphicsDeviceManager` default to `GraphicsProfile::Reach`, which CNA has enforced the way XNA does
   since SOFTWARE-213/217 (merged into `next` 2026-09-13, after the Phase 22 evidence was recorded):
   2048-texel textures, no volume textures, one render target, no back-buffer readback. The engine
   layer needs all of those. → VMG-0004, VMG-0005.
2. **XNA's float-filtering rule** reaching the engine layer's own draws. → VMG-0006.

## VMG-0002 — the build tree

`cmake-build-cnaext` (the CNAEXT tree `CLAUDE.md` names): `CNA_GRAPHICS_RENDERER=VULKAN`,
`CNA_GRAPHICS_RENDERERS="VULKAN;OPENGLES3"` (EasyGL as the runtime-selected reference, with a real GLES
context over EGL — the native X11 backend would have fallen back to a desktop GLX context for an ES
request), `CNA_PLATFORM=WAYLAND`, `CNA_ENABLE_SDL=OFF`, `CNA_AUDIO_PLATFORM=NULL`, `CNA_CNAEXT=ON`,
Debug (so the validation layer loads), `CNA_SHARED_LIBRARY=ON`, ccache launchers. Only the test targets
that run were built (`cmake --build … --target …`, 413 of them), never the whole tree.

Size: 38 MB configured → **2.5 GB** built (`libcna.so` 210 MB Debug, a test executable ~1 MB), against
the brief's 15 GB alarm line. SDL-freeness: `readelf -d libcna.so` lists no SDL; the only X libraries in
`ldd` arrive through FFmpeg's `libva-x11`, not through CNA. `ccache` for the first full build: 22.8 %
hits (a new configuration's compile definitions), all later incremental builds are minutes.

## VMG-0003 — the Vulkan oracles without SDL

`modules/renderers/vulkan/examples/CMakeLists.txt` skipped the **entire** Vulkan example suite —
the classic suite and the twelve modern oracles (`Vulkan_ComputeStorageBuffer`, `…IndirectDraw`,
`…ModernResourceLifetime`, …) — whenever SDL was off (NPV-0106), because its test macro named
`SDL3::SDL3`. No example source includes SDL; the one SDL use, `PixelTestGame.hpp`'s display probe, has
had a `CNA_EXAMPLES_NO_SDL` switch since DX12-0005. The macro now links SDL where it exists and defines
that switch where it does not — the Direct3D suites' idiom. Two sources needed more:

* `viewport_reset_after_resize_test.cpp` asked SDL for the window size; without SDL it asks the
  platform's `GameWindow::ClientBounds` (the same logical size). SDL builds are unchanged.
* `Vulkan_RealWindowResize` plays a user dragging the window edge by resizing the SDL window behind the
  game's back — there is no public API to do that — so it is registered only where SDL is. The modern
  swap-chain recreation keeps SDL-free coverage in `Vulkan_ModernResourceLifetime` and
  `Vulkan_ModernAllocatorStress`, which resize through `GraphicsDeviceManager`.

**Result:** the SDL-free native-Wayland tree registers **381** `Vulkan_*` tests (370 classic, the 12
CNAEXT-gated modern ones, minus the SDL-only resize) and passes **380/381** on RADV, the failure being
the known `Vulkan_DrawRangeValidation`, 0 validation messages, every device the Radeon.

## VMG-0004 — the example oracles were dead

With the private runner and GTI-0007's classifier, the CNAEXT example oracles showed the SOFTWARE-213
pattern again: 17 caught the `GetBackBufferData` refusal and printed "SKIP: this renderer has no
readable back buffer", three aborted on it, two more ("no RGBA32F render targets", "no multiple render
targets") skipped on other Reach limits. On **every** renderer, since 2026-09-13.

All 29 programs with a `GraphicsDeviceManager` (not the capability probe, which draws nothing) now ask
for HiDef where the adapter offers it, and their back-buffer skip lines carry the exception text, so a
profile refusal can never again pass for a capability boundary (GTI-0007 names it). Measured after
VMG-0006..0011: **EasyGL 35/35, Vulkan 35/35**, 0 validation messages.

## VMG-0005 — the shared suite ran on a Reach device

`GraphicsDevice()` (the CNAEXT standalone constructor) asks for `GraphicsProfile::Reach` and always
has; what changed on 2026-09-13 is that Reach started meaning something. The 951-case suite failed on
the 4096 shadow atlases, the 3D LUTs, MRT, float targets and back-buffer reads.

`CnaTest::EngineLayer::HiDefDevice` (`EngineTestSupport.hpp`) is the same headless standalone device
at HiDef, and all 567 standalone-device declarations in the 78 test files use it. The public default
was deliberately **not** changed: it is a behaviour thousands of XNA-layer tests and applications
construct, and nothing documents it as the engine layer's device.

## VMG-0006 — XNA's float-filtering rule and the engine layer

SOFTWARE-217 recovered from Microsoft XNA 4.0 that `Single`, `Vector2`, `Vector4`, `HalfSingle`,
`HalfVector2`, `HalfVector4` and `HdrBlendable` are **point-filter-only** in `VerifyCanDraw`, and made
every XNA draw — SpriteBatch included — enforce it. That is right for XNA code. But the CNAEXT engine
layer is not XNA code: its passes are built on hardware-filtered HDR intermediates (bloom's pyramid,
tonemap, FXAA, the blit chain, WBOIT's resolve), they reach the GPU through SpriteBatch, and they
already ask `GraphicsCapability::HalfFloatTextureLinearFiltering` for their fallback (`BloomPass`'s
manual-filter path). Once both were true, **every HDR engine pass threw** "The active GraphicsProfile
does not support filtering SurfaceFormat 19" on every renderer that can filter — 19 shared cases on
Vulkan, 25 on EasyGL, four example programs.

**The decision, the smallest one that keeps both contracts:**

* `CNA::Internal::EngineLayerFloatFilteringScope` (internal header, a `friend` of `GraphicsDevice`, not
  public API): while one is alive, a draw may use a non-point filter on one of the seven formats
  **exactly when the live renderer filters that format** — the detailed per-format `Filterable` fact
  where the renderer classifies it, otherwise, for the half formats, the capability they are documented
  under; an unclassified 32-bit float format is not filterable (unknown is never support).
* `FullscreenPass` — the one chokepoint every engine fullscreen pass draws through — opens the scope,
  and where the renderer cannot filter the source's format it reads it with `PointClamp` (exact for the
  same-size passes, the documented degradation for resampling ones) instead of throwing.
* An XNA draw outside a scope is unchanged: it still gets XNA's `NotSupportedException`. The detailed
  profile's `Float16TextureLinearFiltering` entry now says so in its note.

**Evidence:** `CnaGraphicsExtTests` after VMG-0005 alone (HiDef devices): Vulkan 898 / 21 / 32, EasyGL
917 / 27 / 7 — the profile failures gone, the filtering refusal now reached in 19 and 25 cases. After
VMG-0006: **Vulkan 917 pass / 2 fail / 32 skip**, **EasyGL 943 / 1 / 7**; 0 validation messages. The 32 Vulkan skips are genuine: tests that feed
arbitrary GLSL source (Vulkan executes none), legacy GLSL-ES-only payloads, and the positive-capability
halves of refusal tests. The remaining failures were the skinned shadow fixtures, VMG-0016.

## VMG-0007 — every shipped SPIR-V module validated

`tools/vulkan/validate_spirv_payloads.py` extracts every `uint32_t NAME[] = {…}` array that starts
with the SPIR-V magic number — the renderer's own `spirv_shaders.hpp`, every generated
`*ShaderPackage.generated.hpp`, inline test modules — and runs `spirv-val --target-env vulkan1.1`
(the environment `VulkanRenderer` creates). **194 modules, all SPIR-V 1.0, 0 invalid.** Negative control:
one corrupted word is reported ("Invalid capability operand"). Registered as `SpirvPayloadValidation`
(exit 77 without spirv-val, like the package reproducibility gates).

## VMG-0008 — two examples skipped work Vulkan does

`CNAEXT_RenderPipeline` and `CNAEXT_GltfPbr` gated their shaded halves on the renderer-wide
`ExecutesShaderEffectSourceEXT()`. That was the right question before MOD-2218–2239, when the passes
were GLSL source; since then bloom, tonemap, FXAA and the shadow caster ship SPIR-V, and the question is
each pass's own `isSupported()` — the answer the pipeline itself uses. Both now ask it, and run fully on
Vulkan.

## VMG-0009 — `InstancedRendererEXT` and the second frame

`draw()` bound its instance and tint streams and left them bound, so the next frame's
`setInstances()` uploaded into a buffer still set on the device — which XNA refuses ("The vertex buffer
resource is in use") — and `CNAEXT_InstancingAndLod` threw on its second frame on every renderer.
`draw()` now restores the caller's vertex and index bindings on every exit, a throwing draw included.
New `TrianglePart.EveryFrameCanUploadAgainAndTheCallersBindingsComeBack` passes on both renderers and
**fails** with the old `InstancedRendererEXT.cpp` (checked).

## VMG-0010 — `CNAEXT_GpuTiming`

Its check C required the GPU total to be 0.70–1.30 of a CPU wall clock that also brackets submission
and a forced read-back. That was llvmpipe's ratio (a 47 ms chain swamping fixed cost). On the Radeon the
chain is 0.63 ms of GPU inside 4.3 ms (EasyGL 0.54 in 2.0), and the two renderers agree pass by pass
(Bloom 0.31 vs 0.25 ms, Tonemap 0.11 vs 0.10 at 720p). The check now holds what it said it was for —
the GPU ranges lie inside the wall clock (≤ 1.30) and on its unit scale (≥ 0.01, a unit slip moves it
1000×) — instead of one machine's performance.

## VMG-0011 — `CNAEXT_Showcase`

Un-gated the same way as VMG-0008, it exposed three more things, fixed in the example:

* **A tangent-less PBR record.** Vulkan's stock PBR pipeline refuses a record with no `Tangent` input
  rather than read an unbound attribute (VULKAN-148; "Vulkan PbrEffect requires vertex stride 48 or
  60"); EasyGL draws it through GL's generic-attribute default. The hero box now uses the documented
  48-byte PBR record. The refusal itself is recorded as boundary VMG-B1.
* **A shadow pass that sampled itself.** The example passed its whole `drawScene()` as
  `RenderPipeline::setShadowScene`'s caster callback, which the API documents as "must draw only
  geometry". Its `Apply()` calls replaced the caster program with the scene's receiver effects, so the
  2048² `R32_SFLOAT` map was filled with shaded colour while the ground sampled it — the validation layer
  reported the map read in `COLOR_ATTACHMENT_OPTIMAL` inside its own pass (VUID-…-imageLayout-00344),
  GL did it silently. Identified by tracing image handles, then fixed the way MOD-2035 fixed the same
  mistake for the prepass: a caster-only callback. The shadow check went from 15 935 "darker" pixels
  (the artefact) to 3 826 (the box's real shadow).
* **A vacuous check.** "ACES compresses the highlights the untonemapped frame clips" held as `0 <= 0` —
  nothing clips at this exposure. It now also requires ACES to change the frame (64 960 of 65 536
  pixels on Vulkan, 64 987 on EasyGL).

## VMG-0012 — renderer-neutral conformance

Six generic compute cases of the shared suite (dispatch geometry, uniforms, validation, image binding)
skip on Vulkan only because their payload is a legacy GLSL ES string. `ModernGpuConformance`
(`ModernGpuConformanceTests.cpp`) asserts the same kind of semantics through one portable package per
program — generated GLSL ES + SPIR-V (`shaders/modern_conformance`, `tools/shader_package`) — so every
renderer that has the API runs identical checks:

| Case | What must hold |
|---|---|
| `ADispatchCoversExactlyItsThreeDimensionalGrid` | a 3×2×2 dispatch of 4×2×2 groups writes exactly its 192 global ids; a one-group dispatch writes its 16 and nothing else |
| `UniformsAndUploadsKeepCallOrderAcrossReadModifyWriteDispatches` | three RMW dispatches, each with a new upload and a new `uScale`, no read between: exact in-order sums; nothing past `uCount` touched |
| `ChainedDispatchesSeeEachOthersWritesWithoutABarrier` | A→B→C→A through one rebound program, no `barrier()` call: every link read the previous one's writes |
| `AcceptedWorkSurvivesDisposalOfItsProgramAndInputs` | the program and its input are disposed right after the dispatch; the result is intact (ADR 0001) |
| `ManySmallUploadsInOneFrameAreEachConsumedInOrder` | 64 upload→dispatch pairs, one frame, no reads: each dispatch saw its own upload |
| `BufferRangesCopiesAndLargeUploadsAreExact` | partial update at an odd offset, `copyTo` between odd offsets, 4 MiB round trip — byte for byte |
| `AStorageImageHoldsExactlyWhatComputeWrote` | an rgba8 `StorageTexture2D` holds exactly what compute stored |

Every case that runs counts its checks and fails if it executed none; the process prints
`[CONFORMANCE] modern GPU: N case(s) ran M check(s), K skipped`.

**Result:** Vulkan (RADV) **7/7, 12 checks**; EasyGL **6/7, 11 checks**, `AStorageImage…` skipped
because EasyGL has no compute image binding (its detailed profile says so). 0 validation messages.
×100 repeats in one process (700 device lifecycles) pass as well (VMG-0015).

**It found a portability gap on its first Vulkan run:** `ComputeShader::setUniform` binds a scalar by
its SPIR-V push-constant **member name**, and `tools/shader_package` compiled only at shaderc's
`performance` level, which drops `OpMemberName` — so no tool-built package could use named scalar
uniforms on Vulkan ("every scalar push constant needs an OpMemberName"). The engine's own packages had
been working around it (packed vectors, constant buffers). The tool gains an opt-in manifest field,
`"optimization": "zero"`, recorded as `kCompilerOptimization`; the default is unchanged and all 16
existing packages regenerate byte for byte (`*ShaderPackageReproducibility`, 17/17 with the new one).

## VMG-0013 — synchronization validation

The layer's own setting (`VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true`, message limit off) over the
Debug renderer, whose messenger turns every warning or error into a `[Vulkan Validation]` line that the
ctest output gate fails. Positive control first: with informational reporting the layer prints
"Current Enables: VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT".

First run (54 modern ctests: the Vulkan modern oracles, all CNAEXT examples; plus the 959-case shared
suite): **one hazard class, 18 reports** — WRITE_AFTER_WRITE at `vkCmdBeginRenderPass` on attachment
2 (3 with a velocity target), an MRT pass's depth attachment transitioned `UNDEFINED →
DEPTH_STENCIL_ATTACHMENT_OPTIMAL` while the previous pass on the same depth image had written it. From
every depth/normal-prepass user (`CNAEXT_Ssao`, `CNAEXT_Showcase`, Decal/DepthNormalPrepass/
PerObjectVelocity/SsaoFromRealPrepass cases). `GetOrCreateMRTRenderPass`'s incoming external dependency
had no depth half; the single-target render-target passes already carry exactly that, and now the MRT
pass does too.

After the fix: both examples and the 32 affected shared cases run hazard-free under sync validation;
`Vulkan_*` without sync validation stays **380/381** with 0 messages. The full sync pass after all fixes
is in VMG-0019.

## VMG-0014 — a dispatch inside a bind cycle erased the draws before it

Found by the conformance case added for it, `ClassicDrawingIsUnchangedByInterleavedCompute`: the same
frame of classic drawing into a render target, once plain and once with a compute dispatch before,
between and after its two draws. On Vulkan **409 of 1,024 pixels differed** — the first triangle was
gone. EasyGL, which has no native render pass to leave, drew both.

Compute and transfer commands cannot be recorded inside a Vulkan render pass, so a modern command issued
while a target is bound ends the native pass and a new one begins after it (MOD-2247). That split
advanced the logical segment exactly as a fresh `SetRenderTarget` does, and the next segment was
recorded with the target's **own** pass. For a `DiscardContents` target that pass clears colour and
depth on entry and throws depth — and, multisampled, the colour samples too — away on exit. One public
bind cycle became two, and the second erased the first.

Fix, in `VulkanRenderer`: `SplitRenderPassForModernCommandEXT` now marks the segment it closes as a
split head, which stores every attachment, and the one it opens as a continuation, which loads every
attachment and folds no clear into its load action. It marks them only when the closed segment has
content (or is itself a continuation), so a dispatch issued straight after the bind still gets the
bind's own load action. `RecordCommandBuffer` picks the pass by those marks (`SplitRenderPassEXT`) for all
three render-target families: the single-sample and MSAA passes gain a "store everything" variant of
their discarding pass, and the MRT pass takes a split role (1 head, 2 continuation). Each variant
differs from the target's own pass only in load/store operations and layouts, which render-pass
compatibility ignores, so the target's framebuffer and every pipeline built for it are reused. It is
substituted only when the target's pass is the ordinary variant of the same shape. `PreserveContents`
targets were already correct, because their pass loads and stores. The backbuffer needed nothing:
every backbuffer cycle after the frame's first already takes the swapchain pass's LOAD variant.

**Evidence.** The case runs four shapes: single-sample `DiscardContents`, single-sample
`PreserveContents`, 4× MSAA and a two-target MRT set. It draws the **near** triangle first, so a
continuation that lost depth would let the far one through where they overlap, and it requires both
triangles present with none of the overlap drawn twice.
* Negative control, with the marking disabled: 409 / 423 / 409 pixels differ for single-sample, MSAA
  and MRT; `PreserveContents` passes.
* With the fix: 10 checks pass on Vulkan under synchronization validation with 0 messages, and pass on
  EasyGL.

## VMG-0015 — stress on RADV

`run_gpu_tests_private.sh --exec` around a throwaway monitor sampling `/proc/<pid>` every 0.25 s:

| Run | Duration | RSS (KiB) | FDs | Threads | Validation / device lost |
|---|---|---|---|---|---|
| `Vulkan_ModernAllocatorStress` (2,048 frames, 2,048 dispatches, 32 real resizes, per-frame allocation/disposal) | 12 s | 2nd quarter 151,734 → last quarter 151,743 | ≤ 19 | ≤ 11 | 0 / 0 |
| the same with synchronization validation | 12 s | 152,344 → 152,904 | ≤ 19 | ≤ 12 | 0 hazards / 0 |
| `CNAEXT_LeakLoop` (1,000 frames, 50 resizes, every subsystem on) | 325 s | warm-up to 366,400, then **flat at 372,848 for the last 40 %** | ≤ 19 | ≤ 12 | 0 / 0 |
| `ModernGpuConformance` ×100 (700 device create/dispatch/destroy cycles) | 219 s | 136,368 → 136,862 | ≤ 31, final 8 | ≤ 11, final 8 | 0 / 0 |

No progressive growth in RSS, file descriptors or threads; FDs and threads return to baseline between
device lifecycles; every run exits 0.

## VMG-0016 — the skinned shadow fixtures drew with no texture

Formerly VMG-F1: `ShadowVisibilityTest.SkinnedEffectReceivesTheShadow` (Vulkan) and
`.ASkinnedMeshShadowsItself` (Vulkan and EasyGL) saw a black mesh, which looked like a stock-effect
defect in both renderers. It is not one. `SkinnedEffect` always samples its texture, and these
fixtures never set one. An unbound stock-effect texture samples opaque black: that is XNA's behaviour
and CNA's settled rule (graphics shared cleanup). `EnableDefaultLighting()` only appeared to "fix" it
because specular light is added on top of the black texel. `BasicEffect` drew on the same buffer
because its texture is off by default.

The fixtures now bind a 1×1 white texture, so they test the shadow they are named for. Both cases
pass on both renderers, and the shared suite has no failures left on either (VMG-0019).

## VMG-0017 — what ASan and UBSan found

The sanitizer runs (VMG-0018) found one renderer defect and four test overreads.

* **Zero-byte copies to a null pointer (renderer, UBSan `nonnull-attribute`).** An indirect draw
  (`DrawPrimitivesIndirectEXT`, `DrawIndexedPrimitivesIndirectEXT`, through `QueueIndirectDrawEXT`)
  queues its draw with a CPU-side count of zero. The draw's snapshot then `memcpy`s zero bytes into
  an empty `std::vector`'s `data()`, which may be null — undefined behaviour even at size 0. It was
  reached by `Vulkan_IndirectDraw`, `CNAEXT_GpuDriven` and `GpuInstanceCullerTests`. It is now guarded
  at the five snapshot sites a zero count can reach (built-in, custom-effect and instanced routes).
  Nothing observable changed on the drivers here, but the next compiler was entitled to.
* **Stack overreads in three Vulkan oracles (tests, ASan `stack-buffer-overflow`).**
  `Texture2D::SetDataRGBA(data, pixelCount)` takes a pixel count. `vulkan_effect_bound_texture_test.cpp`
  (two sites) and `vulkan_modern_resource_lifetime_test.cpp` (one) passed the byte count 4 for a
  one-pixel array, so they read 12 bytes past it. Every other call site passing 4 was checked, and all
  sixteen hold 16-byte (four-pixel) arrays.
* **A histogram overrun in the shared suite (test, ASan `heap-buffer-overflow`).**
  `SsaoFromRealPrepassTest.ThePrepassWritesARangeOfDepthsRatherThanOneValue` bins the decoded depth
  of every pixel into `seen[0..255]`. A cleared (all-ones) pixel decodes to 1 + 1/255 + …, just past
  the far plane, so it rounded to 256 and read and wrote one element past the vector. The decode is
  right for the encoding; the bin index is now clamped.

## VMG-0018 — native X11 and the sanitizers

One tree answers both questions: `build-probe/vmg-x11-asan`, with
* `CNA_PLATFORM=X11` (CNA's own Xlib backend), `CNA_ENABLE_SDL=OFF`, `CNA_GRAPHICS_RENDERER=VULKAN`,
  `CNA_AUDIO_PLATFORM=NULL`, CNAEXT on, `libcna.so`;
* `CNA_SANITIZE=address,undefined`, Debug, so the validation layer loads;
* `CNA_SANITIZE_OPTIMIZATION=O0`, because at `-O1` GCC 14's `-Wmaybe-uninitialized` fires inside
  libstdc++'s `<regex>` under sharp-runtime's `-Werror`, which is a false positive.

The tree is 4.5 GB. It runs on the private rootful Xwayland of `run_gpu_tests_private.sh` (DRI3, RADV),
with `halt_on_error=1` for both sanitizers so that any report fails the test.

**First run** (`Vulkan_*`, `CNAEXT_*`, `Ascii*`: 416; plus the 960-case shared suite): 8 ctest failures,
and the shared suite stopped at its first report. That is VMG-0017's renderer defect and four test
overreads, plus four environmental results:

| Test | Result | Class |
|---|---|---|
| `Vulkan_FullscreenField` | the X11 backend refuses `BorderlessFullscreen` on a display with no EWMH window manager, and the private Xwayland runs none | environment. With openbox (`~/deps/openbox`) it **passes** under ASan, as does `CNAEXT_Settings_Compile_Run` |
| `CNAEXT_Settings_Compile_Run` | *Not Run*: its target was not in the probe's build set | built, then passed |
| `CNAEXT_LeakLoop` | 3 × 1,000 frames exceed the 600 s ctest limit at `-O0` under ASan | run on its own with a one-hour limit, below |
| `Vulkan_DrawRangeValidation` | the known classic item (VMG-B2) | unchanged |

**After VMG-0017:**
* `Vulkan_*`, `CNAEXT_*`, `Ascii*`: **412 / 415** (`CNAEXT_LeakLoop` run separately). The three are
  the three environmental rows above; both of those two pass with a window manager.
* Shared suite: **928 pass / 0 fail / 32 skip of 960**, which is exactly the unsanitized Wayland
  result, with **0** sanitizer reports and **0** validation messages.
* `CNAEXT_LeakLoop`, run on its own with a one-hour limit: 3 × (1,000 frames + 50 resizes) in
  14 min 7 s. The pipeline held a flat 98,304 bytes from frame 10 to the end, with **0** sanitizer
  reports.

**LeakSanitizer.** Setup:
* `detect_leaks=1` with `tools/platform/lsan_x11_mesa.supp`, whose only entry is Mesa's GLX client and
  so irrelevant to Vulkan;
* lavapipe only (`VK_DRIVER_FILES=…/lvp_icd.json`), because the RADV ICD leaks 2 × 128 bytes inside
  `vkCreateInstance` with no CNA code involved (VMG-E2).

Positive control first: a three-line program leaking 4,242 bytes, built with the same sanitizer flags
and run under the same options, is reported and exits 1.

* The twelve modern Vulkan oracles plus every CNAEXT example except `CNAEXT_LeakLoop`: **42 / 42**, on
  35 llvmpipe devices, **0** leak reports.
* The shared suite (`MESA_VK_WSI_DEBUG=noshm`, see below): one leak, 256 bytes in three allocations, all
  indirect. It was a reference cycle in `StorageTexture2DTest.ComputeBindingValidatesSlotDeviceAccessAndRendererAcceptance`'s
  own fakes: the recording compute renderer keeps the bound native texture in shared state that the
  texture itself owns. The sibling retention test already broke that cycle at its end, and this one now
  does too (commit VMG-0018). After the fix: **928 / 0 / 32 of 960**, on 658 llvmpipe devices, with
  **0** leak reports, **0** X errors and **0** validation messages.

The first attempt at the shared suite hung, at device 940 or so, in `vkDestroySwapchainKHR`: lavapipe
joins its "WSI swapchain event" thread, which was waiting in `xcb_wait_for_special_event`.
* On the private Xwayland, **every** lavapipe present had its MIT-SHM `ShmAttach` (request 130.1)
  refused with `BadAccess`, and the DRI3 `FenceFromFD` for the pixmap that never existed (147.4)
  failed with `BadDrawable` — 1,045 of each.
* A present whose pixmap never existed never completes, so one teardown lost the race and waited
  forever.

That is this sandboxed display environment together with a robustness gap in Mesa's software WSI: the
attach failure is asynchronous and unchecked. It is not CNA. RADV presents through DRI3 buffers and
never takes the path, and the ctest part did not hit it. `MESA_VK_WSI_DEBUG=noshm` makes lavapipe
present with `PutImage`: 0 X errors, and the suite runs to completion.

## VMG-0019 — final regression

All numbers below come from RADV via the private runner, after every change of this workstream.

| Suite | Tree / platform | Before (VMG-0001) | After |
|---|---|---|---|
| `Vulkan_*` classic | `cmake-build-vulkan` (SDL3, x11 driver, private Xwayland) | 369 / 370 | **369 / 370** (`Vulkan_DrawRangeValidation`); 0 validation messages; no leftover processes |
| `Vulkan_*` incl. the 12 modern oracles | `cmake-build-cnaext` (native Wayland, SDL-free) | 380 / 381 | **380 / 381** (same one) |
| CNAEXT example oracles (35) | `cmake-build-cnaext`, Vulkan | 11 pass / 20 skip / 4 fail | **35 / 35**, 0 skip, 0 validation messages |
| CNAEXT example oracles (35) | `cmake-build-cnaext`, EasyGL | — | **35 / 35** |
| `CnaGraphicsExtTests` | Vulkan | 886 / 19 fail / 46 skip of 951 | **928 / 0 / 32 of 960** |
| `CnaGraphicsExtTests` | EasyGL (GLES 3.2) | 905 / 20 fail / 26 skip of 951 | **952 / 0 / 8 of 960** |
| `ModernGpuConformance` | Vulkan / EasyGL | — | **8 / 8** / **7 / 8** (storage image: capability skip) |
| the same suites under ASan + UBSan, and LSan on lavapipe | native X11, SDL-free | — | clean after VMG-0017/0018 (see VMG-0018) |

Every remaining skip names a capability boundary. On Vulkan's 32 these are:
* 17 tests that need caller-supplied shader source compiled or executed (Vulkan executes packaged SPIR-V
  only);
* 7 legacy GLSL-ES-only payloads;
* 3 "failing shapes" of the half-float depth bisection that cannot be built here;
* 5 refusal tests whose capability the renderer has (compute, GPU timer ×3, indirect draw).

EasyGL's 8 are: 5 refusal halves, the one SPIR-V-only case, and 2 cases that need a compute image binding, which EasyGL lacks. The 9 new shared cases are
the 8 conformance cases and one `InstancedRendererEXT` case.

## Boundaries and open items

| ID | Item | Class |
|---|---|---|
| VMG-B1 | Stock `PbrEffect` on Vulkan refuses a vertex record without a `Tangent` input; EasyGL accepts it via GL's generic attribute default | Vulkan boundary (deterministic refusal, VULKAN-148's rule) |
| VMG-B2 | `Vulkan_DrawRangeValidation` | known classic item (VKPAR closeout), unchanged |
| VMG-B3 | Vulkan executes packaged SPIR-V only, so the 17 shared cases built on caller-supplied GLSL source and the 7 on legacy GLSL-ES payloads skip there | renderer boundary, reported by `ExecutesShaderEffectSourceEXT()` and each payload's dialect |
| VMG-E1 | On native X11 with no EWMH window manager, the X11 backend refuses `BorderlessFullscreen`, and the private runner's Xwayland runs none; `Vulkan_FullscreenField` passes with openbox | environment (a capability promise, not a defect) |
| VMG-E2 | The RADV ICD leaks 2 × 128 bytes inside `vkCreateInstance`, reproduced without CNA (`tools/platform/lsan_x11_mesa.supp`), so LeakSanitizer runs of Vulkan use lavapipe | driver finding, recorded, not suppressed |
| VMG-E3 | On the private Xwayland, lavapipe's MIT-SHM `ShmAttach` is refused (`BadAccess`), and Mesa's software X11 WSI can then hang in `vkDestroySwapchainKHR`; LSan runs use `MESA_VK_WSI_DEBUG=noshm`. RADV is unaffected | environment + Mesa software-WSI robustness, recorded (VMG-0018) |
| VMG-O1 | OpenGL4 has no compute path until `MOD-2261`, which keeps `MOD-2262` (the three-backend conformance row) 🟨 | out of this workstream's scope |
