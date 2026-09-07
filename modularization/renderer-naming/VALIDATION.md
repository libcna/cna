# Renderer-naming normalization — validation ledger

Base `25db3ccbe` → branch `feature/renderer-naming-normalization`.
Build trees on `build-probe/` (`cmake-build-next-*`,
`cmake-probe-selector`, logs `naming-*.log`, `selector-probes/`); ccache
30 G shared; every build capped at `-j4` under `nice -n 10`.

## Registry / selector gates

- `scripts/check_renderer_identities.py` — **OK: 41 public renderer identities
  preserved in both registries** (enum + STRINGS) after every pass.
- All-selector configure probe (`tools/probe_selectors.sh`,
  `selector-probes/*.log`): **41/41 new selectors accepted** — 24 reach
  `CNA: Using <sel> …` on this Linux host, 16 stop at their correct
  platform/toolchain gate naming the new identity (DIRECTX1..DIRECTX12 +
  DIRECT2D/GDI/GLIDE Windows-only, WEBGL1/WEBGL2/CANVAS/HTML_DOM
  Emscripten-only, METAL macOS-only), FREEDIRECT/OPENGL2 confirmed accepted by
  their `Using` lines (message wording differs from the classifier pattern).
  **12/12 old selectors (`DX1..DX8`, `D3D9..D3D12`, `OPENGLES`) fail as
  `Unknown graphics renderer`** — absent from the selection API.
- `OPENGLES2` does not exist anywhere (deliberately not created).

## No-loss reconciliation (tools/reconcile_naming.py) — **OK**

- 3,521 baseline production/test/build-tooling files: **3,509 byte-identical**
  to the committed engine's mechanical replay, **12 enumerated manual edits**
  (listed in the tool), **564 path moves**, 0 missing, 0 blocking, 0
  unexplained additions in the captured categories.
- Public API declarations: **1,301 rows before → 1,301 rows after** (1,243
  distinct tuples each side); 135 declaration names renamed (8 in place, 127
  together with a file move), 335 rows moved file only, 773 untouched;
  **0 unexplained missing, 0 unexplained added**.
- ctest registrations (configure-time): 88 baseline names → 3 renamed
  (`NOXNA_Settings_Compile_Run` → `CNAEXT_…`, `ModuleProbe_probe_noxna` →
  `…_cnaext`, `ModuleLinkClosure_NoXnaComposition` → `…CnaExtComposition`),
  87/87 real names present after (the 88th is the `CnaTests_NOT_BUILT`
  placeholder of the unbuilt baseline listing). gtest-case names are covered
  by the file-level byte-identity above; the full-corpus diff
  (`after/ctest-headless-reconcile.txt`) shows the 98 mechanical renames and
  confirms input/devices suites correctly kept their own Backend vocabulary.

## Terminology / macro gates

- Compound graphics tokens (`GraphicsBackend`, `CNA_BACKEND_`,
  `cna_backend_graphics`, `GRAPHICS_BACKEND`): **0 occurrences** in active
  scope (single remaining file: `NEXT.md`'s preserved 2026-08-10 promotion
  record — historical evidence).
- Standalone `backend` words in active scope: 612 total — 600 in the
  input/devices/net/audio subsystems and their docs (their own backend
  concepts), 12 in audio-backend prose (media module + CHECKLIST audio row).
  Zero graphics-renderer-meaning "backend" remains.
- `NOXNA`: **0 active occurrences** (uppercase token count in active scope is
  zero); remaining lowercase `noxna` occurrences are citations of the
  historical ledgers `input_noxna.md`/`input_noxna_progress.md`/
  `noxna_devices.md`, preserved verbatim.
- Upstream vocabulary intact: Skia `GrBackend*`/`WrapBackendRenderTarget`,
  sokol `sg_*backend*`/`SG_BACKEND_*` + `CNA_SOKOL_API` values, bgfx
  `RendererType::OpenGLES` (+ its name string), DiligentCore engine names.

