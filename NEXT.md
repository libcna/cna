# NEXT.md — CNA Project Handoff

> ⛔ **WebGPU is forbidden for now** — do not work on any WebGPU task (`plan_webgpu.md`,
> Phases 56–69 / `WEBGPU-1`–`WEBGPU-123`) until the project owner explicitly lifts this
> restriction. See `CLAUDE.md` ("WebGPU Is Forbidden For Now").

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable 3D graphics backend layer. It is a
framework/runtime — not a game — designed so XNA/FNA game code can be ported to C++ with minimal
API-surface changes.

- **Main goal:** full XNA 4.0 API coverage with pixel-accurate behavior, verified against the
  authoritative FNA reference source (`/rv/data/library/github.com/FNA-XNA/FNA/src`), backed by
  unit tests (`CnaTests`) and GPU pixel-readback integration tests (`ctest`). Task-by-task history
  lives in `plan_graphics.md`; per-effect/per-phase synthesis docs live in `docs/*.md`.
- **Current phase:** Phase 55 already declared a qualified **~90% XNA/FNA compatibility milestone**
  for `Microsoft::Xna::Framework::Graphics` (see `docs/graphics-compatibility-report.md`). Work
  since then has been backlog-hygiene plus new gaps found while porting real samples from the
  sibling `../cna-samples` repo (Phases 75–78). **Phase 77 (skeletal animation playback) closed
  2026-07-10** (Tasks 939–942). **Phase 78** (HLSL→GLSL shader conversion for hand-porting sample
  effects) is open but the project owner confirmed most of it (Tasks 943/944/946/947) is
  `../cna-samples` content-porting work, not `cna_graphics` engine scope — only Task 945 (a tooling
  decision) is arguably in-scope, and even that needs Task 946's own first attempt (in the sibling
  repo) to inform it. **Not started as of this handoff** — do not begin it without checking with
  the project owner first.
- **Key architectural decisions:**
  - Backend selection is **compile-time** via the `CNA_GRAPHICS_BACKEND` CMake option
    (`EASYGL` | `VULKAN` | `BGFX` | `SDL_RENDERER`). EasyGL is primary and most heavily tested.
    `SDL_Renderer` is 2D-only by design (no 3D pipeline at all).
  - The `sharp-runtime` sibling repo (`../sharp-runtime/`) provides all `System.*` types and
    primitive type aliases (`bytecs`, `Single`, `String`, …) used on the XNA API surface.
  - Vertex layout dispatch is **stride-keyed**: EasyGL/Vulkan/Bgfx select their GPU vertex layout
    from the raw byte stride of the bound buffer, not from `VertexDeclaration` contents. Only
    strides 16/20/24/32/52 are handled correctly.
  - `Texture3D` and `TextureCube` inherit `GraphicsResource` directly, **not** `Texture` — unlike
    FNA. Known, documented architectural gap (Task 863) with real downstream consequences
    (no texture-in-shader-sampling path for either type via the generic `EffectParameter` route).
  - `GraphicsDevice` stores state objects (`BlendState`/`DepthStencilState`/`RasterizerState`) **by
    value**, unlike FNA's reference-type aliasing (Task 869, deliberate, not fixed).
  - Vulkan and Bgfx both **batch an entire frame's draws into one deferred render pass/view**,
    whose clear (color/depth/stencil) always applies once, before all of that frame's draws —
    regardless of the order `GraphicsDevice::Clear()`/`DrawXxx()` were called in game code within
    that frame. This is load-bearing for how pixel tests must be written on these two backends
    (see §6).
  - XNA compiled `.fx` bytecode: full support is the long-term goal (Phase 74, Tasks 10200–10209),
    not yet implemented — `Effect`'s bytecode constructor throws `NotImplementedException`. Custom
    shaders currently go through `ShaderEffect` (hand-written GLSL/SPIR-V per backend).

---

## 2. Current status

### Build status (verified 2026-07-10)

All 4 configured build directories build clean for their own `CNA`/`CnaTests`/example targets:

| Build dir | Backend | Status |
|---|---|---|
| `cmake-build-debug` | EasyGL | Clean |
| `cmake-build-vulkan` | Vulkan | Clean |
| `cmake-build-bgfx` | Bgfx | Clean |
| `cmake-build-sdl` | SDL_Renderer | Clean |
| `cmake-build-android` | SDL_Renderer (NDK) | Configured; **not rebuilt this session** — Task 920 (sibling `sharp-runtime` NDK build regressions) still blocks a full Android cross-compile |

**One pre-existing, unrelated build error on every desktop backend**: the `cna_demo_xact` example
target fails ("Error copying directory … `examples/demo_xact/Content`") because that directory
doesn't exist in this checkout. Cosmetic, not a CNA bug — do not chase it (see §9).

### Test status (verified 2026-07-11)

