# Plan: retiring VULKAN, DIRECTX11, METAL and SDL_RENDERER (RRC-030..RRC-039)

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
| RRC-039 | Rendering on Android (`OPENGLES3` on the emulator); the report finalised | the RRC-039 commit | done |

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
| RRC-045 | The 30 conformance programs ran only on EasyGL, SDL_GPU and WebGPU because nobody had registered them elsewhere: `cna_register_conformance_programs()` now registers them on SOFTWARE, FNA3D and HEADLESS too. A program skips (exit 77) only for a stated capability reason -- a Diagnostic-category renderer has no pixel oracle, SOFTWARE's documented reduced PBR model, FNA3D's named PBR and stock-instancing refusals -- and the deviations the new families show are recorded in `handoff.md` §5.1 item 7 | done |

## Final report

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
  native-MSVC HLSL verifier and D3DCommon's HLSL toolchain, the Metal header-interface target that
  put 29 Metal policy suites into every CnaTests corpus, the CnaTests/glTF-ladder 2D-only skip
  pattern, 30 SDL_RENDERER-only C API registration arms, the Vulkan-only link-closure probe and
  MojoShader-Vulkan existence probe, `tools/vulkan`.
- **Content and data:** the Vulkan and DirectX 11 glTF L7 golden corpora with their policies and
  reports, the Vulkan materials goldens, graphics-ext's HLSL post-process payloads and generator.
- **CI:** `metal-macos-ci.yml`; the Vulkan legs of the glTF, input and platform workflows (now
  SDL_GPU on lavapipe); input-ci's SDL_RENDERER leg; `d3d-windows-ci.yml` builds `DIRECTX9`
  instead of `DIRECTX11`; Apple CI builds `SOFTWARE` (gating) and `SDL_GPU` (non-gating iOS legs).
- **Spikes, docs, plans:** `spikes/vulkan-{depth-bias,vertex-format}-spike`,
  `spikes/d3d11-vsync-spike`; `docs/{vulkan,directx11,metal}-renderer.md`,
  `metal-shader-effect-contract.md`, `metal-history-map.tsv`, `sdl-renderer-2d-completeness.md`;
  `plans/plan_{vulkan,metal,dx,windows11_directx}.md`.
- **Build trees:** `cmake-build-vulkan` (11 GB) and `cmake-build-d3d11` (4.2 GB) deleted;
  `cmake-build-mingw` (`DIRECTX9` default with `SDL_GPU`, `SOFTWARE`, `HEADLESS`) replaces the latter.

