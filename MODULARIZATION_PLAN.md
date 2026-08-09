# CNA Modularization Plan

Campaign: **CNA MODULARIZATION** (FUTURE.md Phase 1) · Branch: `feature/modularization`
Start HEAD: `5f2c4e94162735c781570209a476400dacbd01b1` (tree `40494b21bcafee517307c932bf588cc2367e7c47`)
Date started: 2026-08-09

Every statement in this document is classified as one of:

- **OBSERVED** — mechanically derived from the current repository state;
- **PROPOSED** — a design decision of this campaign, not yet implemented;
- **PROVEN** — implemented and verified by build/test evidence (updated as the campaign proceeds);
- **DEFERRED** — consciously out of scope for this campaign, with rationale.

Method rule: **target graph first, large file moves later.** The first milestone creates real
CMake module targets over the *unmoved* current sources, proves the graph, then (and only then)
considers physical restructuring.

---

## 1. OBSERVED — current build architecture

### 1.1 Top-level structure

- One root `CMakeLists.txt` (305 lines) including `cmake/*.cmake` modules:
  `ThirdPartySDL`, `TestHelpers`, `ThirdPartyENet`, `BackendSelection`, `BackendLibraries`,
  `CnaLibrary`, `Examples`, ~30 per-backend `Tests/*.cmake`, `Harnesses`, `ToolGltfToCnj`,
  `UnitTests`.
- **No `install()` / `export()` rules exist anywhere.** The only consumer contract is
  `add_subdirectory` of sibling checkouts plus target names (`CNA`, `CNA_GamerServices`,
  `CNA_Net`, `${BACKEND_TARGET}`, `SHARP_RUNTIME`, `SDL3::SDL3`, …).
- Dependencies as sibling repositories (`add_subdirectory(../x)`): `sharp-runtime` (always),
  `easy-gl` (GL family), `free-direct` (FREEDIRECT). Submodules: `third_party/SDL{,_image,_mixer}`,
  `vendor/googletest`. Other backend third-parties fetched/configured per selection
  (`ThirdParty{WebGPU,Magnum,Wicked,Sokol,Diligent,LLGL,Skia,SkiaGanesh}.cmake`, bgfx FetchContent).

### 1.2 Library targets (before modularization)

| Target | Type | Sources | Notes |
|---|---|---|---|
| `CNA` | STATIC | GLOB `src/*.cpp` minus Backends, GamerServices, Net, minus FFmpeg-gated media files | the monolith: math, core, graphics, input, audio, media, content, runtime, storage, devices, NOXNA in one archive |
| `CNA_GamerServices` | STATIC | `src/Microsoft/Xna/Framework/GamerServices/**` + `src/CNA/Internal/GamerServices/**` | only when `CNA_ENABLE_NET` |
| `CNA_Net` | STATIC | `src/Microsoft/Xna/Framework/Net/**` + `src/CNA/Internal/Net/**` | links `CNA_GamerServices` + `enet` |
| `${BACKEND_TARGET}` (`cna_backend_graphics_<x>`) | STATIC | `${BACKEND_DIR}/*.cpp` (one backend per configure) | 41 public identities → 38 backend dirs (4 GL profiles share EasyGL) |
| `cna_backend_graphics_common` | INTERFACE | — | exposes **both `src/` and `include/`** as INTERFACE include dirs |
| `cna_backend_graphics_d3dcommon` | STATIC | `Backends/D3DCommon/*` | D3D11/D3D12 only |
| `cna_backend_graphics_sdl_renderer_core` | STATIC | `Backends/SdlRenderer/*` | ASCII decorator only |
| `cna_backend_graphics_d3d9_effect` | STATIC | 2 D3D9 files | isolates d3dcompiler |

Public compile definitions on `CNA`: `SOUND_ENABLED`, `XNA5`, `${CNA_BACKEND_DEFINE}`,
`CNA_NOXNA`/`CNA_DEVICES`/`CNA_DRACO_AVAILABLE`/`CNA_FFMPEG_AVAILABLE` (conditional).
`BackendSelection.cmake` additionally sets the backend define **globally** via
`add_compile_definitions()`.

Known intentional archive cycle: for 19 backends, `${BACKEND_TARGET}` links `CNA` PRIVATE
(reverse edges: `CNA::Logger`, `Effect::Apply`, `VertexDeclaration` vtable, math/colour values,
`DxtUtil`/`Bc7Util`). Consumers additionally use raw
`-Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group` in `Examples.cmake` and per-backend test
macros (~40+ sites).

