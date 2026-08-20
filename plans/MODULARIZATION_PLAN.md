# CNA Modularization Plan

> **Naming note (2026-08):** after this campaign closed, the renderer-naming
> normalization renamed the graphics "backend" surface to "renderer"
> (`CNA_GRAPHICS_BACKEND` → `CNA_GRAPHICS_RENDERER`, `BackendSelection.cmake` →
> `RendererSelection.cmake`, `GraphicsBackendType.hpp` → `GraphicsRendererType.hpp`),
> the DX*/D3D* identities to `DIRECTX*`, `OPENGLES` to `OPENGLES3`, and `NOXNA` to
> `CNAEXT` — see `docs/RendererNamingMigration.md`. The names below are the ones that
> were accurate during this campaign and are preserved as historical record.

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

---

## 9. PHASE 2 — physical layout + architecture hardening (2026-08-10)

Phase-1 final state `b072f0da6` (tree `ef3cc2a91`) is the accepted input; Phase 2 executes the
physical restructuring Phase 1 deferred (§6), re-reviews the three declared cycles with final
evidence, and hardens the include/dependency hygiene of the target graph. Public `include/` is
untouched by design: consumer `#include <...>` source compatibility outranks internal symmetry,
so the internal-contract headers stay at `include/CNA/Internal/**` while implementation moved.

### 9.1 PROVEN — physical layout (implementation tree only)

Every implementation TU now lives under the module that owns it in the target graph:

    src/
      Math/                 cna_math          (17 TUs, flat)
      Core/                 cna_core          (4 TUs, flat)
      Runtime/              cna_runtime       (15 TUs, flat)
      Storage/              cna_storage       (3 TUs, flat)
      Graphics/
        Xna/                cna_graphics_core (67 TUs — Microsoft::Xna::Framework::Graphics impl)
        Internal/           cna_graphics_core (3 TUs — CNA::Internal::Graphics)
        Backends/<X>/       per-backend targets (38 dirs incl. their shaders/ subtrees)
      Input/
        Xna/  Internal/  NoXna/    cna_input  (18 + 11 + 7)
      Audio/
        Xna/  Internal/            cna_audio  (13 incl. FrameworkDispatcher.cpp + 3)
      Media/
        Xna/  Internal/            cna_media  (21 + 11)
      Content/
        Xna/  Xnb/  GltfImport/    cna_content (6 + 16 + 1)
      Devices/
        Microsoft/  NoXna/         cna_devices (21 + 14)
      NoXna/
        Graphics/                  cna_noxna  (4)
      GamerServices/
        Xna/  Internal/            CNA_GamerServices (35 + 1)
      Net/
        Xna/  Internal/            CNA_Net    (19 + 6)

Naming rule: `<Module>/Xna/` = XNA public-API implementation, `<Module>/Internal/` =
CNA::Internal engine parts, `<Module>/NoXna/` = NOXNA extension surfaces;
single-area modules stay flat. `Devices/Microsoft/` (not `Xna/`) because that
tree implements the WP-era `Microsoft::Devices` namespace, not `Microsoft::Xna`.
Renderer family grouping (modern/historical/diagnostic) was considered and NOT
introduced: no such classification exists anywhere in the build or docs today,
several backends have no unambiguous class (SOFTWARE, SKIA, GDI, MAGNUM), and a
new taxonomy would be exactly the arbitrary-aesthetics move the campaign brief
forbids. `Backends/<X>/` keeps the exact established directory names.

Move mechanics: 5 pure `git mv` commits (674/674 files R100 byte-identical),
then one path-update commit. The only sources whose content changed: 12 backend
files whose 16 src-rooted `#include "CNA/Internal/Backends/<X>/shaders/…"`
directives became includer-relative `#include "shaders/…"` (the form Bgfx/Llgl
already used) plus `examples/d3d9_shadercache_test.cpp` (re-rooted to
`Graphics/Backends/D3D9/shaders/…`). Comments/documentation inside sources:
untouched by construction (R100 everywhere else).

### 9.2 PROVEN — include hygiene after the moves

With the shader directives includer-relative, nothing in any configuration
resolves headers against a `src/` include root any more (mechanically verified
over every tracked TU/header in src/, tests/, examples/, tools/, main.cpp):

- every module's `PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src` include dir: removed;
- `cna_backend_graphics_common` INTERFACE include dirs narrowed to `include/`
  only — the implementation tree is no longer exposed to every consumer;
- the four backend-side `PRIVATE src` include dirs: removed;
- single documented exception: `cna_test_d3d9_shadercache` (D3D9Tests.cmake)
  gets `PRIVATE src/` scoped to that one target — it deliberately includes the
  D3D9 generated-shader header to audit the embedded bytecode inventory.

