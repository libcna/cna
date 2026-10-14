# Plan: retiring VULKAN, DIRECTX11, METAL and SDL_RENDERER (RRC-030..RRC-037)

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
| RRC-037 | Documentation, plans, handoff; validation; this report | this commit | done |

## Report

### What was removed

- **Renderer families:** `modules/renderers/vulkan` (200 files, 86,695 lines), `directx11` (30 /
  11,433), `common/d3d` (`D3DCommon`, 64 / 112,090, about 105,000 of them generated DXBC headers),
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

**Size** (every tracked blob counted with `git cat-file`, campaign base `b1ec33424` against the
RRC-037 tree; `third_party/` and `vendor/` are submodules and not counted): tracked files 7,892 ->
7,160 (-732), text lines 2,512,549 -> 2,246,658 (-265,891); `git diff --shortstat`: 1,410 files
changed, 13,010 insertions, 278,901 deletions, 294 of the files binary (goldens, glTF corpora). The
five removed implementation directories held 434 files and 242,960 lines; the plans and documents
13,000 and 10,700 more. What grew: `modules/graphics` +7,662 (the migrated conformance programs and
shared examples), `tests` +381 (the compiled-effect helpers), `sdl-gpu` +241 (`shadow_sampling.glsl`).
Test registrations removed with the suites, counted from the deleted CMake files: Vulkan about 375,
DirectX 11 about 270 (the parity inventory and the suite), Metal about 18, SDL_RENDERER about 80.

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

What the migrated tests found the survivors get wrong is in `handoff.md` §5.1 (items 8-16): among
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
5. The SDL_GPU defects in `handoff.md` §5.1 items 7, 8, 10, 14 and 17 -- among them the
   compiled-effect half-pixel coverage and device-slot contracts and the C API's exit-time teardown
   crash, both found by this campaign's runs.

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

Every failure is a `handoff.md` §5.1 entry: the multi tree's 5 are EasyGL conformance programs
(item 8); SDL_GPU's 21 are the non-video-platform device cases (item 7), the cube-face MRT refusal,
conformance programs and compiled-effect contracts (item 8) and five C API exit-time teardown
crashes (item 17, the first C API run on `SDL_GPU`); WebGPU's 7 are the occlusion count across a
`Clear` (item 6) and six conformance programs (item 8); HEADLESS's 3 are the cube-face MRT refusal
(item 8). SOFTWARE's 4 are the load-dependent `DynamicSoundEffectInstanceTest` case (item 7) and the
three C API inventory gates, which measured the other session's uncommitted `ContentManager` member
(one new protected symbol); on the committed tree those gates pass (the multi and SDL_GPU runs). The retired-name refusals (`CnaRendererRetired_*` configure probes,
`GraphicsRendererSelectionTests`, `CoreExtSmoke.c`) pass in every tree that registers them.

**Web.** `cmake-build-wasm-multi` (`WEBGL2` default + `WEBGPU`, Emscripten): `cna_c_api_wasm`,
`cna_demo_2d`, `cna_house3d_demo` and `cna_demo_renderer_selection` build. Under node the C API
module reports ABI 0.48.0 with 3,207 of 3,207 exports present. In headless Chrome (inside the
private runner) `cna_demo_2d` and `cna_house3d_demo` pass the 120-frame smoke with `WEBGPU`
selected at run time (clean teardown, no WebGPU error); with the `WEBGL2` default both initialise
EasyGL on WebGL 2.0, load their content and run for the 30-second budget with no abort or error,
but `scripts/run-webgpu-browser-test.sh` has no WebGL completion marker, so the end of the smoke
loop was not observed.

**Android.** `cmake-build-android-x86_64` (NDK 29, Release): a configure that names no renderer
resolves to `OPENGLES3` and no longer warns about it; `cna_c_api` builds. Nothing was run on an
emulator or a device.

