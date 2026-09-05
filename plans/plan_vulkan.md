# plan_vulkan.md — Vulkan renderer parity, correctness and EasyGL-equivalence

> **Created:** 2026-09-04. **Branch:** `vulkan`. **Baseline commit:** `65b89281a`.
> **Build directories:** `cmake-build-vulkan/` (this renderer) and `cmake-build-easygl/`
> (the reference renderer, `CNA_GRAPHICS_RENDERER=OPENGLES3`). Both are in-repo, shared and
> incremental — see `CLAUDE.md` "Build locations & caching (mandatory)".
>
> **This file is the authoritative backlog for current Vulkan renderer parity work.** Historical
> Vulkan work in `plans/plan_graphics.md` (Phase 73, Tasks 664–665 and 825–861, archived into
> `plan_graphics_20260709.md`) may be *cited* but no longer owns anything. Compiled XNA `.fx`
> bytecode stays owned by `plans/plan_fx.md`; the CNAEXT/`graphics-ext` engine layer, including its
> Phase 22 modern-GPU rollout, stays owned by `plans/plan_modern.md`.

---

## 1. Current status

**Implementation under way.** Fifteen tasks are ✅ (`VULKAN-004`, `-020`, `-021`, `-091`, `-097`,
`-130`, `-131`, `-145`, `-146`, `-250`, `-332`, `-333`, `-346`, `-370`, `-390`), plus `VULKAN-011`
from the planning session. `VULKAN-146` took two attempts: the first was reverted for a regression
only the full `ctest` could see (F-21), and the second added the piece the original scope was
missing — which stock program runs has to ask the declaration too, not just where its inputs live. Four rows were opened by work that discovered something the plan had not:
`VULKAN-098` (F-19, the two renderers place the same XNA `z` at different depths and EasyGL is the
one that diverges from XNA), `VULKAN-264` (F-08's tail, `ShaderDialectEXT::GlslVulkan` does not
distinguish GLSL source from SPIR-V bytecode), `VULKAN-150` (F-20, Vulkan's `DualTextureEffect` has
one UV channel and the declaration guard is the only thing hiding it) — plus the
`VULKAN-145`–`VULKAN-149` split of `VULKAN-144` and F-21's correction to the baseline itself.

**The Vulkan suite is 224/225** on `-R '^Vulkan_'`, from the 209/215 this plan opened with, and the
full `ctest` is **9059/9081**. §7.5 carries the running baseline, one line per
completed task. The single remaining `Vulkan_*` failure, `Vulkan_RenderTarget_EffectSource`, is
`VULKAN-148`'s capability gap and now fails cleanly rather than aborting the process
(`VULKAN-346`). **Both numbers matter, and F-21 is why**: `-R '^Vulkan_'` is 215 of this
configuration's 9071 registered CTests, and a regression this session caused was invisible to it.

Four findings were corrected rather than confirmed, and those are the entries worth reading:
`Vulkan_DepthBias` was a **test** written against OpenGL's depth range, not a renderer defect
(`VULKAN-091`); `PixelCountIsPreciseEXT` was **wrong**, not "probably right" as F-14 guessed
(`VULKAN-370`); F-06's silent white sprite is, on the SpriteBatch path it is most likely reached
from, a `VK_NULL_HANDLE` bind and a segfault (`VULKAN-390`); and `VULKAN-147`'s acceptance, written
in this session's own split of `VULKAN-144`, could only have been satisfied by rendering the wrong
picture — which is what F-20 records and `VULKAN-150` now owns.

The rest of this section is the **planning-session baseline**, kept as written. It was produced by a
static audit of the working tree on 2026-09-04, a mechanical inventory of both renderers' registered
CTests, a build of both configurations, a full run of the Vulkan suite and a targeted EasyGL
cross-check. Everything below marked ✅ *in prose* is an observation about the tree, not a completed
task.

Measured at that point: the Vulkan suite was **209/215**, with six deterministic failures, four of
which passed on EasyGL from the same sources (§7.5, §7.6). Headline inventory numbers:

| Measure | EasyGL (`OPENGLES3`) | Vulkan |
|---|---|---|
| Registered CTests in the configuration | 407 | 302 |
| …of which renderer-prefixed | 317 `EasyGL_*` | 215 `Vulkan_*` |
| …of which shared gates/unit suites | 90 | 87 |
| Renderer-owned example/test `.cpp` files | 245 | 91 |
| Example/test sources registered by **both** renderers | 117 | 117 |
| Renderer implementation lines (`src/*.cpp` + `include/**/*.hpp`) | 15,209 | **17,048** |
| Renderer example/test lines | 47,629 | 21,619 |

Two of these numbers are traps and are recorded here so nobody repeats the mistake:

- **The `.cpp` file counts are not a coverage measure.** Vulkan deliberately re-registers 117
  renderer-agnostic sources that live in the EasyGL examples directory, so its 91 own files
  understate its coverage by roughly a factor of three. §7.4 gives the counting rule this plan uses
  instead.
- **Vulkan's implementation is the larger of the two.** `VulkanRenderer.cpp` is 763 KB against
  EasyGL's 609 KB, and Vulkan ships 36 hand-written GLSL shader sources compiled to a SPIR-V header.
  The gap this plan closes is not "Vulkan is a thin renderer"; it is specific, and §9 names it.

§8 lists the commands that produced every number above.

---

## 2. Mission

Make the native CNA Vulkan renderer **at least as complete, correct, robust, well-tested and
trustworthy as EasyGL for the existing non-modern CNA graphics surface.**