### 1.3 Renderer identities (OBSERVED, must remain 41)

`CNA_GRAPHICS_BACKEND` one-of-41: SDL_RENDERER, OPENGLES, OPENGL33, WEBGL1, WEBGL2, BGFX, VULKAN,
WEBGPU, MAGNUM, HEADLESS, SOFTWARE, STUB, D3D11, D3D12, DIRECT2D, CANVAS, HTML_DOM, SKIA, ASCII,
FREEDIRECT, D3D9, DX1, DX2, DX3, DX5, DX6, DX7, DX8, D3D10, SDL_GPU, OPENGLES1, OPENGL4, OPENGL1,
OPENGL2, WICKED, SOKOL, DILIGENT, GLIDE, GDI, LLGL, METAL.
Exactly one backend is built per configure. The 4 GL profiles (OPENGLES/OPENGL33/WEBGL1/WEBGL2)
share the internal EasyGL implementation (one target, 4 public identities — accepted architecture).
ASCII composes the SdlRenderer core; GDI compiles a subset of Software's CPU-2D units. These
sharings are documented, deliberate, and are **not** renderer-A-depends-on-renderer-B shortcuts.

### 1.4 Mechanically derived include graph

Derived by `graph.py` over `include/` + `src/` (683 headers, 453 TUs), clustered by directory
(Framework root split by curated math/runtime file lists). Full edge data: `modularization/baseline/graph.json`.
Key link-level facts (header-only types create **no** link edges):

- `xna.math` → external: sharp-runtime only. Its `CNA/CNAHelper.hpp` and
  `Graphics/PackedVector/IPackedVector.hpp` dependencies are header-only.
- `cna.core` → SDL3 (`Logger.cpp`; `Entrypoint.hpp` is a header-only `SDL_main` shim), sharp-runtime.
- `xna.graphics` ↔ `internal.graphics` ↔ `backends.common` form one coherent graphics-core cluster;
  `GraphicsDevice.cpp` additionally includes backend-specific headers **only under
  `#ifdef CNA_BACKEND_{D3D9,BGFX,DILIGENT,LLGL}`** (capability/selection special cases), and
  `SDL3` in 3 TUs; `ImageLoader.cpp` uses SDL3_image.
- **Cycle (real, XNA-semantic):** `GraphicsDevice.cpp` calls
  `Input::Touch::TouchPanel::setDisplay{Width,Height}Property` (+ TextInputEXT/Mouse integration)
  while `Input::MouseCursor` uses `Graphics::Texture2D` → graphics-core ↔ input.
- **Cycle (already declared):** graphics-core ↔ selected backend (factory + reverse symbol edges).
- `FrameworkDispatcher` (Framework root): `.cpp` includes nothing but its own header; callers are
  `Game.cpp`, `Audio/DynamicSoundEffectInstance.cpp`, `Media/MediaPlayer.cpp`. Its state is the
  audio streams list → assign the TU to the audio module (ownership follows dependency direction).
- Content (`Content/**` + `Internal/Xnb/**` + `Internal/GltfImport/**`) constructs
  SoundEffect/Song/Video via type readers → content → audio + media (XNA reality: ContentManager
  loads every asset type). FFmpeg-unavailable platforms already exclude the video reader TU.
- Media → audio (`MediaPlayer.cpp` → `AudioMixer`), media → graphics-core (VideoPlayer textures,
  MediaLibrary → ImageLoader), media → FFmpeg (PRIVATE today).
- Runtime (Game & friends) → graphics-core, input, content, audio, media, SDL3.
- Storage → sharp-runtime only (+ header-only `PathContainment`, `PlayerIndex`).
- Devices (`Microsoft/Devices` ungated + `CNA/Devices` `CNA_DEVICES`-gated) → SDL3 heavily, core,
  math, graphics (Camera→Texture2D), runtime (DisplayInfo→GameWindow).
- NOXNA graphics (`CNA/Graphics`, TU-level `#ifdef CNA_NOXNA`) → graphics-core.
  `CNA/Input` extension surface (Clipboard/Haptics/Joysticks/…) is **ungated** (always compiled)
  and belongs with input.
- GamerServices → runtime/graphics/input/audio/storage + SDL3 (Guide message box); Net →
  GamerServices + ENet. Both already separate targets.
