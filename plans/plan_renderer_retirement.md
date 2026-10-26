# Plan: retiring VULKAN, DIRECTX11, METAL and SDL_RENDERER (RRC-030..RRC-039; close-out RRC-040 onward)

2026-10-13/14. An owner decision, final: CNA stops maintaining its own native Vulkan, Direct3D 11
and Metal renderers and SDL's 2D renderer. `SDL_GPU` is CNA's modern native cross-platform path --
CNA maps XNA 4.0 onto SDL_GPU, and SDL owns its Vulkan, Direct3D 12 and Metal backends. Nothing is
deprecated, kept behind an option or aliased: each retired name is refused at configure time (by
name, with its reserved value) and at run time (as retired, not as unknown). Git history is the
archive; `docs/removed-renderers.md` is the short record. Qualifying SDL_GPU on native Windows and
Apple hardware is a separate, later campaign (`handoff.md` §5.2).

| Task | Scope | Commit | Status |
|---|---|---|---|
| RRC-030 | Classify every test of the four suites; carry the XNA behaviour into renderer-neutral conformance programs, shared examples, CnaTests cases and compiled-effect helpers; record what survivors get wrong | `357642c7d` | done |
| RRC-031 | Retire `VULKAN` | `fe5e25994` | done |
| RRC-032 | Retire `DIRECTX11` with `D3DCommon` | `def6f7747` | done |
| RRC-033 | Run the two BasicEffect programs only the Vulkan suite registered | `75f24bfd9` | done |
| RRC-034 | Retire `METAL` | `22842c107` | done |
| RRC-035 | Retire `SDL_RENDERER`; new per-host defaults | `86b8152f2` | done |
| RRC-036 | Comments, messages and tool data that described the retired renderers as present | `387b5c20c` | done |
| RRC-037 | Documentation, plans, handoff; validation; this report | `27f8d2ecc` | done |
| RRC-038 | Remaining references, exact counts; Release, FNA3D, Android-emulator and WebGL2 validation | `cc547f522` | done |
| RRC-039 | Rendering on Android (`OPENGLES3` on the emulator); the report finalised | `3df66da5c`, `af798b73a`, `f698d0f54` | done |

### Close-out remediation (RRC-040 onward)

A 2026-10-14 audit found the retirement structurally complete but not closed out across
diagnostics, common renderer contracts, shared test coverage, CI and current documents. These tasks
finish it; none restores a retired renderer, option or constant.