EasyGL is the reference for *implementation maturity and coverage*. It is **not** the semantic
authority. Where EasyGL and XNA 4.0 disagree, XNA wins (`CLAUDE.md`, "Where FNA and XNA disagree,
XNA wins", owner decision 2026-09-04) and Vulkan implements the XNA behaviour — a known EasyGL
defect is never copied into Vulkan to manufacture parity.

---

## 3. Scope

In scope: everything the ordinary (non-`CNA_CNAEXT`) CNA renderer build exposes through the Vulkan
renderer, whether or not the API itself is original XNA:

- `SpriteBatch`, `SpriteFont`, all `Draw`/`DrawString` overloads, sort modes, sprite effects;
- `BlendState`, `DepthStencilState`, `RasterizerState`, `SamplerState`, viewport, scissor;
- `VertexBuffer`, `IndexBuffer`, `DynamicVertexBuffer`/`DynamicIndexBuffer`, `VertexDeclaration`,
  every draw route (`DrawPrimitives`, `DrawIndexedPrimitives`, `DrawUserPrimitives`,
  `DrawUserIndexedPrimitives`, `DrawInstancedPrimitives`);
- `Texture2D`, `Texture3D`, `TextureCube`, `SurfaceFormat`, mip levels, partial regions, readback;
- `RenderTarget2D`, `RenderTargetCube`, MRT, MSAA, depth/stencil formats, `RenderTargetUsage`;
- the stock effects (`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`,
  `SkinnedEffect`) and the ordinary-build `PbrEffect`/`SkinnedPbrEffect` paths;
- custom `ShaderEffect` as it exists in the ordinary build;
- `Model`/`ModelMesh` drawing, skinned animation and the renderer-facing half of glTF/`.cnj`
  material semantics;
- `GraphicsDevice`/`GraphicsDeviceManager` lifecycle, presentation parameters, resize, disposal;
- `OcclusionQuery`;
- Vulkan validation cleanliness, resource lifetime, swapchain recreation and bounded resource use.

---

## 4. Explicit non-goals

These stay with `plans/plan_modern.md` (Phase 22, `MOD-2200`–`MOD-2266`) and must **not** be copied
here. `plan_modern.md` §22's own wording that "`plans/plan_vulkan.md` … does not exist" was written
before this file did; the split is now: **this file owns classic/current renderer parity,
`plan_modern.md` continues to own the modern CNAEXT engine-layer rollout, including its Vulkan
implementation tasks.**

- anything in `modules/graphics-ext/` or under the `CNA_CNAEXT` option;
- compute shaders, storage buffers, storage textures, image bindings;
- indirect drawing (`DrawPrimitivesIndirectEXT` / `DrawIndexedPrimitivesIndirectEXT`);
- GPU timers (`IGpuTimerRenderer`, `SupportsGpuTimerEXT`) and debug-region APIs owned by the
  engine layer;
- float/HDR render-target formats and `CreateRenderTarget2DEXT`/`CreateRenderTargetCubeEXT`
  (`MOD-104`, `MOD-107`, `MOD-123`, `MOD-2223`), display colour spaces (`MOD-2092`);
- texture arrays (`MOD-2226`, `MOD-2243`), bindless resources, VRS;
- ray tracing (`MOD-2096`) and mesh shaders (`MOD-2097`) — `MOD-2096`'s text names
  `plan_vulkan.md` as the place such a capability question *would* be asked; that reference does
  **not** transfer ownership, and this plan deliberately declines it as modern-GPU work;
- a portable shader language, or making one `ShaderEffect` source run on both renderers — that is
  `plans/plan_csl.md` (CSL/CSIR, created 2026-09-04, entirely unimplemented);
- compiled XNA `.fx` bytecode behaviour — `plans/plan_fx.md` (`FX-065`, `FX-110`, `FX-112`).

Existing CNAEXT/internal diagnostic hooks that Vulkan tests already use (the `*EXT` cache-cardinality
counters, `validationMessages_`, `VkSamplerTraceEXT`) stay; this plan does not expand the modern
public API.

Performance work is a non-goal except where §13's four named pathologies make the renderer
materially less robust than EasyGL. No speculative micro-optimization task may be created without a
measurement.

---

## 5. Authorities and reference order

1. **XNA 4.0** as measured — the shipped assemblies' IL, `xna4-decomp/`, the Wine XNA 4.0 runtime at
   `~/.wine-cna-xna40`, and `plans/plan_bindings_upstream.md`'s recorded divergences.
2. **FNA** (`/rv/data/library/github.com/FNA-XNA/FNA`) as the readable day-to-day reference, except
   where it is known to diverge from XNA.
3. **CNA's own shared layer** (`modules/graphics/`) — the renderer-agnostic contract every renderer
   must satisfy, including `IGraphicsRenderer`'s documented defaults.
4. **EasyGL** — the maturity/coverage reference. A behaviour EasyGL implements is *evidence that the
   behaviour is reachable*, never proof that its result is right.
5. **Vulkan spec / validation layers** — for how the behaviour must be expressed here.

A divergence taken on XNA-over-FNA grounds is recorded where it is made (a source comment naming the
IL) and in `plans/plan_bindings_upstream.md`, per `CLAUDE.md`.

---

## 6. Definition of Vulkan ↔ EasyGL parity

Vulkan is **at least as good as EasyGL** when, for every in-scope semantic graphics feature, all of
the following hold.

1. **Behaviour.** One of:
   - Vulkan implements the behaviour correctly (judged against §5's authority order, not against
     EasyGL's output); or
   - Vulkan has an explicitly justified platform/API-specific difference, recorded in this file
     *and* in `docs/vulkan-renderer.md`; or
   - Vulkan deliberately provides stronger behaviour or a wider capability.
2. **Truthful reporting.** `SupportsCapability`, every `*EXT` query, `GetShaderDialectEXT`, applied
   formats/sample counts and `PresentationParameters` echo-back describe what the renderer actually
   does on the actual device. A capability reported true must be reachable through the public API;
   a capability reported false must correspond to a refusal, not to a working path.
3. **Explicit failure.** Unsupported behaviour fails loudly and consistently — a named exception, a
   documented refusal — never a silent no-op, a substituted format, a fabricated readback or a
   substituted resource that renders the wrong pixels.
4. **Meaningful tests.** Every renderer-specific branch has a test that can fail for the defect it
   claims to detect. "Renders without throwing" is not proof of a visual feature.
5. **Validation clean.** With `VK_LAYER_KHRONOS_validation` enabled, the suite produces no
   CNA-attributable errors or warnings; anything remaining is attributed to the driver or the
   environment by name.
6. **Lifetime safe.** No use-after-free, no double free, no unbounded growth of descriptors,
   pipelines, samplers, command buffers or device memory caused by ordinary rendering.
7. **Resize safe.** Backbuffer resize, real window resize, repeated swapchain recreation, zero-sized
   and minimized windows leave every resource valid.
8. **Durable.** Behaviour is still correct after repeated use — many frames, many `Begin`/`End`
   cycles, many bind/unbind cycles, many uploads — not only on the first draw.
9. **No inherited bugs.** An EasyGL defect is not reproduced on Vulkan to obtain a matching pixel.

Parity is **semantic**, never file-count or test-count equality.

---

## 7. Current measured baseline

Recorded 2026-09-04 on commit `65b89281a` (branch `vulkan`).

### 7.1 Environment

| Fact | Value |
|---|---|
| Vulkan loader / instance API | 1.4.309 |
| GPU0 | `AMD Radeon 780M (RADV PHOENIX)`, `DRIVER_ID_MESA_RADV`, Mesa 25.0.7-2+deb13u1, conformance 1.4.0.0 |
| GPU1 | `llvmpipe (LLVM 19.1.7)`, `DRIVER_ID_MESA_LLVMPIPE`, conformance 1.3.1.1 |
| Validation layer | `VK_LAYER_KHRONOS_validation` 1.4.309, present |
| ICDs installed | radeon, lvp, intel, intel_hasvk, nouveau, virtio, gfxstream |
| `glslc` / `glslangValidator` | **absent** (CMake reports "missing components") |
| `libshaderc.so.1` | present — the route `modules/renderers/vulkan/src/shaders/compile_shaders.py` already uses |
| Display for CTest | `CNA_TEST_DISPLAY` set to `:99` in both build dirs; Xvfb required (`SDL_VIDEODRIVER=x11`) |

**The physical GPU is present and the test suite cannot reach it.** Measured this session: under
Xvfb, RADV refuses presentation (`vulkan: No DRI3 support detected - required for presentation`), so
`VulkanRenderer::PickPhysicalDevice` — which requires a device with `VK_KHR_swapchain` *and* a
present-capable queue for the surface — rejects it and selects `llvmpipe`. Forcing the RADV ICD
(`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json`) makes the renderer abort with
`Vulkan: no suitable GPU` instead. Every `Vulkan_*` CTest on a virtual display therefore measures
**Mesa lavapipe**, exactly as the runs recorded in `NEXT.md` and
`docs/graphics-renderer-feature-matrix.md` did.

This is *not* the same situation as WebGPU's, which runs on RADV under Xvfb `:131` because its tests
read back with `GetBackBufferData` and never present. CNA's Vulkan renderer creates a real swapchain,
so presentation support is part of device selection.

Consequences, recorded rather than worked around:

- `BLOCKED_BY_ENVIRONMENT` applies to §6's "on the actual device" clause for every result taken here.
- Reaching RADV needs either the owner's real desktop (`DISPLAY=:0` — pops real windows, has a live
  compositor that has previously produced scattered, untrustworthy pixel results, and needs an
  explicit go-ahead), or a DRI3-capable X server (Xorg with the `dummy` driver, or Xwayland).
  `VULKAN-012` owns establishing one; until it does, "verified on real hardware" may not be claimed.
- A Vulkan environment with no usable device does not skip — it calls `std::terminate` and dumps
  core, which a CTest cannot distinguish from a genuine failure (`VULKAN-013`).

### 7.2 Configuration and build

Both configurations configure cleanly after three environment fixes that are **not** code defects
and must be applied in any fresh worktree:

1. `git submodule update --init third_party/SDL third_party/SDL_image third_party/SDL_mixer vendor/googletest`
   (a fresh worktree has none of them);
2. `-DCNA_ENABLE_DRACO=OFF` (or init `third_party/draco`);
3. `-DCNA_SHARP_RUNTIME_ROOT=/rv/data/development/github.com/openeggbert/sharp-runtimenext` — the
   default sibling `../sharp-runtime` is on `develop`, which lacks
   `SharpRuntime::Storage::StoragePaths::SetIsolatedStorageRootOverride` that
   `modules/storage/src/StorageDevice.cpp:290` requires. Building against the sibling default fails
   at `cna_storage`. This is a checkout-version mismatch, recorded here so it is not re-diagnosed.

### 7.3 Test inventory

```
ctest --test-dir cmake-build-vulkan  -N              → Total Tests: 302   (215 ^Vulkan_)
ctest --test-dir cmake-build-easygl  -N              → Total Tests: 407   (317 ^EasyGL_)
```

> **Correction (`VULKAN-004`, 2026-09-04).** The two totals above were taken while the
> `CnbTextureCodecTests.cpp` break of §7.5 was still present. `gtest_discover_tests` enumerates a
> suite by *running the built binary*, so an unbuildable test object registers nothing, and the
> totals under-counted by an order of magnitude. From a green build the Vulkan configuration
> reports **9071** registered CTests. The renderer-prefixed counts (`215 ^Vulkan_`,
> `317 ^EasyGL_`) come from the renderer's own example/test registrations and did **not** change;
> every parity number in this plan is derived from those, not from the totals. `VULKAN-006` owns
> re-stating this section from a green build of both configurations.

The shared (non-renderer-prefixed) sets are the same except for
`ModuleLinkClosure_VulkanRendererClosure` (Vulkan only) and the four `easy-gl-*` library suites
(EasyGL only, and GL-library-internal).

### 7.4 Coverage comparison rule used by this plan

An EasyGL CTest counts as **already covered on Vulkan** when any of the following holds:

- the Vulkan configuration registers the **same source file** (117 sources do this today); or
- the Vulkan configuration registers a source with the same stem after stripping the
  `easygl_`/`vulkan_` prefix; or
- a `Vulkan_*` CTest normalizes to the same name.

Applied in both directions, that rule gives **143 of EasyGL's 317 CTests with no Vulkan
equivalent** and **39 of Vulkan's 215 with no EasyGL equivalent**. §9.3 classifies all 143; the 39
are the starting evidence for the `VULKAN_STRONGER` rows in §10 (orientation calibration, MRT mixed
formats and MSAA resolve, cube mip chains and per-face MSAA, deferred-resource lifetime, cache
cardinality, swapchain sync, three extra fog families, render-target viewport/scissor, …).

The rule is deliberately generous — a name or source-stem match is treated as coverage without
checking that the two tests assert the same thing. It therefore **under**-reports gaps, which is the
safe direction for a parity plan, and §9.3's classification is what actually decides whether each
one matters.

A practical note that makes many of the resulting tasks small: a large share of the missing sources
are already renderer-agnostic and already live in the shared `modules/graphics/examples/` directory
(`basic_effect_test.cpp`, `alpha_test_effect_test.cpp`, `skinned_effect_test.cpp`,
`sprite_font_test.cpp`, `occlusion_query_test.cpp`, `dxt1_texture_test.cpp`,
`rendertarget2d_golden_test.cpp`, `sampler_component_isolation_contract_test.cpp`, …), and the rest
are public-XNA-API-only sources under `modules/renderers/easygl/examples/` that Vulkan already
re-registers 117 times over. "Register source X on Vulkan" in the phase tables therefore means a
CMake registration plus a real run — not writing a new test — and it is still a real task, because
the run is the evidence and it may fail.

### 7.5 Measured Vulkan suite result (2026-09-04, Xvfb `:99`, llvmpipe)

```
cmake --build cmake-build-vulkan -j12 -- -k 0   → 1 pre-existing error, all 212 cna_test_vulkan_* built
ctest --test-dir cmake-build-vulkan -R '^Vulkan_' -j4   → 209/215 passed, 6 failed  (170 s)
ctest --test-dir cmake-build-vulkan --rerun-failed -j1  → 0/6 passed — all six are deterministic, none is a flake
```

The one build error is renderer-independent and pre-existing:
`modules/content/tests/CNA/Content/Cnb/CnbTextureCodecTests.cpp:475` calls
`CnbDocument::Parse(const std::vector<unsigned char>&)`, which no longer matches the declaration at
`CnbDocument.hpp:151`. It breaks `cna_content_test_objects` only (`VULKAN-004`, fixed 2026-09-04).

| Failing test | What it actually reports | Reading |
|---|---|---|
| `Vulkan_DepthBias` | `DepthBias=-1e6 (flat): (255,0,0) expected GREEN` | Real dynamic `vkCmdSetDepthBias`, ten pipeline sites. §7.6 disproves the historical "lavapipe" attribution: `EasyGL_DepthBias` passes on the same stack. **F-18**, `VULKAN-091`. |
| `Vulkan_DualTextureSlotSamplerContract` | 5/5 palette checks pass, then legs A..M **SKIP**: *"this VertexDeclaration cannot be represented on the ordinary-nonindexed route — the declaration carries `TextureCoordinate0@12 Vector2`, which this renderer's native layout for a 28-byte record does not bind at all. The renderer selects its native vertex layout from the buffer stride and does not translate arbitrary declarations yet."* The process then aborts with `System::InvalidOperationException: Cannot present while render targets are bound`. | Two findings: **F-15** (stride-driven layout selection) and **F-16** (uncaught-exception exit turning a pass into a crash). |
| `Vulkan_DescriptorCapacityContract` | A1–A256, D1, E1, F1, G1, H1/H2, I1/I2, J×3, K1 all pass — including **256 simultaneously live textures**. `B1` fails 15/27 and `C1` fails 142/256, both reporting "interpolating it" rather than reproducing the texel. | The capacity half passes outright — 256 simultaneously live textures — which is direct evidence *against* a naive reading of F-06. The two failures are the mag-linear filters; §7.6 shows EasyGL passes the same source. **F-18**. |
| `Vulkan_XnaPixelCenter` | `XNA 1x1 triangle: 0 covered pixel(s), expected at least 1`; the `BasicEffect` control triangle covers 120 | **F-18** — EasyGL passes the same source (§7.6). |
| `Vulkan_PointSamplingContract` | `U2 3D textured quad, 3x3 magnified onto 10x10 (non-integer): mismatches=19` of 100; every other leg passes, including the tie-guard and the 512-pixel custom-viewport leg | **F-18** — EasyGL passes the same source (§7.6). |
| `Vulkan_RenderTarget_EffectSource` | 7/9 pass. `B1 PbrEffect` and `B1 SkinnedPbrEffect` fail with *"Vulkan PbrEffect requires vertex stride 48 or 60"* / *"stride 68, 76 or 80"*, then the process aborts on the same `std::runtime_error`. The test also records: *"VULKAN has no custom-effect slot on its stock 3D paths (a custom `ShaderEffect` is a SpriteBatch route there)"* | **F-15** again, **F-16** again, and independent confirmation of **F-09**. |

Nothing in this table is counted as a new *pass*; six deterministic failures on the reference
environment are the baseline `VULKAN-003` starts from.

**Running baseline.** Each completed task re-runs `ctest --test-dir cmake-build-vulkan -R '^Vulkan_'`
and records the result here, so a regression is visible against the previous line rather than
against the top of this section.

| After | Result | Failures |
|---|---|---|
| planning (`65b89281a`) | 209/215 | the six above |
| `VULKAN-130` | **210/216** | the same six; the added test is `Vulkan_VertexBuffer_WideStrideBounds`, which passes |
| `VULKAN-131` | **211/217** | the same six; the added test is `Vulkan_IndexBuffer_UploadBounds`, which passes |
| `VULKAN-020` | **212/218** | the same six; the added test is `Vulkan_CapabilityContract`, which passes |
| `VULKAN-021` | **212/218** | the same six; `Vulkan_CapabilityContract` gains leg E and is 25/25 |
| `VULKAN-097` | **215/218** | **three fixed by one change** — `Vulkan_XnaPixelCenter`, `Vulkan_PointSamplingContract`, `Vulkan_DescriptorCapacityContract`. Remaining: `Vulkan_DepthBias` (`VULKAN-091`), `Vulkan_DualTextureSlotSamplerContract` and `Vulkan_RenderTarget_EffectSource` (`VULKAN-144`/`VULKAN-346`) |
| `VULKAN-091` | **216/218** | only `Vulkan_DualTextureSlotSamplerContract` and `Vulkan_RenderTarget_EffectSource` remain, both `VULKAN-144`/`VULKAN-346`. `Vulkan_DepthBias` is 5/5 |
| `VULKAN-390` | **217/219** | the same two; the added test is `Vulkan_DescriptorPoolOverflow`, which passes |
| `VULKAN-346` | **218/219** | only `Vulkan_RenderTarget_EffectSource`, which is `VULKAN-144`'s capability gap and now fails cleanly instead of aborting |
| `VULKAN-250` | **219/220** | the same one; the added test is `Vulkan_ShaderDialectContract`, which passes |
| `VULKAN-370` | **220/221** | the same one; the added test is `Vulkan_OcclusionQuery_Precision`, which passes |
| `VULKAN-332` | **222/223** | the same one; the added tests are `Vulkan_SwapInterval` and `Vulkan_PresentInterval`, both passing |
| `VULKAN-333` | **222/223** | the same one; `Vulkan_SwapInterval` gains its forwarding legs and is 10/10 |
| `VULKAN-145` | **223/224** | the same one; the added test is `Vulkan_VertexInputLayout`, which passes |
| `VULKAN-146` | **224/225** `^Vulkan_`, but **8818/8856** on `-E '^Vulkan_'` — a regression of 8 in the shared `VertexDeclarationLayoutTests`, invisible to the prefixed filter. Reverted; see F-21. |
| after the revert | **223/224** `^Vulkan_`, **8826/8856** `-E '^Vulkan_'` | the shared declaration suite is back to 10 passed / 2 skipped |
| `VULKAN-097` narrowed to filled primitives | **223/224** `^Vulkan_`, **9055/9080** full `ctest` | F-21 also caught a second regression the prefixed filter could not see: the pixel-centre correction was moving indexed LINE lists. Restricted to `TriangleList`/`TriangleStrip`; `IndexedDrawDeferredTest` green, the six pixel-centre-sensitive `Vulkan_*` tests still green. The 25 remaining full-suite failures are audio/ENet/two-process/C-API/content cases outside this plan's surface — `CnjEffectTest`, `CnjStockEffectTest` and `GraphicsDeviceRendererTest.StartupDiagnosticNeverWritesToStdout` were **measured** failing at `dc0cb2057` too, and the set varies run to run under `-j6`, which is the known over-reporting. |
| `VULKAN-146` (second attempt) | **224/225** `^Vulkan_`, **9059/9081** full `ctest` | the shared REMED-GFX-216/234 suite is 26 passed / 0 failed with Vulkan in the translating arm; the added test is `Vulkan_DeclaredVertexLayout`, 6/6 |

### 7.6 The EasyGL cross-check (the measurement that reclassified four failures)

Four of the six failing Vulkan tests run from sources that are registered on **both** renderers.
Running EasyGL's copies on the same machine, the same Xvfb display and the same session:

```
ctest --test-dir cmake-build-easygl -j1 \
      -R '^EasyGL_(XnaPixelCenter|PointSamplingContract|DescriptorCapacityContract|DepthBias)$'
  → 4/4 passed
```

| Shared source | EasyGL | Vulkan |
|---|---|---|
| `xna_pixel_center_contract_test.cpp` | **pass** | fail — the 1×1 XNA triangle covers 0 pixels |
| `point_sampling_contract_test.cpp` | **pass** | fail — U2, 19/100 mismatches |
| `descriptor_capacity_contract_test.cpp` | **pass** | fail — B1 15/27, C1 142/256 |
| `easygl_depth_bias_test.cpp` / `vulkan_depth_bias_test.cpp` | **pass** | fail — `DepthBias=-1e6` renders red, not green |

This is the discriminating measurement, and it moves all four out of "possibly a stale test" and out
of "probably the software driver". EasyGL passes `EasyGL_DepthBias` on the same virtual display and
the same software rasteriser stack, so "lavapipe" is no longer an adequate explanation for
`Vulkan_DepthBias` either. All four are **Vulkan-specific divergences from a shared,
XNA-calibrated contract** and are owned as remediation work (`VULKAN-091`, `VULKAN-097`), not as
audit questions.

The EasyGL build hits the same single pre-existing `CnbTextureCodecTests.cpp` error and is otherwise
clean, which also confirms `VULKAN-004`'s break is renderer-independent.

---

## 8. Commands used to produce §7 (and to re-produce it)

```bash
# configure (once per worktree; reuse the directories afterwards)
git submodule update --init third_party/SDL third_party/SDL_image third_party/SDL_mixer vendor/googletest
cmake -S . -B cmake-build-vulkan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_RENDERER=VULKAN -DCNA_BUILD_TESTS=ON -DCNA_BUILD_EXAMPLES=ON \
      -DCNA_ENABLE_DRACO=OFF -DCNA_TEST_DISPLAY=:99 \
      -DCNA_SHARP_RUNTIME_ROOT=$PWD/../sharp-runtimenext \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake -S . -B cmake-build-easygl -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_BUILD_TESTS=ON -DCNA_BUILD_EXAMPLES=ON \
      -DCNA_ENABLE_DRACO=OFF -DCNA_TEST_DISPLAY=:99 \
      -DCNA_SHARP_RUNTIME_ROOT=$PWD/../sharp-runtimenext \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache

cmake --build cmake-build-vulkan -j$(nproc)     # gate everything on this exit code
ctest --test-dir cmake-build-vulkan -N          # inventory
Xvfb :99 -screen 0 1280x800x24 &                # a real display is required
ctest --test-dir cmake-build-vulkan -R '^Vulkan_' --output-on-failure
ctest --test-dir cmake-build-vulkan --rerun-failed -j1   # the flake discriminator
```

Environment rules that have cost this project time before and are not negotiable here:

- **Run the whole `ctest`, not `-R '^Vulkan_'`.** That filter is 215 of this configuration's 9071
  registered CTests and cannot see the shared, renderer-agnostic suites — including
  `VertexDeclarationLayoutTests.cpp`, which measures F-15 itself. Two regressions this plan caused
  were invisible to it (F-21). Record both numbers.
- **Gate test runs on the build's exit code.** A failed build plus a stale binary passes silently.
- `ctest -j8` over-reports failures; `--rerun-failed -j1` is the discriminator.
- On a Wayland host, `DISPLAY` alone does not reach Xvfb — also set `SDL_VIDEODRIVER=x11`.
- `ctest -N` overwrites `LastTest.log`; capture output before enumerating.
- Golden-image tests need the **repo root** as their working directory.

---

## 9. Initial findings from this planning audit (Level A)

Every finding below was read out of the working tree at commit `65b89281a`. Each is either already
turned into a concrete task in the phase tables, or explicitly marked as needing one more
measurement before a task can be written honestly.

### 9.1 Confirmed implementation gaps

| # | Finding | Evidence | Task |
|---|---|---|---|
| F-01 | **`VertexBuffer` upload can overrun its own allocation.** `VulkanVertexBufferRenderer` allocates `max(1, vertex_capacity * 64)` bytes and `SetData` does an unchecked `memcpy(mappedPtr_, data, vertex_count * stride_in_bytes)`. The *same file's* pipeline-key table recognises strides 68, 76 and 80 (`SkinnedPbrEffect`, dual-UV variants, glTF skinned-PBR + `COLOR_0`), so any buffer using those layouts writes up to 25 % past the mapped range. EasyGL sizes its upload to the real byte count. | `modules/renderers/vulkan/src/VulkanRenderer.cpp:1572`, `:1608`, `:4753`–`:4760`; `modules/renderers/easygl/src/EasyGLRenderer.cpp:7524` | `VULKAN-130` |
| F-02 | **`IndexBuffer` upload is unchecked too.** `SetData16`/`SetData32` `memcpy` `index_count * elemSize` with no comparison to `capacity_`. | `VulkanRenderer.cpp:1654`–`:1664`, allocation at `:1623` | `VULKAN-131` |
| F-03 | **`SetPresentationMode` is a silent no-op.** `void SetPresentationMode(int) override {}` — the only empty `override {}` body in the Vulkan header, and the only one in either renderer's header. EasyGL implements the real `CnaPresentationMode` set (`Letterbox`/`Overscan`/`Stretch`/`NativeBackBuffer`/`FixedHeightDynamicWidth`); about thirty other renderers implement it in their `.cpp`. Vulkan applies a uniform height-derived scale in `TransformWindowToLogical`/`TransformLogicalToWindow` regardless of the requested mode. | `modules/renderers/vulkan/include/CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp:1192`; `EasyGLRenderer.cpp:5287`; `VulkanRenderer.cpp:9965`–`:9989` | `VULKAN-330` |
| F-04 | **`GetDefaultViewportRect` is not overridden**, so the physical viewport rectangle is always `(0, 0, GetViewportSize())`. That is correct only for a renderer with no real letterbox/overscan rectangle — which is a consequence of F-03, not an independent decision. EasyGL overrides it. | `IGraphicsRenderer.hpp:1867`; `EasyGLRenderer.hpp:1553` region | `VULKAN-331` |
| F-05 | **Runtime VSync changes are dropped.** Vulkan honours `args.swapInterval` only at swapchain creation and does not override `SetSwapInterval`, so `GraphicsDeviceManager.SynchronizeWithVerticalRetrace` + `ApplyChanges()` (which routes through `GraphicsDevice::Reset`) reaches nothing. It also does not override `GetSwapIntervalEXT`, so it answers `-1` and the request cannot even be asserted. EasyGL records *and* forwards, specifically so the test can distinguish "CNA never asked" from "the driver declined" (`REMED-GFX-243`). Vulkan already stores `swapInterval_` and already recreates its swapchain on resize. | `VulkanRenderer.cpp:1694`, `:2362`–`:2410`; `IGraphicsRenderer.hpp:1883`, `:1895`; `EasyGLRenderer.cpp:5292` | `VULKAN-332`, `VULKAN-333` |
| F-06 | **Descriptor-pool exhaustion renders white instead of failing.** `GetOrCreateTexSamplerDescSet` returns `defaultWhiteDescSet_` when `vkAllocateDescriptorSets` fails. The pool holds `MaxDescriptorSets = 512` combined-image-sampler sets, the cache is keyed by `(VkImageView, VkSampler)` and entries are evicted only when a view dies — so a game with more than 512 live texture×sampler-state combinations silently draws white sprites with no exception, no log and no validation message. Of the eleven `vkAllocateDescriptorSets` sites, three throw, seven return `VK_NULL_HANDLE`, and this one substitutes a wrong-looking resource. | `VulkanRenderer.cpp:3424` (and `:3406`), pool at `:3269`, cap at `VulkanRenderer.hpp:1416` | `VULKAN-390`, `VULKAN-391` |
| F-07 | **`SupportsCapability` answers for capabilities it has never considered.** The `switch` handles three values and ends `default: return true;`. Every capability added to `CNA::GraphicsCapability` from now on is claimed by Vulkan without a line of implementation. `MultiSampleAntiAliasing` is claimed unconditionally, where EasyGL derives it from `GL_MAX_SAMPLES`; Vulkan has `PickSampleCount`/`VkPhysicalDeviceLimits` available and does not consult them here. | `VulkanRenderer.cpp:1739`–`:1757`; `EasyGLRenderer.cpp:5023`+ | `VULKAN-020`, `VULKAN-021` |
| F-08 | **`GetShaderDialectEXT()` is not overridden**, so `GraphicsDevice::GetShaderDialectEXT()` answers `Unknown` on a renderer whose `ShaderEffect` categorically requires SPIR-V bytecode. Only WebGPU and IGL declare a dialect today. This is the one query that exists precisely so an application need not infer the dialect from the build identity. | `VulkanRenderer.cpp:3475`; `IGraphicsRenderer.hpp:2048`; `WebGPURenderer.hpp:1343`; `IglRenderer.cpp:1394` | `VULKAN-250` ✅, and its tail `VULKAN-264`: the closest honest enumerator is `GlslVulkan`, which IGL's Vulkan backend also reports while taking GLSL *source* rather than bytecode -- so declaring it removes the `Unknown`, and does not yet make the answer sufficient |
| F-09 | **Custom `ShaderEffect` on Vulkan is SpriteBatch-shaped only.** `VulkanEffectRenderer` exposes six scalar/vector/matrix setters mapped onto a fixed 128-byte push-constant block plus the single combined-image-sampler set layout. It overrides none of `SetUniformFloatArray`/`SetUniformVec2Array`/`SetUniformVec3Array`/`SetUniformMat4Array`, and none of `BindTexture`/`BindTextureCube`/`BindTexture3D` — all of which EasyGL implements. EasyGL additionally drives a full 3D draw from a `ShaderEffect` with a custom vertex layout (`EasyGL_ShaderEffect_3D`, `EasyGL_ShaderEffect_CustomVertexLayout`), samples a `Texture3D` and a `TextureCube` from one (`EasyGL_ShaderEffect_Texture3D`, `_TextureCube`) and reads a uniform set through `SpriteBatch` (`EasyGL_ShaderEffect_SpriteBatch_Uniform`). Vulkan has exactly one custom-effect test, `Vulkan_ShaderEffect_SpirV`. | `VulkanRenderer.hpp:358`–`:420`; `VulkanRenderer.cpp:3475`–`:3515` | `VULKAN-251`–`VULKAN-256` |
| F-10 | **Compressed and packed `SurfaceFormat`s are refused on Vulkan and supported on EasyGL.** EasyGL classifies `Dxt1`/`Dxt3`/`Dxt5` (block transfer, with a decode fallback), `Bgr565`/`Bgra5551`/`Bgra4444` and `NormalizedByte2`/`NormalizedByte4` as `Supported`. Vulkan overrides none of `ClassifySurfaceFormatEXT`, `ClassifyRenderTargetFormatEXT`, `ClassifyColorTransferFormatEXT`, `IsCompressedTransferFormatEXT`, `IsCompressedCubeTransferFormatEXT` or `LoadsCompressedContentNativelyEXT`, so every one of those formats falls to the framework rule and `Texture::ValidateFormat` throws. The refusal is honest (`Vulkan_SurfaceFormat_Throws` proves it), but `GraphicsProfile.Reach` promises the game these formats work, the hardware supports BC1–BC3 and R5G6B5 natively, and `Texture2D::FromStream` (DDS) and the `.xnb` reader lose their native path. | `EasyGLRenderer.cpp:5495`–`:5571`; `Texture.cpp:175`; `Texture2D.cpp:115`, `:1263`; `TextureCube.cpp:64`, `:318` | `VULKAN-170`–`VULKAN-174` |
| F-11 | **`ITextureRenderer::GetSurfaceFormatEXT` is not overridden**, so a Vulkan texture reports `Color` (ordinal 0) whatever it holds. Inert today because F-10 makes every texture `Color`; it becomes a live defect the moment F-10 is closed, and the D3D9 one-/two-channel expansion rule it exists for would then be wrong. | `IGraphicsRenderer.hpp:574`; `EasyGLRenderer.hpp` (3 references) | folded into `VULKAN-170` |
| F-12 | **`SetDataOptions` is dropped, and the deferred draw model makes that potentially observable.** Vulkan overrides none of `SetDataWithOptions`/`SetData16WithOptions`/`SetData32WithOptions`, so `Discard` and `NoOverwrite` collapse to the plain path. Its buffers are single, persistently-mapped `HOST_VISIBLE`/`HOST_COHERENT` allocations, and this renderer records draws deferred to `Present()`. Two draws in one frame from the same dynamic buffer with an intervening `SetData` therefore both read the *last* upload, where EasyGL's immediate GL draws each read the data current at their own call. EasyGL orphans on `Discard`. | `VulkanRenderer.cpp:1572`–`:1608`; `IGraphicsRenderer.hpp:130`, `:167`, `:175`; `EasyGLRenderer.cpp:7540` | `VULKAN-132`, `VULKAN-133` |
| F-13 | **Buffer disposal stalls the whole device.** `VulkanVertexBufferRenderer::ReleaseVulkanResources` and its index-buffer twin each begin with `vkDeviceWaitIdle`. Disposing N buffers is N full-device stalls, in a renderer that otherwise has a real deferred-retirement mechanism for textures and render targets (`Vulkan_DeferredResourceLifetime`). This is one of §13's four named pathologies, not a speculative optimization. | `VulkanRenderer.cpp:1579`, `:1630`; contrast the retirement machinery at `:10379`–`:10460` | `VULKAN-392` |
| F-14 | **`PixelCountIsPreciseEXT` is not overridden** (default `true`). EasyGL overrides it because its GLES3 `GL_ANY_SAMPLES_PASSED` answer is boolean. Vulkan uses a real occlusion query and `true` is probably correct — but "probably" is not evidence, and the query exists to be answered deliberately. | `IGraphicsRenderer.hpp:232`; `EasyGLRenderer.hpp` (2 references) | `VULKAN-370` |
| F-15 | **Vulkan selects its vertex layout from the buffer *stride*, not from the caller's `VertexDeclaration`.** `SetVertexDeclaration` is a deliberately non-defaulted renderer operation so each renderer must decide what to do with a declaration; Vulkan accepts it and then picks a native layout from a ten-entry stride table (`MakeExt3DKey`, `:4753`–`:4760`). Two CTests say so in their own output: `Vulkan_DualTextureSlotSamplerContract` skips its whole A..M body because a 28-byte record carrying `TextureCoordinate0@12 Vector2` "is not bound at all", and `Vulkan_RenderTarget_EffectSource` fails two legs with "Vulkan PbrEffect requires vertex stride 48 or 60" / "SkinnedPbrEffect requires vertex stride 68, 76 or 80". EasyGL binds arbitrary declarations (`ApplyLayout`) and proves it (`EasyGL_DrawUserPrimitives_CustomVD`, `EasyGL_ShaderEffect_CustomVertexLayout`). The refusal is honest, which is why this is a capability gap rather than a correctness bug — but it is the largest single one. | `VulkanRenderer.cpp:4753`–`:4760`, `:4535`; §7.5's two tests; `EasyGLRenderer.cpp:7535` | `VULKAN-144` |
| F-16 | **An unsupported combination ends the process instead of the check.** `Vulkan_RenderTarget_EffectSource` prints `[INFO] VULKAN: 7/9 checks passed` and then dies on the same uncaught `std::runtime_error` (`[CRASH] leg B1: killed by signal 6`). `Vulkan_DualTextureSlotSamplerContract` prints `=== 5/5 PASS ===` and then dies on `System::InvalidOperationException: Cannot present while render targets are bound`. Whether the fault is in the test harness or in the renderer's render-target unbind sequencing has to be determined per case — but a CTest that aborts is indistinguishable from a CTest that failed, and the second case in particular looks like a real teardown-ordering defect. | §7.5; `Vulkan_RenderTarget_EffectSource`, `Vulkan_DualTextureSlotSamplerContract` | `VULKAN-346` |
| F-17 | **The Vulkan suite cannot reach the physical GPU on a virtual display.** Under Xvfb, RADV reports `No DRI3 support detected - required for presentation`, `PickPhysicalDevice` therefore rejects it, and the renderer runs on `llvmpipe`; forcing the RADV ICD makes it abort with `Vulkan: no suitable GPU`. Every measurement in §7.5 is a software-driver measurement. | §7.1; `VulkanRenderer.cpp:2220`+ (`PickPhysicalDevice`) | `VULKAN-012`, `VULKAN-013` |
| F-18 | **Four tests fail on Vulkan and pass on EasyGL from the same sources.** `Vulkan_XnaPixelCenter` (a 1×1 XNA triangle covers 0 pixels where the `BasicEffect` control covers 120), `Vulkan_PointSamplingContract` U2 (19/100 mismatches on a non-integer 3×3→10×10 magnification), `Vulkan_DescriptorCapacityContract` B1/C1 (15/27 and 142/256, both "interpolating" instead of reproducing the texel — exactly the mag-linear filters) and `Vulkan_DepthBias`. §7.6 ran EasyGL's copies of all four on the same display in the same session: **4/4 pass**. That rules out both "stale test" and "software driver" — `EasyGL_DepthBias` passes on the same software stack. The first three share a signature (a half-texel / pixel-centre offset appearing wherever a magnifying filter or a sub-pixel-sized primitive is involved) and are likely one root cause; the fourth is separate. XNA uses D3D9 integer pixel centres for 3D and half-integer centres for sprites, and CNA diverges deliberately in places — so the fix is judged against §5's authority order, **not** against making Vulkan's output equal EasyGL's. | §7.5, §7.6; `modules/graphics/examples/xna_pixel_center_contract_test.cpp`, `point_sampling_contract_test.cpp`, `descriptor_capacity_contract_test.cpp`; `vulkan_depth_bias_test.cpp` | `VULKAN-097` ✅ (three of the four; the shared pixel-centre offset was one root cause, as suspected), `VULKAN-091` ✅ (`Vulkan_DepthBias` was separate, and was a test-premise defect, not a renderer one -- see F-19) |
| F-19 | **The two renderers put the same XNA `z` at different depth-buffer values, and EasyGL is the one that diverges from XNA.** With an identity projection, a vertex at `z = 0` lands at depth **0.0** on Vulkan -- XNA's Direct3D 9 range, `z` in `[0,w]` mapping to `[0,1]`, where 0 is the near plane -- and at depth **0.5** on EasyGL, which leaves OpenGL's `[-1,1]` clip depth in place and never remaps it. `easygl_depth_bias_test.cpp` and `vulkan_depth_bias_test.cpp` were both written on the GL premise; only Vulkan failed, because only Vulkan implements the XNA range. By §5's authority order Vulkan is right. Two consequences follow and neither is this renderer's to fix: an XNA scene loses half its usable depth precision on EasyGL, and a shared fixture that encodes a depth value cannot be trusted across the two. **Evidence:** `spikes/vulkan-depth-bias-spike/README.md`; `Vulkan_DepthBias`'s new near-plane leg. | `VULKAN-098` |
| F-20 | **Vulkan's `DualTextureEffect` has one UV channel, not two, and the declaration guard is currently the only thing hiding it.** `dual_texture3d.vert.glsl` declares a single `in vec2 inUV` and emits a single `fragUV` that the fragment shader uses for **both** samplers, so `Texture2` is sampled with `TextureCoordinate0`. XNA's `DualTextureEffect` samples it with `TextureCoordinate1`. Today every declaration that carries an independent second UV set is refused before the draw -- which is the right failure mode, and reads as a stride-table limitation rather than the missing shader input it actually is. Found while preparing `VULKAN-147`: the declaration-driven layout would make such a record "complete" (both of the *program's* inputs are supplied) and so stop the refusal, turning an honest skip into a wrong picture. `RequireDeclarationMatchesStockProgram` would not catch it either -- it ignores declaration elements the selected program does not consume, which is right for padding and wrong for a semantic the effect needs. **Evidence:** `modules/renderers/vulkan/src/shaders/dual_texture3d.vert.glsl:11-12`, `dual_texture3d.frag.glsl`; `Vulkan_DualTextureSlotSamplerContract`'s skip message; `dualtexture_slot_sampler_contract_test.cpp:198`-`:213`. | `VULKAN-150` |
| F-21 | **This plan's baseline cannot see the shared suite that already measures F-15, and it is registered and passing.** §7.5 runs `ctest -R '^Vulkan_'` -- 215 tests. The Vulkan configuration registers **9071**, and `modules/graphics/tests/.../VertexDeclarationLayoutTests.cpp` (REMED-GFX-216/234) is among them: a renderer-agnostic suite whose whole subject is "the native vertex layout must be derived from the `VertexDeclaration`, not guessed from the byte stride". It passes today because Vulkan takes its **refusal** arm. `VULKAN-146`'s conversion turned 10 passes into 8 failures and the `^Vulkan_` filter reported 224/225 throughout. Two consequences. **First**, the parity baseline is incomplete in a way that hides regressions in exactly the area this plan is working in: every remaining row that changes renderer behaviour must run the full `ctest`, not the prefixed subset, and §7.5's running-baseline table should carry both numbers. **Second**, the suite's `TranslatesDeclarations()` is a single boolean over renderer identities, with a refusal arm and a translating arm; a renderer converted family-by-family is neither, and no ordering of `VULKAN-146`–`VULKAN-149` avoids passing through that state. **Evidence:** the pre-change reading was taken by restoring `VulkanRenderer.cpp`/`.hpp` from `dc0cb2057` and rebuilding `CnaTests` -- 10 passed, 2 skipped; after the conversion, 8 failed. `ctest -E '^Vulkan_'` is 8818/8856, and the 8 were the only ones this session moved. | `VULKAN-146` (re-scoped), and §7.5's baseline rule |

### 9.2 Documentation contradicted by the current tree

`CLAUDE.md` and this plan treat documentation as evidence, not truth. These rows are **stale**: the
current code and the current registered tests contradict them. Historical notes stay; the
current-status claims must be corrected.

| # | Stale claim | Contradicting evidence |
|---|---|---|
| D-01 | `docs/texture3d-texturecube-support.md:71` — "Vulkan … **total no-op.** Neither `VulkanTexture3DRenderer` nor `VulkanTextureCubeRenderer` overrides `GetData`"; `:95`/`:37` — mip levels hardcoded to 1, `mipMap` dropped in the factory | `VulkanTexture3DRenderer::GetData` at `VulkanRenderer.cpp:12157`, cube `GetData` declared at `VulkanRenderer.hpp:749`, `imgInfo.mipLevels = levelCount_` at `:11979`/`:12234`; CTests `Vulkan_Texture3D_Mip_RoundTrip`, `Vulkan_TextureCube_Mip_RoundTrip`, `Vulkan_Texture3D_PartialBox_Readback`, `Vulkan_CubeVolume_GetDataContract` |
| D-02 | `docs/rendertarget-support.md:194`–`:195` — Vulkan MSAA "honestly reports `0`, not implemented (Task 879)"; mips "`LevelCount` correct, no real GPU mips (Task 878)"; `:116`/`:125` — the Clear-only-RT gap and the black `RenderTargetCube` | CTests `Vulkan_RenderTarget2D_MsaaResolve`, `Vulkan_RenderTargetCube_MsaaResolve`, `Vulkan_MRT_MsaaResolve`, `Vulkan_RenderTarget2D_MipChain`, `Vulkan_RenderTargetCube_MipChain`, `Vulkan_MrtMipFinalization`, `Vulkan_RenderTarget2D_ClearOnlyRoundtrip`, `Vulkan_RenderTargetCube_SampleAfterUnbind` |
| D-03 | `docs/graphics-renderer-feature-matrix.md` — "Texture2D mip-level `SetData` (level > 0) … Vulkan ❌ silent no-op (Task 867)" | `VulkanTextureRenderer::UpdatePixelsLevel` at `VulkanRenderer.cpp:507`; CTest `Vulkan_Texture2D_Mip_RoundTrip` |
| D-04 | `IGraphicsRenderer.hpp:2180`+ header comment — "Vulkan always allocates a combined depth+stencil buffer using its device-wide format regardless of the exact value requested … tracked as Task 911" | `VulkanRenderer` overrides `GetAppliedDepthStencilFormatEXT`; `PickDepthFormat` per instance at `VulkanRenderer.cpp:~228`; CTest `Vulkan_RenderTarget2D_DepthFormatFidelity`; the feature matrix's own newer row already says ✅ (Task 911) |
| D-05 | `docs/graphics-renderer-feature-matrix.md` — Vulkan cells reading "not separately re-audited (Task 861)" for SpriteBatch/SpriteFont, and "not separately re-confirmed this pass" for `RasterizerState` | `Vulkan_SpriteBatch_Rotation`, `_Scale`, `_SourceRectangleCropping`, `_LayerDepthOrder`, `_TransformMatrix`, `_MultiBeginEnd`, `Vulkan_SpriteEffects_Flip`, `Vulkan_SpriteFont_SingleGlyph`/`_MultiGlyphSpacing`/`_Newline`/`_DefaultCharacterFallback`, `Vulkan_RasterizerState_CullMode`(+`_Camera`, `_IndexedBasicEffect`), `Vulkan_FillMode_WireFrame`, `Vulkan_DepthBias` |
| D-06 | `plans/plan_modern.md:1241` — "The current documentation assigns Vulkan compute to `plans/plan_vulkan.md`, but that file does not exist." | this file | 
| D-07 | `docs/graphics-renderer-feature-matrix.md:375` and `NEXT.md` — the Vulkan baseline and the `Vulkan_DepthBias` residual were measured on **Mesa lavapipe**, a software driver | §7.1: this machine now selects `AMD Radeon 780M (RADV PHOENIX)` |

There is no `docs/vulkan-renderer.md` at all, although every other mature renderer family has one
(`docs/webgpu-renderer.md`, `docs/magnum-renderer.md`, `docs/nanovg-renderer.md`,
`docs/tinygl-renderer.md`, `docs/igl-renderer.md`, `docs/llgl-renderer.md`, …). Vulkan's capability
boundary is spread across four shared documents, three of which are stale. `VULKAN-480` creates it.

### 9.3 Classification of the 143 EasyGL CTests with no Vulkan equivalent

Counts are exact; the per-family detail is in the phase tables that own them.

| Class | Count | Families |
|---|---|---|
| `PORTABLE_RENDERER_BEHAVIOR` — real Vulkan evidence needed | 71 | stock-effect variants (`BasicEffect*` ×7, `AlphaTest*` ×3, `DualTextureEffect_*` ×2, `EnvironmentMapEffect_Fresnel_Gradient`, `SkinnedBones`, `SkinnedEffectVector4BoneIndices`, `EmissiveAmbientComposition`, `Pbr_MaterialMaps`), glTF renderer-facing (×8, minus `ContextLoss`), draw routes (`DrawUserPrimitives*` ×4, validation ×5), textures (`DxtFormat`, `DXT1_FromStream_Readback`, `Packed16Format`, `DepthFormat`, `TextureAddressMode_Mirror`, `TexturedQuad_Readback`, `Texture2D_AnisotropicSingleLevel`, `TextureCube_Faces_RoundTrip`, `Texture3D_PartialBox_RoundTrip`, `SamplerComponentIsolation`), render targets (`MRT_TwoAttachments`, `RenderTarget2D_Readback`, `RenderTargetCube_DepthFormat`, `GFX164_BoundMsaaAlpha`), occlusion (×3), device/lifecycle (×13), SpriteBatch/SpriteFont (×5), sample scenes (×6) |
| `SHARED_LOGIC` — renderer-independent code, but the Vulkan integration can still regress | 9 | `*_Properties` unit suites (`BasicEffect`, `AlphaTestEffect`, `SkinnedEffect`, `SpriteFont`), `ClearOverloads`, `ViewportState`, `DeviceValidation`, `PresentationParameters`, `BufferUsage` |
| `EASYGL_OR_OPENGL_SPECIFIC` — not a Vulkan parity requirement | 3 | `Anisotropic_GlState` (drives `easygl::Sampler` directly), `BackgroundContent_ContextOwnership`, `Gltf_ContextLoss` |
| `SEMANTIC_DIVERGENCE` — GLSL-source `ShaderEffect`; Vulkan's dialect is SPIR-V | 37 | the `*_Shader` family (28), `Bloom_*` (4), `ShaderEffect_*` (5) |
| `ALREADY_COVERED_DIFFERENTLY` | 12 | `*_Golden` families whose non-golden twin already runs on Vulkan (`BasicEffect_Golden`, `AlphaTestEffect_Golden`, `DualTextureEffect_Golden`, `EnvironmentMapEffect_Golden`, `SkinnedEffect_Golden`, `BlendState_Additive_Golden`, `DepthStencilState_WriteEnable_Golden`, `RasterizerState_CullMode_Golden`, `SpriteBatch_Rotation_Golden`, `TextureFilter_Linear_Golden`, `GoldenImage_Smoke`, `PixelTestGame_Smoke`) — the semantic property is covered, but the *cross-renderer image* comparison is not (Phase 12) |
| `CNAEXT_MODERN_OUT_OF_SCOPE` | 0 | none — the `Bloom_*` tests are XNA `BloomSample.fx` conversions through the ordinary `ShaderEffect`, not `modules/graphics-ext` |
| `OBSOLETE_OR_DUPLICATE` | 11 | `EasyGL_DualTexture` vs `EasyGL_DualTextureEffect_Blend`, `House3D_SmokeTest`/`Demo2D_SmokeTest` (demo smoke, Vulkan has `Vulkan_Demo2D_SmokeTest`), and nine name-spelling twins already counted above under a different rule |

The 37 `SEMANTIC_DIVERGENCE` tests are the single largest block and deliberately do **not** become
37 Vulkan tasks. `ShaderEffect` takes renderer-specific source by contract; EasyGL wants GLSL and
Vulkan wants SPIR-V, and making one source serve both is `plans/plan_csl.md`'s entire purpose.
What this plan owns is that Vulkan's *own* custom-effect surface is as capable as EasyGL's
(`VULKAN-251`–`VULKAN-256`) and that it says which dialect it wants (`VULKAN-250`).

### 9.4 Where Vulkan is already stronger than EasyGL

These must be preserved, not levelled down.

| Area | Evidence |
|---|---|
| Validation-message capture as a first-class test surface, including opt-in synchronization validation | `VulkanRenderer.cpp:2188`–`:2208`, `validationMessages_`/`validationMessageIdNames_`, `sRequestSyncValidation` (`REMED-GFX-144`); CTest `Vulkan_RenderTarget_ProducerConsumer_SyncVal` |
| Pipeline/descriptor **cardinality** contracts — proving a feature adds no pipeline variants and no extra submits | `GetGraphicsPipelineCacheEntryCountEXT`, `GetInstancedPipelineCacheSizeEXT`; CTests `Vulkan_EffectDescriptorCacheIdentity`, `Vulkan_InstancedVertexColor_Cardinality`, `Vulkan_DescriptorCapacityContract` |
| A calibrated orientation oracle plus per-family and render-target orientation regressions | `Vulkan_Orientation_Calibration`, `_Effects`, `_RenderTarget` (`REMED-GFX-011`) |
| Deferred-resource lifetime and render-pass ordering under a deferred command model | `Vulkan_DeferredResourceLifetime`, `_DeferredSourceLifetime`, `Vulkan_RenderTarget_ProducerConsumer`(+`_Msaa`), `Vulkan_RenderTarget_PassBoundary`, `Vulkan_CubeFaceReadbackDependency`, `Vulkan_Swapchain_Sync` |
| Per-instance `DepthStencilFormat` fidelity | `Vulkan_RenderTarget2D_DepthFormatFidelity` (Task 911) |
| `SpriteSortMode::Immediate` honoured explicitly at the renderer boundary | `SetImmediateMode` override at `VulkanRenderer.hpp:478`, consumed at `VulkanRenderer.cpp:1443`; EasyGL does not override it at all |
| Real hardware depth bias via `vkCmdSetDepthBias` dynamic state | ten `depthBiasEnable = VK_TRUE` pipeline sites; `docs/graphics-renderer-feature-matrix.md:460` records Vulkan as the only renderer with real depth bias |
| `MultiStreamVertexInput` reported **false** with a written reason instead of silently drawing from stream 0 | `VulkanRenderer.cpp:1747`–`:1753` (`REMED-GFX-201`) |

---

## 10. Parity matrix

Maintained. A row moves only when its "Vulkan evidence" cell names something a reader can run or
open. **No ✅ without evidence** — `NEEDS_DEEPER_AUDIT` is the honest state for a row this planning
session did not measure, and the audit task that owns it is named in the Tasks column.

Classifications: `PARITY` · `VULKAN_STRONGER` · `TEST_GAP` · `IMPLEMENTATION_GAP` ·
`SEMANTIC_DIVERGENCE` · `NOT_APPLICABLE` · `CNAEXT_OUT_OF_SCOPE` · `BLOCKED_BY_ENVIRONMENT` ·
`NEEDS_DEEPER_AUDIT`.

### 10.1 Renderer contract and capability reporting

| Area | EasyGL evidence | Vulkan evidence | Classification | Tasks |
|---|---|---|---|---|
| `SupportsCapability` truthfulness | explicit case per capability, device-derived (`EasyGLRenderer.cpp:5023`+) | `default: return true` (`VulkanRenderer.cpp:1754`) | `IMPLEMENTATION_GAP` | `VULKAN-020`, `VULKAN-021` |
| Capability reaching the renderer at all | — | 6 of the shared `GraphicsCapability` members are answered above the renderer seam | `NEEDS_DEEPER_AUDIT` | `VULKAN-022` |
| `GetShaderDialectEXT` | not overridden (`Unknown`) | not overridden (`Unknown`), but SPIR-V is mandatory | `IMPLEMENTATION_GAP` | `VULKAN-250` |
| `GetAppliedMultiSampleCountEXT` / `GetAppliedBackBufferFormatEXT` / `GetAppliedDepthStencilFormatEXT` (device) | identity defaults | identity defaults; `ApplyMultiSampleCount` overridden and real | `NEEDS_DEEPER_AUDIT` | `VULKAN-023` |
| `GetMaxTextureDimension` / `GetMaxVertexStreams` / `GetMax*ForProfileEXT` | not overridden | not overridden | `PARITY` (both take the shared default) | `VULKAN-024` |
| `Ensure3DSupported`, `SetUnsupported3DGraphicsCallBehavior` | not overridden | not overridden | `NOT_APPLICABLE` (3D-capable renderers) | — |
| `SetContextRecoveryEnabled` / `DebugSimulateContextLoss` / `DebugRestoreContext` | implemented (GL context loss) | not implemented | `NOT_APPLICABLE` — Vulkan has `VK_ERROR_DEVICE_LOST`, not GL context loss; the *observable CNA contract* is compared instead | `VULKAN-334` |
| `AcquireThreadContextLeaseEXT` | implemented | not implemented | `NEEDS_DEEPER_AUDIT` — is the CNA-level promise (background content loading) met another way? | `VULKAN-025` |
| `CanBeginDrawEXT` | overridden | not overridden (default `true`) | `NEEDS_DEEPER_AUDIT` | `VULKAN-026` |
| Validation-message capture | none | `validationMessages_` + `pMessageIdName` + opt-in sync validation | `VULKAN_STRONGER` | preserve |
| Pipeline/descriptor cardinality contracts | none | `GetGraphicsPipelineCacheEntryCountEXT`, `Vulkan_EffectDescriptorCacheIdentity` | `VULKAN_STRONGER` | preserve |

### 10.2 2D — SpriteBatch and SpriteFont

| Area | EasyGL evidence | Vulkan evidence | Classification | Tasks |
|---|---|---|---|---|
| `Begin`/`End`, repeated cycles | `EasyGL_SpriteBatch_*` | `Vulkan_SpriteBatch_MultiBeginEnd` | `PARITY` | — |
| Source rectangle, scale, rotation, origin | `EasyGL_SpriteBatch_SourceRect`/`_Scale`/`_RotationAroundOrigin` | `Vulkan_SpriteBatch_SourceRectangleCropping`/`_Scale`/`_Rotation` (same sources) | `PARITY` | — |
| `SpriteEffects` flip | `EasyGL_Sprite_Effects` | `Vulkan_SpriteEffects_Flip` (same source) | `PARITY` | — |
| Layer depth and sort modes | `EasyGL_SpriteBatch_LayerDepth` | `Vulkan_SpriteBatch_LayerDepthOrder`, `Vulkan_SpriteBatch3DOrder` | `PARITY` for `Deferred`/`BackToFront`/`FrontToBack`; `SpriteSortMode::Immediate`/`Texture` untested on either | `TEST_GAP` → `VULKAN-050` |
| `SpriteSortMode::Immediate` at the renderer boundary | not overridden | `SetImmediateMode` honoured (`VulkanRenderer.cpp:1443`) | `VULKAN_STRONGER`, untested | `VULKAN-050` |
| `BlendState` interaction, state leakage across batches | `EasyGL_SpriteBatch_BlendStateLeak` | `Vulkan_SpriteBatch_BlendState` (different property) | `TEST_GAP` | `VULKAN-051` |
| `SamplerState` interaction | `EasyGL_TextureAddressMode*` | `Vulkan_TextureAddressMode`, `Vulkan_PointSamplingContract`, `Vulkan_TextureFilter_PointVsLinear` | `PARITY`; `Mirror` through the SpriteBatch route is EasyGL-only | `TEST_GAP` → `VULKAN-052` |
| Transform matrix | `EasyGL_TransformMatrix_Translation` | `Vulkan_SpriteBatch_TransformMatrix` | `PARITY` | — |
| Custom effect through `Begin(effect)` | `EasyGL_ShaderEffect_GLSL`, `_SpriteBatch_Uniform` | `Vulkan_ShaderEffect_SpirV` | `SEMANTIC_DIVERGENCE` (dialect) + `TEST_GAP` (uniforms) | `VULKAN-250`, `VULKAN-251` |
| Drawing into a render target | `EasyGL_SpriteBatch_RenderTargetSize` | `Vulkan_SpriteBatch_CustomViewport_RT`, `Vulkan_SpriteBatch_ViewportSwitch` | `TEST_GAP` (RT-size-derived projection) | `VULKAN-053` |
| `SpriteFont` glyph placement, spacing, newline, default char | `EasyGL_SpriteFont_*` ×4 | `Vulkan_SpriteFont_SingleGlyph`/`_MultiGlyphSpacing`/`_Newline`/`_DefaultCharacterFallback` (same sources) | `PARITY` | — |
| `DrawString` rotation/scale/`SpriteEffects` | `EasyGL_SpriteFont_EffectsFlip`, `_EffectsRotationScale` | none | `TEST_GAP` | `VULKAN-054` |
| Renderer state leakage across `SpriteBatch` and 3D draws | `EasyGL_SpriteBatch_BlendStateLeak` | `Vulkan_SpriteBatch_BeginRasterizerState`, `Vulkan_StockEffectSamplerContract` | `NEEDS_DEEPER_AUDIT` | `VULKAN-055` |

### 10.3 Graphics state

| Area | EasyGL evidence | Vulkan evidence | Classification | Tasks |
|---|---|---|---|---|
| `BlendState` presets | `EasyGL_BlendState_*` ×5 | same five sources registered on Vulkan | `PARITY` | — |
| Separate RGB/alpha factors and functions | `EasyGL_BlendState_SeparateFactors`/`_SeparateFunctions` | same sources | `PARITY` | — |
| `BlendFactor` | `EasyGL_BlendState_BlendFactor` | same source + `Vulkan_RenderTarget_BlendFactor` | `PARITY` / partly `VULKAN_STRONGER` | — |
| `ColorWriteChannels` (+ per-MRT) and `MultiSampleMask` | `EasyGL_ColorWriteChannels` | `Vulkan_ColorWriteChannels`; `colorWriteBits`/`sampleMask` in the pipeline key | `PARITY`; per-MRT-slot masks untested on either | `NEEDS_DEEPER_AUDIT` → `VULKAN-090` |
| `DepthStencilState` compare/write/stencil/masks/ops/two-sided/reference | `EasyGL_DepthStencilState_*` ×6, `_ReferenceStencil` | the same six sources plus `Vulkan_GraphicsDevice_ReferenceStencil` | `PARITY` | — |
| `RasterizerState` cull modes | `EasyGL_RasterizerState_CullMode` | same source + `_Camera`, `_IndexedBasicEffect`, `Vulkan_FrontFaceWinding` | `VULKAN_STRONGER` | — |
| `FillMode::WireFrame` | emulated via line re-expansion | `Vulkan_FillMode_WireFrame`, gated on `fillModeNonSolid` | `PARITY` (different mechanism, both honest) | — |
| Depth bias | `EasyGL_DepthBias` **passes** on the same display and driver stack (§7.6) | `Vulkan_DepthBias` **fails** despite real `vkCmdSetDepthBias` on ten pipeline sites | `IMPLEMENTATION_GAP` — the lavapipe attribution is disproved | `VULKAN-091` |
| Scissor | `EasyGL_Scissor` | `Vulkan_ScissorTest`, `Vulkan_RenderTarget_Scissor`, `Vulkan_Deferred_Scissor` | `VULKAN_STRONGER` | — |
| Viewport | `EasyGL_ViewportState`, `_Viewport_Subregion` | `Vulkan_Viewport_Subregion`, `_RenderTarget_Viewport`, `_Deferred_Viewport`, `_ViewportResetAfterResize` | `TEST_GAP` for the public `Viewport` round-trip/survival contract | `VULKAN-092` |
| `SamplerState` filters, address modes, mip filter, anisotropy, LOD bias | `EasyGL_TextureAddressMode*`, `_TextureFilter_*`, `_Texture2D_AnisotropicSingleLevel`, `_SamplerComponentIsolation` | `Vulkan_TextureAddressMode`(+`_Clamp`/`_Mirror` through effects), `_TextureFilter_PointVsLinear`, `_TextureFilterMipContract`, `_TextureFilterOrdinalContract`, `_TextureAnisotropic_DualTextureEffect`, `_TextureMipFilter_DualTextureEffect` | `PARITY`; `SamplerComponentIsolation` and single-level anisotropy are EasyGL-only | `TEST_GAP` → `VULKAN-093` |
| `ApplySamplerAddressW` (volume W wrap) | overridden | overridden | `PARITY`, untested on either | `NEEDS_DEEPER_AUDIT` → `VULKAN-094` |
| MSAA-related state | `EasyGL_MSAA_4x_Readback`, `_MsaaChange` | `Vulkan_MSAA_4x_Readback`, `_MsaaFirstReadback`, `_MsaaDepthContract`, `_MsaaMipReadback`, `_BasicEffect_TexturedMsaa` | `TEST_GAP` for runtime sample-count change | `VULKAN-095` |

### 10.4 Buffers and draw semantics

| Area | EasyGL evidence | Vulkan evidence | Classification | Tasks |
|---|---|---|---|---|
| `VertexBuffer` upload bounds | sized to the real byte count (`EasyGLRenderer.cpp:7524`) | unchecked `memcpy` into a `capacity*64` allocation | `IMPLEMENTATION_GAP` (memory safety) | `VULKAN-130` |
| `IndexBuffer` upload bounds | sized to the real byte count | unchecked `memcpy` | `IMPLEMENTATION_GAP` | `VULKAN-131` |
| 16- and 32-bit indices | `EasyGL_DrawUserIndexedPrimitives_32` | `Vulkan_ModelJsonReader_32BitIndices`; `VK_INDEX_TYPE_UINT32` | `PARITY` for buffers; `TEST_GAP` for the user-primitives route | `VULKAN-134` |
| `SetData` / `GetData` round-trip | `EasyGL_VbSetData`, `_VertexBufferIndexBufferGetData` | `Vulkan_VertexBufferIndexBufferGetData` (same source) | `TEST_GAP` for `VbSetData` | `VULKAN-135` |
| `SetDataOptions` (`Discard`/`NoOverwrite`) under a deferred draw model | `EasyGL_DynamicBufferStress` (orphaning) | not overridden; one persistently-mapped buffer | `SEMANTIC_DIVERGENCE` (suspected) | `VULKAN-132`, `VULKAN-133` |
| `BufferUsage::WriteOnly` `GetData` refusal | `EasyGL_BufferUsage` | none | `TEST_GAP` (shared-layer behaviour, Vulkan integration unproven) | `VULKAN-136` |
| Empty (0-vertex/0-index) buffers | — | explicit `max(1, …)` allocation | `VULKAN_STRONGER`, untested | `VULKAN-137` |
| Disposal | `EasyGL_DisposedBuffer` | none | `TEST_GAP` + `IMPLEMENTATION_GAP` (per-dispose `vkDeviceWaitIdle`) | `VULKAN-138`, `VULKAN-392` |
| `VertexDeclaration` → native input layout | declaration-driven (`ApplyLayout`); arbitrary declarations bind | **stride-driven** — a ten-entry stride table picks the layout and a non-matching declaration is refused at draw time (§7.5) | `IMPLEMENTATION_GAP` | `VULKAN-144` |
| Supported strides, unknown-stride rejection, custom declarations | `EasyGL_VertexFormats_AllStrides`, `_UnknownStride_Rejected`, `_DrawUserPrimitives_CustomVD` | `Vulkan_VertexFormat_AllStrides` | `TEST_GAP` | `VULKAN-139`, `VULKAN-140` |
| `DrawPrimitives` / `DrawIndexedPrimitives` | covered on both | covered on both | `PARITY` | — |
| `DrawUserPrimitives` / `DrawUserIndexedPrimitives` | four dedicated tests | none | `TEST_GAP` | `VULKAN-134`, `VULKAN-140` |
| Instancing | `EasyGL_InstancedModel_Shader` (GLSL) | `Vulkan_DrawInstanced_3Instances`, `_InstancedVertexColor_Cardinality` | `VULKAN_STRONGER` | — |
| `baseVertex` / `startIndex` / `minVertexIndex` semantics | — | — | `NEEDS_DEEPER_AUDIT` | `VULKAN-141` |
| Range and primitive-type validation | `EasyGL_DrawRangeValidation`, `_PrimitiveTypeValidation`, `_DrawNoVertexBuffer`, `_DrawNoIndexBuffer` | none | `TEST_GAP` | `VULKAN-143` |
| Multi-stream vertex input | supported | reported `false` with a reason | `SEMANTIC_DIVERGENCE`, documented and honest | — |

### 10.5 Textures

| Area | EasyGL evidence | Vulkan evidence | Classification | Tasks |
|---|---|---|---|---|
| `Texture2D` `SetData`/`GetData`, full and partial | `EasyGL_Texture2D_*` | `Vulkan_Texture2D_GetDataContract`, `_GetDataTransferRange`, `_PartialRect_RoundTrip` (shared sources) | `PARITY` | — |
| `Texture2D` mip levels (`level > 0`) | `EasyGL_Texture2D_Mip` | `Vulkan_Texture2D_Mip_RoundTrip` (shared source), `UpdatePixelsLevel` real | `PARITY` — the feature matrix's ❌ is stale | `VULKAN-481` (doc) |
| NPOT | `EasyGL_NpotTexture` | `Vulkan_NpotTexture` (shared source) | `PARITY` | — |
| `Texture3D` slices, partial boxes, mips, readback | `EasyGL_Texture3D_*` ×4 | `Vulkan_Texture3D_Slices_RoundTrip`, `_Mip_RoundTrip`, `_PartialBox_Readback`, `_Mip_Layout` | `PARITY`; `Texture3D_PartialBox_RoundTrip` (write side) is EasyGL-only | `TEST_GAP` → `VULKAN-175` |
| `TextureCube` faces, partial rects, mips, content load | `EasyGL_TextureCube_*` ×4 | `Vulkan_TextureCube_Mip_RoundTrip`, `_PartialRect_RoundTrip`, `_ContentLoad`, `_CubeVolume_*Contract` | `PARITY`; `TextureCube_Faces_RoundTrip` is EasyGL-only | `TEST_GAP` → `VULKAN-176` |
| Block-compressed formats (`Dxt1`/`Dxt3`/`Dxt5`) | `EasyGL_DxtFormat`, `_DXT1_FromStream_Readback` | refused by the framework rule | `IMPLEMENTATION_GAP` | `VULKAN-170`, `VULKAN-171`, `VULKAN-172` |
| Packed 16-bit formats (`Bgr565`/`Bgra5551`/`Bgra4444`) | `EasyGL_Packed16Format` | refused | `IMPLEMENTATION_GAP` | `VULKAN-173` |
| `NormalizedByte2`/`NormalizedByte4` | classified `Supported` | refused | `IMPLEMENTATION_GAP` | `VULKAN-174` |
| Non-`Color` render-target formats | `ClassifyRenderTargetFormatEXT` real (incl. float probe) | deferred; refuses rather than substituting | `CNAEXT_OUT_OF_SCOPE` for float (`MOD-2223`); `IMPLEMENTATION_GAP` for the classic formats above | `VULKAN-171` |
| sRGB | — | `Vulkan_Texture2D_ColorFormat_Linear`, `Vulkan_Pbr_SrgbTransfer`, `Vulkan_ColorSpace_MidTone` | `VULKAN_STRONGER` | — |
| Texture lifetime, disposed-texture behaviour, bound-resource disposal | `EasyGL_DisposedResource`, `_BoundResourceDispose`, `_HandleRelease`, `_ResourceLeak` | `Vulkan_BoundTargetLifetime`, `_DeferredResourceLifetime`, `_DeferredSourceLifetime` | `TEST_GAP` for the public disposal contract | `VULKAN-177` |
| `HasDefinedMipLevel` | not overridden | not overridden | `PARITY` | — |

### 10.6 Render targets

| Area | EasyGL evidence | Vulkan evidence | Classification | Tasks |
|---|---|---|---|---|
| `RenderTarget2D` create/bind/clear/readback/unbind | `EasyGL_RenderTarget2D_Readback`, `_RT_Roundtrip` | `Vulkan_RenderTarget2D_FullCycle`, `_ClearOnlyRoundtrip`, `_RT_Roundtrip`, `_Properties` | `PARITY` | — |
| `RenderTargetCube` per-face, sample-after-unbind | `EasyGL_RenderTargetCube_SampleAfterUnbind`, `_Properties` | `Vulkan_RenderTargetCube_PerFace`, `_SampleAfterUnbind`, `_Properties`, `_PluralBinding`, `_PluralMRT` | `VULKAN_STRONGER` | — |
| Mip chains on both target types | `EasyGL_RenderTarget2D_MipComplete` | `Vulkan_RenderTarget2D_MipChain`, `_RenderTargetCube_MipChain`, `_MrtMipFinalization`, `_MsaaMipReadback` | `VULKAN_STRONGER` | — |
| MSAA, requested vs applied sample count, resolve | `EasyGL_RenderTarget2D_MsaaResolve`, `_GFX164_BoundMsaaAlpha` | `Vulkan_RenderTarget2D_MsaaResolve`, `_RenderTargetCube_MsaaResolve`, `_MsaaFace`, `_MRT_MsaaResolve`, `_MsaaFirstReadback`, `_MsaaDepthContract` | `VULKAN_STRONGER`; `GFX164_BoundMsaaAlpha` is EasyGL-only | `TEST_GAP` → `VULKAN-210` |
| Per-instance depth/stencil format fidelity | `EasyGL_DepthFormat`, `_RenderTargetCube_DepthFormat` | `Vulkan_RenderTarget2D_DepthFormatFidelity`, `_RenderTarget2D_DepthBuffer`, `_RenderTarget_DepthStencilUsage` | `PARITY`; the cube half is EasyGL-only | `TEST_GAP` → `VULKAN-211` |
| `RenderTargetUsage` preserve/discard | `EasyGL_RenderTargetUsage` | `Vulkan_RenderTargetUsage`, `_RenderTargetCube_Usage`, `ColorLoadOpIsClearEXT` (`REMED-GFX-136`) | `PARITY` | — |
| MRT, independent attachments, mixed formats | `EasyGL_MRT_TwoAttachments` | `Vulkan_MRT_MixedFormats`, `_MRT_MsaaResolve`, `_CubeMrtBinding`, `_RenderTargetCube_PluralMRT` | `VULKAN_STRONGER`; the plain 2-attachment source is EasyGL-only | `TEST_GAP` → `VULKAN-212` |
| Viewport/scissor interaction and reset | — | `Vulkan_RenderTarget_Viewport`, `_Scissor`, `_ViewportScissorReset` | `VULKAN_STRONGER` | — |
| Readback: partial, mip, cube face | `EasyGL_RenderTarget2D_Readback` | `Vulkan_RenderTargetCube_GetDataContract`, `_CubeFaceReadbackDependency`, `_RenderTarget_GetDataLifetime` | `VULKAN_STRONGER` | — |
| Ordering: producer/consumer, pass boundaries, backbuffer consumers | none | `Vulkan_RenderTarget_ProducerConsumer`(+`_Msaa`, `_SyncVal`), `_PassBoundary`, `_BackbufferConsumer`, `_FirstUse`, `_EffectSource` | `VULKAN_STRONGER` | preserve |
| Bound-target disposal, lifetime across deferred work | `EasyGL_BoundResourceDispose` | `Vulkan_BoundTargetLifetime`, `_DeferredResourceLifetime` | `PARITY` | — |
| Orientation (backbuffer vs render target) | — | `Vulkan_Orientation_Calibration`/`_Effects`/`_RenderTarget`, `_RenderTarget_SamplingOrientation`, `_XnaPixelCenter` | `VULKAN_STRONGER` | preserve |
| Backbuffer restoration after unbind | `EasyGL_RT_Roundtrip` | `Vulkan_RT_Roundtrip`, `_BackbufferReject`, `_Backbuffer_PassOrder` | `PARITY` | — |

### 10.7 Effects and shader-driven ordinary rendering

| Area | EasyGL evidence | Vulkan evidence | Classification | Tasks |
|---|---|---|---|---|
| `BasicEffect` — texture/vertex-colour/lighting/specular/emissive/per-pixel/fog | 18 tests | 15 tests incl. three fog variants EasyGL lacks | `PARITY`, plus `VULKAN_STRONGER` on fog breadth; `Combinations`, `DefaultLighting`, `LitVertexColor`, `PositionNormal`, `VertexColorClamp`, `WorldScalePrecision` are EasyGL-only | `TEST_GAP` → `VULKAN-257` |
| `AlphaTestEffect` — all compare functions, fog, null texture, vertex colour | 6 tests | 4 tests (3 shared sources) | `TEST_GAP` for `AlphaTestModes`, `AlphaCutout` | `VULKAN-258` |
| `DualTextureEffect` | 9 tests | 8 tests (several shared) | `TEST_GAP` for `IndependentUV` | `VULKAN-259` |
| `EnvironmentMapEffect` | 11 tests | 11 tests | `PARITY`; `Fresnel_Gradient` is EasyGL-only | `TEST_GAP` → `VULKAN-260` |
| `SkinnedEffect` | 14 tests | 15 tests | `PARITY`/`VULKAN_STRONGER`; `SkinnedBones`, `Vector4BoneIndices`, `BoneDeformation` are EasyGL-only | `TEST_GAP` → `VULKAN-261` |
| `PbrEffect` / `SkinnedPbrEffect` (ordinary build) | `EasyGL_PbrEffect_Golden`, `_Pbr_FresnelFactors`, `_Pbr_MaterialMaps`, `_Pbr_SrgbTransfer`, `_SkinnedPbrEffect_Golden` | `Vulkan_PbrEffect_Golden`, `_HandDerived`, `_Pbr_FresnelFactors`, `_Pbr_SrgbTransfer`, `_Pbr_TextureSlots`, `_SkinnedPbrEffect_Golden` | `PARITY`; `Pbr_MaterialMaps` is EasyGL-only | `TEST_GAP` → `VULKAN-262` |
| `Effect` clone / `CurrentTechnique` | `EasyGL_EffectClone`, `_EffectCurrentTechnique` | none | `TEST_GAP` | `VULKAN-263` |
| Custom `ShaderEffect` — dialect declaration | not declared | not declared, SPIR-V mandatory | `IMPLEMENTATION_GAP` | `VULKAN-250` |
| Custom `ShaderEffect` — uniform arrays | `SetUniformFloatArray`/`Vec2Array`/`Vec3Array`/`Mat4Array` | none | `IMPLEMENTATION_GAP` | `VULKAN-252` |
| Custom `ShaderEffect` — texture / cube / volume binding | `BindTexture`/`BindTextureCube`/`BindTexture3D` | none | `IMPLEMENTATION_GAP` | `VULKAN-253`, `VULKAN-254` |
| Custom `ShaderEffect` driving a 3D draw with a custom vertex layout | `EasyGL_ShaderEffect_3D`, `_CustomVertexLayout` | none | `IMPLEMENTATION_GAP` | `VULKAN-255` |
| Custom `ShaderEffect` uniforms through `SpriteBatch` | `EasyGL_ShaderEffect_SpriteBatch_Uniform` | `Vulkan_ShaderEffect_SpirV` (tint only) | `TEST_GAP` | `VULKAN-251` |
| The 37 GLSL-source sample-shader ports | 37 tests | none | `SEMANTIC_DIVERGENCE` — dialect, owned by `plans/plan_csl.md` | `VULKAN-256` (record, do not port) |
| Compiled XNA `.fx` bytecode | `CNA_EASYGL_COMPILED_EFFECTS` | `CNA_VULKAN_COMPILED_EFFECTS`, `VulkanCompiledEffect.cpp`, `VulkanCompiledEffectTests.cpp` | owned by `plans/plan_fx.md` (`FX-065`, `FX-110`, `FX-112`) | reference only |

### 10.8 Model, content and glTF renderer-facing behaviour

| Area | EasyGL evidence | Vulkan evidence | Classification | Tasks |
|---|---|---|---|---|
| `Model` draw, hierarchy, multi-mesh, textured, skinned playback | `EasyGL_ModelDraw_RedQuad`, `_Model_HierarchyChildMesh`, `_ModelJsonReader_*` ×5, `_Model_SkinnedAnimationPlayback`, `_Model_TwoMeshesEffects` | the same seven sources registered on Vulkan | `PARITY`; `ModelDraw_RedQuad` is EasyGL-only | `TEST_GAP` → `VULKAN-300` |
| glTF tangent handedness, mirrored tangents | `EasyGL_Gltf_TangentHandedness`, `_MirroredTangent` | none | `TEST_GAP` | `VULKAN-301` |
| glTF sampler wrap, texture transform per map | `EasyGL_Gltf_SamplerWrap`, `_TextureTransformPerMap` | none | `TEST_GAP` | `VULKAN-302` |
| glTF alpha blend, transmission ordering, base-colour factor × texture | `EasyGL_Gltf_AlphaBlend`, `_TransmissionOrdering`, `_BaseColorFactorTexture` | none | `TEST_GAP` | `VULKAN-303` |
| glTF skinned PBR with non-uniform joint scale | `EasyGL_Gltf_SkinnedPbrNonUniformJoint` | none | `TEST_GAP` | `VULKAN-304` |
| glTF behaviour across a GL context loss | `EasyGL_Gltf_ContextLoss` | — | `NOT_APPLICABLE` | — |

### 10.9 Device, presentation and lifecycle

| Area | EasyGL evidence | Vulkan evidence | Classification | Tasks |
|---|---|---|---|---|
| Presentation mode (letterbox/overscan/stretch/…) | real | silent no-op | `IMPLEMENTATION_GAP` | `VULKAN-330`, `VULKAN-331` |
| Runtime VSync / `PresentInterval` | `EasyGL_GraphicsDeviceManager_Vsync`, `_PresentInterval` | none; setter not overridden | `IMPLEMENTATION_GAP` | `VULKAN-332`, `VULKAN-333` |
| `PresentationParameters` round-trip | `EasyGL_PresentationParameters` | none | `TEST_GAP` | `VULKAN-335` |
| Backbuffer resize / real window resize | `EasyGL_BackbufferResize`, `_RealWindowResize` | `Vulkan_ViewportResetAfterResize` only | `TEST_GAP` | `VULKAN-336`, `VULKAN-337` |
| Repeated swapchain recreation, zero-sized/minimized windows | — | `OnSurfaceChanged` guards zero size; `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR` handled | `TEST_GAP` | `VULKAN-338` |
| Runtime MSAA change (`ApplyMultiSampleCount`) | `EasyGL_MsaaChange` (echo only) | real teardown/rebuild, untested | `TEST_GAP` — and the riskier of the two implementations | `VULKAN-095` |
| Device dispose order, double dispose, move semantics, handle release, resource leak, resource events, device-reset events | `EasyGL_*` ×7 | none | `TEST_GAP` | `VULKAN-339`–`VULKAN-343` |
| `GraphicsDevice` argument validation | `EasyGL_DeviceValidation` | none | `TEST_GAP` | `VULKAN-344` |
| `Clear` overloads and `ClearOptions` | `EasyGL_ClearOverloads` | `Vulkan_GraphicsDevice_ClearOptions`, `_ClearDepth`, `_ClearStencil`, `_OrderedClear` | `TEST_GAP` for the overload/validation matrix | `VULKAN-345` |
| Context loss / device lost | `EasyGL_*ContextLoss*` | — | `NOT_APPLICABLE` mechanism; the observable CNA contract still needs comparing | `VULKAN-334` |
| Present lifecycle | — | `Vulkan_PresentLifecycle`, `_Swapchain_Sync` | `VULKAN_STRONGER` | — |

### 10.10 Queries and other ordinary facilities

| Area | EasyGL evidence | Vulkan evidence | Classification | Tasks |
|---|---|---|---|---|
| `OcclusionQuery` visible vs occluded discrimination | `EasyGL_OcclusionQuery_VisibleQuad`, `_OccludedQuad` | `Vulkan_OcclusionQuery_PixelCount` (both directions + multi-draw span) | `PARITY` | — |
| Query reset / reuse cycle | `EasyGL_OcclusionQuery_Cycle` | none | `TEST_GAP` | `VULKAN-371` |
| Default-state occlusion behaviour | — | `Vulkan_GraphicsDevice_DefaultStateOcclusion` (shared source) | `PARITY` | — |
| `PixelCountIsPreciseEXT` | overridden (`false` on GLES3) | not overridden (`true`) | `NEEDS_DEEPER_AUDIT` | `VULKAN-370` |
| GPU timers / debug regions | — | `SetStringMarkerEXT` implemented | `CNAEXT_OUT_OF_SCOPE` | — |

### 10.11 Validation, robustness and bounded resources

| Area | EasyGL evidence | Vulkan evidence | Classification | Tasks |
|---|---|---|---|---|
| Validation layer clean across the suite | N/A | messenger + capture exist; no suite-wide gate | `TEST_GAP` | `VULKAN-393` |
| Descriptor-pool exhaustion behaviour | N/A | silent white substitution | `IMPLEMENTATION_GAP` | `VULKAN-390`, `VULKAN-391` |
| Pipeline-cache growth under ordinary state churn | N/A | cardinality counters exist, no growth bound asserted | `NEEDS_DEEPER_AUDIT` | `VULKAN-394` |
| Sampler-cache growth | N/A | `samplerCache_` unbounded `std::map` | `NEEDS_DEEPER_AUDIT` | `VULKAN-395` |
| Full-device stalls in ordinary paths | N/A | `vkDeviceWaitIdle` on every buffer dispose; `vkQueueWaitIdle` per one-time command | `IMPLEMENTATION_GAP` (dispose) / `NEEDS_DEEPER_AUDIT` (uploads) | `VULKAN-392`, `VULKAN-396` |
| ASan/UBSan over CNA-owned CPU code | `EasyGL_ResourceLeak` | none | `TEST_GAP` | `VULKAN-397` |
| Repeated begin/end, bind/unbind, upload, resize stress | `EasyGL_DynamicBufferStress` | none | `TEST_GAP` | `VULKAN-398` |

### 10.12 Cross-renderer conformance

| Area | EasyGL evidence | Vulkan evidence | Classification | Tasks |
|---|---|---|---|---|
| Shared diagnostic scene + comparator | `cna_diag_easygl` | `cna_diag_vulkan` (built, not a CTest) | `PARITY` infrastructure, no gate | `VULKAN-430` |
| Shared 2D corpus | `cna_corpus2d_easygl` | none | `TEST_GAP` | `VULKAN-431` |
| Golden-image comparison against the *same* PNGs | 17 goldens under `examples/golden/` | 3 already reused on Vulkan (`Vulkan_PbrEffect_Golden`, `_SkinnedPbrEffect_Golden`, `_SkinnedEffect_VertexColor`) | `TEST_GAP` for the remaining 11 families | `VULKAN-432`–`VULKAN-436` |

---

## 11. Task status legend

| Symbol | Meaning |
|---|---|
| ✅ | complete **and verified against the row's own acceptance criterion** |
| 🟨 | partial — code or evidence exists but the criterion is not met; the row says what is missing |
| ⬜ | not started |
| ⛔ | deliberately not done, or blocked; the reason is written in the row itself, never only in a commit message |

Task IDs are `VULKAN-###`, unique and stable forever. Numbering is **sparse inside each phase on
purpose** so work discovered later takes a free neighbouring number instead of forcing a renumbering
pass (`plan_csl.md`'s convention; see also `feedback: renumbering breaks external citations`).

**One task = one commit.** Every row below is sized to be independently verifiable and independently
committable. A row that turns out to be larger than that is split into new rows before it is
started, not silently widened.

---

## 12. Phase 0 — Baseline and audit infrastructure (`VULKAN-001`–`VULKAN-019`)

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-001 | Record the implementation baseline commit and both build configurations | ⬜ | This file's §7 is re-confirmed against the commit work actually starts from: branch, SHA, the three worktree fixes of §7.2, both `cmake` command lines, and `cmake --build` exit codes for both directories. A configure that needs a fourth fix is added to §7.2 rather than worked around locally. |
| VULKAN-002 | Establish a green **EasyGL** test baseline and record it | ⬜ | `ctest --test-dir cmake-build-easygl` run under Xvfb, full output captured, then `--rerun-failed -j1` to separate flakes from failures. The exact pass/fail list is written into this file's §7 as the reference-renderer baseline. A pre-existing EasyGL failure is named, not rounded away. |
| VULKAN-003 | Establish a green **Vulkan** test baseline and record it | ⬜ | Same, for `cmake-build-vulkan`, and separately for `-R '^Vulkan_'`. Every failure is classified: CNA defect, environment, or driver. Nothing is called "accepted" without naming what accepts it. |
| VULKAN-004 | Fix or quarantine the pre-existing non-Vulkan build break so the suite can be built at all | ✅ | **Fixed at the call site.** `CnbTextureCodecTests.cpp:475` passed one argument to `CnbDocument::Parse(std::vector<std::uint8_t>, const std::string& origin, const CnbReadLimits&)`; `origin` has no default and every one of the file's other eleven `Parse` calls supplies one (`:190`, `:210`, `:217`, …). The asset is encoded as `EncodeTexture2DToCnb(data, "mipped")`, so the origin is `"mipped.cnb"`. Introduced by `347139500` (`feat(cnb): generate mip chains when compiling a source image`); not another plan's in-flight work, so it is repaired rather than quarantined. **Evidence:** `cmake --build cmake-build-vulkan -j16` exits **0**; `ctest -R CnbTextureCodecTest` is 15/15 including `GeneratedMipChainSurvivesAnEncodeDecodeRoundTrip`. **Discovered while closing it:** §7.3's `Total Tests: 302` / `407` were measured with this break present. `gtest_discover_tests` runs each test binary *after* it links, so the unbuildable `cna_content_test_objects` suppressed every test it and its dependents own. With the build green the same command reports **9071** registered CTests for the Vulkan configuration. The `215 ^Vulkan_` count is unchanged and is the number this plan's parity arithmetic actually uses. `VULKAN-006` owns re-stating §7.3 from a green build. |
| VULKAN-005 | Record the Vulkan physical device, driver, extensions and layer environment | ⬜ | §7.1 is regenerated from `vulkaninfo --summary` plus the renderer's own startup capability dump, and the **selected** device is named (this machine has both RADV and llvmpipe, and `PickPhysicalDevice` takes the first that qualifies — which device it actually picks is recorded, not assumed). |
| VULKAN-006 | Enumerate both renderers' registered CTests mechanically and check the numbers in §7.3 | ⬜ | `ctest -N` for both directories; the four counts in §7.3 either match or are corrected here. Output is captured **before** any other ctest invocation, because `ctest -N` overwrites `LastTest.log`. |
| VULKAN-007 | Script the EasyGL↔Vulkan coverage comparison and keep it re-runnable | ⬜ | A small script under `tools/` (Python or shell — no new build system, no new build directory) that parses both `examples/CMakeLists.txt` files and emits the three §7.4 sets: shared sources, EasyGL-only CTests, Vulkan-only CTests. It reproduces §9.3's 143/51 numbers on this commit. Non-goal: making it a CTest. |
| VULKAN-008 | Capture Vulkan's current capability reporting as a checked-in snapshot | ⬜ | Every `GraphicsCapability` member, every `Supports*EXT`/`Get*EXT` query and `GetShaderDialectEXT`, read through the **`GraphicsDevice` seam** (not the renderer's switch — 6 members are answered above it), dumped for the real device. The snapshot is the input to `VULKAN-020`–`VULKAN-026` and the oracle for `VULKAN-470`. |
| VULKAN-009 | Turn on validation for the whole Vulkan suite and record the current message inventory | ⬜ | A Debug build already enables the layer; this task records what the suite actually emits today, grouped by `pMessageIdName`, so `VULKAN-393` has a starting set to drive to zero. Attribution (CNA / driver / layer) per message class. |
| VULKAN-010 | Record the EasyGL delta baseline for the final gate | ⬜ | The EasyGL commit and CTest list this plan is measuring against are written down, so `VULKAN-487` can compute what EasyGL gained in the meantime rather than re-deriving the whole comparison. |
| VULKAN-011 | Cross-check the shared-source failures against EasyGL | ✅ | **Executed during the planning session** (§7.6): `EasyGL_XnaPixelCenter`, `EasyGL_PointSamplingContract`, `EasyGL_DescriptorCapacityContract` and `EasyGL_DepthBias` all pass on the same machine, display and session where their Vulkan twins fail. Verdict recorded in §7.6 and §9.1 F-18: the four are Vulkan-specific divergences, not stale tests and not a software-driver artefact. Remediation is owned by `VULKAN-097` and `VULKAN-091`. |
| VULKAN-013 | Make a Vulkan environment with no usable device skip instead of core-dumping | ⬜ | Forcing the RADV ICD under Xvfb produces `terminate called after throwing an instance of 'std::runtime_error' … Vulkan: no suitable GPU` and a core dump; an SDL window created without `VK_KHR_surface` produces the same via `CNA::Platform::PlatformException`. A CTest that aborts cannot be told apart from one that failed, which is exactly how an environment problem gets mistaken for a regression. The example harness must catch device-creation failure and exit with the project's skip convention plus the reason. **Non-goal:** swallowing failures from a device that *was* created. |
| VULKAN-012 | Re-measure every Vulkan result that was attributed to the software driver | ⬜ | §7.1/F-17 measured why this is not simply a matter of re-running: under Xvfb the renderer cannot select RADV at all. This task first **establishes a DRI3-capable virtual display** (Xorg with the `dummy` driver, or Xwayland) so RADV can present without using the owner's real desktop — the owner has asked for virtual displays. Then re-run at minimum `Vulkan_DepthBias`, `Vulkan_OcclusionQuery_PixelCount`, `Vulkan_MSAA_4x_Readback` and the three F-18 tests on RADV and on llvmpipe, and record which results are driver-dependent. A result that passes on real hardware is a driver-dependency finding, not a silent fix. If no DRI3-capable virtual display can be produced here, that is recorded as `BLOCKED_BY_ENVIRONMENT` with the exact commands tried — never inferred away. |
| VULKAN-014 | Decide and document the Vulkan golden-image policy | ⬜ | Three Vulkan CTests already compare against EasyGL-authored PNGs in `examples/golden/` with per-test tolerances. Phase 12 needs a written rule: which comparisons share a golden (and with what tolerance and why), and which need a Vulkan-specific golden (and why the difference is legitimate). Non-goal: bit-identical pixels for floating-point lighting. |
| VULKAN-016 | Write the Phase-0 findings into this file and open any tasks they imply | ⬜ | Closing Phase 0 requires that every number in §7 and §9 is either confirmed or corrected here, and that any new gap found while establishing the baseline has its own `VULKAN-*` row **before** this task is marked ✅. |

---

## 13. Phase 1 — Renderer contract and capability audit (`VULKAN-020`–`VULKAN-049`)

Method for every audit row in this phase: for each `IGraphicsRenderer` (and nested-interface)
virtual, record four things — EasyGL's override, Vulkan's override, the inherited default, and the
observable behaviour through the public XNA API. **A default no-op inherited by Vulkan while EasyGL
implements meaningful behaviour is a prime suspect and must be resolved to a classification, never
left blank.**

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-020 | Replace `SupportsCapability`'s catch-all `default: return true` with an explicit case per `GraphicsCapability` member | ✅ | **Done, and the catch-all was answering wrongly, not merely vaguely.** All 19 members now have their own arm and the switch has **no `default:`**; a value that is not an enumerator falls out to a `return false`. `cna_renderer_vulkan` is compiled with `-Werror=switch` (`modules/renderers/vulkan/CMakeLists.txt`, following `cmake/Harnesses.cmake`'s own `-Werror=deprecated-declarations` precedent), so appending a member to `CNA::GraphicsCapability` stops this target building. **Probed:** appending a throwaway enumerator produces `error: enumeration value 'ProbeOnlyNewCapability' not handled in switch [-Werror=switch]`. All three of the family's TUs were measured `-Wswitch` clean first, so the flag adds no pre-existing noise. **The defect the catch-all was hiding:** six members are answered *above* the renderer by `GraphicsDevice::SupportsCapability` from a virtual whose default is `false`. The renderer's catch-all said `true` for all six, so `IGraphicsRenderer::SupportsCapability` — the answer a C-ABI caller or a renderer-level test gets — **disagreed with the seam a game sees**, and for the two float render-target entries it claimed a format `RenderTarget2D`'s constructor refuses by name (§6.2's "reported true with no reachable path", exactly). Each of the six now answers from the *same* virtual the seam uses. Two device-derived answers also stopped being constants: `MultipleRenderTargets` comes from `VkPhysicalDeviceLimits::maxColorAttachments`, and `StencilBuffer` from whether `FindDepthFormat()`'s pick actually carries a stencil aspect (it falls back to `VK_FORMAT_D32_SFLOAT`, on which `StencilEnable` cannot work however correctly the pipeline maps it). The limits are cached where the device is *selected*, because this machine has two and `PickPhysicalDevice` takes the first that qualifies. **Evidence:** new CTest `Vulkan_CapabilityContract` (`vulkan_capability_contract_test.cpp`), **24/24** — leg A asks both seams the same 19 questions and requires the same 19 answers, leg B requires an out-of-range cast to be refused, leg C drives every `false` answer into the public API and requires a named refusal. **Mutation probe:** with the old three-case + `default: return true` switch restored, the run is **15/24** — six seam disagreements, the out-of-range claim, and both float render-target entries reported `true` beside a constructor that refuses them. No mutation was needed to make leg A meaningful; it fails on the pre-task code as written. **A false positive worth recording:** leg C's first draft bound two `VertexPositionColor` buffers and the draw was *accepted*. That is not a defect — CNA reproduces FNA3D's rule that a later stream repeating an already-claimed `(usage, usageIndex)` pair is "not in use" and contributes nothing, so two identical declarations are one stream. The leg now binds a position-only and a colour-only declaration, which is a genuine split vertex, and gets the `NotSupportedException`. **Not re-proved here:** the `true` answers, each of which already has a dedicated Vulkan CTest that observes the behaviour — `ThreeD`/`Vulkan_BasicEffect_*`, `DepthStencilBuffer`/`Vulkan_GraphicsDevice_DepthContract`, `MultiSampleAntiAliasing`/`Vulkan_MSAA_4x_Readback`, `MultipleRenderTargets`/`Vulkan_MRT_MixedFormats`, `AnisotropicFiltering`/`Vulkan_TextureAnisotropic_DualTextureEffect`, `WireFrame`/`Vulkan_FillMode_WireFrame`, `OcclusionQuery`/`Vulkan_OcclusionQuery_PixelCount`, `CustomEffects`/`Vulkan_ShaderEffect_SpirV`, `Texture3D`/`Vulkan_Texture3D_Slices_RoundTrip`, `Instancing`/`Vulkan_DrawInstanced_3Instances`, `StencilBuffer`/`Vulkan_DepthStencilState_StencilOps`, `AdditiveBlending`/`Vulkan_AdditiveBlendContract`. Duplicating them into a weaker second copy would add no evidence. **Non-goal honoured:** no answer changed. `MultiSampleAntiAliasing` keeps its unconditional `true` here with a comment; `VULKAN-021` owns replacing it with the device's own sample-count masks. **Suite:** 212/218, the same six pre-existing failures. |
| VULKAN-021 | Derive `MultiSampleAntiAliasing` from the device instead of returning `true` unconditionally | ✅ | **Done.** The answer is `(framebufferColorSampleCounts & framebufferDepthSampleCounts) & ~VK_SAMPLE_COUNT_1_BIT`, read from the limits `VULKAN-020` cached at device *selection*. Colour **and** depth, deliberately: every MSAA path in this renderer attaches both, so a device that could multisample colour alone still could not run its MSAA render pass, and claiming the capability from the colour mask alone would be a promise `ApplyMultiSampleCount` then declines to keep. It is the same intersection `PickSampleCount` already reads, asked the same way. **Evidence:** leg E added to `Vulkan_CapabilityContract` (now **25/25**) requires the capability to agree, *in both directions*, with the count `GraphicsDeviceManager.setPreferMultiSampling(true)` + `ApplyChanges()` actually reaches — the public route, not the CNAEXT recreate escape hatch. Measured here: `capability=true, applied MultiSampleCount=4`. **Mutation probe:** forcing the derivation to `false` (what a 1-sample-only device would produce) fails leg E with `capability=false, applied MultiSampleCount=4`. **Limit of the evidence, recorded rather than glossed:** the run device (lavapipe) *does* offer 4x, so leg E discriminates an under-reporting derivation but cannot, on this device, tell a correctly-derived `true` from the old unconditional `true`. Separating those needs a device whose masks hold only `VK_SAMPLE_COUNT_1_BIT`, which this environment does not have; the renderer's own startup line (`MSAA up to 4x`) is the device-derived number the run saw. `VULKAN-012` re-measures on RADV when it provides one. **Suite:** 212/218, the same six pre-existing failures. |
| VULKAN-022 | Audit capability reporting at the `GraphicsDevice` seam, not the renderer's switch | ⬜ | Six `GraphicsCapability` members are answered in `GraphicsDevice` before the renderer is asked. Record, per member, whether the Vulkan answer a game sees comes from the renderer or from shared code, and whether the shared answer is right for Vulkan. Any member where the two disagree becomes a new `VULKAN-*` row before this closes. |
| VULKAN-023 | Audit the applied-vs-requested reporting trio | ⬜ | `GetAppliedBackBufferFormatEXT`, `GetAppliedMultiSampleCountEXT` and `GetAppliedDepthStencilFormatEXT` all take the identity default on Vulkan while `ApplyMultiSampleCount` genuinely clamps. Determine whether `PresentationParameters` can therefore echo back a sample count the device did not apply; if so, open a remediation row. Test if a defect is found: request an unsupported count, assert the echoed value equals the applied one. |
| VULKAN-024 | Audit the `GraphicsProfile` ceiling and limit queries | ⬜ | `GetMaxTextureSizeForProfileEXT`, `GetMaxCubeSizeForProfileEXT`, `GetMaxVolumeExtentForProfileEXT`, `GetMaxRenderTargetsForProfileEXT`, `GetMaxTextureDimension`, `GetMaxVertexStreams` are unoverridden on both renderers. Confirm the shared defaults are correct for a Vulkan device (a device whose `maxImageDimension2D` is below a profile ceiling is the interesting case) and record the verdict. Only open a task if a real device can contradict a default. |
| VULKAN-025 | Decide what `AcquireThreadContextLeaseEXT` means on Vulkan | ⬜ | EasyGL implements it for background content loading on a second GL context. Vulkan has no context to lease. Determine what the *CNA-level* promise is (can a game load content on a worker thread while Vulkan renders?), then either implement the equivalent, or record the refusal with the reason and make it observable through the return value. Non-goal: inventing a thread-safety guarantee the renderer does not have. |
| VULKAN-026 | Decide whether Vulkan needs a `CanBeginDrawEXT` override | ⬜ | EasyGL overrides it; Vulkan takes the `true` default. Identify the states in which a Vulkan draw cannot legally begin (no swapchain, zero-sized surface, mid-recreation) and either return `false` there or record that the default is right because those states are impossible. Test: whichever answer is chosen, a test drives the renderer into the state and asserts the answer. |
| VULKAN-027 | Complete the interface-by-interface contract table and open every gap it finds | ⬜ | A table in this file with one row per virtual of `IGraphicsRenderer`, `IVertexBufferRenderer`, `IIndexBufferRenderer`, `ITextureRenderer`, `ITexture3DRenderer`, `ITextureCubeRenderer`, `IRenderTargetRenderer`, `IRenderTargetCubeRenderer`, `IEffectRenderer`, `ISpriteBatchRenderer`, `IOcclusionQueryRenderer` — EasyGL / Vulkan / default / observable behaviour / classification. **Closing this task requires that every `IMPLEMENTATION_GAP` or `TEST_GAP` it discovers already has its own `VULKAN-*` row.** CNAEXT-only virtuals are marked out of scope and skipped, not audited. |