- `CNA/Internal/{Json,PathContainment,Utf8Decode,CnjEnvelope,CnjSourceFile}.hpp` are header-only
  (no root-level internal TUs).

### 1.5 Tests, examples, harnesses, tools (OBSERVED)

- `CnaTests` (GTest): GLOB `tests/*.cpp` with platform/backend/NET/FFmpeg exclusions; links
  `CNA + SHARP_RUNTIME + gtest_main + SDL3` (+ per-backend extras); registered via
  `gtest_discover_tests(PRE_TEST)`; `SKIP_RETURN_CODE 77` convention; `ctest -L input` subset.
- `Examples.cmake` (1008 lines, `CNA_BUILD_EXAMPLES` default ON): demo/diag executables linking
  `CNA ${BACKEND_TARGET}` (many with raw `--start-group`).
- `Harnesses.cmake`: subprocess harness executables (audio/net/devices) linked against `CNA`.
- `tools/`: gltf_to_cnj + per-subsystem reference/diagnostic tools.
- Per-backend `cmake/Tests/*.cmake`: CTest registration incl. Wine/Xvfb wrappers.

### 1.6 sharp-runtime (OBSERVED, from the modularization branch study)

Studied read-only at `origin/claude/remediation-batch-1804-namespace-b1yjh5` =
`e8340b33e4c1eb021b1eb77061ac806d13facce3` (2026-08-09). Owner statement: **modularization is
DONE**; the branch continues to receive post-modularization audit remediation.

- 41 modules (30 STATIC, 11 INTERFACE), registry-based
  (`sharp_runtime_register_module` metadata + central resolver, lazy target materialization),
  aliases `SharpRuntime::<Component>`; include spellings (`System/…`, `SharpRuntime/…`) unchanged;
  **no C++ namespace renames**.
- Compatibility: `SHARP_RUNTIME` still exists as an INTERFACE → `SharpRuntime::All`, created when
  the (default) `All` selection is active → CNA's current `add_subdirectory` + `SHARP_RUNTIME`
  linking keeps working unchanged.
- Selective consumption: `SHARP_RUNTIME_COMPONENTS=<list>`; then `SHARP_RUNTIME` does **not**
  exist and consumers must link `SharpRuntime::<Component>`.
- **Gap:** the branch forked before develop's FINAL-STAB-001
  (`1e51c2d869697fd827af7ca342ffabf77d30faf8`) and does not contain
  `SHARP_RUNTIME_HAS_NATIVE_INT128` or the i686 toolchain/regression. Building CNA against the raw
  branch would silently compile out the Decimal XNB readers (`#if` on an undefined macro).
  develop = merge-base + FINAL-STAB-001 only → the future merge is structurally clean.
- sharp-runtime also has **no install/export**; its consumer fixtures
  (`test/consumer/`, negative fixtures, selective-component CI) are the reference pattern for
  CNA's minimal-link probes.
- CNA's derived component closure (from CNA's `System/…`/`SharpRuntime/…` includes):
  `Core.Base`, `IO` (+`Uri`, `TimeZone`), `Collections.Core`, `Collections.ObjectModel`
  (+`ComponentModel`), `Runtime`, `Threading`, `Text`, `Globalization`, `Storage`.
  No Net, no Xml, no Text.Json, no compression, no crypto.

---

## 2. PROPOSED — target module graph

### 2.1 Modules (CMake targets over unmoved sources)

All targets STATIC unless noted; every target gets an `ALIAS` (`CNA::<Name>`). Sources are owned
by directory GLOB except the Framework-root split, which uses explicit file lists. A configure-time
**source-partition validator** asserts every production TU under `src/` is owned by exactly one
module (or is an explicitly listed backend/platform-gated source) — this is the permanent no-loss
ownership gate.

