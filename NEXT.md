# NEXT.md — CNA Project Handoff

> ✅ **WebGPU's native 2D baseline is complete (2026-07-12).** `WEBGPU-124`–`WEBGPU-131` are all ✅:
> native clear/present (`WEBGPU-125`), SpriteBatch 2D validation (`WEBGPU-126`), lifecycle
> hardening (`WEBGPU-127`), reproducible native runtime deployment (`WEBGPU-128`), the automated
> smoke harness (`WEBGPU-129`), and a real independent application (`../mobile-eggbert`) reaching
> its menu and a mission-start animation via WebGPU with no validation errors (`WEBGPU-130`,
> mobile-eggbert commit `dcdb648` — **local, not pushed**, see §3). `WEBGPU-131` records the
> baseline. This is an autonomous long session (started 2026-07-12): now proceeding into the
> automated readback/pixel-test tranche (`WEBGPU-88`–`99`) and, time permitting, the 3D backlog
> (Phases 57–66) per `plan_webgpu.md`'s own active execution order. Still treat browser/Emscripten
> support as unstarted, and 3D/effects/render-targets/MRT as not yet implemented until their own
> tasks close.

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
  sibling `../cna-samples` repo (Phases 75–79). **Phase 77 (skeletal animation playback) closed
  2026-07-10** (Tasks 939–942). **Phase 78** (HLSL→GLSL shader conversion for hand-porting sample
  effects) is open but the project owner confirmed most of it (Tasks 943/944/946/947) is
  `../cna-samples` content-porting work, not `cna_graphics` engine scope — only Task 945 (a tooling
  decision) is arguably in-scope, and even that needs Task 946's own first attempt to inform it.
  **Task 946 (manually porting BloomSample's 3 HLSL shaders to GLSL, as the data point for Task
  945) closed 2026-07-11** — the reference HLSL source was located
  (`/rv/tmp/XNAGameStudio/Samples/BloomSample_4_0/BloomPostprocess/Content/*.fx`), all 3 shaders
  proved to port 1:1 with no restructuring, and along the way this uncovered and fixed 2 real,
  previously-unknown `cna_graphics` bugs (Tasks 1077/1078 — see §3): `ShaderEffect::SetUniformXxx()`
  never actually reached a `SpriteBatch`-driven draw on EasyGL (1077), and
  `EasyGLSpriteBatchBackend`'s viewport/projection always sized to the window instead of a bound
  `RenderTarget2D` (1078). **Task 945 itself is still the project owner's call** — Task 946's own
  data point recommends manual porting scales fine for this shader family, no `dxc`+`SPIRV-Cross`
  pipeline needed yet, but the final decision hasn't been made. **Tasks 954/955 closed 2026-07-11** (a real cross-repo investigation of
  a `SimpleAnimation` rendering bug, reported by the project owner: `RasterizerState.CullMode`
  itself was proven correct — the actual bugs were a systematic tank-mesh winding reversal in
  `../cna-samples`' own converted asset data, Task 954, and a `GraphicsDevice`-construction-time
  gap where `BlendState`/`DepthStencilState` defaults never reached the backend, Task 955 — see
  `docs/xna_culling_compatibility_audit.md`/`docs/xna_depth_occlusion_compatibility_audit.md`).
  **Phase 79 opened 2026-07-11** (Tasks 957–1076, not started): a full one-task-per-sample re-audit
  of all 153 `../cna-samples`-catalogued samples (including the 67 previously-ignored ones),
  prompted directly by Tasks 954/955 showing a sample marked "Done" can still hide a real,
  unfound CNA bug — see §8 item 5 and `plan_graphics.md`'s own Phase 79 intro for the full scope.
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

### Test status (verified 2026-07-11, after Tasks 954/955)

| Backend | `CnaTests` (gtest) | `ctest` (integration/pixel) |
|---|---|---|
| EasyGL | 4371/4373 pass (2 hardware-dependent skips: Accelerometer/Gyroscope) | 189/191 pass — 2 pre-existing failures (`EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`) |
| Vulkan | 4371/4373 pass (2 hardware skips) — Task 953 fixed the 3 `ContentManagerSkinnedModelTest` segfaults, no exclusions needed anymore | 126/127 pass — 1 pre-existing failure (`Vulkan_DepthBias`) |
| Bgfx | 4375/4377 pass (2 hardware skips) | **103/105 pass** — 2 remaining failures, neither a crash (see §5): `Bgfx_RenderTarget2D_MsaaResolve` (known environment limitation) and `Bgfx_RenderTargetCube_DepthFormat` (Task 952, **DEFERRED** — a `Depth24Stencil8`-attached `RenderTargetCube` face produces no colour output on Bgfx) |