### 9.3 PROVEN — symbol-level edge audit + the one hardening fix

New permanent-methodology check this phase: an nm-based audit over the built
module archives maps every undefined symbol to its uniquely-defining module and
requires the defining module to be reachable from the referencing module
through DECLARED edges (direct deps expanded through PUBLIC-only edges — i.e.
never only through some other module's `$<LINK_ONLY:…>` private closure).

Finding: `FrameworkDispatcher::Update()` pumps
`Input::Touch::TouchPanel::{getTouchDeviceExistsProperty,Update}` (exactly as
FNA's `FrameworkDispatcher.Update()` does), giving cna_audio a direct symbol
edge into cna_input that previously resolved only through
media→graphics-core→`$<LINK_ONLY:cna_input>` — a link-order-shaped accident of
the kind the cycle-acceptance rule forbids. Fixed by declaring
`target_link_libraries(cna_audio PRIVATE cna_input)` (plain DAG edge; input has
no path back to audio; closure unchanged in practice). Audit result post-fix
(HEADLESS module set, backend + GamerServices/Net included): 35 cross-module
symbol-edge pairs, every one declared or PUBLIC-reachable — zero undeclared
edges; the audit also independently re-confirms the three declared cycle pairs
and that graphics-core's only backend reference is the single factory symbol.

### 9.4 Final cycle review (the three declared cycles)

**graphics-core ↔ input — ACCEPT_INTENTIONAL.**
Exact edges: `GraphicsDevice.cpp` → `TouchPanel::setDisplay{Width,Height}Property`
(3 sites), `TextInputEXT::{set,get}WindowHandleProperty` +
`Mouse::{set,get}WindowHandleProperty` (device create/reset/dispose);
`MouseCursor.cpp` → `Texture2D::{getFormatProperty,getWidthProperty,
getHeightProperty,GetData}` via the public `MouseCursor::FromTexture2D(const
Graphics::Texture2D&, int, int)` signature. Upstream reality: FNA's
GraphicsDevice writes `TouchPanel.DisplayWidth/Height` and
`Mouse.INTERNAL_BackBuffer*` directly (GraphicsDevice.cs:432-437/757-762) — in
XNA both subsystems are one assembly and mutually aware by design. Candidates
examined and rejected:
  (a) static-registration metrics sink — display updates silently vanish when
      the input TU is not pulled from the archive (symbol-driven archive
      semantics; gc-sections/LTO worsen it), and it inverts FNA's data flow;
  (b) relocate the propagation to runtime — breaks fidelity for standalone
      GraphicsDevice use (no Game object), which FNA supports;
  (c) one-file `MouseCursor` module — the artificial single-TU split the brief
      forbids;
  (d) neutral windowing-state holder module — moves TouchPanel's own property
      state out of input; conceptually worse, and a mini-"common" module.
Cost of keeping: none beyond the declared 2-target cycle (CMake repeats the
archives); no native-SDK leakage; input's public API already references
Texture2D so no consumer exists that could want input without graphics.

**graphics-core ↔ selected renderer — ACCEPT_INTENTIONAL.**
Forward edge is ONE symbol: `CNA::Internal::Backends::CreateGraphicsBackend()`
(declared in the Common contract header, defined by exactly the configured
backend, called at GraphicsDevice.cpp:2391). This already is dependency
inversion done correctly for static archives: the interface lives in the
contract layer, the implementation is injected at configure time, and the
explicit call is the anchor that guarantees the backend archive participates
under single-pass linkers on every toolchain (GNU ld, MinGW, MSVC, wasm-ld).
Replacing it with self-registration removes the only undefined-symbol anchor —
the registrar object never gets pulled from the archive and device creation
fails at runtime; the repairs (per-toolchain whole-archive flags across 41
identities, or a reference anchor) are strictly worse or reintroduce the same
edge renamed. Reverse edges (Effect::Apply, VertexDeclaration vtable,
CNA::Logger, named math/colour values, DxtUtil/Bc7Util) are genuine shared
implementations; duplicating them per-backend or splitting them out of the
cohesive graphics cluster was already investigated and rejected (§6). The hard
invariant holds and is re-proven on the new layout: graphics-core depends only
on the one selected `${BACKEND_TARGET}`; per-configure closures carry exactly
the selected backend + its SDK family (probe/closure gates green on OPENGLES,
HEADLESS — incl. `GraphicsNativeSdkFree` — and VULKAN, §9.6; the nm audit
counts exactly one graphics-core→backend symbol reference: the factory).

**audio ↔ media — ACCEPT_INTENTIONAL.**
CNA's `FrameworkDispatcher.cpp` is a line-by-line port of FNA's
FrameworkDispatcher.cs: the `ActiveSongChanged`/`MediaStateChanged` flags are
dispatcher members that MediaPlayer writes back (upstream design), Update()
pumps `MediaPlayer::Update()` unconditionally and fires the flag-driven events,
while MediaPlayer plays through the audio mixer path — audio↔media in both
directions by upstream architecture. A lazy-registration bridge (MediaPlayer
registers its pump on first state use; flags relocate into media) would
preserve observable semantics but relocates upstream-placed state, adds two
global hook seams with first-call ordering subtleties under the strict
init-order sanitizer regime, and buys nothing consumers can observe: content
already links media, runtime links media, and XNA never shipped audio without
media in the assembly. The bridge is more artificial than the cycle; the brief
says retain and document in exactly that case.

No fourth link-level cycle exists. [EVIDENCE: edge audit + build.]

### 9.5 PROVEN — no-loss reconciliation (Phase 2)

Full mapping reconciliation of the Phase-1 production inventory (1357 files)
against the final tree: **683 PRESERVED** (same path, same hash — the entire
`include/` tree), **662 MOVED byte-identical**, **12 MOVED_WITH_REQUIRED_EDIT**
(the shader-directive files; per-file old-blob/new-blob diffs show zero
non-`#include` changed lines), **0 missing, 0 unexpected, 0 removed**. Commit
evidence: 674/674 moved files were R100 across the five move commits; the
final `git diff -M` vs the Phase-1 head shows 662 R100 + 12 R09x + 19 modified
build/tooling files (5 cmake, 12 scripts, 1 tools CMakeLists,
1 examples TU) + 0 additions + 0 deletions. `api-decls.tsv` and
`files-tests.tsv` are byte-identical to the Phase-1 snapshots. OPENGLES ctest
registration: 6537 names = pristine baseline 6526 + exactly the 11 Phase-1
module gates; zero removals/renames. Real build-target names (HEADLESS
configure): 107, identical to the Phase-1 snapshot. The source-partition
validator passes on the new layout and directory-GLOB ownership makes physical
location itself the ownership statement.

### 9.6 PROVEN — retakes on the new layout

- **OPENGLES (principal corpus):** full tree rebuilt from the new layout; all
  11 module gates pass; full-suite A/B against the preserved pristine Phase-1
  binary (`CnaTests-pristine-opengles`, identical command, Xvfb, dummy audio,
  CWD = repo root): both run 6218 tests with identical per-test outcome sets —
  6211 pass + 6 skip (same six conditional skips) + 1 fail, and the single
  failure is a diagnosed A/B-harness environment interaction, not a code
  difference: this session exported the canonical `SDL_AUDIO_DRIVER=dummy` in
  addition to the legacy spelling, and `audio_no_hardware_harness` forces only
  the legacy `SDL_AUDIODRIVER` — the surviving canonical hint opens the dummy
  device, so the expected NoAudioHardwareException never throws. Under
  Phase 1's exact env (legacy spelling only) the test passes on BOTH binaries.
  Net result: byte-equal parity, 6212 effective passes as in Phase 1.
- **HEADLESS (canonical):** rebuilt; full ctest = 6130 registrations (pristine
  control 6118 + the 12 module gates, by name); `-j4` failures reduce, after
  the serial rerun of the known ENet/audio-timing flake families, to exactly
  the control's 2 accepted deterministic residuals (REMED-GFX-133
  `SetRenderTargets_FourTargets`, `Headless_Smoke`).
- **VULKAN (native family):** full tree rebuilt from the new layout; focused
  run against the configured `CNA_TEST_DISPLAY` Xvfb: 222 registrations
  (211 `Vulkan_*` + the 11 module gates), 221 pass; the single failure is
  `Vulkan_DepthBias` failing exactly the Phase-1-recorded arm
  (`DepthBias=-1e6 (flat)` constant-bias, 3/4 arms green) — the accepted
  pre-existing llvmpipe/no-DRI3 environmental residual, unchanged.
  `ModuleLinkClosure_probe_graphics` passes on this configure, re-proving the
  selected-backend-only closure (backend archive + libvulkan, no other native
  SDK) after the physical move.
- **Sanitizers:** the HEADLESS `address,undefined,float-cast-overflow` Debug
  tree rebuilt from the new layout (`ldd` confirms libasan+libubsan); all five
  module probes run clean under
  `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:check_initialization_order=1:strict_init_order=1`;
  the strict curated corpus (identical Phase-1 filter) = **650 ran, 647 pass +
  3 skip + 0 fail, exit 0, zero sanitizer reports** — the REMED-GFX-221
  initialization-order coverage retained and green. Zero NEW CNA-originating
  findings; the pre-existing P6-recorded findings (Net harness-child leak,
  `Vector3::GetHashCode` non-finite-hash UB) were not re-opened.
- **Modular sharp-runtime seam:** the HEADLESS × merge-preview tree
  (`CNA_SHARP_RUNTIME_ROOT` → the read-only preview worktree, base `e8340b33` +
  develop `1e51c2d8`) rebuilt green from the new layout; 12/12 module gates
  pass; the Decimal XNB reader tests are present identically to the
  monolithic-runtime tree (5 = 5 under `*Decimal*` — the INT128 define port
  still holds). The remediation branch HEAD moved to `832726e0` during this
  session, but its module registry is byte-identical to `e8340b33`
  (§9.7), so the preview remains architecturally current.
- **Incidental pre-existing finding (recorded, not fixed):** running `CnaTests`
  with a CWD other than the repo root leaves the `tests/assets/…` fixture tree
  unresolvable; `MediaLibraryTestFixture` then drives the empty-media-library
  state into a pre-existing index-out-of-range throw plus a segfault in
  `ObjectGraphIsInternallyConsistent` — reproduced identically with the
  pristine Phase-1 binary, and absent under ctest (which runs tests with the
  correct working directory). A future MediaLibrary robustness ticket of its
  own, out of Phase-2 scope.

### 9.7 sharp-runtime status (2026-08-10)

Remediation branch `claude/remediation-batch-1804-namespace-b1yjh5` observed at
`832726e014b41685ac47db1bfdbe1de07502806e` (moved accee955→832726e0 during this
session; still active). The module registry
(`cmake/SharpRuntimeComponents.cmake` + `SharpRuntimeModules.cmake`) is
byte-identical between the Phase-1-studied `e8340b33` and today's HEAD — the
CNA dual-mode seam and per-module component sets remain valid without change;
the 33 files that did change are audit tests/docs/source fixes. Merge gate:
OPEN (no owner-accepted checkpoint exists; the branch received a commit hours
before this session's check). sharp-runtime develop remains untouched at
`1e51c2d8`. The `NetworkSessionProperties::operator[]` ↔ `ElementReference<T>`
merge-time adaptation stays documented (§ P5) and deliberately unapplied while
the branch API is still moving.

**CLOSED later on 2026-08-10** (owner decision: integrate the current snapshot
now, without waiting for the post-audit campaign to finish). The exact snapshot
`7888a29f` of the remediation branch was merged into sharp-runtime `develop`
with a history-preserving GPG-signed merge commit `81624983` — five textual
conflicts resolved semantically (FINAL-STAB-001's int128 probe published on the
`sharp_runtime_headers` INTERFACE; Decimal.cpp leaving the Core.Base source
list without native `__int128`; BitConverter keeping both the audit bounds
checks and the int128 guard; README/NEXT reconciled) — validated
(validators green; 0-warning build; 16,341/16,341 tests across 37 executables;
i686 MinGW boundary built and run under Wine with Decimal.cpp proven excluded;
selective components 10/10) and pushed: `origin/develop == 81624983`. The
remediation branch was not touched and keeps advancing (observed `5c8e057f`,
3 commits beyond the integrated snapshot); later increments merge separately.
The P5 adaptation is now APPLIED on `feature/sharp-runtime-modular-adaptation`
— see the NEXT.md top section for the retaken CNA matrix.

---

## 10. PROMOTION — modularization merged into `develop` (2026-08-10)

Promotion is a ref movement, not a code rewrite. `develop` `5f2c4e941` was
fast-forwarded to the accepted head with `git merge --ff-only
feature/modularization`; no merge commit was created and none was permitted as a
fallback.

- develop HEAD `5f2c4e941` → **`41028e99564838a2f7a8557fd75cf4074fdf56b9`**
- develop tree **`d2a9ea2654653989f117a2956ecb50c4a39957c1`** — identical to the
  accepted `feature/modularization` tree, which is the proof that the promotion
  changed no content
- merge-base `5f2c4e941`; 0 behind / 20 ahead; `git merge-tree` produced the
  target tree with zero conflicts before the merge was attempted
- all 20 campaign commits (11 Phase-1 + 9 Phase-2) retained, every one GPG-signed
  by `FB9CE8E20AADA55F`; `git diff --check` over the whole range is clean

### 10.1 Owner-local preservation (no `git stash`)

The `develop` worktree carried four pre-existing owner-local items: `+4` lines in
each of `cmake/Tests/EasyGLTests.cmake` and `cmake/Tests/SdlRendererTests.cmake`
(an ad hoc xvfb screenshot-demo registration), plus untracked `AGENTS.md` and
`examples/xvfb_screenshot_demo.cpp`. The two tracked files are also touched by
this campaign (the `--start-group` collapse), so the fast-forward would have
overwritten them.

They were captured first in the signed snapshot commit `1e63f865` on the
**unpushed** branch `owner/pre-develop-promotion-20260810` (parent `5f2c4e941`,
containing exactly those four files and nothing else), then restored to `HEAD`
for the merge and re-applied to the worktree afterwards — the owner's edits and
the campaign's edits are in disjoint hunks. The earlier
`owner/pre-develop-promotion-20260809` snapshot still byte-matches the two
untracked files but no longer matches the two tracked ones, which is why a new
snapshot was taken. No owner-local work entered the modularization history.

### 10.2 PROVEN — bounded post-promotion gate

The full Phase-2 matrix was deliberately **not** rerun; the branch name changed,
the tree did not. Re-run from the promoted tree:

- **Source partition:** HEADLESS, OPENGLES and VULKAN configures all succeed, so
  the configure-time validator finds no unowned or doubly-owned production TU in
  any of the three.
- **Module gates:** 12/12 HEADLESS (5 probes + 5 closures + the HEADLESS-only
  `ModuleLinkClosure_GraphicsNativeSdkFree` + identity), 11/11 OPENGLES, 11/11
  VULKAN. `ModuleLinkClosure_probe_math` (math-only) and
  `ModuleLinkClosure_probe_graphics` (selected-backend-only, incl. on VULKAN)
  both green.
- **Renderer identities:** `RendererIdentityRegistry` green on all three configures
  and `scripts/check_renderer_identities.py` reports 41 preserved in both
  registries.
- **Registrations:** 6130 HEADLESS / 6537 OPENGLES / 6434 VULKAN — unchanged from
  the accepted Phase-2 numbers.
- **HEADLESS suite:** `-L Headless` = 48 tests, 47 pass + 2 skip + 1 fail
  (`Headless_Smoke`, the accepted primitive-range residual). The preserved
  pristine pre-modularization control build reproduces that same failure
  identically, so it is not promotion-attributable.
- **OPENGLES/EasyGL suite:** the 293-test `EasyGL_*` family on a deterministic
  Xvfb = 291 pass, 1 skip, 1 fail — `EasyGL_GraphicsDevice_ReferenceStencil`, the
  pre-existing documented known failure Task 872 (AUDIT.md:128), whose source is
  byte-identical across the whole modularization range. The first pass of this
  suite ran against the owner's live `:0` desktop and showed seven failures; six
  of them pass when re-run on a clean display (occluded default-framebuffer
  readback), and the seventh is that same Task 872 failure.
- **No-loss:** `modularization/tools/capture_inventory.py` re-run on the promoted
  worktree reproduces the accepted `after-phase2-layout` snapshots byte for byte —
  1357 production files, 1300 API declarations, 483 test files. Against the
  pristine baseline: `api-decls.tsv` is byte-identical (1300 = 1300); production
  is 1357 → 1357 with 674 moved paths (662 `R100` plus 12 whose complete diff is
  15 `#include` directives and zero other lines) and zero additions or deletions
  under `include/` + `src/`; tests 478 → 483, the five module probes, with no
  baseline test blob lost.

### 10.3 Gate status after promotion

The CNA side of P7 is met: reconciliation, docs and the develop promotion are
done. The sharp-runtime audit-remediation develop merge (§9.7) is a separate
external gate and remains **OPEN**; it does not make CNA modularization
incomplete. Observed read-only at promotion time (`git ls-remote`, nothing
fetched, nothing modified, nothing pushed): sharp-runtime `develop` still
`1e51c2d8`, `claude/remediation-batch-1804-namespace-b1yjh5` now `a431bc80`
(moved on from the §9.7 observation `832726e0`) and the sibling
`remediation-batch-1804-namespace-b1yjh5` at `44d5ed96` — the campaign is still
moving, so no accepted checkpoint exists and the merge was not attempted. The stable modularized public `develop` head is the base future work
must start from — FUTURE.md Phase 2 (renderer expansion) is unblocked but not
started and needs its own owner instruction.

*Update, later on 2026-08-10:* the §9.7 gate is now **CLOSED** (see §9.7's
closing note). Modular sharp-runtime is public on its `develop` (`81624983`)
and CNA's modular consumption is live via
`feature/sharp-runtime-modular-adaptation`; after that branch's promotion, the
resulting CNA `develop` head supersedes `60c363a7` as the base for FUTURE.md
Phase 2.

## 11. PHASE 3 — final physical module/package layout (2026-08-10, `feature/physical-modules`)

The owner clarified that the Phase-1/Phase-2 result — complete build modularization plus the
module-first `src/` regrouping — was still not the desired final architecture while the
repository kept the two global `src/` + `include/` trees. Phase 3 transforms the repository
into a module-oriented monorepo: every subsystem and every renderer implementation family
physically owns `modules/<name>/{CMakeLists.txt,include/,src/,tests/}`. Public include
spelling is byte-compatible (each module's `include/` root reproduces the `Microsoft/...` /
`CNA/...` paths), the accepted target graph is unchanged, and the former central manifests
(`cmake/CnaLibrary.cmake`, `cmake/BackendLibraries.cmake`) dissolved into per-module
`CMakeLists.txt` files composed by `modules/CMakeLists.txt`. The complete architectural
inventory (module -> targets/deps/roots, helper targets, cycles, validators) lives in
`docs/physical-modules.md`; the frozen per-file design decisions and the campaign evidence
live in `modularization/physical-modules/`.

Highlights, in the same OBSERVED/PROVEN discipline as §9:

- **Move mechanics.** 1776 tracked files moved from `include/` + `src/` + `tests/` into the
  module tree via 7 pure-rename commits (100% R100), driven by
  `modularization/tools/physical_move_map.py` (deterministic map, totality- and
  uniqueness-checked against base `ea61123e6`). Two files were subsequently reassigned with
  evidence (`IPackedVector.hpp` -> math: interface consumed by Color;
  `GraphicsBackendType.hpp` -> core: self-contained constexpr identity header, the accepted
  `probe_core` surface); `D3D9ShaderRegisters.hpp` moved into the d3d9 module's include tree
  repairing a Phase-2 latent defect (its `CNA/Internal/...` spelling had no physical file
  under any include root since the Phase-2 `src/` include-root removal — pre-existing,
  discovered mechanically, fixed with 4 directive-line edits).
- **Extension split.** The former `cna_noxna` implementation (4 TUs + `CNA/Graphics`
  headers) became `modules/graphics-ext` (`CNA::GraphicsExt`); the CNA-specific half of the
  devices sources (`src/Devices/NoXna`, 14 TUs + `CNA/Devices` headers) became
  `modules/devices-ext` (`CNA::DevicesExt`); `cna_devices` keeps the XNA-compatible
  Microsoft::Devices base. `cna_noxna` survives as an INTERFACE composition over both
  extension modules (CNA::NoXna umbrella; `probe_noxna` +
  `ModuleLinkClosure_NoXnaComposition` REQUIRE both extension archives). Input's NoXna
  surface stays inside the input module — it was never owned by `cna_noxna` and carving
  `cna_input` was not authorized.
- **Include-root distribution.** `cna_build_flags` no longer carries any include directory;
  each module exposes its own `include/` root PUBLIC, and the umbrella aggregates them by
  composition. The monolithic include tree had been hiding four real include-contract edges,
  now declared: math -> core-headers and storage -> core-headers (headers-only:
  NOXNA marker, PlayerIndex, PathContainment — `cna_core_headers` INTERFACE keeps both
  modules' accepted link closures byte-exact), media -> input (private; the
  FrameworkDispatcher pump surface — the input archive was already in every media link
  closure through audio's own private edge), and the 66 renderer headers that spelled the
  backend contract as `"../Common/IGraphicsBackend.hpp"` now use the canonical
  `CNA/Internal/Backends/Common/...` spelling resolved through the graphics module root.
- **Validators evolved.** The configure-time source-partition validator now enforces
  physical location == ownership over `modules/**` and rejects a resurrected legacy
  `src/`/`include/` root; `modularization/tools/check_include_reachability.py` proves every
  module include (transitively from every TU) resolves through the declared module graph;
  the probe fleet grew from 5 to 14 modules plus the NoXna composition, Net/ENet, HEADLESS
  native-SDK-free (graphics/content/graphics-ext/devices-ext) and VULKAN closure gates.

### 11.1 PROVEN — Phase-3 validation retakes (2026-08-10)

- **No-loss:** production 1357 → 1357 (1287 byte-identical moves, 70 directive-only edited
  moves, zero missing/added); API declaration set zero removed (+1: the relocated generated
  D3D9 register struct now counted under an include tree); tests 483 → 492 (zero missing,
  the 9 probes added); registered names HEADLESS 6120 → 6143 and OPENGLES 6527 → 6546 with
  **zero removals, zero renames**. `modularization/physical-modules/` +
  `modularization/tools/reconcile_phase3.py`.
- **HEADLESS:** full suite 6099 ran, 10 first-pass failures — all 10 pass on rerun
  (including the historically accepted `Headless_Smoke` and
  `SetRenderTargets_FourTargets` residuals); the FFmpeg-restored 69 video tests pass;
  the complete probe fleet + closure gates + `RendererIdentityRegistry` pass.
- **OPENGLES/EasyGL:** the 293-test family on the dedicated `:96` Xvfb = 290 pass + the
  documented accepted `EasyGL_GraphicsDevice_ReferenceStencil` failure (Task 872) + two
  late-family MSAA readback flakes that pass standalone (the known one-Xvfb flake class).
- **VULKAN:** the 211-test family = 210 pass + exactly the known `Vulkan_DepthBias`
  failure — byte-exact parity with the Phase-2 accepted run.
- **ASan+UBSan (Debug, address,undefined,float-cast-overflow, strict init order):** full
  6074-test corpus = 6025 pass; the 5 failures are exactly the baseline-known set (3×
  TwoProcessLoopback + GamerServicesDispatcherHangRegression under ASan, plus the known
  SetRenderTargets residual). Zero new sanitizer classes: the GetHashCode signed-overflow
  findings carry byte-identical arithmetic to the pre-campaign log, the
  Net/GamerServices member-access class fires inside the same baseline-known-failing
  subprocess test, and the exit leak profile matches (215502 B / 912 allocations vs
  214998 B / 913).
- **Header self-containment:** 542 public headers compile standalone against only their
  module's declared closure; the three documented skips are the FFmpeg-private
  VideoDecoder.hpp and the Windows-only Glide ABI loader pair.
- **Include reachability:** every `#include` in every module TU (transitive) resolves
  through the declared module graph (`check_include_reachability.py` clean).

### 11.2 PROMOTION — physical module layout merged into `develop` (2026-08-10)

Promotion is a ref movement, not a code rewrite. `develop` `ea61123e6` was fast-forwarded to
the accepted `feature/physical-modules` head `3ecbbce72`.

- **Relationship proven before the ref moved.** merge-base = `ea61123e6` = `develop`, so
  `develop` was an ancestor; 0 behind / 19 ahead; `git merge-tree --write-tree develop
  feature/physical-modules` produced exactly `a116280e0`, the accepted branch tree. The
  promotion therefore could not introduce content.
- **`git merge --ff-only feature/physical-modules`.** No merge commit; the resulting `develop`
  is `3ecbbce72` with tree `a116280e0` — equal to the accepted head and tree, verified
  immediately and before any documentation commit. All 19 commits kept their signatures
  (`git verify-commit` good on every one, key `255C69CC…0AADA55F`).
- **`feature/physical-modules` was not moved** and still points at `3ecbbce72`; the
  implementation branch keeps recording the implementation result.

#### 11.2.1 Owner-local preservation (no `git stash`)

The `develop` worktree carried the same four owner-local items as §10.1. They were captured
verbatim in a new **unpushed** signed snapshot `owner/pre-develop-promotion-20260810-physical-modules`
(`ea84f4537`, parent `ea61123e6`, so its diff is exactly the owner-local delta). A new snapshot
was required rather than reusing `owner/pre-develop-promotion-20260810`: that older snapshot
carries the same intent but different bytes for the two cmake files, because the Phase-1
promotion rewrote their base blobs (the `--start-group` collapse).

No re-application was needed after the fast-forward. `cmake/Tests/EasyGLTests.cmake` and
`cmake/Tests/SdlRendererTests.cmake` are byte-identical across `ea61123e6..3ecbbce72`, and
neither `AGENTS.md` nor `examples/xvfb_screenshot_demo.cpp` exists on the feature branch, so the
promotion touched none of the four paths; all four were verified byte-identical afterwards. The
owner-local intent still holds on the promoted tree: an OPENGLES configure of the promoted
`develop` worktree creates the ad hoc `cna_xvfb_screenshot_demo_easygl` target (the macro builds
an executable and deliberately registers no ctest test, matching the edit's own comment).

#### 11.2.2 PROVEN — bounded post-promotion gate

The full Phase-3 matrix (§11.1) was deliberately not rerun; only enough to prove the promoted
`develop` behaves like the accepted branch. Configure-time gates were run **from the promoted
`develop` worktree**; runtime gates were run on the byte-identical tree in the existing
`cmake-build-next-*` trees.

- **Physical ownership / source partition:** HEADLESS, OPENGLES and VULKAN all configure clean
  (0 CMake warnings, 0 errors), so the `modules/CMakeLists.txt` validator — unowned or
  doubly-owned production TU, and resurrected legacy root — passes on all three.
- **Legacy roots absent:** no `src/` or `include/` at the repository root; 0 tracked paths
  beneath either.
- **Module inventory:** 14 framework modules, each owning
  `CMakeLists.txt`+`include/`+`src/`+`tests/`; 38 renderer families plus
  `modules/renderers/common/d3d`.
- **Renderer identities:** `scripts/check_renderer_identities.py` = **41**;
  `RendererIdentityRegistry` green on all three configurations.
- **No-loss:** `reconcile_phase3.py` → **RECONCILIATION: OK** (production 1357 → 1357; tests
  483 → 492; api-decls 1300 → 1301 with zero declarations removed, the one addition being the
  relocated `D3D9ShaderConstantSlot`; ctest names HEADLESS 6120 → 6143 and OPENGLES
  6527 → 6546, zero removed and zero renamed). Independently, a fresh `capture_inventory.py`
  run against the promoted worktree is **byte-identical** to the committed
  `modularization/physical-modules/after/` captures for `files-production.tsv` (1357),
  `files-tests.tsv` (492) and `api-decls.tsv` (1301).
- **Include/header compatibility:** every module `include/` root exposes only the public
  `CNA/` and `Microsoft/` namespace paths, so consumer include spelling is unchanged;
  `check_include_reachability.py` clean; header self-containment 542 checked / 540 pass. The
  2 failures are the Windows-only Glide ABI pair (`GlideAbi.hpp`, `GlideAbiLoader.hpp`, which
  include `<windows.h>` for `HMODULE` loading) — they are in the script's own `SKIP` set but
  re-added by its `EXTRA_PORTABLE` pass, a pre-existing script quirk, and the script blob is
  identical on both refs, so this is host-inherent and not promotion-attributable.
- **Module/minimal-link probe fleet:** **35/35** HEADLESS, **30/30** OPENGLES, **31/31**
  VULKAN — covering every module probe and link closure, the SharpRuntime component closures,
  `ModuleProbe_probe_devices_ext` / `ModuleProbe_probe_graphics_ext` /
  `ModuleLinkClosure_NoXnaComposition`, `ModuleLinkClosure_NetHasENet`, the four HEADLESS
  native-SDK-free gates and `ModuleLinkClosure_VulkanRendererClosure`. All three builds
  completed with 0 errors and 0 warnings.
- **Representative suites:** HEADLESS `-L Headless` 48 = 45 pass + 2 skip + the accepted
  `Headless_Smoke` residual; EasyGL 293 on the dedicated `:96` Xvfb = 291 pass + 1 skip +
  exactly the documented `EasyGL_GraphicsDevice_ReferenceStencil` failure (Task 872); VULKAN
  211 = 210 pass + the accepted `Vulkan_DepthBias` llvmpipe residual, after the single `-j4`
  contention flake `Vulkan_BoundTargetLifetime` passed standalone.
- **D3D common graph:** `cna_backend_graphics_d3dcommon` is linked by the d3d11 and d3d12
  families only; d3d10 has no build-level reference to it and d3d9 documents its independence
  and keeps its own `cna_backend_graphics_d3d9_effect` sub-target.
- **`git diff --check`** clean, both over the promoted range and over the worktree.

#### 11.2.3 Campaign status after promotion

The CNA modularization campaign is closed: target modularization COMPLETE, physical
module/package modularization COMPLETE AND PROMOTED, modular sharp-runtime consumption ACTIVE
AND PUBLIC (sibling `develop` `81624983`, clean and unmodified by this promotion). The
sharp-runtime post-audit remediation continues independently on its own branch and merges in
later increments — it does not make CNA modularization incomplete. The next CNA phase is the
FUTURE.md renderer expansion (41 → 55), which has **not** begun and needs its own owner
instruction; its common base is the public `develop` head produced by this promotion.

**Superseded base (2026-08-10).** Two further owner-directed pre-expansion campaigns were
accepted and promoted after this section was written — the renderer terminology normalization
(`feature/renderer-naming-normalization`, endpoint `16f76cf1a`) and the module-owned examples
(`feature/module-examples`, endpoint `675e04c7a`). Neither changes the module architecture
recorded above, but both moved `develop`. The renderer-expansion base is therefore the public
`develop` head produced by that later pre-renderer-expansion promotion, not `3ecbbce72`; see
`NEXT.md` and `FUTURE.md` for the authoritative value.