| Target | Alias | Owns (sources) | PUBLIC deps | PRIVATE deps |
|---|---|---|---|---|
| `cna_build_flags` | `CNA::BuildFlags` | — (INTERFACE) | include dir `include/`, defines `SOUND_ENABLED`, `XNA5`, backend define, NOXNA/DEVICES/DRACO/FFMPEG flags | — |
| `cna_math` | `CNA::Math` | 17 Framework-root math TUs (Vector2/3/4, Matrix, Quaternion, Plane, Ray, Bounding{Box,Sphere,Frustum}, Rectangle, Point, Color, MathHelper, Curve, CurveKey, CurveKeyCollection) | `cna_build_flags`, sharp-runtime | — |
| `cna_core` | `CNA::Core` | `src/CNA/{Logger,CNAException,Platform,DesktopOS}.cpp` | `cna_build_flags`, sharp-runtime | SDL3 (`Logger.cpp`) |
| `cna_graphics_core` | `CNA::GraphicsCore` | `src/Microsoft/Xna/Framework/Graphics/**`, `src/CNA/Internal/Graphics/**`, `src/CNA/Internal/Backends/Common/**` | `cna_math`, `cna_core` | SDL3, SDL3_image; cycle: `cna_input`, `${BACKEND_TARGET}` |
| `cna_input` | `CNA::Input` | `src/Microsoft/Xna/Framework/Input/**`, `src/CNA/Internal/Input/**`, `src/CNA/Input/**` | `cna_graphics_core`, `cna_math`, `cna_core` | SDL3 |
| `cna_audio` | `CNA::Audio` | `src/Microsoft/Xna/Framework/Audio/**`, `src/CNA/Internal/Audio/**`, `src/Microsoft/Xna/Framework/FrameworkDispatcher.cpp` | `cna_core`, `cna_math` | SDL3 |
| `cna_media` | `CNA::Media` | `src/Microsoft/Xna/Framework/Media/**`, `src/CNA/Internal/Media/**` (minus FFmpeg-gated TUs when unavailable) | `cna_audio`, `cna_graphics_core` | FFmpeg, SDL3 |
| `cna_content` | `CNA::Content` | `src/Microsoft/Xna/Framework/Content/**`, `src/CNA/Internal/Xnb/**` (minus video reader when no FFmpeg), `src/CNA/Internal/GltfImport/**` | `cna_graphics_core`, `cna_audio`, `cna_media`, `cna_math`, `cna_core` | cgltf/stb include dirs, draco |
| `cna_storage` | `CNA::Storage` | `src/Microsoft/Xna/Framework/Storage/**` | sharp-runtime, `cna_build_flags` | — |
| `cna_runtime` | `CNA::Runtime` | 15 Framework-root runtime TUs (Game, GameComponent*, DrawableGameComponent, GameServiceContainer, GameTime, GameWindow, GraphicsDeviceManager, GraphicsDeviceInformation, LaunchParameters, TitleContainer, TitleLocation, event-args TUs) | `cna_graphics_core`, `cna_input`, `cna_content`, `cna_audio`, `cna_media`, `cna_core`, `cna_math` | SDL3 |
| `cna_devices` | `CNA::Devices` | `src/Microsoft/Devices/**`, `src/CNA/Devices/**` | `cna_runtime`, `cna_graphics_core`, `cna_core`, `cna_math` | SDL3 |
| `cna_noxna` | `CNA::NoXna` | `src/CNA/Graphics/**` (TU-gated by `CNA_NOXNA`) | `cna_graphics_core` | — |
| `CNA_GamerServices` | `CNA::GamerServices` | unchanged dirs | `cna_runtime`, `cna_storage` | SDL3 |
| `CNA_Net` | `CNA::Net` | unchanged dirs | `CNA_GamerServices`, `enet` | — |
| `CNA` | (umbrella) | — (INTERFACE) | all modules above + `${BACKEND_TARGET}` | — |

Notes:

- **`CNA` stays the compatible umbrella** (becomes INTERFACE): every existing consumer
  (`Examples`, tests, harnesses, external games) keeps `target_link_libraries(x CNA)` working with
  the same include dirs and public defines, now carried by `cna_build_flags`.
- sharp-runtime dependency: every module links sharp-runtime; §3 narrows this to specific
  components where the modular runtime is present.
- All modules PRIVATE-include `src/` (internal headers stay reachable, unchanged), content also
  `third_party/cgltf` + `third_party/stb` (as today).
- The `#ifdef CNA_BACKEND_*` includes inside `GraphicsDevice.cpp`/`GraphicsAdapter.cpp`/…
  stay as-is (single-backend-per-configure keeps them per-build); the graphics-core target itself
  carries **no native SDK link or include dependency** — native SDKs stay behind
  `${BACKEND_TARGET}` (verified by the probe matrix).
- `main.cpp` (hello-world) and `examples/`, `tools/`, `tests/` targets keep their current
  ownership; only their link lines change from raw `--start-group` hacks to plain `CNA`.

### 2.2 Cycles and their resolutions