| Task | Scope | Status |
|---|---|---|
| RRC-040 | One run-time retired-renderer table (`CNA/Internal/RetiredGraphicsRenderers.hpp`); name refusals carry the reserved value, the C API's numeric routes refuse 1/8/14/42 as retired and an unknown value as unknown; strict-C, C++ and environment-variable regression tests | done |
| RRC-041 | Remove `ISpriteBatchRenderer::SetSamplerAddressModeWEXT` and its fan-out call (its only implementor was VULKAN); AddressW keeps reaching DIRECTX9 through `SetSamplerAddressW` and the complete-hook renderers through `SetSamplerState`, pinned by `SpriteBatchTests` | done |
| RRC-042 | Remove `GraphicsRendererDescriptor::wantsHighDpi`: only the retired METAL descriptor set it, so it was permanently false; pixel density stays a platform request (`WindowDescription::highDpi`), documented as an Apple known gap | done |
| RRC-043 | `SDL_GPU` implements `RendererFeature::BaseInstanceDrawing` (its three implementors were retired, so both `InstancedDrawRangeTest` base-instance cases skipped everywhere): every host-expanded per-instance stream starts at `firstInstance`. An instanced draw with no per-instance stream now draws `instanceCount` copies on `SDL_GPU` and `WEBGPU`, as XNA does (both drew one), pinned by a new shared case | done |
| RRC-044 | Remove the two empty `CheckValidationClean()` stubs left by the Vulkan validation gate; the shared stock-effect sampler contract tags failures by effect family, not by the retired renderer's descriptor-set names | done |
| RRC-045 | The 30 conformance programs ran only on EasyGL, SDL_GPU and WebGPU because nobody had registered them elsewhere: `cna_register_conformance_programs()` now registers them on SOFTWARE, FNA3D and HEADLESS too. A program skips (exit 77) only for a stated capability reason -- a Diagnostic-category renderer has no pixel oracle, SOFTWARE's documented reduced PBR model, and (at this historical checkpoint) FNA3D's named PBR and stock-instancing refusals -- and the deviations the new families show are recorded in `handoff.md` §5.1 item 7. FNA3D-58 later replaced the PBR refusal with native rigid/skinned shading. | done |
| RRC-046 | Behaviour held by exactly one surviving family gets an independent second one (`modules/graphics/examples/parity/CrossFamilyPrograms.cmake`, `<Family>_CrossFamily_*`): 141 programs EasyGL alone ran after the retirement -- 129 of them from `DIRECTX11`'s 264-fixture shared corpus, 12 the `VULKAN` suite shared -- now also run on SDL_GPU (145 of 148 SDL_GPU registrations pass; two new deviations recorded in `handoff.md` §5.1 item 7), and the fog, presentation, deferred-blend-factor and clamp programs one other family held gain EasyGL and/or SDL_GPU. The surface-format contract gains SDL_GPU sources in place of the retired DIRECTX11 HLSL; five programs stay single-family for a reason recorded in that file | done |
| RRC-047 | The three XNA behaviours the retirement left weakly tested, restored to strong shared coverage: every `DrawUserPrimitives`/`DrawUserIndexedPrimitives` shape without an applied effect (exact type and message, `DrawUserPrimitivesNoEffectTests`); clamped multisample counts for requests 0-16 through `Reset` and `RenderTarget2D` (`*_Conformance_multisample_count_clamp`); `GetData` on an active render target refused with the destination untouched, in every overload and as the second MRT slot (`TextureDataBindingContractTest`, and the shared pixel program's leg B9 made strict). Also `Model.Draw` over a part without an index buffer and the Texture2D-copy-survives-Dispose rule (GoogleTest, every family). The orphaned `graphics_adapter_query_contract_test.cpp` is removed after its two renderer-neutral checks moved into the conformance port | done |
| RRC-048 | Active plans and current documents reconciled: `plan_fx` (six compiled-effect backends; Metal's FX-066 withdrawn), `plan_platform` (SDL3 renderers are `SDL_GPU` and `FNA3D`), `plan_graphics`, `plan_csl` (its renderer matrix marked historical planning input, 25 more retired-identity rows withdrawn), `plan_postaudit`, `plan_fna3d` and the other plans given banners or corrected counts; `renderer-registry` states the exact per-host default; `removed-renderers` no longer claims every retirement plan was deleted (`plan_vulkan_parity.md` stayed under a History banner until it was deleted on 2026-10-26) and gives the full RRC range; dead links to deleted plans and documents repaired | done |
| RRC-049 | Source comments, scripts and tool data that used a retired renderer as present-day justification or pointed at a deleted plan, classified hit by hit and corrected where stale (SDL_GPU shader comments, `ModuleProbes` counts, `ApplePlatform`'s MoltenVK rationale, the vkd3d wrapper, the MojoShader build, "cross-renderer" titles of EasyGL-only tests) | done |
| RRC-050 | The C API's generator inputs describe the 10 identities: `limitations.json` (three verification identities, not "four of the 14"), `coverage_mappings.json` (category/maturity accept any of the 10; the extension-layer cases run on `SDL_GPU` with `CNA_CNAEXT=ON`, not on `SDL_RENDERER`); `LIMITATIONS.md` and `COVERAGE.md` regenerated from them, every C API check passing | done |
| RRC-051 | CI builds the new default renderer where the retired native ones used to be: a gating `macos-sdlgpu` job (SDL's Metal backend) and an `SDL_GPU` leg in the manual native-MSVC Windows workflow (Direct3D 12), both running `scripts/ci/run-sdlgpu-smoke.sh`, which requires every program to exit 0. The iOS `SDL_GPU` legs stay non-gating (no recorded iOS build, determinism unmeasured). Neither new job has run: CI runs on push, and nothing was pushed | done (unrun in CI) |
| RRC-052 | The live pointers left in the files RRC-049's sweep excluded: `plans/plan_street_sdlgpu.md` (deleted with the retirement) prefixes in SDL_GPU sources and tests, and two "cross-renderer control for Vulkan's correction" registrations | done |
| RRC-053 | The final report re-measured from Git history (`b1ec33424`..`f698d0f54`): its "after" sizes are those of `3df66da5c`; three other sessions' commits, not two; removal-commit sizes beside the at-base ones; test registrations split into literal calls and expanded CTest cases (DIRECTX11 329, METAL 258), GoogleTest cases and test identities; the migrated-versus-deleted table rebuilt in units Git can show; handoff references corrected to items 7 and 13 | done |
| RRC-054 | Validation of the close-out: renderer, platform and C API gates; full Debug suites of six trees (below) | done |

**Close-out validation (RRC-054, 2026-10-14).** Linux x86-64, RADV, Debug, every GPU/window test
through `tools/platform/run_gpu_tests_private.sh`. Gates passing: `check_renderer_identities`,
`check_runtime_renderer_discipline`, `check_removed_renderer_api`, `check_renderer_combinations`,
`check_renderer_target_discipline`, `check_renderer_descriptors`,
`check_build_performance_policy`, `check-apple-platform-cmake.sh`, the six platform gates
(`sdl_ratchet` with `--strict`), every C API generator check (routes and their tests, ABI baseline
with the built library, declared exports, coverage, limitations, compatibility, release gate, route
coverage, documented export counts), and `check_clean_checkout` after the runs.
`check_no_posix_setenv.py` fails on 42 pre-existing `setenv`/`unsetenv` uses in files this series
did not touch (it adds none).

| Tree | Tests | Failed | Skipped |
|---|---:|---:|---:|
| `cmake-build-multi` (`OPENGLES3` default + 7; C API) | 11,340 | 6 | 210 |
| `cmake-build-sdlgpu` (`SDL_GPU`, CNAEXT, C API) | 11,189 | 27 | 268 |
| `cmake-build-software` | 10,888 | 8 | 120 |
| `cmake-build-headless` | 8,680 | 6 | 567 |
| `cmake-build-fna3d` (OpenGL driver) | 10,707 | 5 | 419 |
| `cmake-build-webgpu` | 10,872 | 8 | 273 |

Every failure is a `handoff.md` §5.1 entry (items 5, 6, 7 and 16, including the deviations the new
SOFTWARE/FNA3D/HEADLESS conformance and SDL_GPU cross-family registrations exposed), except cases
that pass alone: a GamerServices keyring case (multi), `XnaDifferentialBuildTest` (a Wine-hosted
XNA build that runs outside the private runtime; SDL_GPU) and five network/input cases that ran
beside a concurrent ENet test (SOFTWARE). Not run: native Windows, macOS, iOS, Android, the browser,
DIRECTX9 under Wine, Release builds; the new macOS and Windows `SDL_GPU` CI jobs have not run.

## Final report

The report describes the campaign as it stood at RRC-039; its counts were re-measured from Git
history in RRC-053, and the close-out tasks above changed some of what it lists as open (marked
where they did).

### What was removed

- **Renderer families:** `modules/renderers/vulkan` (200 files, 86,695 lines), `directx11` (30 /
  11,433), `common/d3d` (`D3DCommon`, 64 / 112,090, 105,453 of them in two generated DXBC headers),
  `metal` (68 / 20,578, Objective-C++) and `sdl-renderer` (72 / 12,164): descriptors, factories,
  shaders, examples and suites.
- **Identity and registration:** the four enumerators of `GraphicsRendererType`, their registry,
  selection, category and maturity arms, `CNA_RENDERER_<X>` options and compile definitions, the
  `CNA_VULKAN/DIRECTX11/METAL_COMPILED_EFFECTS` options, the Windows-only and macOS-only
  combination partitions for them, the descriptor gate's SDK-bound `directx11` entry, the C
  constants `CNA_GRAPHICS_RENDERER_{SDL_RENDERER,VULKAN,DIRECTX11,METAL}`.
- **Platform:** the Vulkan-surface service (`IPlatformVulkanSurface`, `PlatformCapability::VulkanSurface`,
  the SDL3 implementation and loader probe), the Vulkan and Metal window intents and window kinds,
  `SDL_WINDOW_VULKAN`/`SDL_WINDOW_METAL` in the SDL3 platform.
- **Build and test infrastructure:** the Vulkan validation-output gate on every CTest registration,
  the DirectX parity inventory (264 fixtures), the D3D11 Wine/DXVK wrappers and gate test, the
  native-MSVC HLSL verifier and D3DCommon's HLSL toolchain, the Metal header-interface target that put 29 Metal test files (260 cases) into every CnaTests corpus, the CnaTests/glTF-ladder 2D-only skip
  pattern, the C API's 44 SDL_RENDERER-only test-environment arms (no C API registration changed), the Vulkan-only link-closure probe and
  MojoShader-Vulkan existence probe, `tools/vulkan`.
- **Content and data:** the Vulkan and DirectX 11 glTF L7 golden corpora with their policies and
  reports, the Vulkan materials goldens, graphics-ext's HLSL post-process payloads and generator.
- **CI:** `metal-macos-ci.yml`; the Vulkan legs of the glTF, input and platform workflows (now
  SDL_GPU on lavapipe); input-ci's SDL_RENDERER leg; `d3d-windows-ci.yml` builds `DIRECTX9`
  instead of `DIRECTX11`; Apple CI builds `SOFTWARE` (gating) and `SDL_GPU` (non-gating iOS legs; a gating macOS `SDL_GPU`
  smoke since RRC-051).
- **Spikes, docs, plans:** `spikes/vulkan-{depth-bias,vertex-format}-spike`,
  `spikes/d3d11-vsync-spike`; `docs/{vulkan,directx11,metal}-renderer.md`,
  `metal-shader-effect-contract.md`, `metal-history-map.tsv`, `sdl-renderer-2d-completeness.md`;
  `plans/plan_{vulkan,metal,dx,windows11_directx}.md`.
- **Build trees:** `cmake-build-vulkan` (11 GB) and `cmake-build-d3d11` (4.2 GB) deleted;
  `cmake-build-mingw` (`DIRECTX9` default with `SDL_GPU`, `SOFTWARE`, `HEADLESS`) replaces the latter.

**Size** (every tracked blob read with `git cat-file`. A blob is text unless its first 8,000 bytes
contain a NUL, which is git's binary heuristic, and its lines are its newline bytes, as `wc -l`
counts them. `third_party/` and `vendor/` are not counted. Measured from the campaign base
`b1ec33424` to `3df66da5c`, the RRC-039 commit that finalised this report):

- tracked files 7,892 -> 7,160 (-732);
- text lines 2,512,549 -> 2,246,828 (-265,721);
- `git diff --shortstat` with git's default rename detection: 1,419 files changed, 13,204
  insertions, 278,925 deletions. Without rename detection: 1,438 files, +17,638/-283,359.
- The 294 binary files in the diff are all deletions: the Vulkan and DirectX 11 glTF L7 corpora
  (140 each) and 14 Vulkan material goldens.

Three commits from other sessions landed in that range: `c1006b03e`, `33ccbda36` and `49b1d3a99`
(12 files, +103/-8 lines, net +95). The campaign's own reduction is therefore 265,816 lines and 732
files.

The campaign's last commit is `f698d0f54`. Between `3df66da5c` and `f698d0f54` there are two more
documentation-only RRC-039 commits and eleven unrelated ones (the FNA3D-51 and -54 to -57 fixes
among them). At `f698d0f54` the tree has 7,168 files and 2,248,343 text lines.

The five implementation directories held 434 files and 242,960 lines at the base. RRC-030 first
moved 10 of those files (2,197 lines) into shared locations, so the five removal commits deleted
424 files and 240,760 lines:

| Directory | At base | Deleted by its removal commit |
|---|---|---|
| `vulkan` | 200 / 86,695 | 193 / 85,035 |
| `directx11` | 30 / 11,433 | 30 / 11,433 |
| `common/d3d` | 64 / 112,090 | 64 / 112,090 |
| `metal` | 68 / 20,578 | 68 / 20,575 |
| `sdl-renderer` | 72 / 12,164 | 69 / 11,627 |

The plans and documents removed 12,913 and 10,687 more lines. What grew:

- `modules/graphics`: +7,662 (the migrated conformance programs and shared examples);
- `tests`: +381 (the compiled-effect helpers);
- `sdl-gpu`: +241 (`shadow_sampling.glsl`, moved from the Vulkan shaders).

**Test registrations removed**. Each suite's registrations are counted in its own
`examples/CMakeLists.txt` at the parent of its removal commit. "Calls" are literal
`cna_register_renderer_test` calls. "CTest cases" expands every `foreach` and registration
function.

| Suite | Calls | CTest cases | Notes |
|---|---:|---:|---|
| `VULKAN` | 374 | 374 | no loops |
| `DIRECTX11` | 6 + 2 function calls | 329 | 6 own + the 264 fixtures of `cmake/DirectXParityTests.cmake` (2 of them SDL-only) + the 32 shared `Parity` and 27 `Conformance` programs it also registered |
| `METAL` | 18 | 258 | 99 shared programs, 131 EasyGL oracles, 12 reused fixtures and 2 decoded-fallback runs among them. `Metal_PortableHelpers` was registered in every tree. |
| `SDL_RENDERER` | 80 | 80 | no loops |

All but `Metal_PortableHelpers` existed only in a tree whose default was that renderer.

Of the 264 DirectX parity fixtures:

- 256 sources are still named in a surviving family's CMake;
- 2 were the DX11 suite's own and were deleted;
- 4 Direct3D-only contracts were deleted as already covered by CnaTests;
- 2 became the conformance programs `capability_truth` and `graphics_adapter_query`.

GoogleTest cases (`TEST`) in the deleted directories:

| Directory | Cases | Where they ran |
|---|---:|---|
| `VULKAN` | 31 | compiled-effect builds of the Vulkan tree |
| `DIRECTX11` | 22 | compiled-effect builds of the DX11 tree |
| `D3DCommon` | 9 | 8 in every tree |
| `METAL` | 304, in 29 files | 260 in 28 host-portable policy suites that ran in every tree's CnaTests; 44 in the Apple-only compiled-effect suite |

In GoogleTest files outside those directories, measured by test identity:

- 29 renderer-specific cases went. 23 were deleted outright and 6 were renamed renderer-neutral:
  the 3 `RenderTargetSemantics` plural cube-face cases, 2 base-instance cases and 1 SDL3
  graphics-service case.
- 41 new cases were added: 31 calls of the 7 compiled-effect helpers (7 each for DIRECTX9, EasyGL,
  FNA3D and SDL_GPU; 3 for WebGPU), the truncation sweep, 4 `Viewport`, 3 `PresentationLifecycle`,
  1 `TextureCube` and 1 retired-name selection case.
- SOFTWARE calls the 7 helpers inside two existing cases.
- Counted as `TEST` lines in the diff, that is 30 removed and 48 added.

**Identities:** 14 public identities over 12 families before, 10 over 8 after (`OPENGLES3`,
`OPENGL33`, `WEBGL2` = EasyGL; `WEBGPU`, `HEADLESS`, `SOFTWARE`, `STUB`, `DIRECTX9`, `SDL_GPU`,
`FNA3D`). 41 identities are retired.

### Tests: migrated versus deleted

Every test of the four suites was classified first (RRC-030). Type A tests an implementation
detail and went with the renderer. Type B tests XNA-visible behaviour and was deleted only where
equivalent coverage already ran on a survivor. The per-file verdicts were not committed. The
table counts what Git shows.

| Suite | Test sources | GoogleTest cases | Moved to a shared location | Conformance programs derived |
|---|---|---:|---:|---:|
| VULKAN | 147 (145 programs, 2 GoogleTest files) | 31 | 7 | 18 (from 20 sources) |
| DIRECTX11 | 5 (4 programs, 1 GoogleTest file) + 6 shared programs only it registered | 22 | 1 (`renderer_capability_truth` -> `conformance/capability_truth`) | 2 |
| D3DCommon | 2 GoogleTest files | 9 | 0 | 0 |
| METAL | 35 (6 programs, 29 GoogleTest files) | 304 | 0 | 2 |
| SDL_RENDERER | 67 programs | 0 | 3 (into `conformance/`) | 6 |

Where the XNA behaviour went:

- **30 conformance programs** in `modules/graphics/examples/conformance/`. 28 were derived from the
  four suites. The other 2 are the 36-byte and per-pixel BasicEffect vertex-colour programs
  (RRC-033): shared sources that only `VULKAN` registered, moved in beside the others. They are run
  by every suite that registers conformance programs as `<Renderer>_Conformance_*` (EasyGL,
  SDL_GPU, WebGPU) and cover: disposed guards, Texture2D FromStream / SaveAs / partial SetData,
  PBR hand-derived BRDF and vertex colour, BasicEffect textured MSAA and declaration inputs,
  declared/padded/unsupplied stock-effect layouts, render-target MSAA, ShaderEffect refusals and 3D
  layouts, deferred effect/query lifetime, clip depth range, the ClearOptions matrix, runtime MSAA
  changes, occlusion-query precision and reuse, effect orientation, cube-face MRT binding,
  back-buffer reset/resize, the seven SpriteBatch.Draw overloads, GraphicsAdapter queries,
  SkinnedEffect vertex colour, the stock-effect uniform ring, capability truth, and the two
  BasicEffect vertex-colour programs.
- **Seven shared examples** moved out of the Vulkan tree into `modules/graphics/examples/`: fog for
  every stock effect, fill mode and per-unit samplers. Each was already compiled by one survivor
  through its old path and still is through the new one: DIRECTX9 runs the five fog programs,
  EasyGL fill mode, SDL_GPU the per-unit samplers. The move dropped only the Vulkan and Metal
  registrations.
- **Compiled effects:** seven helpers in `tests/support/CNA/TestSupport/CompiledEffectConformance.hpp`,
  called by the EasyGL, SDL_GPU, FNA3D, DIRECTX9, Software and WebGPU compiled-effect tests, and a
  truncation sweep through the renderer seam.
- **CnaTests:** viewport, presentation-lifecycle and TextureCube cases; three cube-face
  plural-binding cases (`RenderTargetSemantics`) made renderer-neutral; the base-instance cases gated
  on `RendererFeature::BaseInstanceDrawing` rather than a renderer name; three cases that pinned XNA
  behaviour on METAL alone (float cube typed transfer, Short2 positions, Short4 blend indices) gated
  to the survivors measured to pass them.

What the migrated tests found the survivors get wrong is in `handoff.md` §5.1 item 7, with the
SPIR-V `vPos`/`TEXBEM` gaps in item 13. Among them: SkinnedEffect's vertex-colour fold,
BasicEffect without TEXCOORD0 accepted, the adapter multisample answer, cube faces in MRT sets
refused by SDL_GPU and DIRECTX9, SDL_GPU/WebGPU ShaderEffect gaps, the unlit 36-byte vertex-colour
saturate on SDL_GPU and WebGPU, and the SPIR-V compiled-effect gaps that only the Metal route had
fixed.

### ABI

- C ABI **0.48.0** (from 0.47.0): the four `CNA_GRAPHICS_RENDERER_*` constants are removed; their
  values 1, 8, 14 and 42 are permanently reserved and refused by every route that takes a renderer
  identity; `CNA_GRAPHICS_RENDERER_MAXIMUM` stays 43 (`FNA3D`) and the next new identity takes 52.
  No surviving value is renumbered; no route, structure, field, capability bit or exported symbol
  changes (3,205 exports). `AbiHeaderC.c` fails to compile if a removed constant reappears;
  `CoreExtSmoke.c` sweeps the reserved values. The baseline (`tools/c-api/abi_baseline.json`),
  coverage pins, `COVERAGE.md`, `LIMITATIONS.md`, `COMPATIBILITY.md` and `RELEASE_GATE.md` were
  regenerated against the rebuilt library.
- The C++ `GraphicsRendererType` enum is dense and loses the four enumerators (its ordinals were
  never a stable contract; the C values are).
- C#: CNA.NET (`libcna/cna-dotnet`) names no renderer constant; it admits exactly 0.47.0 and must
  admit 0.48.0 before it moves its pin.

### Platform impact

| Target | Before | After |
|---|---|---|
| Windows | Default `SDL_RENDERER`; native `DIRECTX11` and `DIRECTX9` | Default `SDL_GPU` (Direct3D 12 underneath); `DIRECTX9`, `SOFTWARE`, `FNA3D` |
| Linux | Default `OPENGLES3`; native `VULKAN` | Default `OPENGLES3`; `SDL_GPU` (Vulkan underneath) |
| macOS | Default `SDL_RENDERER`; native `METAL` | Default `SDL_GPU` (Metal underneath); `SOFTWARE`, EasyGL |
| iOS | Allow-list `SDL_RENDERER`, `METAL` | Allow-list `SDL_GPU` (never yet built for iOS), `SOFTWARE`; default `SDL_GPU` |
| Android | Default `SDL_RENDERER` (fell through) | Default `OPENGLES3`, the validated path; `SDL_GPU` unqualified |
| Web | `WEBGL2` default, `WEBGPU` | unchanged |

### SDL_GPU follow-up (the next campaign)

1. Native Windows (Direct3D 12) qualification: only Wine + vkd3d-proton exists today.
2. Apple qualification: the full tree passed on a Mac mini M4 before the retirement (AM4); re-run it,
   and build SDL_GPU for iOS for the first time (CI legs exist, non-gating).
3. Android: SDL_GPU on the emulator and a device.
4. Capabilities the retired native backends had and SDL_GPU lacks or gates: `ShaderEffect`
   (`CustomEffects`) compiled out on Windows, Apple and Emscripten (no runtime source-to-DXIL/MSL
   path); occlusion queries (none in SDL_gpu 3.5); `RendererFeature::BaseInstanceDrawing` (no
   survivor implemented it; SDL_GPU does since RRC-043).
5. The SDL_GPU defects in `handoff.md` §5.1 items 6, 7, 9, 10, 13 and 16 -- among them the
   compiled-effect half-pixel coverage and device-slot contracts and the C API's exit-time teardown
   crash in Debug builds (the validation layer is unloaded first; Release passes), both found by
   this campaign's runs.

### Validation

Linux x86-64 host (AMD Radeon 780M, RADV), Debug unless stated; GPU and window tests through
`tools/platform/run_gpu_tests_private.sh` (headless Weston + private Xwayland), never the desktop.
Every Linux, Emscripten and Android tree below was reconfigured from an empty cache (`cmake --fresh`)
on the final code, so no stale retired option survived in a cache; the MinGW tree was configured
fresh on 2026-10-13, after `DIRECTX11` was retired, and reconfigured incrementally since. Another
session's uncommitted, unrelated `ResourceContentManager` change (three files in `modules/content`)
was in the working tree during the Emscripten, Android, MinGW and `SOFTWARE` builds; it is not part
of these commits.

**Gates.** `check_renderer_identities.py` (10 identities, 8 families, 41 retired),
`check_renderer_descriptors.py`, `check_runtime_renderer_discipline.py`,
`check_removed_renderer_api.py`, `check_renderer_combinations.py`,
`check_build_performance_policy.py`, `check-apple-platform-cmake.sh`; the six platform gates
(`sdl_inventory`, `sdl_classify`, `renderer_sdl_audit` -- allowlist exactly `sdl-gpu` and `fna3d` --,
`sdl_ratchet`, `hot_path_lint`, `nonproduction_sdl_audit`); the C API coverage, limitations and
release-gate checks and the ABI baseline; the per-host default in CMake script mode (Linux, Android,
FreeBSD → `OPENGLES3`; Windows, Darwin, iOS → `SDL_GPU`; Emscripten → `WEBGL2`); SDL_GPU's shader
regeneration byte-identical to the committed `spirv_shaders.hpp`.

**Built and run (full CTest corpus of each tree):**

| Tree | Configuration | Tests | Failed | Skipped |
|---|---|---:|---:|---:|
| `cmake-build-multi` (before the FNA3D fixes) | `OPENGLES3` default + `OPENGL33`, `WEBGPU`, `SDL_GPU`, `FNA3D`, `SOFTWARE`, `HEADLESS`, `STUB`; C API | 11,313 | 5 | 197 |
| `cmake-build-sdlgpu` (before the FNA3D fixes) | `SDL_GPU` (SDL's Vulkan backend), CNAEXT, C API | 11,024 | 21 | 257 |
| `cmake-build-webgpu` | `WEBGPU` (wgpu-native on Vulkan), CNAEXT | 10,854 | 7 | 260 |
| `cmake-build-headless` | `HEADLESS` platform and renderer | 8,633 | 3 | 528 |
| `cmake-build-software` | `SOFTWARE`, its compiled effects, CNAEXT | 10,843 | 4 | 103 |
| `cmake-build-fna3d` (before the FNA3D fixes) | `FNA3D` (its OpenGL driver, as its CTest environment forces) | 10,664 | 23 | 399 |
| `build-probe` (Release) | the multi-renderer set above, Release, C API | 11,316 | 5 | 197 |
| reruns after the FNA3D fixes (`handoff.md` §2) | multi / sdlgpu / fna3d | 11,319 / 11,028 / 10,665 | 5 / 22 / 0 on FNA3D's SDL_GPU driver, 1 on its OpenGL driver | 197 / -- / -- |

Every failure is a `handoff.md` §5.1 entry: the multi tree's 5 are EasyGL conformance programs
(item 7); SDL_GPU's 21 are the non-video-platform device cases (item 6), the cube-face MRT refusal,
conformance programs and compiled-effect contracts (item 7) and five C API exit-time teardown
crashes (item 16, the first C API run on `SDL_GPU`); WebGPU's 7 are the occlusion count across a
`Clear` (item 5) and six conformance programs (item 7); HEADLESS's 3 are the cube-face MRT refusal
(item 7). SOFTWARE's 4 are the load-dependent `DynamicSoundEffectInstanceTest` case (item 6) and the
three C API inventory gates, which measured the other session's uncommitted `ContentManager` member
(one new protected symbol); on the committed tree those gates pass (the multi and SDL_GPU runs).
FNA3D's 23 were two unsaturated `COLOR0` cases, 17 cases that keep two devices or games alive, two
multisampled read-backs that returned 0 and two content-pipeline cases that failed only in parallel;
all were fixed after this report was finalised (`3df66da5c`), before the last RRC-039 commit (`plans/plan_fna3d.md` FNA3D-51 and -54 to -57, and `XNAPP-021`'s
shared temporary directory); the FNA3D-default tree's corpus then passed on FNA3D's SDL_GPU driver
and, but for one SystemLink discovery case that passes alone, on its OpenGL driver. The Release
tree fails exactly the Debug multi tree's 5. Item 16 is Debug-only: with `SDL_GPU`
selected at run time the seven `CApi_TeardownLifetime_*` cases pass in the Release tree (no
validation layer) and `game-alive` segfaults in the Debug one. The retired-name refusals (`CnaRendererRetired_*` configure probes,
`GraphicsRendererSelectionTests`, `CoreExtSmoke.c`) pass in every tree that registers them.

**Web.** `cmake-build-wasm-multi` (`WEBGL2` default + `WEBGPU`, Emscripten): `cna_c_api_wasm`,
`cna_demo_2d`, `cna_house3d_demo`, `cna_demo_renderer_selection` and `cna_bench_graphics_renderer`
build. Under node the C API module reports ABI 0.48.0 with 3,207 of 3,207 exports present (the 3,205 C routes plus `_malloc`/`_free`). In
headless Chrome (inside the private runner): with `WEBGPU` selected at run time `cna_demo_2d` and
`cna_house3d_demo` pass the 120-frame smoke (clean teardown, no WebGPU error); on the `WEBGL2`
default `cna_bench_graphics_renderer` draws its 500-sprite scene (47-57 fps) and exits with status
0, and a page-side frame counter shows both demos' smoke loops end on their own (`cna_demo_2d`
stops at 117 browser frames, `cna_house3d_demo` at 106 for its 120 fixed-step updates) with no
abort or error. `scripts/run-webgpu-browser-test.sh` has no WebGL completion marker of its own,
which is why the counter was added to a scratch copy of its page.

**Android.** `cmake-build-android-x86_64` (NDK 29, Release): a configure that names no renderer
resolves to `OPENGLES3` and no longer warns about it; `cna_c_api` builds. On the x86_64 Android 35
emulator a throwaway NDK program linked against that `libcna_c_api.so` (with the tree's SDL3 and
SDL3_mixer) passed 15 of 15 checks: ABI 0.48.0, `OPENGLES3` the default and the only renderer
compiled in, the four retired names refused in either spelling, the reserved values 1, 8, 14 and
42 refused, `OPENGLES3` accepted. Rendering (RRC-039): an APK built from SDL's own
`android-project` template around those libraries ran the C API's `LifecycleSmoke.c` -- a game
whose load, update and draw callbacks issue both draw-command families and check the back buffer
-- on the same emulator: EasyGL initialised on OpenGL ES 3.0 (SwiftShader), the active renderer was
`OPENGLES3`, and the smoke exited 0. The probe project was deleted afterwards.

**Windows.** `cmake-build-mingw` (MinGW-w64 GCC 14 cross build, Release; `DIRECTX9` default with
`SDL_GPU`, `SOFTWARE`, `HEADLESS`; C API, NET), rebuilt on the final code: every target builds
except the eight test units that use POSIX-only APIs, so `CnaTests.exe` does not link
(`handoff.md` §5.1 item 14). Under Wine: the C API with `SOFTWARE` passes 106 of 108
(`CApi_MediaLibrarySmoke` skips, `CApi_MediaPlayerSmoke` fails as before the campaign); the
`DIRECTX9` suite through DXVK passes 43 of 57 (the 14 failures of item 15); and
`cna_bench_graphics_renderer.exe` runs `SDL_GPU` on its Direct3D 12 backend through vkd3d-proton
(500 sprites and 20 stock-effect meshes per frame, clean exit). The module gtest binaries ran under
Wine with `DIRECTX9` on 2026-10-13, after `DIRECTX11` was retired (item 15).

**Not run here:** native Windows (MSVC, Direct3D 12 hardware); macOS and iOS (no Apple host, so
`SDL_GPU` on Metal and the iOS allow-list are configure-checked only); a physical Android device;
Release builds other than the multi-renderer tree.

### Intentional remaining references

Every tracked file outside `third_party/` was searched for every spelling (`Vulkan`, `Direct3D 11`,
`DirectX 11`, `D3D11`, `DX11`, `D3DCommon`, `Metal` but not `metallic`, `SDL_Renderer`,
`SDL_RENDERER`, `sdl-renderer`, `SdlRenderer`), and every hit outside the history files was read --
by three review passes over three partitions, then by a classification script whose first-match
patterns sort each line into one kind. Rewritten or removed were present-tense claims,
current-renderer lists naming a retiree, and pointers to deleted files, plans and tests (RRC-036,
RRC-038). 5,974 lines remain (counted at `3df66da5c` by an uncommitted classification script; a plain case-insensitive `git grep` of the same spellings finds 6,079), every one of them one of:

| Kind | Lines | Goal category | Examples |
|---|---:|---|---|
| History files | 4,375 | historical | `plans/`, `misc/`, `CHANGELOG.md`, `docs/removed-renderers.md`, `docs/c-api/ABI_VERSIONING.md`, `spikes/`, the XNA-runtime oracles in `tools/xna-oracle/` |
| Dated audits and migration documents | 184 | historical, migration documentation | `AUDIT.md`, `RAM.md`, the culling and depth audits, `docs/graphics-compatibility-report.md`, `docs/build-performance.md` measurements, `docs/RendererNamingMigration.md`, `docs/xna-4-api-coverage.md`'s history sections -- marked as predating the retirement where they read as current |
| Retirement statements and machinery | 343 | retired-selector diagnostic, historical | "the retired VULKAN renderer ...", refusal and reserved-value text, `CnaRendererRetired_*` |
| Task-ID provenance | 204 | historical | `VULKAN-`, `DX-`, `AM4-`, `METAL-` task IDs on the finding they explain |
| SDL_GPU's own backends | 175 | not the retired renderer: SDL's drivers | SDL's Vulkan, Direct3D 12 and Metal drivers, `SDL_HINT_GPU_DRIVER`, the two SDL Vulkan patches |
| Vulkan-GLSL dialect and SPIR-V | 122 | not the retired renderer: a shader language | `.vulkan.` shader-package sources, `VK_*`/`Vk*` API names, clip-space conventions |
| Other past-tense provenance | 98 | historical | what a retired renderer did or measured where it explains a test |
| Vulkan implementations and tools | 51 | not the retired renderer: drivers and translation layers | lavapipe, llvmpipe, RADV, MoltenVK, DXVK, vkd3d-proton, DRI3, loaders |
| SDL3's own `SDL_Renderer` API | 47 | not the retired renderer: SDL's 2D API | the surface presenter that shows `SOFTWARE`'s frames, the SDL classifier |
| Direct3D API semantics | 47 | not the retired renderer: an API | D3D9/D3D11/D3D12 rules cited for comparison |
| wgpu and browser backends | 37 | not the retired renderer: wgpu's drivers | `WebGPUMetalSurface`, wgpu's Metal adapter |
| FNA3D and MojoShader | 22 | not the retired renderer: FNA3D's drivers | FNA3D's own D3D11 and SDL_GPU drivers, MojoShader's Metal/D3D11 profiles |
| Identity tables | 12 | ABI-reserved | `cmake/RendererIdentities.cmake`, `scripts/check_renderer_identities.py` |
| False positives | 11 | not the retired renderer: other meanings | glTF "metal" materials, "a magnet or metal object" |
| Apple APIs | 6 | not the retired renderer: Apple's API | Metal as Apple's API under SDL_GPU and wgpu |
| Build-tree names | 4 | historical | `cmake-build-vulkan` in past build-performance figures |
| Read by hand, no pattern | 236 | ABI-reserved, negative test, historical | `GlslVulkan`/`CNA_SHADER_DIALECT_GLSL_VULKAN` (ABI dialect identities), the reserved values in `graphics.h`, the negative tests in `GraphicsRendererSelectionTests` and `ModuleProbes.cmake`, API comparisons and provenance the patterns miss |

The SDL_GPU, wgpu, FNA3D and Vulkan-GLSL rows are not references to CNA's retired renderers: they
name the backends SDL, wgpu and FNA3D drive underneath the surviving renderers -- the delegation
this campaign chose -- and the shader dialect whose SPIR-V `SDL_GPU` consumes; removing them would
misdescribe how the survivors work. Older retirements' leftovers (LLGL, bgfx, Sokol notes) were out
of scope and left alone.