**Windows.** `cmake-build-mingw` (MinGW-w64 GCC 14 cross build, Release; `DIRECTX9` default with
`SDL_GPU`, `SOFTWARE`, `HEADLESS`; C API, NET), rebuilt on the final code: every target builds
except the eight test units that use POSIX-only APIs, so `CnaTests.exe` does not link
(`handoff.md` §5.1 item 15). Under Wine: the C API with `SOFTWARE` passes 106 of 108
(`CApi_MediaLibrarySmoke` skips, `CApi_MediaPlayerSmoke` fails as before the campaign); the
`DIRECTX9` suite through DXVK passes 43 of 57 (the 14 failures of item 16); and
`cna_bench_graphics_renderer.exe` runs `SDL_GPU` on its Direct3D 12 backend through vkd3d-proton
(500 sprites and 20 stock-effect meshes per frame, clean exit). The module gtest binaries ran under
Wine with `DIRECTX9` on 2026-10-13, after `DIRECTX11` was retired (item 16).

**Not run here:** native Windows (MSVC, Direct3D 12 hardware), macOS and iOS (no Apple host), an
Android emulator or device, Release builds of the Linux trees, and the `FNA3D`-default family
example suite (it registers only in a tree whose default is `FNA3D`); `FNA3D` ran inside
`cmake-build-multi` through runtime-selected `CnaTests` cases and its 59 compiled-effect tests.

### Intentional remaining references

Every tracked file outside `third_party/` was searched for every spelling (`Vulkan`, `Direct3D 11`,
`DirectX 11`, `D3D11`, `DX11`, `D3DCommon`, `Metal` but not `metallic`, `SDL_Renderer`,
`SDL_RENDERER`, `sdl-renderer`, `SdlRenderer`), and every hit outside the history files was read
in three partitions. Rewritten were present-tense claims, current-renderer lists naming a
retiree, and pointers to deleted files, plans and tests. About 6,300 lines remain; each is one of:

| Kind | Lines (approx.) | Examples |
|---|---:|---|
| History | 4,300 | `plans/`, `remediation/`, `misc/`, `CHANGELOG.md`, this report, `docs/removed-renderers.md`, `docs/c-api/ABI_VERSIONING.md`, `spikes/`, the XNA-runtime oracles in `tools/xna-oracle/`; dated audits (`AUDIT.md`, `RAM.md`, the culling and depth audits, `docs/graphics-compatibility-report.md`, `docs/build-performance.md` measurements), marked as predating the retirement where they read as current |
| SDL_GPU's own backends | 450 | SDL's Vulkan (Linux, Android), Direct3D 12 (Windows) and Metal (Apple) drivers, `SDL_HINT_GPU_DRIVER`, the two SDL Vulkan patches in `cmake/patches/`, lavapipe in CI |
| Provenance | 570 | past-tense records of what a retired renderer did or measured where that explains a test or an XNA finding; `VULKAN-`, `DX-`, `AM4-` task IDs; "moved from the retired VULKAN suite" |
| Vulkan-GLSL dialect, SPIR-V | 130 | `ShaderLanguageEXT::GlslVulkan`, `CNA_SHADER_DIALECT_GLSL_VULKAN` (ABI), the `.vulkan.` shader-package sources that produce SDL_GPU's SPIR-V and WebGPU's WGSL |
| Vulkan implementations and tools | 70 | lavapipe, llvmpipe, RADV, MoltenVK, DXVK, vkd3d-proton, DRI3, loaders and ICDs |
| API semantics | 100 | SDL3's own `SDL_Renderer` in the platform's surface presenter for `SOFTWARE`; D3D11 and Metal API rules cited for comparison |
| wgpu, FNA3D | 65 | `WebGPUMetalSurface` (wgpu's Metal backend), FNA3D's own D3D11 and SDL_GPU drivers, MojoShader's D3D11 and Metal patches |
| Retirement machinery | 30 | `cmake/RendererIdentities.cmake`, `RetiredRendererNames`, the reserved values in `graphics.h`, `AbiHeaderC.c`, `CoreExtSmoke.c`, `CnaRendererRetired_*`, `GraphicsRendererSelectionTests` |
| False positives | 20 | glTF "metal" materials, "a magnet or metal object" |

`cmake-build-vulkan` survives only as the measured tree in past build-performance figures. Older
retirements' leftovers (LLGL, bgfx, Sokol notes) were out of scope and left alone.