| Cycle | Kind | Resolution |
|---|---|---|
| graphics-core ↔ selected backend | genuine (factory inversion + reverse symbol edges), already declared pre-campaign | keep; re-declared at module level (`${BACKEND_TARGET}` PRIVATE-links `cna_graphics_core`/`cna_core`/`cna_math` instead of the monolith); CMake's static-cycle handling replaces the raw `--start-group` consumer hacks |
| graphics-core ↔ input | genuine XNA semantics (GraphicsDevice ↔ TouchPanel/Mouse; MouseCursor → Texture2D) | keep as declared mutual STATIC-library cycle; no API change (semantic-change policy) |
| runtime ↔ audio (via FrameworkDispatcher) | ownership artifact | dissolved by assigning `FrameworkDispatcher.cpp` to `cna_audio` (its state is the audio stream list; media/runtime callers depend on audio) |

No other link-level cycles are expected; the validator + build will prove it (PROVEN section will
record the final list).

### 2.3 Backend/renderer model (unchanged identity semantics)

- One `${BACKEND_TARGET}` per configure, exactly as today; the 41 public identities and their
  truthful capability reporting are untouched. A mechanical identity-count check
  (over `include/CNA/GraphicsBackendType.hpp` + `BackendSelection.cmake`) is added as a permanent
  test/script.
- Backend targets link `cna_backend_graphics_common` + sharp-runtime + their own native/third-party
  deps (unchanged), plus the declared reverse edges above.
- Renderer-to-renderer sharing remains only the documented deliberate cases
  (EasyGL 4-profile family; ASCII→SdlRenderer core; GDI→Software CPU-2D units; D3D11/D3D12→D3DCommon).

---

## 3. PROPOSED — sharp-runtime consumption

- Add `CNA_SHARP_RUNTIME_ROOT` (default `${CMAKE_CURRENT_SOURCE_DIR}/../sharp-runtime`) so builds
  can point at a pinned modular checkout without moving worktrees.
- **Dual-mode consumption seam:** after `add_subdirectory`, detect the modular runtime
  (`if(TARGET SharpRuntime::Core.Base)`). If present, CNA modules link their specific components;
  otherwise (current monolithic develop) they link the `SHARP_RUNTIME` umbrella. The seam is
  removed in favour of components-only once modular sharp-runtime lands in develop.
- Per-module component mapping (to be PROVEN by selective builds):

| CNA module | sharp-runtime components |
|---|---|
| `cna_math` | `Core.Base` |
| `cna_core` | `Core.Base` |
| `cna_graphics_core` | `Core.Base`, `IO`, `Collections.ObjectModel` |
| `cna_input` | `Core.Base` |
| `cna_audio` | `Core.Base`, `IO`, `Threading` |
| `cna_media` | `Core.Base`, `IO`, `Threading`, `Collections.ObjectModel` |
| `cna_content` | `Core.Base`, `IO`, `Collections.ObjectModel`, `Globalization` |
| `cna_storage` | `Core.Base`, `IO`, `Threading`, `Storage` |
| `cna_runtime` | `Core.Base`, `IO` |
| `cna_devices` | `Core.Base` |
| `CNA_GamerServices` / `CNA_Net` | `Core.Base`, `IO`, `Collections.ObjectModel`, `Threading` |

  (The table is seeded from the include graph; exact minimal sets get corrected by actual
  selective-component builds and recorded here as PROVEN.)
- Umbrella `SHARP_RUNTIME` remains what tests/examples link transitively; no CNA target may link
  `SharpRuntime::All` directly except the compatibility seam.
- **Merge gate (owner-directed):** merge `remediation-batch-1804-namespace-b1yjh5` into
  sharp-runtime develop only at a stable accepted checkpoint of that branch (it is actively
  receiving audit fixes). History-preserving normal merge, no rebase, FINAL-STAB-001 preserved;
  validation matrix; then a normal push of develop (owner-authorized). Until then, CNA validates
  selective consumption against a local read-only **merge-preview** worktree
  (branch head + develop merged locally, never pushed) — this also front-loads conflict
  intelligence for the real merge, including the `SHARP_RUNTIME_HAS_NATIVE_INT128` gap.

---

## 4. PROPOSED — minimal-link probe matrix

Permanent probe consumers (tiny TUs) + a mechanical link-closure checker
(`scripts/check_module_link_closure.py`, reading the generated link line per probe and asserting
forbidden archives are absent), registered as CTest entries:

| Probe | Links | Must NOT appear in link closure |
|---|---|---|
| `cna_probe_math` | `CNA::Math` | cna_graphics_core, cna_content, cna_runtime, SDL3, any backend, ENet, FFmpeg |
| `cna_probe_core` | `CNA::Core` | cna_graphics_core, any backend, ENet, FFmpeg |
| `cna_probe_graphics_headless` | `CNA::GraphicsCore` (+ backend HEADLESS configure) | native SDKs (vulkan, GL, d3d, wgpu…), cna_content, cna_media, CNA_Net |
| `cna_probe_content` | `CNA::Content` | CNA_Net, CNA_GamerServices, any backend beyond configured, cna_devices |
| `cna_probe_runtime_headless` | `CNA::Runtime` + backend | CNA_Net, cna_devices (unless enabled) |
| umbrella control | `CNA` | (sanity: closure == full expected set) |

Plus: per selected renderer config, assert the native dependency closure contains exactly that
renderer's SDK family (e.g. VULKAN build has no d3d/wgpu/bgfx libs). sharp-runtime side: with the
modular runtime, assert CNA's closure excludes Net/Xml/Json/compression/crypto components and the
`sharp_runtime_tinyxml2`/`sharp_runtime_miniz`/ZLIB externals.

---

## 5. PROPOSED — no-loss / completeness gate

Baseline captured **before any production change** under `modularization/baseline/`
(machine-readable): tracked production sources+headers, test sources, CTest registration names per
validated config, `--gtest_list_tests` name sets, build target list, public API inventory
(mechanical extraction over `include/`), generated-source inputs/outputs, and the include graph.
After each milestone and at final acceptance, the same inventories are re-captured and reconciled;
every pre-item classified PRESERVED / MOVED / INTENTIONALLY_REPLACED /
INTENTIONALLY_REMOVED_WITH_PROOF. The CMake source-partition validator makes TU-ownership
completeness permanent (configure fails on unowned/doubly-owned production TUs). Test registration
compared **by name**, not by count.

---

## 6. DEFERRED (with rationale)

- **Physical source moves** (`core/`, `math/`, `graphics/backends/`, … layout): only after the
  target graph, tests and probes are green; each move as pure `git mv` commits separated from any
  edit. Whether to execute within this campaign depends on remaining risk budget after the graph
  stabilizes; the target-first milestone is the campaign's required core either way.
- **Splitting `include/` per module**: headers stay in the single public `include/` tree this
  campaign (public source compatibility; install story does not exist yet).
- **`cna-effects` / `cna-shaders` targets**: investigated; stock effects and shader metadata are
  dense in-cluster dependencies of `Graphics/**` with native shader representations already
  renderer-side. A split would cut a cohesive cluster without improving any real dependency (no
  consumer wants effects without graphics-core). Not created.
- **`cna-platform-*` targets**: no platform abstraction layer exists in current code; SDL use is
  embedded per subsystem (each module links SDL3 PRIVATE). Creating platform targets now would
  require semantic extraction. The SDL-touching TU inventory per module is documented instead;
  a real platform split is future work.
- **Input core vs SDL-impl split**: single implementation exists (SdlInputBridge); a second target
  adds no proven value now.
- **`install()`/`export()`/`find_package(CNA)`**: neither CNA nor sharp-runtime has an install
  contract; the ecosystem consumes via `add_subdirectory`. Added value now: none; risk: inventing
  an untested contract. Deferred with the note that module boundaries created here map 1:1 onto a
  future export set.
- **Dynamic renderer plugins, C ABI, C# binding**: explicitly out of campaign scope (owner).
- **Registry-based lazy component engine (sharp-runtime style)**: CNA has ~14 modules and one
  mandatory backend per configure; direct target definitions with a partition validator give the
  same guarantees at far lower complexity. Revisit if CNA ever needs selective configures.
- **`SHARP_RUNTIME_COMPONENTS` selective configure for CNA's own build**: CNA's default build keeps
  the `All`-backed umbrella until modular sharp-runtime is merged to develop; the dual-mode seam
  covers the transition.

---

## 7. Migration phases and acceptance