## Build/test matrix

- **HEADLESS** (`cmake-build-next-headless`): configure + full build green
  after passes A, B1 and C. Full corpus: **6,161 tests, 6,154 pass**;
  failures = `Headless_Smoke` (pre-existing abort, identical in
  `phys-headless-ctest.log`/`next-headless-ctest.log`),
  `GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow`
  (pre-existing, identical in both physical-modules campaign logs), plus 5
  load-flaky audio/net tests that pass on serial rerun (also flaky in prior
  campaign logs). No new failures versus the physical-modules baseline.
- **CNAEXT matrix**: default builds are CNAEXT-off. `-DCNA_CNAEXT=ON
  -DCNA_DEVICES=ON` configures green and builds `probe_cnaext` +
  `cna_example_cnaext_settings` (`naming-c-cnaext-on.log`, exit 0) — the gate
  compiles the same graphics-ext extension surface `CNA_NOXNA` used to.
  Strict-marker semantics: see harness result below.
- **Include reachability** (`modularization/tools/check_include_reachability.py`,
  tables updated to renderer/DIRECTX names): **OK — every module include
  resolves through the declared module graph** (module graph unchanged).
- **OPENGLES3** (`cmake-build-next-opengles`, `-DCNA_GRAPHICS_RENDERER=OPENGLES3`,
  `CNA_TEST_DISPLAY=:96` + Xvfb): configure selects the EasyGL family exactly
  as OPENGLES did; full build green (`naming-opengles3-build.log`, exit 0),
  6,564 tests registered. EasyGL family corpus (293 tests): **291 pass**;
  `EasyGL_GraphicsDevice_ReferenceStencil` failed with the byte-identical
  signature already present in `phys-easygl-family.log` (pre-existing), and
  `EasyGL_MsaaMipReadback` is the documented long-run Xvfb readback flake —
  passes standalone here exactly as in `phys-easygl-msaa-rerun.log`.
  Configure-time registrations reconcile 331/331 (2 renames, `CnaTests_NOT_BUILT`
  placeholder aside).
- **VULKAN** (`cmake-build-next-vulkan`, `-DCNA_GRAPHICS_RENDERER=VULKAN`,
  `CNA_TEST_DISPLAY=:96`): reconfigure + full build green (queue log, exit 0),
  6,462 tests registered. Vulkan family (211 tests, serial): **210 pass**;
  the only failure is `Vulkan_DepthBias`, whose identical
  `DepthBias=-1e6 (flat)` case already fails in the pre-rename control run
  (`vk-control-depthbias-run.log`, 3/4) and in `phys-vulkan-family.log` —
  pre-existing environment behavior, not a rename effect. The known-flaky
  `Vulkan_BoundTargetLifetime` passed serially. Configure-time registrations
  reconcile 246/246 (2 renames, placeholder aside).
- **ASan+UBSan** (`cmake-build-next-headless-asan`,
  `CNA_SANITIZE=address,undefined,float-cast-overflow`, HEADLESS): reconfigure
  + full instrumented build green; representative subset (module probes/link
  closures + graphics core suites, 209 tests serial): **208 pass**, zero
  sanitizer diagnostics; the one failure is the same pre-existing
  `SetRenderTargets_FourTargets_DoesNotThrow` assertion, identical to its
  uninstrumented pre-existing behavior.