---

## 14. Phase 2 — 2D: SpriteBatch and SpriteFont (`VULKAN-050`–`VULKAN-089`)

`docs/graphics-renderer-feature-matrix.md` still says these Vulkan cells were "not separately
re-audited (Task 861)". They have since gained twelve dedicated CTests (§9.2 D-05). This phase
re-audits from the current tree rather than from that sentence.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-050 | Prove `SpriteSortMode::Immediate` and `SpriteSortMode::Texture` on Vulkan | ⬜ | Vulkan is the only renderer that overrides `SetImmediateMode`, and nothing tests it. A test draws sprites whose correct output differs between `Immediate` and `Deferred` (an intervening `GraphicsDevice` state change, or a texture mutated between two `Draw` calls), asserts both, and asserts `Texture`-sorted batching does not reorder overlapping sprites incorrectly. Must fail if `SetImmediateMode` is stubbed out. Files: `modules/renderers/vulkan/src/VulkanRenderer.cpp:1443`, `modules/graphics/src/Xna/SpriteBatch.cpp:234`. |
| VULKAN-051 | Port the SpriteBatch blend-state leak proof to Vulkan | ⬜ | Register `easygl_spritebatch_blendstate_leak_test.cpp` on Vulkan (the source is public-API only). It must prove that a `Begin(SpriteSortMode, BlendState)` does not leave its blend state applied to a following 3D draw or a following default `Begin()`. On Vulkan the state lives in a pipeline key rather than in GL state, so this is a genuinely different mechanism and a genuine regression risk. If it fails, the fix is a Vulkan task, not a shared one. |
| VULKAN-052 | Prove `TextureAddressMode::Mirror` through the plain SpriteBatch route on Vulkan | ⬜ | Vulkan proves Clamp/Mirror through `DualTextureEffect` and Wrap/Mirror through `Vulkan_TextureAddressMode`, but `easygl_texture_address_mode_mirror_test.cpp` (SpriteBatch with a source rectangle larger than the texture) has no Vulkan twin. Register it; the sampler on the SpriteBatch path comes from `SetSamplerAddressMode`, a different code path from `ApplySamplerState`. |
| VULKAN-053 | Prove SpriteBatch's render-target-derived projection on Vulkan | ⬜ | Register `easygl_spritebatch_rendertarget_size_test.cpp`. It asserts that `SpriteBatch` inside a bound `RenderTarget2D` of a *different* size from the backbuffer places sprites by the target's size, not the backbuffer's. Vulkan derives the viewport from the active pass; this is the discriminating case. |
| VULKAN-054 | Prove `DrawString` rotation, scale and `SpriteEffects` on Vulkan | ⬜ | Register `easygl_spritefont_effects_flip_test.cpp` and `easygl_spritefont_effects_rotation_scale_test.cpp`. Vulkan proves glyph placement, spacing, newline and the default-character fallback with the shared sources already; the transformed-glyph half is the gap. |
| VULKAN-055 | Audit renderer state leakage across SpriteBatch↔3D boundaries on Vulkan | ⬜ | With `Vulkan_SpriteBatch_BeginRasterizerState` and `Vulkan_StockEffectSamplerContract` as the starting evidence, enumerate every piece of state a `SpriteBatch` batch sets at the renderer boundary (blend, sampler filter, sampler address, transform, custom effect, immediate mode, viewport, scissor) and check each survives or is restored per the XNA contract across `End()` → 3D draw → `Begin()`. Every leak found gets its own row before this closes. |
| VULKAN-056 | Audit `SpriteBatch` `Draw` overload coverage on Vulkan against the XNA surface | ⬜ | Enumerate the public `Draw`/`DrawString` overloads and map each to the Vulkan CTest that covers it. Overloads with no coverage anywhere become rows; overloads covered only by shared renderer-independent code are recorded as `SHARED_LOGIC` with the reason. |