| Phase | Content | Gate |
|---|---|---|
| P0 | start gate, graph derivation, this plan | done at plan commit |
| P1 | no-loss baseline + pristine control build/test (OPENGLES) | control green, baseline committed |
| P2 | module targets + umbrella + backend relink + `--start-group` replacement | OPENGLES build + CnaTests parity by name; partition validator green |
| P3 | representative backend configs (HEADLESS, STUB/SOFTWARE, one GPU-family configure) + identity check | builds/tests green; 41 identities |
| P4 | minimal-link probes + closure checker | probe matrix green |
| P5 | sharp-runtime dual-mode seam + component narrowing against merge-preview | selective build green; Decimal readers proven present via preview (INT128 gate) |
| P6 | sanitizers (ASan+UBSan HEADLESS portable corpus incl. init-order coverage) | zero new CNA-originating findings |
| P7 | reconciliation + docs + (owner-gated) sharp-runtime develop merge/push if its checkpoint is reached | acceptance matrix (campaign brief) |

Final acceptance follows the campaign brief's 25-point matrix; this document's PROVEN sections are
updated with evidence as phases complete.

---

## 8. PROVEN — evidence ledger (updated during the campaign)

### P1 — baseline (commit 9bd90c2f2)
- Pristine OPENGLES config: 6526 ctest registrations, full gtest list, file hashes, API decls
  captured under `modularization/baseline/`.
- Pristine full-suite single-process OPENGLES run segfaults in `MetalResourceHealth` device churn
  under Xvfb (environmental; per-test ctest processes pass) — recorded in the baseline README.
- Pristine HEADLESS control (built from the untouched `cna` develop worktree, same commit):
  6118 ctest tests at `-j4`: 11 failed → serial rerun: 9 were parallel flakes (ENet×6,
  audio-timing×3), leaving exactly 2 deterministic pristine failures:
  `GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow` (the accepted
  REMED-GFX-133 residual) and `Headless_Smoke` (aborts with an index-buffer primitive-range
  throw in this environment). This is the control profile the modular build must match.

### P2 — module split (commits 268d3b962 + 6c9a64da3)
- Full OPENGLES tree builds with the 12 module targets + INTERFACE umbrella; 12 `libcna_*.a`
  module archives produced.
- ctest registration names and normalized gtest list **byte-identical** to baseline.
- A/B behavior run (identical command, `--gtest_filter=-MetalResourceHealth.*`, Xvfb, dummy
  audio): pristine binary 6218 = 6212 pass + 6 skip; modular binary 6218 = 6212 pass + 6 skip;
  per-test outcome sets identical; both exit 0.
- Configure-time source-partition validator active (no unowned/doubly-owned production TUs).
- All five module probes run; all link-closure gates pass on OPENGLES; `probe_math`'s build
  closure is exactly sharp-runtime + `cna_math`; `probe_core` adds only `cna_core`.