**Size** (every tracked blob counted with `git cat-file`, campaign base `b1ec33424` against
the RRC-039 commit, the campaign's last; `third_party/` and `vendor/` are not counted):
tracked files 7,892 -> 7,160 (-732), text lines 2,512,549 -> 2,246,828 (-265,721);
`git diff --shortstat`: 1,419 files changed, 13,204 insertions, 278,925 deletions, 294 of the
files binary (goldens, glTF corpora). Two other sessions' commits landed in that range
(`c1006b03e`, `49b1d3a99`: 9 files, +97/-2 lines), so the campaign's own reduction is 265,816
lines. The five removed implementation directories held 434 files and 242,960 lines; the plans and
documents 13,000 and 10,700 more. What grew: `modules/graphics` +7,662 (the migrated conformance
programs and shared examples), `tests` +381 (the compiled-effect helpers), `sdl-gpu` +241
(`shadow_sampling.glsl`).
Test registrations removed, counted exactly: CTest registrations in the deleted directories --
`VULKAN` 374, `DIRECTX11` 6, `METAL` 18, `SDL_RENDERER` 80 -- plus the 264 shared-corpus fixtures
`cmake/DirectXParityTests.cmake` registered as `DirectX11_*` (the same programs still run on the
survivors); GoogleTest cases in the deleted directories -- `VULKAN` 31, `DIRECTX11` 22, `D3DCommon`
9, `METAL` 304 (its 29 policy suites). In shared test files 30 renderer-specific cases went and 48
were added (the migrated cases and the compiled-effect helper calls).

**Identities:** 14 public identities over 12 families before, 10 over 8 after (`OPENGLES3`,
`OPENGL33`, `WEBGL2` = EasyGL; `WEBGPU`, `HEADLESS`, `SOFTWARE`, `STUB`, `DIRECTX9`, `SDL_GPU`,
`FNA3D`). 41 identities are retired.

### Tests: migrated versus deleted

Every test of the four suites was classified first (RRC-030): Type A tests an implementation
detail and went with the renderer; Type B tests XNA-visible behaviour and was deleted only where
equivalent coverage already ran on a survivor.

| Suite | Files | Deleted | Migrated | Moved |
|---|---:|---:|---:|---:|
| VULKAN | 150 | 122 | 21 | 7 |
| DIRECTX11 | 5 + 4 shared contracts | 8 | 2 | 0 |
| METAL | 35 + 29 policy suites | 59 | 5 | 0 |
| SDL_RENDERER | 67 + 1 shared | 55 | 10 | 3 |

Where the XNA behaviour went:

- **30 conformance programs** in `modules/graphics/examples/conformance/`, run by every suite that
  registers conformance programs as `<Renderer>_Conformance_*` (EasyGL, SDL_GPU, WebGPU): disposed
  guards, Texture2D FromStream / SaveAs / partial SetData, PBR hand-derived BRDF and vertex colour,
  BasicEffect textured MSAA and declaration inputs, declared/padded/unsupplied stock-effect layouts,
  render-target MSAA, ShaderEffect refusals and 3D layouts, deferred effect/query lifetime, clip depth
  range, the ClearOptions matrix, runtime MSAA changes, occlusion-query precision and reuse, effect
  orientation, cube-face MRT binding, back-buffer reset/resize, the seven SpriteBatch.Draw overloads,
  GraphicsAdapter queries, SkinnedEffect vertex colour, the stock-effect uniform ring, capability
  truth, and the two 36-byte BasicEffect vertex-colour programs (RRC-033).
- **Seven shared examples** moved into `modules/graphics/examples/` (fog for every stock effect,
  fill mode, per-unit samplers) and re-registered on the survivors.
- **Compiled effects:** seven helpers in `tests/support/CNA/TestSupport/CompiledEffectConformance.hpp`,
  called by the EasyGL, SDL_GPU, FNA3D, DIRECTX9, Software and WebGPU compiled-effect tests, and a
  truncation sweep through the renderer seam.
- **CnaTests:** viewport, presentation-lifecycle and TextureCube cases; three cube-face
  plural-binding cases (`RenderTargetSemantics`) made renderer-neutral; the base-instance cases gated
  on `RendererFeature::BaseInstanceDrawing` rather than a renderer name; three cases that pinned XNA
  behaviour on METAL alone (float cube typed transfer, Short2 positions, Short4 blend indices) gated
  to the survivors measured to pass them.

What the migrated tests found the survivors get wrong is in `handoff.md` §5.1 (items 7-15): among
them SkinnedEffect's vertex-colour fold, BasicEffect without TEXCOORD0 accepted, the adapter
multisample answer, cube faces in MRT sets refused by SDL_GPU and DIRECTX9, SDL_GPU/WebGPU
ShaderEffect gaps, the unlit 36-byte vertex-colour saturate on SDL_GPU and WebGPU, and the SPIR-V
compiled-effect `vPos`/`TEXBEM` gaps that only the Metal route had fixed.

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
   survivor implements it; SDL_gpu's draw calls take a first instance).
5. The SDL_GPU defects in `handoff.md` §5.1 items 6, 7, 9, 13 and 16 -- among them the
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
| `cmake-build-multi` | `OPENGLES3` default + `OPENGL33`, `WEBGPU`, `SDL_GPU`, `FNA3D`, `SOFTWARE`, `HEADLESS`, `STUB`; C API | 11,313 | 5 | 197 |
| `cmake-build-sdlgpu` | `SDL_GPU` (SDL's Vulkan backend), CNAEXT, C API | 11,024 | 21 | 257 |
| `cmake-build-webgpu` | `WEBGPU` (wgpu-native on Vulkan), CNAEXT | 10,854 | 7 | 260 |
| `cmake-build-headless` | `HEADLESS` platform and renderer | 8,633 | 3 | 528 |
| `cmake-build-software` | `SOFTWARE`, its compiled effects, CNAEXT | 10,843 | 4 | 103 |
| `cmake-build-fna3d` | `FNA3D` (its OpenGL driver, as its CTest environment forces) | 10,664 | 23 | 399 |
| `build-probe` (Release) | the multi-renderer set above, Release, C API | 11,316 | 5 | 197 |

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
all were fixed after the campaign (`plans/plan_fna3d.md` FNA3D-51 and -54 to -57, and `XNAPP-021`'s
shared temporary directory); the FNA3D-default tree's corpus then passed on FNA3D's SDL_GPU driver
and, but for one SystemLink discovery case that passes alone, on its OpenGL driver. The Release
tree fails exactly the Debug multi tree's 5. Item 16 is Debug-only: with `SDL_GPU`
selected at run time the seven `CApi_TeardownLifetime_*` cases pass in the Release tree (no
validation layer) and `game-alive` segfaults in the Debug one. The retired-name refusals (`CnaRendererRetired_*` configure probes,
`GraphicsRendererSelectionTests`, `CoreExtSmoke.c`) pass in every tree that registers them.

**Web.** `cmake-build-wasm-multi` (`WEBGL2` default + `WEBGPU`, Emscripten): `cna_c_api_wasm`,
`cna_demo_2d`, `cna_house3d_demo`, `cna_demo_renderer_selection` and `cna_bench_graphics_renderer`
build. Under node the C API module reports ABI 0.48.0 with 3,207 of 3,207 exports present. In
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
RRC-038). 5,974 lines remain (counted after RRC-039), every one of them one of:

| Kind | Lines | Goal category | Examples |
|---|---:|---|---|
| History files | 4,375 | historical | `plans/`, `remediation/`, `misc/`, `CHANGELOG.md`, `docs/removed-renderers.md`, `docs/c-api/ABI_VERSIONING.md`, `spikes/`, the XNA-runtime oracles in `tools/xna-oracle/` |
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