- **DIRECTX MinGW cross-builds** (`cmake-build-next-directx{11,12,9,5}`,
  `cmake/toolchains/mingw-w64.cmake`; zlib for the mingw target staged once at
  `~/deps/zlib-mingw` and passed as explicit `ZLIB_LIBRARY`/`ZLIB_INCLUDE_DIR`
  because sharp-runtime's modular io-compression component now requires ZLIB
  and the base tree fails identically without it — host gap, proven at base):
  - all four configures accept the new selectors and print
    `CNA: Using DIRECTX<N> graphics renderer` (exit 0);
  - **DIRECTX5: full cross build green (exit 0)** — framework, renderer, all
    example executables and the complete CnaTests corpus compile and link;
  - **DIRECTX11/DIRECTX12**: framework + renderer libraries (incl.
    `cna_renderer_d3dcommon`) build green; example executables fail compiling
    family headers that include `CNA/Internal/Renderers/D3DCommon/...` —
    the family links d3dcommon PRIVATE, so its PUBLIC include never reaches
    example targets. Base-identical: the base tree links
    `cna_backend_graphics_d3dcommon` PRIVATE the same way, and these example
    targets were never cross-built after the physical modularization dissolved
    the global include tree. Empirically replayed at base: the pristine
    develop tree fails identically
    (`.../Backends/D3DCommon/D3DShaderCache.hpp: No such file or directory`,
    `base-cross-d3d11-smoke.log`);
  - **DIRECTX9**: libraries (incl. `cna_renderer_d3d9_effect`) and most
    example executables link green (`cna_test_directx9_smoke` etc.);
    `cna_test_directx9_hresult` fails at link on
    `Mouse/TextInputEXT::set/getWindowHandleProperty` — the unguarded
    `GraphicsDevice` → input window-handle reverse edge exceeds the declared
    static-cycle repetition (2×) for this target's object-demand order.
    Base-identical by construction: the cycle declarations, multiplicities and
    the unguarded call site are byte-identical at base modulo mapped renames
    (`tools/reconcile_naming.py`), and no D3D example target was linked
    post-modularization before this campaign.
  These two example-level gaps are pre-existing physical-modules-era items,
  not naming effects, and are left for the owner (fixing them means changing
  link visibility/multiplicity — out of scope for a naming campaign).
  The engine gained two B1 rules from this gate (bare `D3D<N>::` family-
  namespace qualifiers; `examples/dx<N>_*` content references) — commit
  42f40cc48.
- **CNAEXT strict-marker harness** (probe tree, `-DCNA_CNAEXT=ON
  -DCNA_DEVICES=ON`): `StrictXnaApiSurfaceCheck_Compile_Run` **passed** (the
  strict TU compiles and runs while avoiding CNAEXT members) and
  `StrictXnaApiSurfaceLeakCheck_MustFailToCompile` **passed** (a deliberate
  CNAEXT-tagged call still fails to compile under `CNA_STRICT_XNA_API` +
  `-Werror=deprecated-declarations`) — the renamed marker preserves the exact
  NOXNA on/off semantics in both directions, and the runtime trio
  `ModuleProbe_probe_cnaext` / `ModuleLinkClosure_CnaExtComposition` /
  `CNAEXT_Settings_Compile_Run` is 3/3.
- **FREEDIRECT control** (unchanged identity): configure green and
  `cna_renderer_freedirect` builds against the `../free-direct` sibling
  (exit 0). **DIRECT2D control** (unchanged identity): configure probe reaches
  its correct Windows-only gate.
- **Header self-containment**
  (`modularization/tools/check_header_self_containment.py`, defines updated to
  `CNA_RENDERER_HEADLESS`/`CNA_CNAEXT`): **542 public headers checked, 2
  failures — exactly the documented Windows-only Glide ABI pair**, the same
  host-inherent Linux result the physical-modules campaign recorded (its
  EXTRA_PORTABLE pass bypasses its own SKIP set). No new failures.
- **Generated/shader surfaces**: the d3d9 vendored `.fx` stock shaders and
  `gradlew.bat` are byte-identical to base (binary/CRLF-safe reconciliation);
  bgfx embedded shader header keeps its upstream `RendererType::OpenGLES`
  rows; no shader generation inputs changed.
- **git diff --check** over the full range: clean. All campaign commits GPG
  `Good signature`. Nothing pushed.