- `RendererIdentityRegistry` gate: 41 identities preserved in both registries (enum order exact;
  cmake STRINGS as a set — the two registries' orderings differ pre-existing).

### P5 (in progress) — sharp-runtime consumption seam (commit 5b9746d32)
- Seam verified behavior-neutral against monolithic develop: OPENGLES reconfigure + rebuild is a
  no-op.
- Local read-only **merge preview** built at `../sharp-runtime-merge-preview`
  (branch `preview/modularization-develop-merge`, base `e8340b33` + develop `1e51c2d8`, never to
  be pushed): 5 conflicts total (.gitignore, root CMakeLists, NEXT.md, README.md,
  BitConverter.hpp); FINAL-STAB-001's `SHARP_RUNTIME_HAS_NATIVE_INT128` re-published on the
  `sharp_runtime_headers` INTERFACE; Decimal.cpp i686 exclusion ported into the Core.Base setup;
  BitConverter keeps both the audit bounds-checks and the native-int128 guard. Merged tree builds;
  `SharpRuntimeTests_Core_Base` passes 5586/5586.
- The remote branch remains ACTIVE during this campaign (observed movement
  `894135fd → e8340b33 → accee955` across the session); the develop merge gate is therefore not
  eligible yet.
- **CNA × modular sharp-runtime (commit 139730119):** per-module component sets corrected to the
  mechanically derived closures; the complete non-Net CNA tree (HEADLESS, `CNA_ENABLE_NET=OFF`)
  compiles and links against the merge preview; all 12 module gates pass; the Decimal XNB reader
  tests are present exactly as on the monolith (3 = 3 — the INT128 define port works);
  `probe_math`'s sharp closure is the single `libsharp_runtime_core.a` archive.
- **Merge-time CNA adaptation list (Net-only, upstream-driven):**
  `System::Collections::Generic::IList<T>::operator[]` on the modular branch returns
  `System::Collections::detail::ElementReference<T>` (audit remediation); CNA's
  `NetworkSessionProperties::operator[]` (`include/Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp:66`)
  still returns `std::optional<int>&`, making the type abstract at
  `AvailableNetworkSession.hpp:144`, `NetworkSession.hpp:843/884`. One root cause, four error
  sites; must be adapted together with the sharp-runtime develop merge (not now — the branch API
  is still moving and CNA's Net public surface must not chase an unmerged contract).

### P3 — representative configurations (commits ede258a35 + 08224fe9a)
- **HEADLESS (canonical):** modular full ctest = 6130 tests (pristine control 6118 + exactly the
  12 new module gates; name diff shows additions only); serial rerun leaves exactly the control's
  2 deterministic pristine residuals (REMED-GFX-133 `SetRenderTargets_FourTargets`,
  `Headless_Smoke` primitive-range abort). `ModuleLinkClosure_GraphicsNativeSdkFree` passes —
  graphics-core's closure carries no native renderer SDK.
- **VULKAN (native family):** full tree builds after two findings: (1) the backend reverse edge
  is structural for every backend (unconditional declaration replaced the observed-only 19-entry
  list); (2) a pre-existing latent gap — the Vulkan test macro alone never linked SDL3::SDL3
  while 63 example TUs include SDL directly; the same TU fails in the pristine tree (verified by
  a single-object pristine build). Focused Xvfb run: 222 registrations, 221 pass; the single
  failure `Vulkan_DepthBias` (llvmpipe constant-bias arm, no DRI3 under Xvfb) reproduces
  **identically (3/4, same arm) with the pristine-built binary** — pre-existing environmental
  residual on this display route. probe_graphics's VULKAN closure is exactly the backend archive
  + libvulkan.so — no other native SDK.
- Renderer identity gate green in every configuration (41 preserved).

### P6 — sanitizers (HEADLESS, `address,undefined,float-cast-overflow`, Debug, ccache)
- `ldd CnaTests` links `libasan.so.8` + `libubsan.so.1`; all five module probes run clean under
  the strict environment.
- Strict curated corpus (`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:check_initialization_order=1:strict_init_order=1`,
  `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; Vector2/Decimal/DateTime/audio/registry/
  capability areas): **650 = 647 pass + 3 skip + 0 fail, zero reports** — the REMED-GFX-221
  initialization-order coverage is retained and green.
- A broader full-suite sweep (beyond any previously gated sanitizer scope) additionally surfaced
  **pre-existing** findings in unchanged code, each proven present in the pristine tree:
  (a) the four Net subprocess-harness tests fail under leak checking because the harness children
  leak the `NetworkSession::EndCreate` allocation at exit (`NetworkSession.cpp:762`; reproduced
  against the pristine control binary via standalone-LSan preload, identical stack);
  (b) `Vector3::GetHashCode` has signed-int overflow hashing non-finite component bit patterns
  (`Vector3.cpp:117`; reproduced by compiling the pristine worktree's own sources standalone
  under UBSan). Zero NEW CNA-originating sanitizer defects; the pre-existing findings are
  recorded for their own future remediation, not reopened here.
- Collect-all sweep (halt off, full 6066-test suite): 6019 pass / 3 fail (the Net harness-child
  leak tests); the remaining report inventory is a Net/GamerServices object-lifetime vptr family
  (`SignedInGamer`/`NetworkSessionAction`/`NetworkSessionProperties`/`IAsyncResult` "does not
  point to an object of type" during session teardown paths) — all in pristine-identical
  sources, useful input for the merge-time Net adaptation.

### Physical source moves — decision
Deferred (plan §6): the target graph, parity gates and probe contracts are the campaign's
substantive modularization; a later pure-`git mv` phase can relocate sources without touching
`include/` (public include paths are unaffected) once the owner settles the final directory
blueprint. No file was moved in this campaign; the no-loss reconciliation therefore reduces to
hash-identity for all production sources plus enumerated additions.

### Cycles (final observed list)
1. graphics-core ↔ input — XNA-semantic, declared. 2. graphics-core ↔ selected backend —
factory + reverse symbol edges, declared per-module. 3. audio ↔ media — FrameworkDispatcher
pumps MediaPlayer while MediaPlayer plays through the audio mixer; declared. (The planned
"runtime ↔ audio via FrameworkDispatcher" cycle dissolved by assigning the dispatcher TU to
audio; the audio↔media coupling replaced it as the true minimal form.) No other link-level
cycle exists.