| Backend | `CnaTests` (gtest) | `ctest` (integration/pixel) |
|---|---|---|
| EasyGL | 4371/4373 pass (2 hardware-dependent skips: Accelerometer/Gyroscope) | 188/190 pass — 2 pre-existing failures (`EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`) |
| Vulkan | 4371/4373 pass (2 hardware skips) — Task 953 fixed the 3 `ContentManagerSkinnedModelTest` segfaults, no exclusions needed anymore | 125/126 pass — 1 pre-existing failure (`Vulkan_DepthBias`) |
| Bgfx | 4375/4377 pass (2 hardware skips) | **102/104 pass** — 2 remaining failures, neither a crash (see §5): `Bgfx_RenderTarget2D_MsaaResolve` (known environment limitation) and `Bgfx_RenderTargetCube_DepthFormat` (Task 952, **DEFERRED** — a `Depth24Stencil8`-attached `RenderTargetCube` face produces no colour output on Bgfx) |

All pre-existing failures above were independently reconfirmed via `git stash` (present on the
unmodified baseline too — not introduced by any change in this session).

### Recently implemented

- **Task 950** (closed 2026-07-11): `GraphicsDevice::Clear()`'s `depth` parameter now actually
  takes effect on Vulkan and Bgfx (previously every clear silently hardcoded depth=1.0). Mirrors
  Task 871's `clearStencil_` pattern exactly — new `clearDepth_`/`clearDepthValue_` members threaded
  into the same 4 `VkClearValue.depthStencil` sites / the one `bgfx::setViewClear()` call Task 871
  already touched. New shared `examples/graphicsdevice_clear_depth_test.cpp`, registered on all 3
  hardware backends (EasyGL passes unmodified, confirming it was never affected).
- **Task 952 investigation continued twice more** (2026-07-11, still open — see
  `plan_graphics.md` Task 952 for the full trail; `apitrace` and RenderDoc 1.45 are both now
  available, see `programs.md`). **Round 1 (`apitrace`)**: kept 2 real fixes — the test's readback
  methodology (unbind-then-`EnvironmentMapEffect`-sample with a non-degenerate
  `Matrix::CreateLookAt` eye position — the old read-while-bound approach relied on a quirk Task
  951 correctly removed) and confirmed `DepthFormat::None` reads back correctly with it (green),
  isolating the remaining symptom to `Depth24Stencil8` specifically. A promising-looking "orphaned
  render-target view gets silently re-cleared" theory was investigated, a candidate fix
  implemented, then **retracted** after 2 controlled repro attempts showed no observable effect —
  reverted, not committed. **Round 2 (RenderDoc, project owner provided a manual download)**:
  `qrenderdoc --python` (the only way to get RenderDoc's Python scripting API in this Linux
  release) hangs indefinitely under this sandbox's `Xvfb` (no window manager installed, no
  `offscreen`/`minimal` Qt platform compiled in) — not solved. But `renderdoccmd convert -c
  zip.xml` works fully headless and gave a structured, precisely-ordered GL call log good enough
  to rule out several more hypotheses with hard evidence: `bgfx::createFrameBuffer()`'s handle for
  the color+depth cube-face combo is valid; the correct view id is used for both quad draws; the
  actual GL draws do execute against the cube's real FBO; the cube's real texture is correctly
  bound for sampling on every one of ~20 retry attempts. Despite all of that being correct, the
  sample never picks up any drawn content. Root cause still not found — needs either a working WM
  to unblock `qrenderdoc`'s pixel-history/texture-viewer tools, or a from-scratch raw-GL repro —
  see §5/§8.