---

## 15. Phase 3 — Graphics state (`VULKAN-090`–`VULKAN-129`)

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-090 | Prove per-MRT `ColorWriteChannels1/2/3` and `MultiSampleMask` on Vulkan | ⬜ | The Vulkan pipeline key already carries `colorWriteBits` and `sampleMask` (`REMED-GFX-077`), and `Vulkan_ColorWriteChannels` covers slot 0 only. A test binds two render targets, sets a different `ColorWriteChannels` per slot and asserts each attachment received only its permitted channels; a second check sets a `MultiSampleMask` that excludes half the samples on a 4x target and asserts the resolved coverage changed. Must fail if the per-slot masks are collapsed to slot 0's. |
| VULKAN-091 | Root-cause and fix `Vulkan_DepthBias` | ✅ | **Root cause: the test's depth convention was OpenGL's, and the renderer was right.** `vulkan_depth_bias_test.cpp` placed its flat triangles at `z = 0` under an identity projection and called that "depth 0.5" -- the GL mapping, which clips `z` to `[-1,1]`. XNA uses Direct3D 9's convention (`z` in `[0,w]` -> depth `[0,1]`) and so does Vulkan, so `z = 0` is the **near plane**: the viewport depth range clamps at 0, the biased redraw stays at exactly 0, `LESS` fails, and the strip stays red. Nothing can be biased in front of the near plane, and that clamp is not a dropped bias. **Proof it is not the driver, which the row demanded before this could be said:** `spikes/vulkan-depth-bias-spike/` runs the same coplanar experiment **off screen** -- no surface, no swapchain -- on every device the loader offers, and `vkCmdSetDepthBias`'s constant factor behaves exactly as Vulkan specifies on **both** drivers: llvmpipe with the very `D24_UNORM_S8_UINT` format this renderer picks, and AMD RADV, which the renderer itself cannot reach under Xvfb. **That off-screen route is the finding worth reusing**: RADV is unreachable here only because PRESENTATION needs DRI3, so any Vulkan question that does not need a swapchain can be asked on real hardware today, without waiting for `VULKAN-012`. **Proof it is not the renderer:** moving the flat triangles to `z = 0.5` makes CNA pass 4/4 with no production change at all. **Fix:** the test's flat scenarios sit at `z = 0.5`, the tilted ones span 0.2..0.8 instead of straddling (and being clipped by) the near plane, and a **fifth leg** is added that pins the convention -- a flat triangle *at* `z = 0` with the same `-1e6` bias must stay RED. That leg is the one whose absence let the whole file be written against OpenGL's mapping and read a correct renderer as broken. **Evidence:** `Vulkan_DepthBias` **5/5**. **Mutation check** (the row's own requirement): forcing the constant factor to 0 at `vkCmdSetDepthBias` gives 4/5, failing exactly the `-1e6` leg. **No tolerance was widened and no driver was blamed.** **Discovered while closing it:** `EasyGL_DepthBias` passes from the identical premise only because EasyGL never remaps GL's `[-1,1]` clip depth, so the same XNA `z = 0` lands at depth 0.5 there. That is a real divergence from XNA in the *reference* renderer -- recorded as **F-19** and owned by **`VULKAN-098`**. |
| VULKAN-092 | Prove the public `Viewport` contract on Vulkan | ⬜ | Register `easygl_viewport_state_test.cpp`: the initial viewport matches `PresentationParameters`, `setViewportProperty` round-trips exactly (including `minDepth`/`maxDepth`), and the viewport survives `Clear()` and `DrawUserPrimitives()` without being reset. Vulkan sets the viewport as dynamic state per recorded command buffer, so "survives a Clear" is a real Vulkan question, not a shared-code one. |
| VULKAN-093 | Prove sampler component isolation and single-level anisotropy on Vulkan | ⬜ | Register `sampler_component_isolation_contract_test.cpp` and `easygl_texture2d_anisotropic_singlelevel_test.cpp`. The first asserts that changing one `SamplerState` component does not silently change another (Vulkan caches whole `VkSampler` objects keyed by a struct — exactly where a dropped field hides); the second asserts anisotropy on a texture with one mip level does not blur or fail. |
| VULKAN-094 | Prove `SamplerState.AddressW` on Vulkan | ⬜ | `ApplySamplerAddressW` is overridden on both renderers and tested on neither. A `Texture3D` sampled with W wrap vs W clamp at a coordinate outside `[0,1]` produces two distinguishable results; assert both. If Vulkan's volume sampling path cannot express it, that is a finding with its own row. |
| VULKAN-095 | Prove the runtime MSAA sample-count change on Vulkan | ⬜ | Register `easygl_msaa_change_test.cpp` on Vulkan and extend it there: `ApplyMultiSampleCount` on Vulkan really does `vkDeviceWaitIdle` + tear down and rebuild every sample-count-dependent render pass and pipeline (`VulkanRenderer.cpp:10010`+), where EasyGL merely echoes. The test must assert that rendering is still correct **after** the change (a drawn quad reads back right), not only that the reported count changed, and that no resource created before the change dangles. This is the highest-risk untested path in Phase 3. |
| VULKAN-097 | Root-cause and fix the shared pixel-centre / magnifying-filter divergence (F-18) | ✅ | **Fixed, and F-18's "likely one root cause" hypothesis is confirmed: one change turned all three green.** **Root cause:** XNA 4.0 addresses pixel *centres* with integer coordinates (Direct3D 9); Vulkan addresses pixel corners, like OpenGL. This renderer applied no correction, so the screen-space triangle `(x,y),(x+1,y),(x,y+1)` landed entirely on an excluded fill edge and vanished, and every magnifying filter sampled half a texel off. **Fix:** `XnaPixelCenterCorrectionEXT()` returns the same slightly-under-half-pixel clip-space translation EasyGL and Wine use (63/64 of a clip unit = 63/128 of a viewport pixel), post-multiplied into `world * view * projection` in XNA row-vector order at all four WVP sites, and onto the projection half of the instanced route's view-projection product. **The one thing that looks like a bug and is not:** the Y sign is EasyGL's, unchanged. This renderer's vertex shaders negate `clip.y` themselves for Vulkan NDC, and that negation cancels the NDC-direction difference, so the identical offset produces the identical half-pixel shift down-and-right on screen. Working that out is the whole of the port. **Two inherited rules, both with their reasons:** the correction is suppressed when the destination is multisampled (REMED-GFX-235 — it is a *geometry* translation, equivalent to a pixel-centre rule only at one sample; at four, the outer sample positions sit inside the sub-half-pixel margin and it starts removing coverage), and the scale is clamped by the device's `subPixelPrecisionBits` so it can never round back UP to exactly half a pixel, which is the trap EasyGL hit on WebGL's four subpixel bits. **Authority:** XNA, not EasyGL. `xna_pixel_center_contract_test.cpp` records `covered=1` at pixel (16,16) measured on the real XNA 4.0 runtime (`spikes/xna-pixel-center-spike/`), and that is what the fix targets. **Evidence:** `Vulkan_XnaPixelCenter`, `Vulkan_PointSamplingContract` and `Vulkan_DescriptorCapacityContract` all pass, with **no tolerance widened and no expected value changed** — the acceptance criterion as written. The two strong ones are positional, not count-based: `PointSamplingContract` U2 magnifies 3x3 onto 10x10 at a non-integer scale and required 19 wrong texels to become 0, and `DescriptorCapacityContract` B1/C1 required 15/27 and 142/256 mag-linear texel reproductions to become exact. **Regression evidence:** the correction moves *every* 3D draw by ~0.49 px, and the full suite went from 212/218 to **215/218** — three fixed, nothing else moved. The three remaining failures are `Vulkan_DepthBias` (`VULKAN-091`, confirmed separate) and the two `VULKAN-144`/`VULKAN-346` stride refusals, re-checked to be failing on the same text as the baseline. **Split rule not needed:** the rasterisation rule and the sampler coordinate turned out to be the same offset, so the row was not split. |
| VULKAN-098 | Prove Vulkan's clip-space depth range is XNA's, and record the EasyGL divergence (F-19) | ⬜ | **Opened by `VULKAN-091`, which is what F-19 fell out of.** **Observed:** an identity projection puts a `z = 0` vertex at depth **0.0** on Vulkan and **0.5** on EasyGL, because EasyGL leaves OpenGL's `[-1,1]` clip depth in place. **Classification:** `VULKAN_STRONGER` -- Vulkan implements XNA's Direct3D 9 range and EasyGL does not. **Intended:** a positive regression test that reads depth back across the range rather than inferring it from a bias experiment: draw at several known `z` values under an identity projection and assert each one occludes and is occluded exactly where XNA's `[0,1]` mapping says it should, including both endpoints. `Vulkan_DepthBias`'s new near-plane leg is a start, not the whole thing -- it pins one endpoint. **Also required:** the divergence is written into `docs/vulkan-renderer.md` (`VULKAN-480`) as a named difference from the reference renderer, with the consequence stated (an XNA scene loses half its depth precision on EasyGL, and a shared fixture that encodes a depth value cannot be shared between the two). **Non-goal:** changing EasyGL or the shared layer. This plan owns Vulkan; the EasyGL side needs a row in its own owner's plan, and this task's close includes saying which plan that is rather than silently leaving it. **Depends on:** nothing. |
| VULKAN-096 | Audit the `BlendState`/`DepthStencilState`/`RasterizerState` → pipeline-key mapping for dropped fields | ⬜ | Read the pipeline-key construction against the public state objects field by field and record any state that does not reach the key (and therefore cannot change the pipeline). Each dropped field becomes a row. Method note: a key that omits a field is invisible to a pixel test that only ever sets one value of it, so the audit is by reading plus a cardinality check (`GetGraphicsPipelineCacheEntryCountEXT` must increase when the field changes). |

---

## 16. Phase 4 — Vertex/index buffers and draw semantics (`VULKAN-130`–`VULKAN-169`)

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-130 | Fix the unchecked `VertexBuffer` upload (F-01) | ✅ | **Fixed.** `VulkanVertexBufferRenderer` now records the bytes it has mapped (`allocatedBytes_`) and `SetData` calls `EnsureByteCapacity(max(vertex_count, capacity_) * stride)` before its `memcpy`. The reservation is the whole logical capacity at the new stride, not the bytes this call writes, because the draw routes copy out of the mapping at the caller's own `vertexStart`/`vertexCount`, which the shared layer bounds by the buffer's capacity rather than by the last upload -- a mapping sized to one short upload would still be read past its end by a legal draw. **What the fix rests on, recorded because it is not obvious and constrains any future change:** this `VkBuffer` is never bound to a command buffer. `GetBuffer()` has no caller anywhere in the renderer; every draw route (`DrawColoredPrimitives`, `DrawPrimitivesEx`, `DrawIndexedPrimitivesEx`, `DrawInstancedPrimitivesEx`) `memcpy`s the vertices out of `GetMappedPtr()` into its own deferred record, and `liveVertexBuffers_` is a teardown list only. Reallocating therefore needs no fence, no `vkDeviceWaitIdle` and no retirement-queue entry -- and a comment on `EnsureByteCapacity` says so, because the day something binds the handle that stops being true. The already-uploaded bytes are carried across a grow. **Evidence:** new CTest `Vulkan_VertexBuffer_WideStrideBounds` (`modules/renderers/vulkan/examples/vulkan_vertexbuffer_wide_stride_bounds_test.cpp`), **10/10**. Leg A reads the renderer's own new `GetLiveVertexBufferBytesEXT()` counter as a before/after delta for a 512-vertex buffer at each of the three wide strides the pipeline-key table recognises; leg B round-trips every uploaded byte through `GetDataRawEXT`; leg C draws two triangles from `vertexStart=506` of a 512-vertex stride-68 buffer -- byte 34,408 of a mapping the old code sized at 32,768 -- with a same-frame head-of-buffer control proving the tail is what coloured the pixel; leg D asserts the validation layer stayed silent. **Mutation probe:** with `EnsureByteCapacity` short-circuited the run is 7/10 -- leg A fails at all three strides with the exact shortfall (`mapped=32768, upload needs 34816 / 38912 / 40960`). Legs B and C pass under the mutation, which is recorded rather than tuned away: B measures the shared CPU shadow and C survives because lavapipe's allocation rounding happened to leave the overrun pages present. **That is the finding's true severity** -- the pre-fix code was latent undefined behaviour whose observable damage depends on the allocator, not a reliable crash, which is exactly why leg A asserts the allocation and not the symptom. Leg A is the discriminator; B and C exist to stop a truncating "fix" and to prove the read side. **Suite:** `ctest -R '^Vulkan_'` re-run after the change (see §7.5's updated baseline). |
| VULKAN-131 | Fix the unchecked `IndexBuffer` upload (F-02) | ✅ | **Fixed, and the finding is narrower than F-02 stated.** `SetData16`/`SetData32` now call `RequireByteCapacity` before their `memcpy` and refuse an over-long upload by name (`The Vulkan renderer: a 16-bit index upload would write 6146 bytes into an index buffer mapped at 6144 bytes (3072 indices of 2 bytes)...`). This buffer **grows nothing**, unlike `VULKAN-130`'s twin, and the asymmetry is the point: a vertex buffer's allocation comes from a stride guess and a wider real stride is a legal thing for a caller to have, whereas an index buffer's comes from the element width it was constructed with, so the only routes past its end are more indices than its capacity or a width that is not this buffer's -- both caller errors, and widening for the second would make it draw from misread bytes instead of failing. **Reachability, measured rather than assumed:** every public path is already bounded above the renderer. `IndexBuffer::SetDataInternal` throws `ArgumentOutOfRangeException` on `elementCount > indexCount_` and `ArgumentException` on a width mismatch; `SetDataAtInternal` bounds the window against the same capacity and always uploads exactly `capacity` indices; and the two internal creators (`GraphicsDevice::DrawUserIndexedPrimitives` and its typed siblings) size the buffer to the index count they then upload. So F-02's overrun is **not reachable through the public API today** and this is defence in depth -- the invariant moved to the object that owns the allocation. That is recorded rather than used to close the row as a non-finding, because the shared guard is not the allocation's owner and nothing binds the two together. **Evidence:** new CTest `Vulkan_IndexBuffer_UploadBounds` (`modules/renderers/vulkan/examples/vulkan_indexbuffer_upload_bounds_test.cpp`), **9/9** -- the allocation is exactly `capacity x 2` and `capacity x 4` (read from the new `GetLiveIndexBufferBytesEXT()` counter as a before/after delta), a full-capacity upload of each width round-trips every index, an over-capacity upload is refused by name *and leaves the existing contents intact*, a 32-bit source into a 16-bit buffer is refused by name, and a full-capacity 32-bit buffer still draws from its very last six indices (`startIndex = 3066`, the boundary case where the draw route's own copy reads the last byte of the mapping). **Mutation probe** -- the one that proves the *renderer's* guard rather than the shared layer's: with `IndexBuffer::SetDataInternal`'s capacity throw short-circuited, leg C fails as `wrong type: The Vulkan renderer: a 16-bit index upload would write 6146 bytes into an index buffer mapped at 6144 bytes` -- the renderer catching what the shared layer no longer does, where before this task the same mutation wrote two bytes past the mapping in silence. **Suite:** 211/217, the same six pre-existing failures. |
| VULKAN-132 | Determine whether the deferred draw model makes dynamic-buffer updates observable (F-12) | ⬜ | **Audit, not a fix.** Build a scene that draws twice in one frame from one `DynamicVertexBuffer` with a `SetData(…, SetDataOptions::Discard)` between the two draws, and read back both regions. On EasyGL each draw shows its own data. Record what Vulkan does. If they differ, the XNA/FNA semantics decide which is right and a remediation row is opened here **before** this task closes. If they agree, record that and close with no production change — a valid result. |
| VULKAN-133 | Port the dynamic-buffer stress proof to Vulkan | ⬜ | Register `easygl_dynamic_buffer_stress_test.cpp`: 12 frames cycling `None`/`Discard`/`NoOverwrite` on a `DynamicVertexBuffer` and a `DynamicIndexBuffer`, with per-frame pixel readback. Depends on `VULKAN-132`'s verdict for what the expected values are. |
| VULKAN-134 | Prove `DrawUserPrimitives` and `DrawUserIndexedPrimitives` on Vulkan | ⬜ | Register `easygl_draw_user_primitives_vpc_test.cpp`, `easygl_draw_user_indexed_primitives_vpc_test.cpp` and `easygl_draw_user_indexed_primitives_32_test.cpp`. These routes allocate a throwaway `VertexBuffer` per call (`GraphicsDevice.cpp:1753`+) — on Vulkan that is a `vkCreateBuffer`/`vkAllocateMemory` **and a `vkDeviceWaitIdle` on destruction** per draw, so this is also the first evidence for `VULKAN-392`. |
| VULKAN-135 | Prove `VertexBuffer::SetData` overloads on Vulkan | ⬜ | Register `easygl_vertexbuffer_setdata_test.cpp` (offset/stride/element-count overloads). `Vulkan_VertexBufferIndexBufferGetData` already covers the read side with the shared source. |
| VULKAN-136 | Prove `BufferUsage::WriteOnly` refusal on Vulkan | ⬜ | Register `easygl_buffer_usage_test.cpp`. The refusal lives in shared code, but the test also asserts the buffer still *works* for drawing on this renderer, which is the Vulkan-specific half. |
| VULKAN-137 | Prove zero-length buffers on Vulkan | ⬜ | Vulkan explicitly allocates `max(1, …)` for empty buffers because `vkCreateBuffer` rejects size 0 — a real divergence from EasyGL with no test. A `VertexBuffer`/`IndexBuffer` of 0 elements must construct, dispose and be bindable-without-drawing without a validation message. |
| VULKAN-138 | Prove disposed-buffer behaviour on Vulkan | ⬜ | Register `easygl_disposed_buffer_test.cpp`: using a disposed `VertexBuffer`/`IndexBuffer` throws `ObjectDisposedException` rather than reaching a freed `VkBuffer`. |
| VULKAN-139 | Prove unknown-stride rejection on Vulkan | ⬜ | Register `easygl_unknown_stride_rejection_test.cpp`. Vulkan's `MakeExt3DKey` maps ten known strides and falls to `s = 0` for anything else — the shape that silently renders from the wrong layout. The test must show a stride the renderer cannot express is rejected by name, not drawn wrongly. If Vulkan currently draws it, that is an implementation row. |
| VULKAN-140 | Prove a genuinely custom `VertexDeclaration` on Vulkan | ⬜ | Register `easygl_draw_user_primitives_custom_test.cpp`. `SetVertexDeclaration` is a required (non-defaulted) renderer operation precisely so a renderer must decide what to do with it; record what Vulkan decides and prove it. |
| VULKAN-141 | Audit `baseVertex` / `startIndex` / `minVertexIndex` / `numVertices` semantics on Vulkan | ⬜ | Draw a sub-range of a larger indexed buffer with a non-zero `baseVertex` and a non-zero `startIndex` and assert exactly the intended triangles appear. XNA's `baseVertex` is added to every index; Vulkan's `vkCmdDrawIndexed` `vertexOffset` has the same meaning, so this is a mapping check with a discriminating scene. Any mismatch becomes a row. |
| VULKAN-143 | Prove draw-argument validation on Vulkan | ⬜ | Register `easygl_draw_range_validation_test.cpp`, `easygl_primitivetype_validation_test.cpp`, `easygl_draw_novertexbuffer_test.cpp`, `easygl_draw_noindexbuffer_test.cpp`. Mostly shared-layer guards, but they must reach the guard rather than the renderer on Vulkan too — and a `PrimitiveType` the renderer cannot express must be refused, not silently mapped. Relevant history: `Headless_Smoke`'s primitive-range abort. |
| VULKAN-144 | Bind an arbitrary `VertexDeclaration` instead of selecting the layout from the stride (F-15) | 🟨 | **Split, before work started, under §26.5 -- it is larger than one commit by a wide margin.** The original text stands as the statement of the gap and is not repeated here; what changed is that the *fourteen* pipeline factories that each bake their own `VkVertexInputAttributeDescription` array cannot be converted in one reviewable change, and converting them piecemeal without a shared builder would leave two layout mechanisms in the file at once. **The design the split assumes**, recorded so the children are not each free to re-decide it: the attribute set is produced by matching the declaration's elements to the *selected stock program's* inputs **by `(usage, usageIndex)`**, taking each matched element's own byte offset and format -- not by declaration order, which is what EasyGL's `ApplyLayout` uses and what `VertexDeclarationFidelity.hpp`'s own contract explicitly permits a renderer to improve on ("A renderer that maps semantics to locations may bind either order faithfully"). The shared `CNA::Internal::Graphics::StockProgramInput` table type already exists for exactly this and is reused rather than reinvented. A buffer with an **empty** declaration keeps the stride-derived layout: that is the `VertexBuffer(device, count)` convenience constructor, which many existing tests use and which has no declaration to bind from. **Children:** `VULKAN-145` (the builder and its pipeline-key hash), `VULKAN-146` (BasicEffect families), `VULKAN-147` (alpha-test, dual texture, environment map, skinned), `VULKAN-148` (PBR and skinned PBR), `VULKAN-149` (instanced). **This row closes** when all five are ✅ and the cardinality check its original acceptance names -- two declarations of equal stride and different element offsets producing two pipeline entries, not one -- passes. **Preserved from the original:** the explicit by-name refusal stays for anything still unsupported afterwards; multi-stream input remains deliberately `false` (`REMED-GFX-201`); `IVertexBufferRenderer`'s signature and EasyGL are untouched. |
| VULKAN-145 | Build a vertex input layout from the declaration, and key a pipeline on it | ✅ | **Done, and deliberately inert:** no pipeline factory uses it yet, so this row changes no rendered pixel. `modules/renderers/vulkan/include/CNA/Internal/Renderers/Vulkan/VulkanVertexInputLayout.hpp` adds `BuildVulkanVertexInputLayoutEXT(declaration, StockProgramInput*, count)`, which matches each of the selected program's inputs to a declaration element **by `(usage, usageIndex)`** and emits a `VkVertexInputAttributeDescription` at that input's location with the matched element's own byte offset and `VulkanVertexFormatHelper` format. Location `i` is `inputs[i]`, the convention every shader under `src/shaders/` already follows. **Two decisions worth having in writing:** an input the declaration does not supply gets **no** description and a bit in `missingInputMask` -- never a guessed offset, because a wrong offset renders wrong pixels while a missing one is a question the caller can answer; and an element format with no `VkFormat` is reported in a *separate* mask, because "the declaration said nothing" and "the declaration said something unrepresentable" are different problems. `Hash()` folds only what the pipeline bakes -- location, format, offset -- so it is the pipeline-key term `VULKAN-146`+ need. **Evidence:** new CTest `Vulkan_VertexInputLayout` (`vulkan_vertex_input_layout_test.cpp`), **11/11**, pure logic with no device. It proves the two properties the conversion rests on: two **32-byte** declarations whose normal sits at offset 12 vs 20 produce different attribute arrays and different hashes (the stride table gives both the same pipeline -- that is the defect), and listing the same elements in a different order produces the **same** layout, which is what makes semantic matching better than EasyGL's index-based binding and is why compiled XNB data ordering `TextureCoordinate` before `Normal` is not a problem here. It also shows the 28-byte dual-texture record F-15 names by name -- `Position@0, TextureCoordinate0@12, TextureCoordinate1@20` -- is **fully expressible** from its declaration, which is the thing the stride table cannot say. **Suite:** 223/224. |
| VULKAN-146 | Drive the BasicEffect pipelines from the declaration | ✅ | **Landed on the second attempt, after the first was reverted (F-21). The difference is the part the original scope was missing: which stock PROGRAM runs also has to ask the declaration.** `GetOrCreatePipelineFogColored3D`, `GetOrCreatePipelineFogTex3D`, `GetOrCreatePipelineLitTextured3D` and its `VertexLit` sibling take a `VulkanVertexInputLayoutEXT`, overwrite their baked attribute array with the declaration-derived one when it is complete, and fold `Hash()` into the pipeline key (a new `PipelineKey::vl` field, because `a` is full). The layout is built at **draw** time and snapshotted into `Pending3DDraw`, since the record is replayed at `Present()` by which point the buffer may carry a different declaration. **What the first attempt got wrong:** it made the offsets declaration-driven and left program selection on the stride. Stride 32 is `VertexPositionNormalTexture`'s, and the lit programs take `{aPos, aNormal, aUV}` with **no colour input at all** -- so a Position+Colour vertex padded to 32 reached them and had nothing to bind its colour to. No offset can fix that. **The rule, taken from EasyGL rather than invented:** REMED-GFX-234's -- *a declaration that names no normal cannot be a lit vertex whatever its stride, so ask it; an absent declaration keeps the stride's answer*, which is what every `VertexBuffer(device, count)` relies on. The chosen shape is recorded in the draw (`BasicProgramShapeEXT`) rather than re-derived at replay, and `GetOrCreatePipelineFogColored3D` now takes the real stride for its binding (it hard-coded 16) with that stride folded into its key. **Vulkan therefore joins `TranslatesDeclarations()`** in the shared REMED-GFX-216/234 suite, with a comment saying exactly which families are converted and which still refuse by name. **Evidence, both halves:** the shared suite -- `VertexDeclarationLayoutTest`, `DeclarationGuardTest`, `IndexedDrawDeferredTest` -- is **26 passed, 0 failed**, and the translating control renders all three colliding declarations (`colorPosition16`, `positionTextureColor24`, `positionColorPadded32`), which is the assertion the first attempt could not satisfy. New CTest `Vulkan_DeclaredVertexLayout`, **6/6**: two 24-byte declarations with the colour at 12 vs 20 draw their own colours and get their own pipelines (`0 -> 1 -> 2` cache entries), a buffer with no declaration keeps the stride path, and Position+Colour padded to 32 renders its colour instead of being read as a lit vertex. **Mutation probes from the first attempt still apply and were re-run: layout out of the key -> legs B and C fail with `(0,0,64)`, the UV floats read as a colour; layout in the key but not in the attributes -> only B fails. **Suite:** `-R '^Vulkan_'` 224/225, full `ctest` **9059/9081** -- both numbers, per F-21. |
| VULKAN-147 | Drive the alpha-test, environment-map and skinned pipelines from the declaration | ⬜ | **Acceptance corrected before the row was started, and the dual-texture family removed from it -- see F-20.** Convert `GetOrCreatePipelineAlphaTest3D`, `GetOrCreatePipelineEnvMap3D` and the two skinned factories the way `VULKAN-146` converted the BasicEffect four: `VulkanVertexInputLayoutEXT` parameter, attribute override when the layout is complete, `Hash()` in the pipeline key, guard conditional on `IsComplete()`. Input tables, read from the shaders: alpha-test is `{Position0, Color0, TextureCoordinate0}` at stride 24 and `{Position0, TextureCoordinate0}` otherwise (one VS, UV remapped to location 1 -- `alpha_test3d.vert.glsl` binds it at offset 24 for stride 32 and 12 for stride 20, which is exactly the guesswork a declaration removes); env-map is `{Position0, Normal0, TextureCoordinate0}`; skinned is `{Position0, Normal0, TextureCoordinate0, BlendWeight0, BlendIndices0}` plus `Color0` at stride 56. **Acceptance:** a test in `Vulkan_DeclaredVertexLayout`'s shape for each of the three -- two declarations of equal stride with a moved element, each drawing its own result and getting its own pipeline -- plus every existing `Vulkan_AlphaTest_*`, `Vulkan_EnvironmentMapEffect_*` and `Vulkan_SkinnedEffect_*` test staying green. **`Vulkan_DualTextureSlotSamplerContract` is NOT the criterion any more**; that is `VULKAN-150`'s, and the reason is F-20. **Depends on:** `VULKAN-146`. |
| VULKAN-150 | Give the Vulkan `DualTextureEffect` a second UV channel (F-20) | ⬜ | **Opened by `VULKAN-147`'s preparation, which found that its stated acceptance could only be reached by rendering the wrong picture.** **Observed:** `dual_texture3d.vert.glsl` declares `layout(location = 0) in vec3 inPos` and `layout(location = 1) in vec2 inUV` -- **one** UV -- and emits a single `fragUV` that `dual_texture3d.frag.glsl` uses for **both** samplers. So `DualTextureEffect` on Vulkan samples `Texture2` with `TextureCoordinate0`, and a declaration carrying an independent `TextureCoordinate1` cannot be honoured. **Why it is invisible today:** the declaration guard refuses any such record before the draw -- `Vulkan_DualTextureSlotSamplerContract`'s 28-byte `VtxDualPT` (`Position0@0, TextureCoordinate0@12, TextureCoordinate1@20`) is refused for its *stride*, and the skip reads as a stride-table limitation. **The trap this row exists to stop:** converting the dual-texture factory to the declaration-driven layout makes that record `IsComplete()` -- both of the *program's* inputs are supplied -- so the guard would stop refusing and the draw would be accepted, silently sampling both textures with `TextureCoordinate0`. That is a wrong picture where there is currently an honest refusal, which §6.3 and §6.9 both forbid, and it is what `VULKAN-147`'s original acceptance would have rewarded. `RequireDeclarationMatchesStockProgram` does **not** catch it either: its contract says in as many words that "declaration elements the selected program does not consume are ignored", which is right for padding and wrong for a semantic the effect's own contract needs. **Intended:** a second `in vec2 inUV1` in `dual_texture3d.vert.glsl` and `dual_texture_colored3d.vert.glsl`, a second `out`/`in` varying, the fragment shader sampling `Texture2` with it, the input tables extended to `{Position0, (Color0,) TextureCoordinate0, TextureCoordinate1}`, and the factory converted. The declaration decides the offsets; a record with only `TextureCoordinate0` keeps today's behaviour by pointing both inputs at the same element. **Acceptance:** `Vulkan_DualTextureSlotSamplerContract` runs its whole `A..M` body instead of skipping it, and `easygl_dualtextureeffect_independent_uv_test.cpp` is registered and passes -- which also closes the substance of `VULKAN-259`. **Depends on:** `VULKAN-146`. **Note for `VULKAN-259`:** its own text points at "the dual-UV strides 60/76 in `MakeExt3DKey`", and those are the **PBR** dual-UV strides, not `DualTextureEffect`'s. The DualTextureEffect gap is this row. |
| VULKAN-148 | Drive the PBR and skinned-PBR pipelines from the declaration | ⬜ | Same conversion for `GetOrCreatePipelinePbr3D` and `GetOrCreatePipelinePbrSkinned3D`, and retire `RequirePbrStrideEXT`'s stride list in favour of the declaration check once the layout no longer comes from the stride -- keeping a by-name refusal for a declaration the shaders genuinely cannot consume. **Acceptance:** `Vulkan_RenderTarget_EffectSource` reaches 9/9, which makes the Vulkan suite fully green for the first time since this plan opened. **Depends on:** `VULKAN-146`; interacts with `VULKAN-346`, whose draw-time refusal must survive the change. |
| VULKAN-149 | Drive the instanced pipeline from the declaration | ⬜ | Same conversion for `GetOrCreatePipelineInstanced3D`, whose per-vertex binding already takes the raw stride and whose `PackedColorOffsetForStride` table is the instanced twin of the stride dispatch this whole row removes (`REMED-GFX-212`). The per-INSTANCE binding is a separate stream and stays as it is -- `MultiStreamVertexInput` remains `false`. **Acceptance:** `Vulkan_DrawInstanced_3Instances` and `Vulkan_InstancedVertexColor_Cardinality` stay green, and a position-only and a position+colour declaration of equal stride still get separate pipelines. **Depends on:** `VULKAN-146`. |

---

## 17. Phase 5 — Textures (`VULKAN-170`–`VULKAN-209`)

The block-compressed and packed-format rows are one coherent feature, split into independently
committable pieces. They are **not** the modern engine layer's float-format work (`MOD-2223`), which
stays out of scope.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-170 | Implement `ClassifySurfaceFormatEXT` on Vulkan, answering from `vkGetPhysicalDeviceFormatProperties` | ⬜ | **Observed:** Vulkan overrides none of the format-classification queries, so every non-`Color` `SurfaceFormat` falls to `Texture::ValidateFormat` and throws (`Texture.cpp:175`), while EasyGL classifies eight more formats as `Supported` (`EasyGLRenderer.cpp:5495`). **Intended:** Vulkan answers `Supported`/`Unsupported` from the device's real `VkFormatProperties` for the formats it implements, and `Defer` for everything else — never widening the verdict past what `CreateTexture` can actually allocate. Also override `ITextureRenderer::GetSurfaceFormatEXT` (F-11) so a texture reports the format it was created with. **Test:** for each classified format, the verdict agrees with whether construction succeeds; an unsupported-on-this-device format still refuses by name. **Non-goal:** float/HDR formats (`MOD-2223`). |
| VULKAN-171 | Implement `ClassifyRenderTargetFormatEXT` and `ClassifyColorTransferFormatEXT` on Vulkan | ⬜ | Renderability is a strictly narrower question than storability; answer it from `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT` rather than reusing `VULKAN-170`'s answer. `GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT` must then be truthful on Vulkan. Depends on `VULKAN-170`. |
| VULKAN-172 | Implement block-compressed `Texture2D` storage and transfer (`Dxt1`/`Dxt3`/`Dxt5`) on Vulkan | ⬜ | `IsCompressedTransferFormatEXT` returns true for the three formats where the device reports `VK_FORMAT_BC1_RGBA_UNORM_BLOCK`/`BC2`/`BC3` sampled support; `SetData` uploads raw 4×4 blocks through the staging path with correct `bufferRowLength`/block extents; a device without BC support keeps returning false so the shared decode fallback runs. **Test:** register `easygl_dxt_format_test.cpp` (two blocks side by side — a mis-sized block or a mis-walked block stream shows as a wrong half) and `dxt1_texture_test.cpp` (`Texture2D::FromStream` DDS round-trip). Both must pass with the native path and with the path forced off. Depends on `VULKAN-170`. |
| VULKAN-173 | Implement the packed 16-bit formats (`Bgr565`, `Bgra5551`, `Bgra4444`) on Vulkan | ⬜ | `GraphicsProfile.Reach` permits them; the device offers `VK_FORMAT_R5G6B5_UNORM_PACK16`, `A1R5G5B5`/`R5G5B5A1` and `R4G4B4A4`. Component order is the whole risk: assert byte-exact round-trip against hand-computed expansions (31/31 → 255), not "some colour appeared". Test: register `easygl_packed16_format_test.cpp`. Depends on `VULKAN-170`. |
| VULKAN-174 | Implement `NormalizedByte2`/`NormalizedByte4` on Vulkan | ⬜ | `VK_FORMAT_R8G8_SNORM` / `R8G8B8A8_SNORM`. `ClassifyColorTransferFormatEXT` must report `Unsupported` for them exactly as EasyGL does, so a `Color`-shaped transfer cannot read the wrong bits. Depends on `VULKAN-170`. |
| VULKAN-175 | Prove `Texture3D` partial-box **writes** on Vulkan | ⬜ | Register `easygl_texture3d_partial_box_test.cpp`. Vulkan already proves partial-box readback; the write side (`vkCmdCopyBufferToImage` with a sub-`imageOffset`/`imageExtent`) is checked by code inspection only, per `docs/texture3d-texturecube-support.md:56`. |
| VULKAN-176 | Prove `TextureCube` per-face round-trip on Vulkan | ⬜ | Register `easygl_texturecube_faces_test.cpp` — six distinct face payloads written and read back, proving no face aliases another. Vulkan proves mip and partial-rect round-trips already. |
| VULKAN-177 | Prove disposed-texture and bound-resource disposal behaviour on Vulkan | ⬜ | Register `easygl_disposed_resource_test.cpp` and `easygl_bound_resource_dispose_test.cpp`. On Vulkan, a texture destroyed while its `VkImageView` is still referenced by a cached descriptor set is the exact case `EvictSampledViewFromCaches` exists for (`VulkanRenderer.cpp:10379`) — the test must reach that eviction, not merely dispose an unused texture. |
| VULKAN-178 | Audit `Texture2D::FromStream` / `SaveAsPng` / `SaveAsJpeg` on Vulkan | ⬜ | These go through readback and upload paths that differ per renderer. Record which are covered on Vulkan today and open rows for those that are not. |

---

## 18. Phase 6 — Render targets (`VULKAN-210`–`VULKAN-249`)

Vulkan is the **stronger** renderer in most of this area (§10.6). The tasks here close the few
EasyGL-only rows and audit the parts neither renderer proves.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-210 | Prove the bound-MSAA-target alpha case on Vulkan | ⬜ | Register `easygl_gfx164_bound_msaa_alpha_test.cpp`. `GFX164` was a real cross-renderer defect (alpha lost when sampling a bound MSAA target's resolve); Vulkan resolves on unbind through its own path and has no equivalent proof. |
| VULKAN-211 | Prove `RenderTargetCube` depth-format fidelity on Vulkan | ⬜ | Register `easygl_rendertargetcube_depthformat_test.cpp`. `Vulkan_RenderTarget2D_DepthFormatFidelity` covers the 2D half (Task 911); the cube half has no test on Vulkan, and `CreateRenderTargetCube` takes the same `depthFormat` parameter. |
| VULKAN-212 | Prove the plain two-attachment MRT case on Vulkan | ⬜ | Register `easygl_mrt_test.cpp`. Vulkan has `MRT_MixedFormats`, `MRT_MsaaResolve` and the cube-MRT cases but not the plain "one draw writes two attachments with different content" source EasyGL uses. |
| VULKAN-213 | Audit render-target lifetime across deferred work end to end | ⬜ | Vulkan is already strong here (`Vulkan_RenderTarget_GetDataLifetime`, `_DeferredResourceLifetime`, `_BoundTargetLifetime`, `_CubeFaceReadbackDependency`). Audit the remaining shapes: a target disposed while its content is still pending a flush; a target used as a source in the same frame it was written and then disposed; a `RenderTargetCube` face disposed between faces. Each hole found gets a row. |
| VULKAN-214 | Audit `RenderTargetUsage::PreserveContents` across a swapchain recreation | ⬜ | `REMED-GFX-136` fixed cube preserve-contents. A resize destroys and rebuilds render passes; whether a `PreserveContents` target created before a resize still preserves after it is untested. Test: draw into a preserved target, resize the window, draw again, assert the first draw survived. |

---

## 19. Phase 7 — Effects and shader-driven ordinary rendering (`VULKAN-250`–`VULKAN-299`)

Compiled `.fx` bytecode behaviour is **`plans/plan_fx.md`'s** (`FX-065`, `FX-110`, `FX-112`;
`CNA_VULKAN_COMPILED_EFFECTS`). Cross-dialect shader portability is **`plans/plan_csl.md`'s**. This
phase owns Vulkan's own stock-effect and `ShaderEffect` surface.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-250 | Declare the shader dialect the Vulkan renderer actually requires | ✅ | **Declared: `ShaderDialectEXT::GlslVulkan`.** It is not device- or build-dependent -- this renderer has exactly one custom-effect intake and `CompileProgram` rejects anything that is not SPIR-V words. **And the review the row asked for concluded the enumerator is genuinely imprecise, so the coordination row is opened rather than waved at:** `IGL`'s Vulkan backend reports the **same** `GlslVulkan` and takes GLSL **source** (`IglEffectRenderer::CompileProgram` -> `igl::ShaderStagesCreator::fromModuleStringInput`, and `igl_spritebatch_shadereffect_test.cpp` gates on exactly this value before handing it a GLSL string), while this renderer takes compiled **bytecode**. An application that queried the dialect and acted on it would still have to guess which of the two to send -- which is the one thing this query exists to prevent. Narrowing it needs a new enumerator, a C-ABI change through `MapShaderDialect` (`modules/c-api/src/CnaCApiGraphics.cpp:144`), and that is **`VULKAN-264`**. **Evidence:** new CTest `Vulkan_ShaderDialectContract` (`vulkan_shader_dialect_contract_test.cpp`), **4/4** -- the dialect is declared and is `GlslVulkan`; ordinary Vulkan-flavoured GLSL source of exactly the shape IGL compiles is **refused** here (`SPIR-V size must be a multiple of 4 bytes`) rather than accepted and drawn from nothing; and the ambiguity itself is asserted, so a future enumerator that resolves it turns this leg red and says so instead of leaving a stale comment behind. That a valid SPIR-V pair is accepted and renders is not re-proved -- `Vulkan_ShaderEffect_SpirV` owns that end to end with a real tinted draw. **Suite:** 219/220. |
| VULKAN-264 | Coordinate a shader-dialect enumerator that distinguishes source from bytecode (F-08's tail) | ⬜ | **Opened by `VULKAN-250`, which measured the ambiguity rather than assuming it.** **Observed:** `ShaderDialectEXT::GlslVulkan` is reported by two renderers that want different payloads -- IGL's Vulkan backend compiles GLSL **source** through `fromModuleStringInput`, CNA's Vulkan renderer requires already-compiled **SPIR-V words** and refuses source by name. Both answers are honest under the enumerator's own wording ("GLSL compiled to SPIR-V"); the enumerator is the thing that is short. **Why it matters:** `GetShaderDialectEXT` exists so an application need not infer the payload from the build identity, and today it cannot answer that question for a Vulkan target. **Intended:** a distinct enumerator (working name `SpirV`) meaning "already-compiled SPIR-V bytecode", with `GlslVulkan` retained for a renderer that compiles Vulkan GLSL itself. **This is a C-ABI change** -- `CNA_ShaderDialect` and `MapShaderDialect` (`modules/c-api/src/CnaCApiGraphics.cpp:144`) -- so it must be agreed with `plans/plan_binding.md`/`plan_bindings_upstream.md` before either side moves, and appending rather than inserting is mandatory so no existing ordinal shifts. **Acceptance:** the new enumerator exists, Vulkan reports it, IGL keeps `GlslVulkan`, `Vulkan_ShaderDialectContract`'s leg C is rewritten to assert the *resolved* contract instead of the ambiguity, and the C ABI's own dialect test covers the added value. **Non-goal:** making one `ShaderEffect` source run on both renderers -- that is `plans/plan_csl.md`. |
| VULKAN-251 | Prove `ShaderEffect` uniform delivery through `SpriteBatch` on Vulkan | ⬜ | `Vulkan_ShaderEffect_SpirV` proves a tint; `EasyGL_ShaderEffect_SpriteBatch_Uniform` proves a *named uniform set from game code* reaches the shader. Author the SPIR-V twin (via the existing `libshaderc` route in `modules/renderers/vulkan/src/shaders/compile_shaders.py`) and assert the drawn colour is a function of the uniform, so a dropped uniform fails. |
| VULKAN-252 | Implement the array uniform setters on `VulkanEffectRenderer` | ⬜ | `SetUniformFloatArray`, `SetUniformVec2Array`, `SetUniformVec3Array`, `SetUniformMat4Array` are implemented on EasyGL and absent on Vulkan, so a custom effect cannot receive a bone palette or a kernel. The 128-byte push-constant block cannot hold them; a uniform buffer is required. Test: a custom effect that reads a 4-element `mat4` array and renders a result that differs per element. |
| VULKAN-253 | Implement `IEffectRenderer::BindTexture` on `VulkanEffectRenderer` | ⬜ | Today a custom effect can only sample whatever `SpriteBatch` bound at binding 0. Test: a custom effect samples an explicitly bound second texture and the output depends on it. |
| VULKAN-254 | Implement `BindTextureCube` / `BindTexture3D` for custom effects on Vulkan | ⬜ | EasyGL proves both (`EasyGL_ShaderEffect_TextureCube`, `_Texture3D`); IGL proves the volume case on its Vulkan backend, so this is reachable rather than a boundary. Test: the SPIR-V twins of those two EasyGL tests, sampling a cube face and two Z slices. Depends on `VULKAN-253`. |
| VULKAN-255 | Support a `ShaderEffect` driving a 3D draw with a custom vertex layout on Vulkan | ⬜ | `EasyGL_ShaderEffect_3D` and `_CustomVertexLayout` have no Vulkan equivalent; `VulkanEffectRenderer::GetOrCreatePipeline` bakes the SpriteBatch vertex input. Either extend it to take the caller's `VertexDeclaration`, or refuse the combination by name so a game does not get a silently mis-fed shader. Depends on `VULKAN-139`'s stride verdict. |
| VULKAN-256 | Record the GLSL-source shader-test divergence rather than porting 37 tests | ⬜ | Write the decision into `docs/vulkan-renderer.md` and §9.3 here: `ShaderEffect` takes renderer-specific source by contract; the 37 EasyGL `*_Shader`/`Bloom_*`/`ShaderEffect_*` tests carry GLSL payloads and are not a Vulkan parity requirement. What *is* required is `VULKAN-250`–`VULKAN-255` (an equally capable Vulkan custom-effect surface) plus a named dependency on `plans/plan_csl.md` for one source serving both. Closing this task requires that the SPIR-V equivalents chosen for `VULKAN-251`/`VULKAN-254` are the ones that exercise capability, not the ones that are easiest to translate. |
| VULKAN-257 | Close the `BasicEffect` evidence gaps on Vulkan | ⬜ | Register `easygl_basiceffect_combinations_test.cpp`, `_default_lighting_`, `_lit_vertex_color_`, `_position_normal_`, `_vertex_color_clamp_`, `_world_scale_precision_`. `VertexColorClamp` and `WorldScalePrecision` are the two that have historically found real defects; keep them separate commits from the rest if the first run is not green. |
| VULKAN-258 | Close the `AlphaTestEffect` evidence gaps on Vulkan | ⬜ | Register `easygl_alphatest_modes_test.cpp`, `alpha_test_integration_test.cpp`, `alpha_test_effect_test.cpp`. Vulkan covers the compare-function sweep, fog, null texture and vertex colour already. |
| VULKAN-259 | Prove `DualTextureEffect` independent UV sets on Vulkan | ⬜ | Register `easygl_dualtextureeffect_independent_uv_test.cpp`. The second UV channel is a vertex-layout question on Vulkan (the dual-UV strides 60/76 in `MakeExt3DKey`), so this also exercises `VULKAN-130`'s territory. |
| VULKAN-260 | Prove the `EnvironmentMapEffect` Fresnel **gradient** on Vulkan | ⬜ | Register `easygl_environmentmapeffect_fresnel_gradient_test.cpp`. `Vulkan_EnvironmentMapEffect_Fresnel` proves the term exists; the gradient test is the one shaped to catch a per-vertex/per-pixel interpolation defect — the exact defect the D3D9 oracle corpus found on EasyGL (`NEXT.md`, `D9-A6`). Because EasyGL is a *suspect* here, the expected values come from XNA/FNA semantics, not from EasyGL's output. |
| VULKAN-261 | Close the `SkinnedEffect` evidence gaps on Vulkan | ⬜ | Register `easygl_skinned_effect_bones_test.cpp`, `easygl_skinnedeffect_vector4_bone_indices_test.cpp`, `skinned_effect_integration_test.cpp`. Vulkan already has more skinned coverage than EasyGL elsewhere; these three are the remaining shapes. |
| VULKAN-262 | Prove PBR material maps on Vulkan | ⬜ | Register `easygl_pbr_material_maps_test.cpp`. `Vulkan_Pbr_TextureSlots` proves slot mapping and `Vulkan_PbrEffect_Golden` proves the BRDF; the per-map contribution test (normal/metallic-roughness/occlusion/emissive each changing the result) is the gap. |
| VULKAN-263 | Prove `Effect::Clone` and `CurrentTechnique` on Vulkan | ⬜ | Register `easygl_effect_clone_test.cpp` and `easygl_effect_current_technique_test.cpp`. Mostly shared logic, but a cloned effect must not share renderer-side pipeline or descriptor state — which is a Vulkan-specific property. |

---

## 20. Phase 8 — Model, content and glTF renderer-facing behaviour (`VULKAN-300`–`VULKAN-329`)

Parser-side glTF/`.cnj` correctness stays with `plans/plan_gltf.md` and `plans/plan_cnj.md`. The
question here is only whether the Vulkan renderer executes the same already-parsed scene and
material semantics.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-300 | Prove basic `Model` drawing on Vulkan | ⬜ | Register `easygl_model_draw_test.cpp`. The seven `ModelJsonReader`/hierarchy/skinned-playback sources already run on Vulkan; this one does not. |
| VULKAN-301 | Prove tangent handedness and mirrored tangents on Vulkan | ⬜ | Register `easygl_gltf_tangent_handedness_test.cpp` and `easygl_gltf_mirrored_tangent_test.cpp`. The bitangent sign lives in `TANGENT.w` and is consumed by the renderer's own normal-mapping code, so a sign flip is a renderer defect, not a parser one. |
| VULKAN-302 | Prove glTF sampler wrap and per-map texture transforms on Vulkan | ⬜ | Register `easygl_gltf_sampler_wrap_test.cpp` and `easygl_gltf_texture_transform_per_map_test.cpp`. Per-map transforms need per-map UV state in the shader; Vulkan's PBR descriptor set carries seven views (`VulkanRenderer.cpp:7681`) and this proves each honours its own transform. |
| VULKAN-303 | Prove glTF alpha blending, transmission ordering and base-colour factor × texture on Vulkan | ⬜ | Register the three EasyGL sources. Transmission ordering is a draw-order property the deferred Vulkan command model could plausibly change, which is what makes it worth a Vulkan run rather than a shared-code argument. |
| VULKAN-304 | Prove skinned PBR under non-uniform joint scale on Vulkan | ⬜ | Register `easygl_gltf_skinned_pbr_nonuniform_joint_test.cpp`. Non-uniform scale is the case where an inverse-transpose is required and a shortcut shows; `Vulkan_SkinnedEffect_WorldNormal` proves the stock-effect half already. |

---

## 21. Phase 9 — Device, presentation and lifecycle (`VULKAN-330`–`VULKAN-369`)

Vulkan has no GL context loss, and this phase does **not** translate EasyGL's context-recovery
machinery. It compares the observable CNA contract.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-330 | Implement `SetPresentationMode` on Vulkan (F-03) | ⬜ | **Observed:** `void SetPresentationMode(int) override {}` (`VulkanRenderer.hpp:1192`) — the only empty override body in either renderer. A game selecting `CnaPresentationMode::Letterbox` gets Vulkan's uniform height-derived scale instead. **Classification:** implementation bug (silent no-op where EasyGL implements the behaviour). **Intended:** the requested mode changes the presented rectangle the way EasyGL's does — letterbox bars, overscan crop, stretch, native, fixed-height — expressed in Vulkan's own terms (viewport/scissor over the swapchain image), not by porting GL machinery. **Test:** for each mode, a distinctive scene rendered at a virtual resolution with a different aspect ratio from the window, with pixel probes inside the presented rectangle and in the letterbox bar. Must fail with the current no-op. **Non-goal:** changing the shared `CnaPresentationMode` contract or any other renderer. |
| VULKAN-331 | Implement `GetDefaultViewportRect` on Vulkan (F-04) | ⬜ | Once `VULKAN-330` produces a physical rectangle that differs from the logical size, `GraphicsDevice::UpdateViewportFromWindow` needs the real rectangle. The base default `(0, 0, GetViewportSize())` is correct only while every mode is a no-op. Test: after a resize under `Letterbox`, `GraphicsDevice.Viewport` still reports the virtual size while draws land inside the physical sub-rectangle. Depends on `VULKAN-330`. Prior art worth reading: `feedback: CNA viewport logical→physical bug — GetDefaultViewportRect() is the discriminator`. |
| VULKAN-332 | Implement `SetSwapInterval` on Vulkan (F-05) | ✅ | **Implemented, and the mechanism was already there.** `swapInterval_` was read once, at swapchain creation, so `GraphicsDeviceManager.SynchronizeWithVerticalRetrace` + `ApplyChanges()` -- which does reach `SetSwapInterval` through `GraphicsDevice::Reset` -- changed nothing. `CreateSwapchain` already picks its `VkPresentModeKHR` from `swapInterval_` and this renderer already rebuilds its swapchain on every resize; the override records the request and reruns that path when the interval actually differs. `IGraphicsRenderer`'s own comment naming Vulkan as a renderer that "cannot change VSync at runtime" described the old implementation, not the API, and is corrected in place. **Evidence:** new CTest `Vulkan_SwapInterval` (`vulkan_swap_interval_test.cpp`), **7/7**, stable over three consecutive runs. The oracle is the **live swapchain's own `VkPresentModeKHR`** -- written only by `CreateSwapchain`, so it cannot move unless the swapchain was genuinely rebuilt -- observed going FIFO -> IMMEDIATE -> FIFO through the public `ApplyChanges()` route, with a drawn quad read back after each rebuild. `easygl_present_interval_test.cpp` is also registered (`Vulkan_PresentInterval`, 5/5) for the renderer-agnostic round-trip half. **Two things learned the hard way, both now written into the test:** (1) the recreation *counter* is useless as an oracle -- `Reset` also re-applies the virtual resolution and presentation format, and the same no-change `ApplyChanges()` cost 0 recreations before a draw and 1 after one, so asserting it tests `Reset`'s bookkeeping rather than this; it is printed, not asserted. (2) The first version had an **escape hatch that excused the defect**: FIFO is the only present mode Vulkan guarantees, so it inferred "this surface has no unsynchronised mode" from the mode not changing -- and with `SetSwapInterval` reverted to its no-op the test still passed 7/7. The branch now turns on a *measured* fact, `SupportsUnsynchronisedPresentModeEXT()`, read from the surface's own present-mode list. **Mutation probes, both run against the fixed test:** the pre-task no-op gives 6/7 (vsync off never leaves FIFO); recording the interval without rebuilding gives 5/7 (the mode lags the request in both directions -- worth noting, because `Reset`'s other rebuilds would eventually apply a recorded interval, just at the wrong time). **Suite:** 222/223. |
| VULKAN-333 | Implement `GetSwapIntervalEXT` on Vulkan | ✅ | **Implemented, and the row's registration instruction is deliberately NOT followed -- with the reason, per §26.6.** `GetSwapIntervalEXT()` now returns `swapInterval_`, so the `-1` that means "this renderer does not record what it was asked for" -- the honest answer while `SetSwapInterval` was the inherited no-op -- is gone. **Why `easygl_graphicsdevicemanager_vsync_test.cpp` is not registered on Vulkan:** its driver half is `SDL_GL_GetSwapInterval`, which has no meaning on a window created for Vulkan. Registered verbatim, its check A (`interval == 0` with vsync off) would pass **vacuously** on the zero left in an untouched `int`, and check B would take the skip path -- which is precisely the failure mode that file's own header documents as the reason `REMED-GFX-243` existed at all ("deleting the very fix this file was written to guard left it reporting PASS and SKIP"). Registering it here would recreate that, one renderer over. **What is done instead:** the two questions it asks are asked in `Vulkan_SwapInterval` with the probe that means something on this renderer. Its `P1`/`P2` forwarding half becomes legs `E`, read through `IGraphicsRenderer&` rather than the concrete type -- the same cross-renderer seam -- and its GL driver half becomes `VULKAN-332`'s `GetAppliedPresentModeEXT()` assertion, which is a real driver-side observation rather than a CNA-internal echo. **Evidence:** `Vulkan_SwapInterval` is now **10/10**: the interval is not `-1`, `SynchronizeWithVerticalRetrace=false` reads back as `0` and `true` as non-zero, alongside `VULKAN-332`'s present-mode legs. **Suite:** 222/223. |
| VULKAN-334 | Decide and document the Vulkan answer to device loss | ⬜ | EasyGL implements `SetContextRecoveryEnabled`/`DebugSimulateContextLoss`/`DebugRestoreContext` for GL context loss. Vulkan's analogue is `VK_ERROR_DEVICE_LOST`, which is not the same event and is not simulable the same way. Audit what CNA promises a game across such an event, record whether Vulkan meets it, and either implement the equivalent or record the refusal with the reason in `docs/vulkan-renderer.md`. **Explicit non-goal:** a fake Vulkan context-loss state machine built by analogy with GL. |
| VULKAN-335 | Prove `PresentationParameters` round-trip on Vulkan | ⬜ | Register `easygl_presentation_parameters_test.cpp`. Interacts with `VULKAN-023`: an echoed sample count or depth format the device did not apply is exactly what this test should catch. |
| VULKAN-336 | Prove backbuffer resize on Vulkan | ⬜ | Register `easygl_backbuffer_resize_test.cpp`: change `PreferredBackBufferWidth/Height`, `ApplyChanges()`, assert the viewport, the readback dimensions and a drawn quad's position all follow. Vulkan recreates the swapchain here; every render pass, framebuffer and depth resource is rebuilt. |
| VULKAN-337 | Prove real window resize on Vulkan | ⬜ | Register `easygl_real_window_resize_test.cpp` — a platform-driven resize rather than an API-driven one, reaching `OnSurfaceChanged` (`VulkanRenderer.cpp:9951`) rather than `SetVirtualResolution`. |
| VULKAN-338 | Prove repeated swapchain recreation, zero-sized and minimized windows on Vulkan | ⬜ | Vulkan-specific, with no EasyGL twin. Drive at least 20 resize cycles including a 0×0 (minimized) step and a restore, and assert: no validation message, every pre-existing `RenderTarget2D`/`Texture2D`/`VertexBuffer` still valid and readable afterwards, and rendering still correct. `RecreateSwapchain` returns early on a zero extent (`:3163`) — the test must cover what happens to a frame submitted while in that state. |
| VULKAN-339 | Prove GPU-handle release and double dispose on Vulkan | ⬜ | Register `easygl_handle_release_test.cpp` and `easygl_double_dispose_test.cpp`. `HasRenderer()` false after the first `Dispose()` and no second release attempt — on Vulkan this is the path that also runs `vkDeviceWaitIdle` and the retirement lists. |
| VULKAN-340 | Prove no resource leak over many create/dispose cycles on Vulkan | ⬜ | Register `easygl_resource_leak_test.cpp` (80 resources over 20 iterations). Extend it on Vulkan with a device-memory or allocation-count assertion if one is cheaply available, since `GetTrackedResourceCount() == 0` proves the CNA side only. |
| VULKAN-341 | Prove move semantics do not double-free Vulkan handles | ⬜ | Register `easygl_move_semantics_test.cpp`. Vulkan's resources register themselves in `liveVertexBuffers_`/`liveIndexBuffers_`/`liveRenderTargets_` on the renderer, so a move must not leave two entries pointing at one object — a shape the EasyGL registry does not have. |
| VULKAN-342 | Prove device/resource destruction ordering on Vulkan | ⬜ | Register `easygl_device_dispose_order_test.cpp`. `~VulkanRenderer` explicitly releases externally-owned resources and disconnects their owners (`VulkanRenderer.cpp:1779`+); destroying the device before the resources is the case that machinery exists for and nothing tests. |
| VULKAN-343 | Prove resource and device-reset events on Vulkan | ⬜ | Register `easygl_resource_events_test.cpp` and `easygl_device_reset_events_test.cpp`. `ApplyChanges()` on a live device is a full `Reset` on Vulkan. |
| VULKAN-344 | Prove `GraphicsDevice` argument validation on Vulkan | ⬜ | Register `easygl_device_validation_test.cpp` — including `Present()` while a render target is bound, which on Vulkan interacts with the deferred pass machinery rather than with a bound FBO. |
| VULKAN-345 | Prove the `Clear` overload and `ClearOptions` matrix on Vulkan | ⬜ | Register `easygl_clear_overloads_test.cpp`. Vulkan has `_ClearOptions`, `_ClearDepth`, `_ClearStencil` and `_OrderedClear` but not the overload/argument-validation matrix, including the depth-range guard being flag-gated. |
| VULKAN-346 | Stop an unsupported combination from ending the process (F-16) | ✅ | **Both cases fixed, and the first turned out to be a real renderer defect rather than only a harness fault.** **Case 1, `Vulkan_RenderTarget_EffectSource`:** the stride refusal (`Vulkan PbrEffect requires vertex stride 48 or 60`) was raised by `GetOrCreatePipelinePbr3D`, which is called from `RecordCommandBuffer` -- at `Present()`. So the draw was **accepted**, the leg recorded its failure and unbound its target, and the exception then arrived at a frame boundary the caller could no longer associate with anything, outside every recovery it had. That is §6.3's failure exactly: not refused at the point of the mistake. `RequirePbrStrideEXT` now asks the same question where the draw is **queued** (both `Draw*PrimitivesEx` routes), with the same two messages, so nothing is queued that the replay cannot build. **Measured:** the `[CRASH] leg B1: killed by signal 6 (Aborted) (core dumped)` is gone; the leg reports `[FAIL] leg B1: an unexpected exception escaped: Vulkan PbrEffect requires vertex stride 48 or 60` and the CTest exits 1 -- the verdict it printed. It stays red until `VULKAN-144` supplies the capability, which is the correct state for a capability gap. **Case 2, `Vulkan_DualTextureSlotSamplerContract`:** a harness fault, as the row predicted. Its support probe calls `Render()`, which binds a render target before the draw it is probing with; the refusal unwinds past the unbind, the shutdown `Present()` correctly refuses, and nothing catches it, so an orderly `[SKIP]` became a `std::terminate`. The probe's catch now unbinds and resets device state first -- the same recovery `rendertarget_effect_source_test.cpp` already documents for its own legs. **Measured:** prints `=== 5/5 PASS ===` and exits **0**. **The second clause needed no new test:** "ending a frame with a render target bound is either legal or refused at the point of the mistake" is already owned, end to end, by `present_lifecycle_contract_test.cpp` (`Vulkan_PresentLifecycle`, passing) -- it reads the contract out of FNA's own `GraphicsDevice.Present`, establishes that it is REJECT rather than auto-unbind, asserts each link of the chain separately, and reproduces the process-level abort deliberately. Duplicating it here would add nothing. **Suite:** 218/219 -- only `Vulkan_RenderTarget_EffectSource` remains, and it is `VULKAN-144`'s. |

---

## 22. Phase 10 — Queries and other ordinary facilities (`VULKAN-370`–`VULKAN-389`)

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-370 | Answer `PixelCountIsPreciseEXT` deliberately on Vulkan | ✅ | **F-14 said "probably correct"; it was not.** A Vulkan occlusion query is only required to produce an exact count when the device's `occlusionQueryPrecise` feature is **enabled** and the query is begun with `VK_QUERY_CONTROL_PRECISE_BIT`. This renderer did **neither** -- `CreateLogicalDevice` did not request the feature, `vkCmdBeginQuery(... , 0)` passed no flags -- and inherited the shared `true` default. So it promised XNA's real Direct3D 9 tally while asking the device only "did anything pass", and the lensflare idiom (`PixelCount()` over an area) is exactly what that breaks. **Fix:** enable the feature where the device offers it, pass the PRECISE bit only then (passing it without the feature is a usage error), and override `PixelCountIsPreciseEXT()` to report that same flag -- so on a device that cannot count, the honest `false` reaches `OcclusionQuery::isPixelCountPreciseEXT()`. **Evidence:** new CTest `Vulkan_OcclusionQuery_Precision` (`vulkan_occlusionquery_precision_test.cpp`), **4/4**. An occluder covers the left half at a nearer depth and the queried quad covers the whole frame at a farther one, so the surviving fragment count is known in advance: **2048** exactly, asserted as that number rather than as `> 0` (a boolean-shaped query answers 1). **The measurement that shaped the test:** llvmpipe counts exactly *even without* the precise bit, so "asked for precise" and "did not ask and got lucky" are indistinguishable through the public API -- which is precisely how the unearned promise survived unnoticed. Leg D closes that from the outside: passing the PRECISE bit without the feature is `VUID-vkCmdBeginQuery-queryType-00800`, so a silent validation log is what shows the bit and the feature agree. **Probes, both run:** with the feature not enabled the report becomes `precise=false` and the test takes its honest branch (3/3 legs, no failure -- it does not punish a device for being honest); with the feature claimed but not enabled, leg D fails with `vkCmdBeginQuery(): flags includes VK_QUERY_CONTROL_PRECISE_BIT, but occlusionQueryPrecise feature was not enabled.` **Suite:** 220/221. |
| VULKAN-371 | Prove occlusion-query reset and reuse on Vulkan | ⬜ | Register `occlusion_query_test.cpp` (`EasyGL_OcclusionQuery_Cycle`): the same query object reused across frames, `IsComplete()` transitions, and a `Begin` without an `End`. Vulkan needs a `vkCmdResetQueryPool` per use, which is exactly the thing a reuse test catches. |
| VULKAN-372 | Audit the remaining ordinary facilities EasyGL exposes and Vulkan is expected to support | ⬜ | Sweep for anything reachable from the public XNA/CNA surface not covered by Phases 2–9 — `GraphicsAdapter`/`DisplayMode` enumeration, `GraphicsDevice.Textures`/`VertexTextures` collections, `GetBackBufferData` variants, `MouseCursor::FromTexture2D`'s colour-transfer route. Each gap found becomes a row before this closes. |

---

## 23. Phase 11 — Validation, robustness and stress (`VULKAN-390`–`VULKAN-429`)

Correctness and bounded resource behaviour, not optimization. No row here may be created from a
guess about performance; §13 of the instruction that produced this plan names the four pathologies
that qualify, and each row below cites the one it addresses.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-390 | Make descriptor-pool exhaustion fail loudly instead of drawing white (F-06) | ✅ | **Fixed, and F-06 understated it.** `GetOrCreateTexSamplerDescSet` now chains another pool of `MaxDescriptorSets` when every existing one refuses, and raises a **named** exception if the device refuses that too (`...Refused rather than drawing a substituted texture.`). Each cache entry carries the pool it came from, because a set freed against the wrong pool is undefined behaviour and there is no longer only one; eviction routes through the existing `poolDescriptorSets` retirement path, and teardown destroys the chained pools. **What the mutation probe showed, which changes the finding's severity:** restoring the old `return defaultWhiteDescSet_` does not draw white on the SpriteBatch path -- it **binds `VK_NULL_HANDLE` and segfaults**. `EnsureDefaultWhiteTexture()` is called from the three 3D draw routes only (`VulkanRenderer.cpp:11171`, `:11454`, `:11762`), so in a 2D-only frame `defaultWhiteDescSet_` has never been created. Measured: `vkCmdBindDescriptorSets(): pDescriptorSets[0] (VK_NULL_HANDLE) ...`, `vkCmdDrawIndexed(): ... uses set #0 but that set is not bound`, then `Segmentation fault (core dumped)`, exit 139. F-06 called this a silent white sprite; on the route it is most likely to be reached from it was a crash. **Reaching the arm at all was the hard part, and the measurement is the reason the test is shaped as it is:** the exhaustion is **not reachable by volume** on either driver here. `vkAllocateDescriptorSets` keeps succeeding past the pool's `maxSets` -- which the spec permits, since running out is a runtime error an implementation *may* report and not a usage violation the layer flags -- and **4000** simultaneously live pairs still did not trigger it. Shrinking the pool to 8 sets did not either. A test built on volume alone would have passed while executing none of the new code. The failure is therefore **injected**, through the test-only `SetTexSamplerDescriptorAllocationFailuresForTestEXT` knob -- the other option this row allows, and the seed of the hook `VULKAN-391` needs. **Evidence:** new CTest `Vulkan_DescriptorPoolOverflow` (`vulkan_descriptor_pool_overflow_test.cpp`), **5/5** -- one injected failure chains exactly one pool and the sprite drawn across it still shows its own texture; 640 distinct live textures in one frame each draw their own (the production-shape volume leg, with the pool count printed as evidence that this driver did not enforce `maxSets`); an injection the chained pool cannot satisfy either is refused by name; the layer stays silent. **Mutation:** the old fallback fails leg A (`1 -> 1 pool(s)`) and then crashes, as above. **Non-goal honoured:** the descriptor strategy is unchanged -- this is a chained pool, not a redesign. **Suite:** 217/219. |
| VULKAN-391 | Make the eleven `vkAllocateDescriptorSets` failure paths consistent | ⬜ | Three throw, seven return `VK_NULL_HANDLE`, one substitutes white. Decide one contract, apply it to all eleven, and make each `VK_NULL_HANDLE` return path's *caller* handle the null rather than binding it. Test: a fault-injection hook (or a deliberately tiny pool under a test-only knob) exercises each family's failure arm and asserts the contract. Depends on `VULKAN-390`. |
| VULKAN-392 | Remove the per-buffer-dispose full-device stall (F-13) | ⬜ | **Observed:** `VulkanVertexBufferRenderer::ReleaseVulkanResources` and the index-buffer twin each start with `vkDeviceWaitIdle` (`:1579`, `:1630`). `DrawUserPrimitives` creates and destroys a temporary vertex buffer per call, so this is a per-draw full-device stall on that route. **Classification:** robustness (a §13-named pathology: `vkDeviceWaitIdle` in a routine path). **Intended:** buffers join the same fence-based retirement mechanism textures and render targets already use (`RetiredResources`, `:10379`+). **Test:** a scene issuing many `DrawUserPrimitives` calls per frame completes with correct pixels and zero validation messages, and a counter assertion shows no `vkDeviceWaitIdle` on the draw path. Depends on `VULKAN-134` for the exercising scene. |
| VULKAN-393 | Gate the Vulkan suite on zero CNA-attributable validation messages | ⬜ | The renderer already records every message and its `pMessageIdName`. Add a suite-level gate (a test that runs a representative workload and asserts an empty CNA-attributable set, plus a documented allowlist of driver/layer messages with a reason each). Depends on `VULKAN-009`'s inventory. Sync validation stays opt-in (`sRequestSyncValidation`) but must be exercised by at least the render-target producer/consumer family, as it already is. |
| VULKAN-394 | Bound the pipeline caches under ordinary state churn | ⬜ | The renderer keeps ~20 `PipelineKey`-keyed maps and never evicts. Measure how many entries an ordinary frame mix produces and assert an upper bound with the existing `GetGraphicsPipelineCacheEntryCountEXT`. A cache that is bounded by the key space is fine and the task closes by recording that bound; a cache that grows with frame count is a defect and gets its own row. |
| VULKAN-395 | Bound the sampler cache | ⬜ | `samplerCache_` is a `std::map<SamplerStateKey, VkSampler>` with no eviction. Same method: establish the key space, assert the bound, or open a row. |
| VULKAN-396 | Measure the cost of `vkQueueWaitIdle` per one-time command and decide | ⬜ | `EndOneTimeCommands` (`:8395`) waits the queue for every texture upload, layout transition and readback. Measure it on a realistic upload workload; if it is a material robustness problem (not merely slower), open a remediation row, otherwise record the measurement and close. **No optimization task may be opened from this row without the measurement.** |
| VULKAN-397 | Run the Vulkan example suite under ASan/UBSan for CNA-owned CPU code | ⬜ | Build `build-asan`/`build-ubsan` per `CLAUDE.md` (never a new directory, never in the scratchpad) and run the Vulkan suite. Judge by UBSan lines, ASan errors and CNA-frame leak stacks — never by the `SUMMARY:` line, which always reports driver allocations at exit. `VULKAN-130`/`VULKAN-131` are expected to show here. |
| VULKAN-398 | Repeated-use stress across the whole surface | ⬜ | One test that runs many frames of: `Begin`/`End` cycles, render-target bind/unbind cycles, texture `SetData` updates, buffer updates, and a resize every N frames — asserting correctness at the end, not only at the start, and asserting no unbounded growth in the counters `VULKAN-394`/`VULKAN-395` establish. This is the "still correct after repeated use" clause of §6.8 made testable. |

---

## 24. Phase 12 — Cross-renderer conformance (`VULKAN-430`–`VULKAN-469`)

The infrastructure already exists and is under-used: `cross_renderer_diagnostic_scene.cpp` is built
for EasyGL, Software, WebGPU **and Vulkan**, with `cna_diag_compare` as the comparator; three Vulkan
CTests already compare against EasyGL-authored golden PNGs. This phase turns that into a gate.

Rules for every row here: assert exact logical properties where the property is exact; use pixel
probes chosen to discriminate; use a tolerance only with a written justification; **never** demand
bit-identical pixels for floating-point rasterization or lighting; and never accept "renders without
throwing" as proof of a visual feature.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-430 | Make the shared diagnostic scene a real EasyGL↔Vulkan gate | ⬜ | `cna_diag_vulkan` and `cna_diag_easygl` already produce 64×64 RGBA8 dumps from one source. Add a script (not a CTest — it needs two builds) that runs both and diffs them with `cna_diag_compare` under a documented tolerance, plus a documented expected-difference list. Acceptance: the script fails when one renderer's output is deliberately perturbed. |
| VULKAN-431 | Register the shared 2D corpus on Vulkan | ⬜ | `cross_renderer_2d_corpus.cpp` is registered for EasyGL and Direct2D only. Register `cna_corpus2d_vulkan` and extend `VULKAN-430`'s script to diff the corpus too. |
| VULKAN-432 | Cross-renderer golden comparison for the stock-effect goldens | ⬜ | Register `easygl_basiceffect_golden_test.cpp`, `easygl_alphatesteffect_golden_test.cpp`, `easygl_dualtextureeffect_golden_test.cpp` on Vulkan against the same `examples/golden/` PNGs, following the precedent of `Vulkan_PbrEffect_Golden` (repo-root working directory, per-check tolerance). Each tolerance is justified in the registration comment. Depends on `VULKAN-014`'s policy. |
| VULKAN-433 | Cross-renderer golden comparison for `EnvironmentMapEffect` and `SkinnedEffect` | ⬜ | Same for `easygl_environmentmapeffect_golden_test.cpp` and `easygl_skinnedeffect_golden_test.cpp`. `EnvironmentMapEffect`'s golden is one where EasyGL is itself under suspicion (`VULKAN-260`); if Vulkan disagrees, the XNA semantics decide which is right and the golden may need to change rather than Vulkan. |
| VULKAN-434 | Cross-renderer golden comparison for the state goldens | ⬜ | `easygl_blendstate_additive_golden_test.cpp`, `easygl_depthstencilstate_write_enable_golden_test.cpp`, `easygl_rasterizerstate_cullmode_golden_test.cpp`. These are exact-arithmetic cases; a tolerance above a couple of LSBs needs a reason. |
| VULKAN-435 | Cross-renderer golden comparison for the 2D goldens | ⬜ | `easygl_spritebatch_rotation_golden_test.cpp`, `easygl_texture_filter_linear_golden_test.cpp`, `easygl_goldenimage_smoke_test.cpp`, `easygl_pixeltestgame_smoke_test.cpp`. Filtering differs legitimately between backends; the rotation and smoke cases should be near-exact. |
| VULKAN-436 | Register the shared sample scenes on Vulkan | ⬜ | `easygl_sample_dualtexture_swap_test.cpp`, `_keyboard_cube3d_`, `_layered_blend_`, `_moving_quad3d_`, `easygl_fullscreen_field_test.cpp`. These are whole-scene smoke tests through the public API; they are the cheapest broad regression net Vulkan does not have. |
| VULKAN-437 | Write the cross-renderer conformance methodology down | ⬜ | A section in `docs/vulkan-renderer.md` (or a dedicated doc) stating: what must match exactly, what may differ and why, how a tolerance is chosen, and the standing rule that a conformance test must be able to fail for the defect it claims to detect. Depends on `VULKAN-430`–`VULKAN-436` having produced real numbers. |

---

## 25. Phase 13 — Final parity gate (`VULKAN-470`–`VULKAN-489`)

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| VULKAN-470 | Re-verify capability reporting against measured behaviour | ⬜ | Re-run `VULKAN-008`'s snapshot and check every entry against a test that observes the behaviour. A capability reported true with no reachable path, or false with a working path, blocks the gate. |
| VULKAN-471 | Confirm every in-scope parity-matrix row is resolved | ⬜ | Every row in §10 is `PARITY`, `VULKAN_STRONGER`, or an explicitly justified `NOT_APPLICABLE`/`SEMANTIC_DIVERGENCE`/`CNAEXT_OUT_OF_SCOPE`. No row may still say `NEEDS_DEEPER_AUDIT`. |
| VULKAN-472 | Confirm every `IMPLEMENTATION_GAP` row has a closed task | ⬜ | Each of F-01…F-14 either has a ✅ task or a ⛔ row with the reason written here. |
| VULKAN-473 | Confirm every meaningful `TEST_GAP` row is closed | ⬜ | Every `TEST_GAP` row in §10 points at a registered, passing Vulkan CTest, or at a written justification for why it is not a Vulkan requirement. |
| VULKAN-474 | Confirm no silent no-op remains where EasyGL implements the behaviour | ⬜ | Re-run `VULKAN-027`'s interface table. Zero rows may read "Vulkan inherits a no-op default while EasyGL implements it" without a justification in the row. |
| VULKAN-475 | Full-suite green, with pre-existing failures named | ⬜ | `cmake --build cmake-build-vulkan` exits 0; `ctest --test-dir cmake-build-vulkan` is green apart from failures that are individually named, attributed and justified. `--rerun-failed -j1` is the discriminator, not the parallel run. |
| VULKAN-476 | Dedicated Vulkan suite green | ⬜ | `ctest -R '^Vulkan_'` green except explicitly justified environment/hardware skips, each naming the exact command, the exact failure and what must be rerun where. |
| VULKAN-477 | Validation clean | ⬜ | `VULKAN-393`'s gate passes with an allowlist that contains no CNA-attributable entry. |
| VULKAN-480 | Write `docs/vulkan-renderer.md` | ⬜ | The capability boundary for this renderer in one place, in the style of `docs/webgpu-renderer.md`: what is supported, what is deliberately not, what differs from EasyGL and why, the shader dialect, the format list, and the environment the numbers were measured on. Vulkan is the only mature renderer family without such a document. |
| VULKAN-481 | Correct the stale current-status claims in the shared docs | ⬜ | Fix §9.2's D-01…D-05 and D-07 in `docs/texture3d-texturecube-support.md`, `docs/rendertarget-support.md`, `docs/graphics-renderer-feature-matrix.md` and the `IGraphicsRenderer.hpp` header comment — **without deleting the historical record**. Each correction says what was true when it was written and what is true now, and cites the test that shows it. |
| VULKAN-482 | Update `NEXT.md` with the measured Vulkan baseline | ⬜ | The Vulkan lines in `NEXT.md` currently describe a lavapipe run. Replace them with the measured RADV numbers and keep the lavapipe figures as history. |
| VULKAN-487 | Re-audit against the **then-current** EasyGL before declaring parity | ⬜ | Recompute `VULKAN-007`'s comparison against EasyGL as it exists at gate time, not as it existed on 2026-09-04 (`VULKAN-010` recorded the starting point). Every EasyGL CTest added in the meantime is classified with §9.3's taxonomy, and every `REAL_VULKAN_TEST_GAP` or `REAL_VULKAN_IMPLEMENTATION_GAP` it finds becomes a new `VULKAN-*` row **before** this task can close. Parity cannot be declared while this task has open children. |

---

## 26. Dynamic backlog-expansion rules

This plan is **expected to grow**. That is the mechanism that prevents false closure, not a sign the
planning was incomplete.

1. **An audit task cannot close on a finding.** When an audit row discovers a defect or a meaningful
   test gap, closing it requires, in order: (a) the evidence recorded in this file — §9 for a new
   finding, §10 for a matrix row; (b) one or more new concrete `VULKAN-*` rows in the owning phase,
   each with the eight fields §27 requires; (c) only then ✅.
2. **New IDs are taken from the owning phase's free range.** Never renumber, never reuse. External
   documents and commit messages cite these IDs.
3. **A finding that turns out not to be a defect is recorded as such.** "Audited, no Vulkan bug
   found, here is the evidence" is a valid close. Do not invent work to justify an audit.
4. **A test-gap task may close with no production change.** If the new test passes on first run,
   that is a real result — the evidence now exists where it did not. Say so in the row.
5. **Splitting is preferred to widening.** A row that turns out to be larger than one commit is
   split into new rows before work starts.
6. **Every deviation and refusal is written in the row itself**, never only in a commit message
   (`CLAUDE.md`, `plan_modern.md`'s own convention).
7. **A status may move backwards.** A ✅ row contradicted by a later measurement returns to 🟨 with
   the contradicting evidence, exactly as §9.2's stale documentation is being treated here.
8. **`NEEDS_DEEPER_AUDIT` is a real state with an owner.** Every such matrix row names the audit
   task responsible for resolving it; a row with no owner is a planning defect.

---

## 27. What a remediation task must contain

Every non-audit row states, at minimum:

1. the exact observed problem or the exact missing evidence;
2. its classification — implementation bug, test gap, or documentation gap;
3. the source files and functions involved, with line numbers where they help;
4. the EasyGL evidence (or the explicit note that EasyGL has none either);
5. the current Vulkan evidence;
6. the intended behaviour, judged against §5's authority order;
7. the exact permanent regression test to add, and why it discriminates — it must be able to fail
   for the defect it claims to detect;
8. the validation command, the dependencies, and the explicit non-goals where scope could creep.

A row that says only "improve X parity" is not a task and must be split or specified before work
begins.

---

## 28. Definition of done

Vulkan parity is complete when **all** of the following hold at the same commit:

1. Every in-scope row of §10 is `PARITY`, `VULKAN_STRONGER`, or an explicitly justified
   `NOT_APPLICABLE` / `SEMANTIC_DIVERGENCE` / `CNAEXT_OUT_OF_SCOPE`. No `NEEDS_DEEPER_AUDIT`
   remains.
2. Every `IMPLEMENTATION_GAP` in §9.1 has a ✅ task or a ⛔ row whose reason is written here.
3. Every meaningful `TEST_GAP` is closed by a registered, passing Vulkan CTest, or is justified in
   writing as not a Vulkan requirement.
4. No silent no-op remains where EasyGL implements the behaviour (`VULKAN-474`).
5. Capability reporting matches measured behaviour at the `GraphicsDevice` seam (`VULKAN-470`).
6. `cmake --build cmake-build-vulkan` exits 0 and the full suite is green apart from failures that
   are individually named, attributed and justified.
7. The `^Vulkan_` suite is green apart from explicitly justified environment/hardware skips, each
   naming the exact command, the exact failure, what remains unverified and where it must be rerun.
8. Vulkan validation produces no unexplained CNA-attributable errors or warnings.
9. `docs/vulkan-renderer.md` exists and matches measured current behaviour; §9.2's stale claims are
   corrected without deleting the historical record.
10. `VULKAN-487` has re-audited against the **then-current** EasyGL, and every gap that re-audit
    found has been closed — not merely filed.

Point 10 is deliberately the last: the comparison is against EasyGL as it is at gate time, never
against EasyGL as it was on 2026-09-04.

---

## 29. Dependencies on other plans

| Plan | Relationship |
|---|---|
| `plans/plan_graphics.md` | Historical owner of Vulkan work (Phase 73, Tasks 664–665 and 825–861, archived in `plan_graphics_20260709.md`). Cite it for history; it owns no current Vulkan parity work. |
| `plans/plan_fx.md` | Owns compiled XNA `.fx` bytecode on every backend, including `CNA_VULKAN_COMPILED_EFFECTS`, `VulkanCompiledEffect.cpp` and `VulkanCompiledEffectTests.cpp` (`FX-065`, `FX-110`, `FX-112`). This plan does not duplicate a single row of it. Where Phase 7 touches an effect path shared with compiled effects, it references `plan_fx.md` rather than re-owning it. |
| `plans/plan_modern.md` | Owns the CNAEXT engine layer and its Phase 22 modern-GPU rollout, including every Vulkan row `MOD-2240`–`MOD-2254`. §4 lists exactly what stays there. `MOD-2096`'s mention of `plan_vulkan.md` is a pointer, not a transfer. |
| `plans/plan_csl.md` | Owns a portable shader language, so one `ShaderEffect` source can serve GLSL and SPIR-V backends. It is why this plan does **not** port EasyGL's 37 GLSL shader tests to Vulkan (`VULKAN-256`). |
| `plans/plan_gltf.md`, `plans/plan_cnj.md` | Own glTF/`.cnj` parsing. Phase 8 asks only whether the Vulkan renderer executes the already-parsed semantics correctly. |
| `plans/plan_binding.md` | Owns the C API/C ABI. `VULKAN-250` may need a new `ShaderDialectEXT` enumerator, which is an ABI change and must be coordinated there (`modules/c-api/src/CnaCApiGraphics.cpp:924`). |
| `plans/plan_platform.md` | Owns `IPlatform` and the SDL boundary. Vulkan is one of the intentional native edges; the boundary gates in `CLAUDE.md` must still pass after any change here. |
| `plans/plan_runtimerenderer.md` | Multi-renderer builds. `CNA_GRAPHICS_RENDERERS` combinations that include `VULKAN` must keep configuring; renderer-local changes are preferred to common-interface changes for exactly this reason. |
| `plans/plan_bindings_upstream.md` | Where an XNA-over-FNA divergence taken here is recorded, per `CLAUDE.md`. |

---

## 30. Historical notes and superseded Vulkan work

Kept because it is evidence about how the renderer reached its current shape. **None of it is a
current-status claim**, and none of it may be used to suppress a gap this plan measures today.

- **Phase 73 of `plans/plan_graphics.md` — "Vulkan: full 2D+3D pixel-verified parity (gap closure)"
  (Tasks 664–665, 825–861).** Every row complete and archived into `plan_graphics_20260709.md` on
  2026-07-09. That phase closed against the target that existed in July 2026; EasyGL has kept
  moving, which is the entire reason this plan exists.
- **Task 861 (2026-07-09) concluded Vulkan had "exactly 1" confirmed-open limitation**
  (`Vulkan_DepthBias`) and that `SpriteBatch` sort-mode ordering, rotation/scale/crop and
  `SpriteFont` had "no dedicated pixel test" on Vulkan. The second half has since been closed by
  twelve CTests (§9.2 D-05); the first half was measured on a software driver (§9.2 D-07).
- **Tasks 864/865/867/875/876/877/878/879/911** — the Vulkan gaps `docs/rendertarget-support.md` and
  `docs/texture3d-texturecube-support.md` still describe as open. Every one of them is contradicted
  by code and tests in the current tree (§9.2 D-01…D-04). `VULKAN-481` corrects the documents and
  keeps the history.
- **`REMED-GFX-*` remediation series.** Vulkan carries a large amount of this work — `011`
  (orientation oracle), `077` (per-MRT write masks), `095` (ShaderEffect pipelines are render-pass
  state), `127`/`130`/`134`/`135`/`136` (readback and preserve-contents contracts), `144`
  (synchronization validation), `201` (multi-stream refusal), `212`/`243`/`244`. These are the
  reason several §10 rows read `VULKAN_STRONGER` rather than `PARITY`.
- **`plans/plan_webgpu.md` `WEBGPU-123`** contributed the shared cross-renderer diagnostic scene that
  Phase 12 builds on; `cna_diag_vulkan` exists because of it.
- **`plans/plan_cnj.md` `CNB-58`/`CNB-67`/`CNB-91` and `plans/plan_gltf.md` `GLTF-463`** brought the
  PBR/skinned-PBR vertex layouts (strides 48/56/60/68/76/80) to Vulkan — and, unremarked at the
  time, the stride range that finding F-01 shows the buffer allocator never caught up with.