`ctest` counts include the 6 new Task 954 CullMode reproducer tests and the 3 new Task 955
`GraphicsDevice_DefaultStateOcclusion` tests (1 per backend), all passing.

All pre-existing failures above were independently reconfirmed via `git stash` (present on the
unmodified baseline too — not introduced by any change in this session).

### Recently implemented

- **Task 955** (closed 2026-07-11): `GraphicsDevice`'s constructor now syncs `BlendState`/
  `DepthStencilState` to the backend, not just `RasterizerState` (Task 896 had ported only that
  3rd line from FNA's own constructor). Root cause of a real SimpleAnimation part-occlusion bug
  reported by the project owner after Task 954's fix: on EasyGL, depth testing was OpenGL's raw
  default (disabled) for any game — like `Tank.hpp`, correctly mirroring real XNA's own `Tank.cs`
  — that never explicitly sets `DepthStencilState` itself. Fix mirrors real FNA's
  `GraphicsDevice.cs` constructor line-for-line; zero `cna-samples` changes needed (diagnostic
  isolation proved this before the fix was written). Bonus: fixed Bgfx's own wrong blend default
  (`BGFX_STATE_BLEND_ALPHA`) as a side effect. New shared 3-backend test
  `graphicsdevice_default_state_occlusion_test.cpp` — deliberately the one test in this project
  that never explicitly sets state, to actually exercise `GraphicsDevice`'s real defaults. See
  `docs/xna_depth_occlusion_compatibility_audit.md`. Found+documented (not fixed) a second,
  separate `SpriteBatch` blend-state-leak bug, Task 956.
- **Task 954** (closed 2026-07-11): cross-repo XNA culling-compatibility audit. `RasterizerState.
  CullMode` proven correct — confirmed twice, once via 2 new NDC-area-prediction reproducer tests
  (36/36 PASS across all 3 backends) and once independently against real XNA 4.0 (not just FNA
  source reading) via a C# `CullModeTest` project the project owner ran on a real Windows 7 VM.
  The actual SimpleAnimation symptom (a dark, disc-shaped turret underside) was root-caused —
  after an initial too-narrow conclusion was corrected on project-owner pushback — to a systematic
  winding reversal across all 12 of `tank.fbx`'s converted mesh parts in `../cna-samples`' own
  asset data, fixed by reversing triangle winding in all 12 `tank_*_idx.bin` files. No `cna`
  framework change was needed. See `docs/xna_culling_compatibility_audit.md`.
- **Phase 79 opened** (2026-07-11, not started): full one-task-per-sample re-audit of all 153
  `../cna-samples`-catalogued samples (Tasks 957–1076), prompted by the above 2 tasks — see §8.
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

---

## 3. Recent changes

Most recent first. Full detail (exact formulas, discriminating-power verification, per-backend fix
shape) is in `plan_graphics.md` — this section is intentionally a short index.

| Commit | Task | Summary |
|---|---|---|
| *(pending)* | 130/131 | WebGPU's native 2D baseline **CLOSED**. `WEBGPU-130`: `../mobile-eggbert`'s `CMakeLists.txt` now targets `../cna_graphics` (not the stale `../cna` clone), adds WebGPU as an explicit opt-in desktop backend (default stays `SDL_RENDERER`, matching that repo's own last real committed default), and fixes a previously-disabled/buggy `worlds/` directory copy step (mobile-eggbert commit `dcdb648`, **local — not pushed**, per CLAUDE.md's "do not push unless asked"). On a real desktop session (`DISPLAY=:0`; `wgpu-native` needs a real GPU context, the sandbox's Xvfb `:99` was not used for this), `WindowsPhoneSpeedyBlupi` built clean against WEBGPU, reached its main menu automatically with pixel-correct `SpriteBatch` rendering, and a simulated Play-button click correctly drove its mission-start sequence (animated progress-bar cutscene + cross-fade), all with zero WebGPU validation errors. That sequence's fade back to the menu was confirmed via an identical `EASYGL` comparison build to be pre-existing, backend-independent `Game1` state-machine behavior in `../mobile-eggbert` itself — not a WebGPU regression, and out of scope to fix here. `WEBGPU-131` records this as the closed native 2D baseline in `plan_webgpu.md`/`docs/webgpu-backend.md`, and reconciles several stale 🟨 status notes in Phase 56/59/61/62 that had said "has not run against a device" despite `WEBGPU-125`–`127` already having verified those exact code paths. |
| `de62d528` | 946 | Manual HLSL→GLSL shader-conversion workflow **PROVED end-to-end** on all 3 of BloomSample's custom pixel shaders (`BloomExtract`/`GaussianBlur`/`BloomCombine`, reference source at `/rv/tmp/XNAGameStudio/Samples/BloomSample_4_0/BloomPostprocess/Content/*.fx`) — the data point Task 945's tooling decision (manual port vs. `dxc`+`SPIRV-Cross`) was waiting on. All 3 ported 1:1 to GLSL with no restructuring beyond GLSL's stricter typing (HLSL's implicit scalar↔vector broadcasts and float4→float3 truncation become explicit `vec4(x)`/`.rgb` in GLSL) — confirms manual porting is straightforward for this whole shader shape (full-screen post-process pixel shaders over an existing `RenderTarget2D`, XNA's most common custom-shader use case). Required extending `ShaderEffect` (a `NOXNA` CNA extension) with 2 capabilities every custom shader beyond a trivial one-uniform case needs: `SetUniformFloatArray()`/`SetUniformVec2Array()` (`GaussianBlur`'s `SampleOffsets[15]`/`SampleWeights[15]`, using EasyGL's already-existing `Program::set_uniform_fv()` — no `easy-gl` sibling-repo change needed) and `SetTexture(unit, Texture2D&)` (`BloomCombine`'s 2nd sampler, matching real XNA's `GraphicsDevice.Textures[unit]=tex`). Also proved, for the first time in this repo, the `.shader.json` + GLSL descriptor round-trip via `ContentManager::Load<Effect>()` (`EffectTypeReader`, implemented since some earlier task but never exercised by any test until now). 4 new tests: `EasyGL_Bloom_Extract`/`_GaussianBlur`/`_Combine` (one shader each, exact hand-computed expected pixel values per shader's own formula) and `EasyGL_Bloom_Pipeline` (all 3 chained end-to-end exactly like `BloomComponent.cs`'s real 4-pass Draw(): extract→blurH→blurV→combine over half-res intermediate `RenderTarget2D`s, checking for genuine bloom-halo spillover around a bright square). Writing the pipeline test surfaced and fixed a separate real bug, **Task 1078**: `EasyGLSpriteBatchBackend::FlushBatch()` always sized its viewport/orthographic projection to the *window* (`SDL_GetWindowSize`), never to whatever `RenderTarget2D` is actually bound — invisible in every prior `ShaderEffect`/`SpriteBatch` test because they all coincidentally used a same-size-as-window RT, but a hard blocker for `BloomComponent.cs`'s real half-res-intermediate-RT design. Fixed by adding `EasyGLGraphicsBackend::GetCurrentRenderTarget2DSize()` and preferring it over the window size whenever a 2D render target is bound. Both fixes verified genuinely discriminating via `git stash`. Full suite reconfirmed clean: EasyGL `CnaTests` 4371/4373 (2 hardware skips, unchanged) + `ctest` 201/201 with the 2 known pre-existing failures unchanged, +4 new tests. Vulkan/Bgfx untouched — EasyGL only for this proof, matching `ShaderEffect`'s own existing EasyGL-first precedent (Bgfx's `ShaderEffect` backend is a known no-op stub). **Recommendation for Task 945**: manual porting scales fine for this shader family; no immediate need for a `dxc`+`SPIRV-Cross` pipeline unless/until a much larger or more complex shader turns up among the remaining 13 blocked samples — final call is still the project owner's. |
| `de62d528` | 1078 | See Task 946 above — `EasyGLSpriteBatchBackend::FlushBatch()`'s viewport/projection now sizes to the currently-bound `RenderTarget2D`, not the window. |
| `a0510d07` | 1077 | `ShaderEffect::SetUniformXxx()` **FIXED** — was a total no-op when the effect is used via `SpriteBatch::Begin(sortMode, blend, ..., &fx)` on EasyGL. **Root cause**: `EasyGLSpriteBatchBackend::FlushBatch()` compiled its own **separate** GL program (`customProgram_`) from the `ShaderEffect`'s raw GLSL source text every flush, instead of reusing the `ShaderEffect`'s own already-compiled program (`effectBackend_`). `ShaderEffect::SetUniformFloat/Vec2/Vec3/Vec4/Mat4/Int()` write via `glUniform*`, which only affects whichever GL program is *currently bound* — but the real draw always bound `customProgram_`, a distinct GL program object with its own independently-assigned uniform locations, so any value set through `ShaderEffect`'s API was silently orphaned. Only `projection` (set directly against `customProgram_` by `SpriteBatch` itself, bypassing `ShaderEffect` entirely) and the default texture sampler (unit 0, GLSL's implicit default) ever worked — which is why this went unnoticed: the one existing test (`EasyGL_ShaderEffect_GLSL`, Task 132) never calls `SetUniformXxx()` at all. Found while prototyping Task 946 (BloomSample's HLSL→GLSL shader-conversion proof) — a probe test confirmed a `SetUniformVec4()`-set tint colour never reached the rendered pixel. **Fixed** by adding a new generic `Effect::GetEffectBackendPtr()` virtual (returns `nullptr` by default, overridden by `ShaderEffect` to return its own `effectBackend_.get()` — mirrors the existing `GetVertexSource()`/`GetFragmentSource()` pattern used to avoid a concrete-`ShaderEffect`-type dependency) and a new `EasyGLEffectBackend::GetProgram()` accessor; `FlushBatch()` now binds that same program directly instead of recompiling a redundant copy, removing the `customProgram_`/`compiledFor_` members and their GL-context-loss-recovery plumbing entirely. This also makes EasyGL's custom-effect dispatch consistent with Vulkan's (which already binds the effect's own pre-compiled SPIR-V pipeline directly, no separate per-flush recompilation, and never had this bug). New regression test `EasyGL_ShaderEffect_SpriteBatch_Uniform` (`examples/easygl_shader_effect_spritebatch_uniform_test.cpp`), verified genuinely discriminating via `git stash` (fails with the bug reverted — reads back black instead of the set blue tint — passes with the fix restored). Full suite reconfirmed clean: EasyGL `CnaTests` 4371/4373 (2 hardware skips, unchanged) + `ctest` 197/197 with the 2 known pre-existing failures unchanged (`EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`), +1 new test. Vulkan/Bgfx untouched (not affected by this bug) — not rebuilt for this fix. |
| `a8bc189b` | 895 | `SkinnedEffect.WeightsPerVertex` **FIXED** — a real GPU no-op: the C++-side property validated correctly (throws for anything other than 1/2/4), but every backend's skinning vertex shader always summed all 4 weight/index pairs unconditionally regardless of the value, matching FNA's real `Skin(vin, boneCount)` HLSL only in the trivial case where the caller never actually relies on the 3rd/4th slots being ignored. **Fixed** by threading a new `GpuDrawParams::weightsPerVertex` field (set from `SkinnedEffect::FillGpuDrawParams()`) through to each backend's skinning stage and gating the bone-weight accumulation with `if (weightsPerVertex >= 2)` / `if (weightsPerVertex >= 4)` — simpler than FNA's own compile-time-unrolled 3-shader-variant approach, but observably identical since XNA's own property setter already restricts the value to exactly {1, 2, 4}. EasyGL added a `uWeightsPerVertex` int uniform + `loc_weightsPerVertex` lookup; Vulkan packed the value into the previously-unused `eyePos_pad.w` padding component of the existing `FogParams` UBO (no size change needed, still 240 of 256 bytes) and regenerated `spirv_shaders.hpp`; Bgfx added a new dedicated `u_weightsPerVertex` uniform (no spare padding slot available) and regenerated `bgfx_shaders.hpp`. New `*_SkinnedEffect_WeightsPerVertex` test on all 3 backends: a 3-bone blend where slots 0/1 are the real weights (matching Task 408's own single/2-bone test's net +0.5 shift) and slots 2/3 deliberately carry non-zero "garbage" weights pointing at a 3rd bone with a huge, unmistakable +100 translation — so an unfixed backend pushes the quad entirely off-screen instead of producing a subtly-wrong pixel color. Verified genuinely discriminating via `git stash` on all 3 backends (fails with the bug reverted, passes with the fix restored). Full suite reconfirmed clean on all 3 backends: EasyGL `CnaTests` 4371/4373 + known `ctest` baseline unchanged; Vulkan `CnaTests` 4371/4373 (2 hardware skips) + `ctest` 130/131 (1 known pre-existing: `Vulkan_DepthBias`); Bgfx `CnaTests` 4375/4377 (2 hardware skips) + `ctest` 107/109 (2 known pre-existing: `Bgfx_RenderTarget2D_MsaaResolve`, `Bgfx_RenderTargetCube_DepthFormat`) — all baselines unchanged except +1 new test each. Closes the last of Tasks 890/893/894/895. |
| `a76ba78c` | 894 | `SkinnedEffect` real specular highlights **FIXED** — zero existing infrastructure (no `SpecularColor`/`SpecularPower` forwarding, no per-light `SpecularColor`, no `EyePosition`/world-position plumbing in any of the 3 backends' skinned shaders, unlike `BasicEffect`'s already-fixed Task 886). FNA's real `SkinnedEffect.fx` has genuine per-light specular (unlike `EnvironmentMapEffect`, Task 890, which hardcodes it to zero) — half-vector Blinn-Phong via the shared `Lighting.fxh` `ComputeLights()`: per-light `specular = pow(max(dot(halfVector,N),0)*zeroL, SpecularPower)`, summed and weighted by each light's own `SpecularColor`, then the material's `SpecularColor` applied once to that sum. **Fixed** by forwarding `SpecularColor`/`SpecularPower`/per-light `SpecularColor` in `FillGpuDrawParams()` (using the property getters, not the raw fields, so a directly-set `Parameters["SpecularColor"]` is respected), and adding real World/EyePosition plumbing + the Blinn-Phong formula to each backend's skinned shader: EasyGL added `uWorld`/`vWorldPos`/`uEyePosition` plus `uLight0/1/2Specular`/`uSpecularColor`/`uSpecularPower` uniforms (reusing already-declared but previously-unused generic `Prog3D` fields for the latter); Vulkan's push constant had zero room (already at the 128-byte guaranteed minimum), so `World` (mat4) + `EyePosition` + specular fields went into the same `FogParams` UBO Task 893 already extended (32→96→240 bytes of the 256-byte `kSkinnedFogUBOStride`, still fits) and regenerated `spirv_shaders.hpp`; Bgfx reused the already-existing shared `world3DUnif_`/`eyePos3DUnif_`/`light0-2Spec3DUnif_`/`specularColorPower3DUnif_` uniforms, needed a new `v_worldPos` varying (added to `varying.def.sc` at the free `TEXCOORD3` slot), and regenerated `bgfx_shaders.hpp`. New `*_SkinnedEffect_Specular` test on all 3 backends, reusing the exact same precomputed expected pixel values as `BasicEffect`'s own specular test (identical shared formula, identity World/bones) — **passed on the first attempt on all 3 backends**, a strong correctness signal that the port is faithful. Verified genuinely discriminating via `git stash` (2 of 4 checks fail with the fix reverted — the other 2 trivially "pass" since specular defaults to zero without the fix, expected). Full suite reconfirmed clean on all 3 backends (same baselines as Task 893's commit, +1 new test each). |
| `266ba4b6` | 893 | `SkinnedEffect::FillGpuDrawParams()` **FIXED** — same gap as Task 890 (`EnvironmentMapEffect`), only ever forwarded `DirectionalLight0`. FNA's real `SkinnedEffect.fx` sums all 3 directional lights' diffuse via the shared `Lighting.fxh` `ComputeLights()`, and (unlike `EnvironmentMapEffect`) also has genuine per-light specular support — tracked separately as Task 894, not touched here. **Fixed** by forwarding `DirectionalLight1`/`DirectionalLight2` with the same `Enabled`-gating pattern, and extending each backend's skinned shader to sum all 3 lights' diffuse: EasyGL added `uLight1Dir`/`uLight1Diffuse`/`uLight2Dir`/`uLight2Diffuse` uniforms (previously the skinned program declared none of these at all); Vulkan's skinned pipeline already uses a 128-byte push constant at Vulkan's guaranteed-minimum size for `DirectionalLight0` + other params, so light1/2 went into the existing per-draw `FogParams` UBO instead (expanded from 32 to 96 bytes of its 256-byte `kSkinnedFogUBOStride`, no resize needed) and regenerated `spirv_shaders.hpp`; Bgfx reused the shared `light1Dir3DUnif_`/`light2Dir3DUnif_` uniforms and regenerated `bgfx_shaders.hpp`. New `*_SkinnedEffect_MultiLight` pixel test on all 3 backends (using an identity bone palette to isolate skinning from the lighting formula under test), verified genuinely discriminating via `git stash`. Found while writing the Bgfx test: `GraphicsDevice.SetDepthTestEnabled()`/`SetBlend*()` convenience methods throw `"not yet wired into bgfx state flags"` on Bgfx (a known, deliberate limitation, not this task's to fix) — the test simply doesn't call them, matching every other Bgfx `SkinnedEffect` test's existing practice. Full suite reconfirmed clean on all 3 backends (same baselines as Task 890's commit, +1 new test each). |
| `744f25d6` | 890 | `EnvironmentMapEffect::FillGpuDrawParams()` **FIXED** — only ever forwarded `DirectionalLight0`'s direction/diffuse to the GPU on all 3 backends (confirmed via source read, matching `docs/graphics-backend-feature-matrix.md`'s ❌ across EasyGL/Vulkan/Bgfx, not just the latter two as some other docs had claimed). FNA's real `EnvironmentMapEffect.fx` sums all 3 directional lights' diffuse via the shared `Lighting.fxh` `ComputeLights()`, identical to `BasicEffect`'s formula (Tasks 885/886) minus per-light specular (FNA hardcodes `DirLightNSpecularColor=0` for this effect — its own "specular" is the env-map reflection amount, unrelated). **Fixed** by forwarding `DirectionalLight1`/`DirectionalLight2` with the same `Enabled`-gating `BasicEffect` already uses, and extending each backend's env-map shader to sum all 3 lights: EasyGL reused the generic `Prog3D` `loc_l1dir`/`loc_l2dir` fields already declared for `BasicEffect` (previously unused for the env-map program); Vulkan expanded its dedicated `EnvMapParams` UBO from 128 to 192 bytes (4 new vec4 fields, `kEnvMapUBOStride=256` had headroom, no ring-buffer resize needed) and regenerated `spirv_shaders.hpp` via `compile_shaders.py`; Bgfx reused its already-existing shared `light1Dir3DUnif_`/`light2Dir3DUnif_` uniforms and regenerated `bgfx_shaders.hpp` via its own `compile_shaders.py` (needs the vendored `shaderc` binary + bgfx source dir, both present under `cmake-build-bgfx/_deps/`). New `*_EnvironmentMapEffect_MultiLight` pixel test on all 3 backends, verified genuinely discriminating via `git stash` (fails with the bug reverted). Along the way, found and worked around a **separate, pre-existing Bgfx gap** (not fixed, out of this task's scope): `BgfxGraphicsBackend`'s env-map draw path has no fallback when `EnvironmentMapEffect.EnvironmentMap` is null (unlike EasyGL/Vulkan, which both bind a default white cube) — crashes with `GL_INVALID_OPERATION` deep in bgfx's own renderer; the new test binds a real (irrelevant, `EnvironmentMapAmount=0`) cube to avoid it, matching every other `EnvironmentMapEffect` pixel test's existing practice. Full suite reconfirmed clean on all 3 backends: EasyGL `CnaTests` 4371/4373 + `ctest` 190/192 (2 known pre-existing), Vulkan `CnaTests` 4371/4373 + `ctest` 127/128 (1 known pre-existing), Bgfx `CnaTests` 4375/4377 + `ctest` 104/106 (2 known pre-existing) — all baselines unchanged from before this fix except for the 1 new test each. |
| `481c0293` | 956 | `EasyGLSpriteBatchBackend::Begin()` **FIXED** — previously hardcoded `set_blend_enabled(true)` + `SrcAlpha`/`OneMinusSrcAlpha` unconditionally, clobbering whatever `EasyGLGraphicsBackend::ApplyBlendState` had just set via `GraphicsDevice::setBlendStateProperty(blendState)` (called by `SpriteBatch::Begin()` immediately before `backend_->Begin()` runs). This both silently ignored any non-`AlphaBlend` `BlendState` passed to `SpriteBatch::Begin()` and left the real GL blend state stuck at that hardcoded value after `End()` — any 3D draw issued afterward without the game explicitly reassigning `BlendState` inherited the leftover raw state instead of whatever `GraphicsDevice.BlendState` still claimed was active. Same bug shape SDL_Renderer already fixed (Task 695). **Fixed** by removing the hardcoded call entirely — `SpriteBatch::Begin()` already correctly applies the real requested blend state via `setBlendStateProperty` before the backend's own `Begin()` runs, matching real FNA (`SpriteBatch.cs` genuinely leaves `GraphicsDevice.BlendState` changed after `End()`, it does not restore the prior state). Vulkan/Bgfx checked and confirmed to not have this gap (`VulkanSpriteBatchBackend::Begin()`/`BgfxSpriteBatchBackend::Begin()` never touch blend state at all). New dedicated regression test `examples/easygl_spritebatch_blendstate_leak_test.cpp` (`EasyGL_SpriteBatch_BlendStateLeak`), verified genuinely discriminating (fails with the bug reverted, passes with the fix). Fixing this also uncovered and fixed a masked bug in `examples/easygl_sample_layered_blend_test.cpp` itself: it set `GraphicsDevice.BlendState` explicitly right before calling `SpriteBatch::Begin()` with no arguments, which immediately re-clobbers it back to the default `AlphaBlend` — the test only ever passed because the old hardcoded backend value happened to equal what it intended to test. Fixed by passing the `BlendState` directly to `Begin(sortMode, blendState)` instead. Full suite reconfirmed clean afterward: `CnaTests` 4371/4373 (2 hardware skips, unchanged), `ctest` 190/192 (2 known pre-existing failures unchanged: `EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`). |
| `c8f47b1a` | 955 | XNA depth-occlusion compatibility audit **RESOLVED** (cross-repo follow-up to Task 954, `../cna-samples` SimpleAnimation finding). After Task 954's winding fix, some tank parts still rendered visible when they should've been occluded — a depth problem, not placement. **Root cause: `GraphicsDevice`'s constructor only pushed its `RasterizerState` default to the backend (Task 896) — `BlendState`/`DepthStencilState` were left as C++ fields only, never applied.** On EasyGL this left OpenGL's raw depth-test default (disabled) in effect for any game (like `Tank.hpp`, mirroring real XNA's own `Tank.cs`) that never explicitly sets `DepthStencilState` itself. Confirmed via real FNA source: `GraphicsDevice.cs`'s constructor sets all 3 of `BlendState`/`DepthStencilState`/`RasterizerState` unconditionally; Task 896 ported only the 3rd. **Fixed** by adding the other 2 lines to CNA's `GraphicsDevice` constructor, matching FNA exactly — zero `cna-samples` changes needed (diagnostic isolation proved this: an explicit `DepthStencilState`-only override in the sample reproduced pixel-identical results to the real constructor fix). Bonus: fixed Bgfx's own wrong blend default (`BGFX_STATE_BLEND_ALPHA`) as a side effect of the same root cause. New shared 3-backend regression test `graphicsdevice_default_state_occlusion_test.cpp` — deliberately the one test in this project that never explicitly sets state, to actually exercise `GraphicsDevice`'s own defaults. Found+documented (not fixed) a second, separate `SpriteBatch` blend-state-leak bug — see Task 956. |
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
| **RESOLVED (2026-07-11)** | `EasyGLSpriteBatchBackend::Begin()` used to enable blending unconditionally with a hardcoded `SrcAlpha`/`OneMinusSrcAlpha`, clobbering whatever `GraphicsDevice.BlendState` had just been set to; any 3D draw issued after a `SpriteBatch.Begin()`/`End()` pair without an explicit `BlendState` reassignment inherited that leftover hardcoded state instead. Fixed by removing the hardcoded call — `SpriteBatch::Begin()` already applies the real requested blend state correctly via `setBlendStateProperty` before the backend's own `Begin()` runs. Vulkan/Bgfx confirmed to never have had this gap. New regression test `EasyGL_SpriteBatch_BlendStateLeak`; also fixed a masked bug in `easygl_sample_layered_blend_test.cpp` this exposed. See §3. | 956 |
| **RESOLVED (2026-07-11)** | `../cna-samples` SimpleAnimation's dark turret underside/hollow wheels: all 12 of `tank.fbx`'s converted mesh parts had reversed triangle winding relative to real XNA's content-pipeline convention (not just `turret_geo` — an earlier narrower conclusion was corrected after project-owner pushback). Fixed by reversing winding in all 12 `tank_*_idx.bin` files under `cna-samples/samples/SimpleAnimation/Content/`; no CNA change, no SimpleAnimation-specific CullMode workaround. `CameraShake`/`CustomModelClass`/`ReachGraphicsDemo` have their own independent, still-unfixed copies of the same tank mesh files — likely share the defect, flagged for whoever next touches those samples. See `docs/xna_culling_compatibility_audit.md`. | 954 |
| **RESOLVED (2026-07-11)** | `EnvironmentMapEffect`'s `DirectionalLight1`/`DirectionalLight2` were unforwarded on all 3 backends (`FillGpuDrawParams()` only ever populated `light0Dir`/`light0Diffuse`). Fixed by forwarding both lights the same way `BasicEffect` already does (Tasks 885/886), with `Enabled`-gating; EasyGL/Vulkan/Bgfx shaders extended to sum all 3 lights' diffuse contribution (Vulkan needed its dedicated `EnvMapParams` UBO expanded 128→192 bytes; Bgfx/EasyGL reused already-existing generic light1/2 uniforms). New `*_EnvironmentMapEffect_MultiLight` test on all 3 backends. See §3. | 890 |
| **RESOLVED (2026-07-11)** | `SkinnedEffect`'s `DirectionalLight1`/`DirectionalLight2` were unforwarded on all 3 backends. Fixed the same way as `EnvironmentMapEffect`'s Task 890: `SkinnedEffect::FillGpuDrawParams()` now forwards both lights with `Enabled`-gating; EasyGL/Vulkan/Bgfx skinned shaders extended to sum all 3 lights. New `*_SkinnedEffect_MultiLight` test on all 3 backends. See §3. | 893 |
| **RESOLVED (2026-07-11)** | `SkinnedEffect` had zero specular infrastructure (no `SpecularColor`/`SpecularPower` forwarding, no per-light specular, no `EyePosition`/world-position plumbing in any backend's skinned shader). Fixed with real half-vector Blinn-Phong specular matching `BasicEffect`'s Task 886 formula exactly — new `World`/`EyePosition` plumbing added to all 3 backends' skinned shaders (previously absent). New `*_SkinnedEffect_Specular` test on all 3 backends, using the exact same precomputed expected pixel values as `BasicEffect`'s own specular test (same shared formula) — passed on the first attempt on all 3 backends, a strong correctness signal. See §3. | 894 |
| **RESOLVED (2026-07-11)** | `SkinnedEffect`'s `WeightsPerVertex` was a GPU no-op — every backend's skinning shader always summed all 4 weight/index pairs regardless of the property value. Fixed by threading a new `GpuDrawParams::weightsPerVertex` field through to each backend's skinning vertex stage, gated with `>=2`/`>=4` conditional accumulation. New `*_SkinnedEffect_WeightsPerVertex` test on all 3 backends (a "garbage 3rd bone with huge translation" technique makes an unfixed backend's quad go entirely off-screen instead of subtly wrong). See §3. | 895 |
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

1. **Decide Task 945** (manual HLSL→GLSL port vs. `dxc`+`SPIRV-Cross` tooling) — ~~Task 946~~'s
   data point is now in (see §3): manual porting scaled fine for BloomSample's 3 shaders, no
   restructuring needed beyond GLSL's stricter typing. Still requires project-owner input to make
   the final call; do not pick an approach unilaterally.
2. ~~Fix Task 890~~ / ~~Task 893~~ / ~~Task 894~~ / ~~Task 895~~ — **all four done**, see §3.
3. Task 952 (`RenderTargetCube` depth-gating bug on Bgfx) is **DEFERRED**, not a next task — see §9.
4. ~~Fix Task 956~~ — **done**, see §3.
5. **New standing work queue: Phase 79** (Tasks 957–1076) — a full re-audit of all 153 `../cna-samples`
   catalogued samples (including the 67 previously-ignored ones), one task per sample, prompted
   directly by Tasks 954/955 showing a "Done" sample can still hide a real CNA bug. Most rows need
   only a light re-verification pass; the real, already-known open CNA gaps are DEFERRED.md items
   #10/#18/#22/#27/#28/#29 and the Phase 78 shader-conversion umbrella (#11). Start with Task 1006
   (`SimpleAnimation`'s own flagged future re-review list) or any `⬜` row — do not touch the `⛔`
   rows (structural/permanent, no CNA action possible).

---

## 9. Do not do yet

- **Do not overstate WebGPU parity**: its initial native 2D baseline is active, but the open tasks
  in `plan_webgpu.md` still cover 3D, effects, render targets, readback, conformance and WASM.
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