- **Task 951** (closed 2026-07-11): root-caused and fixed 5 of the 6 pre-existing Bgfx
  `RenderTarget2D`/`RenderTargetCube` `ctest` crashes (93/99 → 97/99). Root cause: bgfx processes
  views in ascending id order each frame, and every CNA render-target view has id > 0 (reserved
  view 0 = backbuffer) — so any RT with same-frame pending work is always the *last* view bgfx
  processes, leaving its fbo GL-bound when `ReadBackbuffer()`'s `bgfx::requestScreenShot()`-driven
  `glReadPixels()` fires, crashing outright for depth/MSAA/mip-attached RTs under this sandbox's
  legacy-GL-2.1 bgfx OpenGL context. Fixed via a dedicated, permanently-reserved highest-id "flush"
  view (255) touched right before every screenshot request — guaranteed to always be processed
  last, without ever touching (and so never discarding) any real RT's own still-pending draws. A
  first attempt (releasing the outgoing RT's own view on every `SetRenderTarget2D`/
  `SetRenderTargetCubeFace` switch) was tried and reverted — it silently discarded same-frame
  pending draws, regressing `Bgfx_ConcurrentRenderTargets` and corrupting 3 tests' own RT content
  to black. A 6th, unrelated crash (`Bgfx_RenderTargetCube_DepthFormat`, a bgfx hard-assert on an
  invalid depth-attachment resolve flag) was also fixed; it now surfaces a separate, real,
  pre-existing bug (RenderTargetCube depth doesn't gate face draws on Bgfx) opened as **Task 952**.
- **Phase 77 — skeletal animation playback** (Tasks 939–942, closed 2026-07-10): `.model.json` can
  now carry an optional `"skeleton"`/`"animations"` schema; `Content.Load<Model>()` attaches a real
  `SkinningData` to the loaded `Model`'s `Tag`; a new `AnimationPlayer` class samples keyframe clips
  and feeds `SkinnedEffect::SetBoneTransforms()` before `Model.Draw()` — proven end-to-end with a
  pixel test showing a loaded, animated mesh visually deform over time.
- **Task 871** (closed 2026-07-10): `GraphicsDevice::Clear()` now actually clears the stencil
  buffer to the requested value on all 4 backends (previously silently discarded `stencil` and
  never checked `ClearOptions::Stencil` anywhere).

### Known working examples

Representative, currently-passing demos/examples: `cna_demo_2d`, `cna_demo_avatar` (+ several
avatar sub-demos), `cna_house3d_demo`, and ~180 EasyGL / ~120 Vulkan / ~90 Bgfx registered `ctest`
pixel-verification examples under `examples/`.

### Does NOT work yet

- XNA compiled `.fx` bytecode (`Effect` constructor throws `NotImplementedException`) — Phase 74.
- `Texture3D`/`TextureCube` cannot be bound as a shader sampler through the generic `EffectParameter`
  path — Task 863, needs an architecture decision (see §5).
- `GraphicsDevice.ReferenceStencil`'s independent-override semantics — EasyGL/Bgfx only; Vulkan
  already fixed (Task 872, remaining EasyGL/Bgfx scope not started).
- Android cross-compile (blocked on sibling `sharp-runtime` NDK build regressions, Task 920,
  cross-repo — needs project-owner approval to touch that repo).
- `EnvironmentMapEffect`/`SkinnedEffect` `VertexColorEnabled`/multi-light/specular gaps opened by
  earlier per-effect audits (Tasks 890–895) — real, scoped, not started. (Task 889,
  `DualTextureEffect`'s own gap, is now fixed — see §3.)

---

## 3. Recent changes

Most recent first. Full detail (exact formulas, discriminating-power verification, per-backend fix
shape) is in `plan_graphics.md` — this section is intentionally a short index.

| Commit | Task | Summary |
|---|---|---|
| *(pending)* | 955 | XNA depth-occlusion compatibility audit **RESOLVED** (cross-repo follow-up to Task 954, `../cna-samples` SimpleAnimation finding). After Task 954's winding fix, some tank parts still rendered visible when they should've been occluded — a depth problem, not placement. **Root cause: `GraphicsDevice`'s constructor only pushed its `RasterizerState` default to the backend (Task 896) — `BlendState`/`DepthStencilState` were left as C++ fields only, never applied.** On EasyGL this left OpenGL's raw depth-test default (disabled) in effect for any game (like `Tank.hpp`, mirroring real XNA's own `Tank.cs`) that never explicitly sets `DepthStencilState` itself. Confirmed via real FNA source: `GraphicsDevice.cs`'s constructor sets all 3 of `BlendState`/`DepthStencilState`/`RasterizerState` unconditionally; Task 896 ported only the 3rd. **Fixed** by adding the other 2 lines to CNA's `GraphicsDevice` constructor, matching FNA exactly — zero `cna-samples` changes needed (diagnostic isolation proved this: an explicit `DepthStencilState`-only override in the sample reproduced pixel-identical results to the real constructor fix). Bonus: fixed Bgfx's own wrong blend default (`BGFX_STATE_BLEND_ALPHA`) as a side effect of the same root cause. New shared 3-backend regression test `graphicsdevice_default_state_occlusion_test.cpp` — deliberately the one test in this project that never explicitly sets state, to actually exercise `GraphicsDevice`'s own defaults. Found+documented (not fixed) a second, separate `SpriteBatch` blend-state-leak bug — see Task 956. |
| `fe893b91` | 954 | XNA culling compatibility audit **RESOLVED** (cross-repo, `../cna-samples` SimpleAnimation finding). **Framework confirmed correct** on all 3 backends — first via 2 new NDC-area-prediction reproducer tests (36/36 PASS), then independently confirmed against real XNA 4.0 (not just FNA source reading) via a C# `CullModeTest` project the project owner ran on a real Windows 7 VM: real XNA's default `CullMode` matches CNA's exactly. No CNA change made, none justified. Asset-level root cause **corrected in scope** after project-owner pushback (a first pass wrongly narrowed it to `turret_geo` alone) to a **systematic winding reversal across all 12 of `tank.fbx`'s converted mesh parts**, confirmed via a whole-model diagnostic CullMode flip. **Fixed** by reversing triangle winding in all 12 `tank_*_idx.bin` files (originals backed up first); verified visually at 3 rotation angles against the XNA reference. Found+reverted a real, separate Bgfx `DrawIndexedPrimitivesEx` startIndex/baseVertex bug (still open, needs its own follow-up task — see §5). |
| `4425be1d` | 891 | Fixed `EnvironmentMapEffect`'s cube-map lerp target not being scaled by combined texture×diffuse alpha (only the already-correct specular term was). One-line `mix()` fix on all 3 backends (`envSample.rgb * combinedAlpha` instead of unscaled `envSample.rgb`). New shared 3-backend test `environmentmapeffect_alphascaledlerp_test.cpp`. |
| `a79469f2` | 889 | Fixed `DualTextureEffect.VertexColorEnabled` being a total no-op on all 3 backends. New stride-24-only sibling vertex shader/program on each backend (Vulkan `dual_texture_colored3d.vert.glsl`, Bgfx `vs_dual_texture_colored3d.sc`, EasyGL `EnsureDualTexturedColored3DProgram()`), mirroring Task 887's exact `AlphaTestEffect` pattern; stride-20 path unchanged. New shared 3-backend test `dualtextureeffect_vertexcolor_test.cpp`. |
| `07ca2ad7` | 953 | Fixed Vulkan segfaulting on a 0-length index/vertex buffer (3 `ContentManagerSkinnedModelTest` crashes). `VulkanIndexBufferBackend`/`VulkanVertexBufferBackend` constructors now clamp their computed `VkDeviceSize` to a minimum of 1 byte before `vkCreateBuffer`/`vkAllocateMemory` (previously size 0 for an empty model part, invalid per spec). `ContentManagerSkinnedModelTest.*` now 9/9 pass; full Vulkan `CnaTests` 4371/4373 clean. |
| `d562a813` | 952 | Explicitly marked Task 952 as **DEFERRED** per project owner instruction — no further investigation this session. Docs-only: `plan_graphics.md`, `NEXT.md` §5/§8/§9 updated to flag "do not resume without explicit direction." |
| `5a094666` | 952 | Continued the Task 952 investigation with a real RenderDoc 1.45 install (project owner provided a manual download after the apitrace round hit its wall). `qrenderdoc --python` hangs indefinitely in this sandbox (no WM, no offscreen Qt platform); `renderdoccmd convert -c zip.xml` works headless and gave enough structured GL-call detail to rule out FBO-handle validity, view-id targeting, and texture-handle identity as the bug, with hard evidence. Root cause still not found. No code changes (all diagnostics reverted); `programs.md` updated with real RenderDoc findings. |
| `d0146c67` | 952 | Continued the Task 952 investigation with `apitrace`. Fixed the depth-format test's own readback methodology (unbind-then-`EnvironmentMapEffect`-sample) and a degenerate-eye-position sampling bug, isolating the real remaining symptom to `Depth24Stencil8` specifically. Investigated and retracted a candidate "orphaned view gets silently re-cleared" fix after it failed 2 controlled repro attempts. Root cause still not found — needs RenderDoc or a raw-GL repro. |
| `22bbaa7f` | 950 | `GraphicsDevice::Clear()`'s `depth` parameter now actually takes effect on Vulkan/Bgfx (was hardcoded to 1.0). New `clearDepth_`/`clearDepthValue_` members mirror Task 871's `clearStencil_` pattern exactly. New shared 3-backend test `graphicsdevice_clear_depth_test.cpp`. |
| `981f1b0c` | 951 | Fixed 5 of 6 pre-existing Bgfx `RenderTarget2D`/`RenderTargetCube` `ctest` crashes via a dedicated highest-view-id "backbuffer flush" view touched before every screenshot request; fixed a 6th, unrelated crash (invalid depth-attachment resolve flag) that then surfaced Task 952 (RenderTargetCube depth doesn't gate face draws on Bgfx). |
| `eba5d0bb`/`9762cdac` | 871 | `GraphicsDevice::Clear()` now clears the stencil buffer on all 4 backends. Vulkan needed `stencilLoadOp` fixed on 5 separate render-pass-creation sites (was hardcoded `DONT_CARE`); Bgfx needed a real stencil value threaded into its `bgfx::setViewClear()` call. Opened Task 950 (depth value still hardcoded on Vulkan/Bgfx). |
| `225fc60b` | 942 | Proved `AnimationPlayer::GetSkinTransforms()` → `SkinnedEffect::SetBoneTransforms()` → `Model.Draw()` renders correctly end-to-end via a new pixel test; no engine code was actually missing. Closes Phase 77. |
| `e495dbc0` | 941 | `ModelTypeReader::Read()` parses `.model.json`'s new `"skeleton"`/`"animations"` fields into a `SkinningData` on `Model.Tag`, plus a stride-52 GPU-skinned vertex branch and `"effect": "SkinnedEffect"` support. |
| `c173e6ed` | 940 | Added `AnimationClip`/`Keyframe`/`AnimationPlayer`/`SkinningData` (all `NOXNA`). |
| `41245c7f` | 939 | Design decision: reuse `SkinnedModelEXT`'s existing `.skeleton.bin`/`.clip.bin` formats verbatim for `.model.json`'s new schema, rather than inventing a new one. |
| `8003ac3f` | 949 | Vulkan `GetViewportSize()` no longer mixes logical/physical pixel units (fixed a real HiDPI/resize viewport bug). |
| `edc37e2e` | 937 | `ModelTypeReader::Read()` gives each mesh its own real `ModelBone` instead of leaving `ParentBone` null. |
| `0596a602` | 936 | Added `ModelMesh::setParentBoneProperty(ModelBone*)`. |
| `fb245962` | 948 | Added `BgfxGraphicsBackend::DrawIndexedPrimitivesEx` override. |

---

## 4. Current blocker / main problem

**No hard blocker exists** — all 4 backends build clean and the vast majority of tests pass. The
Bgfx crash cluster that previously occupied this section (6 crashing `RenderTarget2D`/
`RenderTargetCube` tests) was root-caused and fixed by Task 951 (2026-07-11) — see §3. The 2
remaining Bgfx `ctest` failures are both non-crashing, already-diagnosed, and tracked in §5
(`Bgfx_RenderTarget2D_MsaaResolve` — known environment limitation; `Bgfx_RenderTargetCube_DepthFormat`
— new Task 952). Nothing else in the current test suite is unexplained.

---

## 5. Known bugs and limitations

| Status | Description | Task |
|---|---|---|
| **DEFERRED (2026-07-11)** — investigated 3 times (apitrace + RenderDoc), not fixed, explicitly paused by the project owner | A `Depth24Stencil8`-attached `RenderTargetCube` face produces no colour output at all on Bgfx (not even the "wrong" quad — just background) — reproduces with a single draw call and with `DepthStencilState::None` (depth testing fully disabled), regardless of depth format or stencil presence. Not a depth-*test* bug. `RenderTarget2D`'s structurally-similar depth attachment works fine, and it's specific to `RenderTargetCube`, not depth attachments generally. RenderDoc-based analysis (`renderdoccmd convert -c zip.xml`, headless) confirmed with hard evidence that the FBO handle is valid, the correct view id is used for both quad draws, the GL draws do execute against the cube's real FBO, and the cube's real texture is correctly bound for sampling on every retry attempt — yet the content is never visible when sampled. See `plan_graphics.md`'s Task 952 entry for the full investigation trail (2 rounds, both without a fix). Needs either a working window manager to unblock `qrenderdoc`'s GUI pixel-history tools in this sandbox, or a from-scratch raw-GL repro bypassing bgfx entirely. **Do not resume without explicit instruction** — see §9. | 952 |
| Confirmed bug, environment limitation | `Bgfx_RenderTarget2D_MsaaResolve`: this sandbox's bgfx OpenGL path negotiates only a legacy GL 2.1 context (llvmpipe), under which MSAA-flagged framebuffer textures don't sub-pixel resolve — pre-existing, already diagnosed at Task 878/879. The `CNA_BGFX_RENDERER=VULKAN` workaround recorded there no longer routes around it in this sandbox (bgfx silently falls back to `active renderer: OpenGL 2.1` even when Vulkan is requested, confirmed while investigating Task 951) — worth its own future look, not chased here. | — |
| Confirmed bug | `EasyGL_MRT_TwoAttachments`: a basic 2-target same-size/format MRT setup doesn't render correctly — attachment 1 stays black. Pre-existing, off-limits for opportunistic fixing per project convention (see §9). | 145 |
| Confirmed bug | `Vulkan_DepthBias` fails; pre-existing, not investigated further. | — |
| Test-order-dependent flakiness, environment-only | `DrawUserIndexedPrimitivesArgumentGuardTest.VD_16bit_ZeroCount_Throws` (and other unrelated tests) occasionally fail only as part of a full `CnaTests` run on Vulkan/llvmpipe, never in isolation — documented resource-contention flakiness under rapid SDL-window creation on a software rasterizer (Task 883's own write-up has the same signature with different tests). Not tied to any specific diff. | — |
| Confirmed bug, not fixed | `GraphicsDevice.ReferenceStencil`'s independent-override semantics have zero backend connection on EasyGL/Bgfx (Vulkan already fixed, an undocumented side effect of Task 870). | 872 |
| Confirmed bug, found+reverted, needs its own task | `BgfxGraphicsBackend::DrawIndexedPrimitivesEx`'s non-wireframe path silently discards `GpuDrawParams::startIndex`/`baseVertex` (always binds the whole buffer from index/vertex 0). Not visible in any current CNA sample/test (every real `Model`/`ModelMeshPart` owns its own dedicated buffer starting at 0), but affects any genuine sub-range indexed draw. A fix was attempted (offset-aware `bgfx::setIndexBuffer`/`setVertexBuffer` overloads) but caused a worse regression (the offset draw stopped rendering at all) — reverted, not committed. See `docs/xna_culling_compatibility_audit.md` §8. | 954 |
| Confirmed bug, not fixed | `EasyGLSpriteBatchBackend::Begin()` enables blending unconditionally; `End()`/`FlushBatch()` never restores the prior blend state — any 3D draw issued after a `SpriteBatch.Begin()`/`End()` pair inherits its blend state instead of whatever was active before, unless the game explicitly resets `BlendState` itself. Found while investigating Task 955; not implicated in that bug (SimpleAnimation's own help-overlay `SpriteBatch` never ran in either repro). Confirmed via source read only, not yet reproduced live. See `docs/xna_depth_occlusion_compatibility_audit.md` §8. | 956 |
| **RESOLVED (2026-07-11)** | `../cna-samples` SimpleAnimation's dark turret underside/hollow wheels: all 12 of `tank.fbx`'s converted mesh parts had reversed triangle winding relative to real XNA's content-pipeline convention (not just `turret_geo` — an earlier narrower conclusion was corrected after project-owner pushback). Fixed by reversing winding in all 12 `tank_*_idx.bin` files under `cna-samples/samples/SimpleAnimation/Content/`; no CNA change, no SimpleAnimation-specific CullMode workaround. `CameraShake`/`CustomModelClass`/`ReachGraphicsDemo` have their own independent, still-unfixed copies of the same tank mesh files — likely share the defect, flagged for whoever next touches those samples. See `docs/xna_culling_compatibility_audit.md`. | 954 |
| Confirmed bug, not fixed | `EnvironmentMapEffect`/`SkinnedEffect` `DirectionalLight1`/`DirectionalLight2` are unforwarded; `EnvironmentMapEffect`'s cube-map lerp isn't alpha-scaled; `SkinnedEffect` has zero specular implementation and `WeightsPerVertex` is a GPU no-op. | 890/891/893/894/895 |
| Investigated, not fixed | EasyGL: a full-backbuffer `SpriteBatch` draw before any 3D draw call in the same frame breaks that frame's 3D rendering entirely. Investigated 2026-07-10, root cause not yet isolated. | 933 |
| Needs architecture decision | `Texture3D`/`TextureCube` inherit `GraphicsResource`, not `Texture` — no shader-sampling bind path via the generic `EffectParameter` route. Two named fix options, neither picked; touches `EffectParameter`, `TextureCollection`, and every backend's texture-bind code. | 863 |
| Needs architecture decision | `GraphicsDevice` state objects (`BlendState`/`DepthStencilState`/`RasterizerState`) use C++ value semantics; FNA uses reference semantics. Project-wide implication, not a small patch. | 869 |
| Needs project-owner decision | HLSL→GLSL conversion approach for Phase 78 (manual line-by-line port vs. a `dxc`+`SPIRV-Cross`-assisted pipeline). Recommend deciding after a first real attempt (Task 946, in `../cna-samples`). | 945 |
| Incomplete, cross-repo | Android NDK cross-compile blocked by build regressions in the sibling `sharp-runtime` repo. Cross-repo changes need the project owner's real-time approval before pushing. | 920 |
| Known, cosmetic | `cna_demo_xact` example fails to build (`examples/demo_xact/Content` directory doesn't exist in this checkout) — pre-existing on every backend, not a CNA bug. | — |

---

## 6. Architecture notes

### Main modules

| Layer | Location | Notes |
|---|---|---|
| XNA public API | `include/Microsoft/Xna/Framework/…` | Must match XNA 4.0 / FNA exactly |
| Backend contracts | `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` | One abstract interface, 4 concrete implementations |
| EasyGL backend | `src/CNA/Internal/Backends/EasyGL/` | Primary; OpenGL ES 3.2 via the `easy-gl` wrapper (sibling repo) |
| Vulkan backend | `src/CNA/Internal/Backends/Vulkan/` | Defers a whole frame's draws into one command buffer, recorded/replayed once per `Present()` |
| Bgfx backend | `src/CNA/Internal/Backends/Bgfx/` | Similar per-frame/per-view batching to Vulkan; `ReadBackbuffer()` only reliably reflects the *first* read per rendered frame |
| SDL_Renderer backend | `src/CNA/Internal/Backends/SdlRenderer/` | 2D-only; every 3D method throws `std::runtime_error` |
| CNA utilities | `include/CNA/`, `src/CNA/` | `NOXNA` helpers, logging, math |
| sharp-runtime | `../sharp-runtime/` (sibling repo) | `System.*` types, primitive aliases |
| Content pipeline | `src/Microsoft/Xna/Framework/Content/ContentManager.cpp` | Single large file, one `ContentTypeReader` subclass per asset type in an anonymous namespace; shared JSON/binary parsing helpers must be declared *before* their first use (plain C++ ordering — bit everyone once already) |

### Critical invariants (do not break these)

- **`NOXNA` macro** tags every non-XNA extension in public headers — required for any new CNA-only
  public method/constructor/type. Requires `#include "CNA/CNAHelper.hpp"`.
- **C# properties** → `getXProperty()` / `setXProperty()` — never public fields on the XNA surface.
- **Type aliases** from `SharpRuntime/SharpRuntimeHelper.hpp` (`bytecs`, `Single`, `String`, …) must
  be used on XNA API surfaces — never raw `uint8_t`/`float`/`std::string` directly.
- **Backend selection is compile-time** — no runtime branch between backends in the same binary.
- **Stride-keyed vertex layout** — only strides 16/20/24/32/52 work correctly for 3D.
- **Doxygen required** on every public `.hpp` member: full `/** @brief … @param … @return */`.
- **SPDX header** `// SPDX-License-Identifier: MS-PL` at the top of every `.hpp`/`.cpp`.
- **`Texture3D`/`TextureCube` inherit `GraphicsResource`, not `Texture`** — a known deviation from
  FNA (Task 863). Do not assume code that works for `Texture2D` "just works" for these two.
- **Vulkan/Bgfx clear semantics**: a frame's color/depth/stencil clear always applies once, before
  all of that frame's draws — a `Clear()` call mid-frame does not take effect "at that point," only
  at the next frame boundary. Any new pixel test that needs to prove a `Clear()`-family change took
  effect between two draws **must** split the sequence across separate real frames (one step per
  `Game::Draw()` call via a step counter) — see `examples/easygl_graphicsdevice_clear_stencil_test.cpp`
  / `examples/bgfx_graphicsdevice_clear_stencil_test.cpp` for the established pattern. A same-frame
  test structure will silently fail to discriminate the very bug it's meant to catch.
- **`Effect`/`EffectTechnique`/`EffectPass`/`EffectParameterCollection`** — must use
  `vector<unique_ptr<T>>`, not `vector<T>` by value, if any code caches a raw pointer/reference
  across an `Add()` call (a real dangling-pointer/use-after-free bug class, fixed everywhere it was
  found so far).

### FNA reference

Authoritative behavioral reference: `/rv/data/library/github.com/FNA-XNA/FNA/src`. When CNA
intentionally diverges from FNA, document it in the commit/PR description and in `plan_graphics.md`
— not as a source comment explaining the deviation's rationale.

---

## 7. Useful commands

```bash
# Configure (pick one backend per build dir)
cmake -B cmake-build-debug  -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCNA_BUILD_TESTS=ON
cmake -B cmake-build-bgfx   -DCNA_GRAPHICS_BACKEND=BGFX   -DCNA_BUILD_TESTS=ON

# Build the CNA library
cmake --build cmake-build-debug --target CNA -j$(nproc)

# Build and run all unit tests (any backend build dir)
cmake --build cmake-build-debug --target CnaTests -j$(nproc)
SDL_VIDEODRIVER=x11 DISPLAY=:99 ./cmake-build-debug/CnaTests

# Run one gtest suite
SDL_VIDEODRIVER=x11 DISPLAY=:99 ./cmake-build-debug/CnaTests --gtest_filter="Texture2DTest.*"

# Full ctest run for one backend (run one backend's suite at a time — see §9)
ctest --test-dir cmake-build-debug -R "^EasyGL_" --timeout 60
ctest --test-dir cmake-build-vulkan -R "^Vulkan_" --timeout 60
ctest --test-dir cmake-build-bgfx  -R "^Bgfx_"   --timeout 60

# Reproduce the remaining known Bgfx RenderTargetCube depth-gating bug (§5, Task 952)
SDL_VIDEODRIVER=x11 DISPLAY=:99 ./cmake-build-bgfx/cna_test_bgfx_rendertargetcube_depthformat

# Run one example/integration test directly
SDL_VIDEODRIVER=x11 DISPLAY=:99 ./cmake-build-bgfx/cna_test_bgfx_rendertarget2d_depth
```

**Environment note:** this sandbox has no real X server on `:0` — every GPU/window-creating binary
must run with `SDL_VIDEODRIVER=x11 DISPLAY=:99` (a virtual `Xvfb` display), not the CMake cache's
own default of `DISPLAY=:0` (override via `-DCNA_TEST_DISPLAY=:99` if driving through `ctest`
directly without the env vars above already set).

---

## 8. Next smallest tasks

1. **Decide Task 945** (manual HLSL→GLSL port vs. `dxc`+`SPIRV-Cross` tooling) — or defer until
   Task 946's first real attempt exists in `../cna-samples`. Requires project-owner input; do not
   pick an approach unilaterally.
2. **Fix Task 890/893/894/895** (`EnvironmentMapEffect`/`SkinnedEffect` `VertexColorEnabled`/
   multi-light/specular gaps — same family as the now-closed Tasks 889/891, see §2 "Does NOT
   work yet"). Task 891 (this same family's `EnvironmentMapEffect` alpha-scaled-lerp gap) is now
   also closed.
3. Task 952 (`RenderTargetCube` depth-gating bug on Bgfx) is **DEFERRED**, not a next task — see §9.
4. **Fix Task 956** (`EasyGLSpriteBatchBackend`'s blend state leaking into subsequent 3D draws —
   found while investigating Task 955, not yet reproduced live or fixed). Needs a minimal
   reproducer first, then a fix in `EasyGLSpriteBatchBackend::End()`; check Vulkan/Bgfx for the
   same gap before scoping to EasyGL only.

---

## 9. Do not do yet

- **Do not touch WebGPU** (`plan_webgpu.md`, Phases 56–69) — hard-forbidden by the project owner.
- **Do not start Phase 78's sample-porting tasks (943/944/946/947) in `../cna-samples`** without
  explicit direction — the project owner confirmed most of Phase 78 is out of `cna_graphics` scope
  and chose to stop before entering it (2026-07-10).
- **Do not attempt Task 863 or Task 869** (the two architecture-decision items in §5) without the
  project owner picking a direction first — both touch wide surface area
  (`EffectParameter`/`TextureCollection` for 863; every `GraphicsDevice` state setter for 869).
- **Do not resume Task 952** (`Depth24Stencil8`-attached `RenderTargetCube` face produces no colour
  output on Bgfx) without explicit direction — explicitly marked **DEFERRED** by the project owner
  on 2026-07-11 after 2 full investigation rounds (apitrace, then RenderDoc 1.45) found no root
  cause. See §5 and `plan_graphics.md`'s Task 952 entry for the full trail and what's still needed
  (a working window manager to unblock `qrenderdoc`'s GUI pixel-history tools, or a from-scratch
  raw-GL repro) before picking this back up.
- **Do not chase `cna_demo_xact`'s build failure** — it's a missing example asset directory, not a
  CNA bug; fixing it is out of scope for engine work.
- **Do not attempt `EasyGL_MRT_TwoAttachments`** opportunistically — pre-existing, previously
  flagged as off-limits without a dedicated task.
- **Do not run more than one backend's `ctest`/`CnaTests` suite concurrently** — confirmed this
  session that concurrent unrelated build/test processes cause spurious `Subprocess aborted`
  failures on otherwise-passing tests (resource contention under `Xvfb`/`llvmpipe`, not real bugs).
  Always check `ps aux | grep -i ctest` before starting a run.
- **Do not bundle multiple task numbers into one commit** — this project's convention is one task
  per commit, staged by explicit filename (never `git add -A`/`.`).
- **No broad refactors** of `GraphicsDevice::Clear`, `IGraphicsBackend`, or the Vulkan/Bgfx render
  pass creation code beyond what Tasks 871/950 already scope — both backends' render-pass/view
  clear behavior has several interacting call sites; changes need the same site-by-site care Task
  871 required, not a sweeping rewrite.

---

## 10. Resume prompt

```
Read NEXT.md first, in full, before touching any code.

Pick exactly one task from §8 "Next smallest tasks" (default to the first one unless told
otherwise). Inspect only the files that task names — do not go exploring unrelated modules, and do
not refactor anything you find along the way that isn't directly required for this task.

Make one small, verified improvement:
1. Investigate/reproduce the issue first (run the exact failing command from §4/§8).
2. Implement the smallest correct fix.
3. Write or extend a discriminating test that would fail without your fix (verify this via
   `git stash` or a targeted mutation, per this project's established methodology).
4. Run the relevant build/test command from §7 for every backend your change touches — not just
   the one you're actively working in.
5. Update `plan_graphics.md` with the task's closure detail, then update NEXT.md (this file): move
   the task out of §8, add a one-line entry to §3, and refresh §2/§4/§5 if your fix changed the
   current test-pass counts or closed a known bug.
6. Commit (staged by explicit filename, one task per commit) and push, following this repo's
   existing commit-message style (`git log --oneline`).

Do not start a second task in the same session unless the first is fully closed, tested, committed,
and NEXT.md is updated.
```
