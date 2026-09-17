# Renderer set curation: retiring 25 renderer identities

CNA carried 50 public renderer identities. This workstream retires exactly 25 of them, leaving a
curated set of 25, and removes everything that existed only for the retired ones — implementations,
CMake selection, runtime registry entries, C ABI constants, dependencies, patches, tests, CI jobs,
scripts and live documentation — without renumbering a single surviving C ABI value.

Task IDs: `RRC-001`, `RRC-002`, … . The status table is the source of truth for this plan.

**Policy this plan puts in writing.** CNA intentionally maintains a curated renderer set. New
renderers are added only when they provide meaningful platform coverage, compatibility value,
architectural value, or a capability not reasonably covered by the existing renderer set. A high
renderer count is not a goal.

## Status

| ID | Task | Status |
|---|---|---|
| RRC-001 | Baseline, repository-wide audit, this plan | ✅ |
| RRC-002 | Retire the 25 identities from the registry, CMake selection and C ABI; reject retired selectors explicitly | ✅ `fc1b6a537` |
| RRC-003 | Remove the 25 implementation families and everything that existed only for them | ✅ `fc1b6a537` |
| RRC-004 | Tests, test infrastructure and CI | ✅ `643b587d5` |
| RRC-005 | Live documentation, historical banners, tombstones, curated-set policy | ✅ `1321ca346` |
| RRC-006 | Regression guard: exact 25-identity whitelist and permanently retired IDs | ✅ `d1a579e00` |
| RRC-007 | Second pass: stale-reference audit and code-quality cleanup | ✅ `750ed854d` |
| RRC-008 | Build matrix, negative configure matrix, full test corpus, closing report | ✅ |

---

## RRC-001 — Baseline and audit

| Fact | Value |
|---|---|
| Branch | `next` |
| Baseline HEAD | `c0225e9d570a1451233ed1a018e4e9c0ac9089f6` (merge of `graphics-shared-cleanup`), clean tree |
| Public identities | 50 (`GraphicsRendererType`, `CNA_GRAPHICS_RENDERER` STRINGS, `cmake/RendererRegistry.cmake`) |
| Implementation families | 46 (`modules/renderers/<family>/src`; EasyGL serves five identities) |
| C ABI | `0.27.0`, `CNA_GRAPHICS_RENDERER_MAXIMUM` = 51 (`RLGL`), value 19 (`SKIA`) already retired |
| Test tree | `cmake-build-multi` (OPENGL33 default; VULKAN, SOFTWARE, HEADLESS compiled in; X11 platform; RelWithDebInfo), built at the baseline HEAD, 9 510 registered tests |

### Precedent

This is the second retirement in the registry's history and follows the first one's conventions
rather than inventing new ones:

- 2026-08-30: eleven identities removed (`b6b275cb0` … `87cc390ff`, ABI `0.20.0`); ten of them restored
  on 2026-09-04 (`44f0cd16c`, ABI `0.22.0`); `SKIA` stayed retired and its number 19 is a gap.
- What that series had to repair afterwards is the checklist for this one: the `std::array` extent in
  `CnaCApiCoreExt.cpp`, `MAXIMUM` compared against the table *size* instead of the highest live value,
  bare enumerators inside `CNA_RENDERER_IS(...)` lists (hard compile errors, not dead text),
  hand-written `std::array` extents in the glTF policy tables (value-initialised tails that segfault),
  the `check_cnaext_matrix.py` tripwire, and the identity-count `static_assert`s in three test files.
- `docs/removed-renderers.md` is the tombstone register: identity, enum, C ABI value, family, removal
  commit, size, the exact pinned dependency, and what the renderer proved.

### The 25 retired identities

Sizes are the family directory only (`git ls-files modules/renderers/<family>`).

| Identity | Enum | C ABI | Family | Files | Lines | Dependency (as pinned at removal) |
|---|---|---:|---|---:|---:|---|
| `BGFX` | `Bgfx` | 7 | `bgfx` | 152 | 51 260 | `bkaradzic/bgfx.cmake` @ `572868c0cb952add48019d267223453958e958b8` + `cmake/patches/bgfx-max-render-target-msaa.patch` (FetchContent) |
| `MAGNUM` | `Magnum` | 10 | `magnum` | 30 | 9 598 | `mosra/corrade` @ `783e4e4807536ec52c352986fc9317db986ace96`, `mosra/magnum` @ `5a7424643bfd4621fbcff8c361d37795502cf890` |
| `BLEND2D` | `Blend2D` | 20 | `blend2d` | 16 | 4 063 | `blend2d/blend2d` @ `def0d1238c3e5d0983bb848e5676049d829e435b`, `asmjit/asmjit` @ `b56f4176cb9b0c0501da659ac54d4c5877862c7b` |
| `DIRECTX1` | `DirectX1` | 23 | `directx1` | 14 | 3 130 | MinGW `ddraw` + `dxguid` import libraries; Wine at run time |
| `DIRECTX2` | `DirectX2` | 24 | `directx2` | 23 | 5 549 | same |
| `DIRECTX3` | `DirectX3` | 25 | `directx3` | 23 | 5 570 | same |
| `DIRECTX5` | `DirectX5` | 26 | `directx5` | 23 | 5 569 | same |
| `DIRECTX6` | `DirectX6` | 27 | `directx6` | 24 | 5 829 | same |
| `DIRECTX7` | `DirectX7` | 28 | `directx7` | 24 | 5 813 | same |
| `DIRECTX8` | `DirectX8` | 29 | `directx8` | 24 | 5 212 | DXVK's `d3d8.dll.a` (`/usr/lib/dxvk/wine64`), DXVK/D8VK at run time |
| `DIRECTX10` | `DirectX10` | 30 | `directx10` | 11 | 2 740 | MinGW `d3d10`, `dxgi`, `d3dcompiler`; Wine `d3d10.dll` over DXVK `d3d10core` |
| `OPENGLES1` | `OpenGLES1` | 32 | `opengles1` | 12 | 6 034 | system `GLESv1_CM` library and `GLES/gl.h` |
| `OPENGL1` | `OpenGL1` | 34 | `opengl1` | 33 | 5 598 | system OpenGL (`OpenGL::GL`) |
| `OPENGL2` | `OpenGL2` | 35 | `opengl2` | 53 | 12 838 | system OpenGL (`OpenGL::GL`) |
| `WICKED` | `Wicked` | 36 | `wicked` | 9 | 6 991 | `turanszkij/WickedEngine` @ `27c0df160d738925474a2181d3f88bfd59edaefe` + four `cmake/patches/wicked-*.patch` |
| `SOKOL` | `Sokol` | 37 | `sokol` | 29 | 25 822 | `floooh/sokol` @ `27b49604b19be8cee0dcc6b2bbfe803dd9517585` |
| `DILIGENT` | `Diligent` | 38 | `diligent` | 40 | 15 209 | `DiligentGraphics/DiligentCore` @ `v2.5.6` |
| `GLIDE` | `Glide` | 39 | `glide` | 34 | 6 786 | caller-supplied `glide3x.dll` at run time (dgVoodoo2 not redistributable); i686 toolchain |
| `LLGL` | `Llgl` | 41 | `llgl` | 91 | 27 011 | `LukasBanana/LLGL` @ `Release-v0.04b` |
| `OPENVG` | `OpenVg` | 45 | `openvg` | 17 | 3 251 | `ileben/ShivaVG` @ `6122ccb3c4b86f69a326f1a65b0f86bc79f69c50` + `cmake/patches/shivavg-context-dtor-leak.patch` |
| `TINYGL` | `TinyGL` | 47 | `tinygl` | 14 | 5 154 | `C-Chads/tinygl` @ `36a7987e7bebfda19615ea33341b1cc0ff9c3b13` |
| `IGL` | `Igl` | 48 | `igl` | 48 | 14 406 | `facebook/igl` @ `v1.1.1` |
| `PIXIJS` | `PixiJs` | 49 | `pixijs` | 13 | 3 465 | `pixi.js` 7.4.2 UMD, SHA-256 `9ddba9cd78bc8610a1d445ec939393888be83925c78e40d66d9a17e98450228d` |
| `NANOVG` | `NanoVg` | 50 | `nanovg` | 21 | 5 571 | `memononen/nanovg` @ `ce3bf745eb2d2dbc14a50bf2446783f691ac4353` |
| `RLGL` | `Rlgl` | 51 | `rlgl` | 48 | 26 690 | `raysan5/raylib` @ `dbc56a87da87d973a9c5baa4e7438a9d20121d28`, archive SHA-256 `81b06ce7c19cf3b634b0271c23c361ba6ad8bf45fb8b036abbfeb4260ec1e126` |
| **Total** | | | **25 families** | **826** | **269 159** | |

### The 25 identities that stay

| Identity | Enum | C ABI | Family |
|---|---|---:|---|
| `SDL_RENDERER` | `SdlRenderer` | 1 | `sdl-renderer` |
| `OPENGLES2` | `OpenGLES2` | 2 | `easygl` |
| `OPENGLES3` | `OpenGLES3` | 3 | `easygl` |
| `OPENGL33` | `OpenGL33` | 4 | `easygl` |
| `WEBGL1` | `WebGL1` | 5 | `easygl` |
| `WEBGL2` | `WebGL2` | 6 | `easygl` |
| `VULKAN` | `Vulkan` | 8 | `vulkan` |
| `WEBGPU` | `WebGPU` | 9 | `webgpu` |
| `HEADLESS` | `Headless` | 11 | `headless` |
| `SOFTWARE` | `Software` | 12 | `software` |
| `STUB` | `Stub` | 13 | `stub` |
| `DIRECTX11` | `DirectX11` | 14 | `directx11` |
| `DIRECTX12` | `DirectX12` | 15 | `directx12` |
| `DIRECT2D` | `Direct2D` | 16 | `direct2d` |
| `CANVAS` | `Canvas` | 17 | `canvas` |
| `HTML_DOM` | `HtmlDom` | 18 | `html-dom` |
| `FREEDIRECT` | `FreeDirect` | 21 | `freedirect` |
| `DIRECTX9` | `DirectX9` | 22 | `directx9` |
| `SDL_GPU` | `SdlGpu` | 31 | `sdl-gpu` |
| `OPENGL4` | `OpenGL4` | 33 | `opengl4` |
| `GDI` | `Gdi` | 40 | `gdi` |
| `METAL` | `Metal` | 42 | `metal` |
| `FNA3D` | `Fna3d` | 43 | `fna3d` |
| `SVG_DOM` | `SvgDom` | 44 | `svg-dom` |
| `PORTABLEGL` | `PortableGL` | 46 | `portablegl` |

That is 25 identities over **21 implementation families** (46 − 25; EasyGL still serves five).

### Reference inventory

A `git grep` per identity (all spellings: `BGFX`/`Bgfx`/`bgfx`, `CNA_RENDERER_<X>`, directory and
class names, dependency names) found **1 313 tracked files outside the 25 family directories** that
mention at least one retired identity. By area:

| Area | Nature | Treatment |
|---|---|---|
| `modules/` (≈560 files) | code, shared tests and examples, other renderers' comments | edit: active references removed, comparative comments reclassified (RRC-002/003/004/007) |
| `cmake/` (35), `CMakeLists.txt`, `scripts/` (29), `tools/` (8), `.github/` (6) | build, gates, CI | edit or delete (RRC-002/003/004) |
| `docs/` (≈100) | live product documentation | rewrite; renderer-specific pages deleted and tombstoned (RRC-005) |
| `plans/` (≈70), `NEXT*.md`, `handoff_*.md` | task logs and ledgers | kept as history; per-renderer ones get a retired banner (RRC-005) |
| `audit/` (321), `modularization/` (102), `integration/` (33), `remediation/` (8), `spikes/` | dated evidence snapshots of earlier trees | kept verbatim as history; spikes of retired renderers get a retired banner (RRC-005) |

### Shared code that must stay

Checked by who includes it, never by its name:

- `modules/renderers/common/d3d` — DIRECTX11/DIRECTX12 only. Its comments cite the old DirectX
  families; code untouched.
- `modules/renderers/common/mojoshader` — FNA3D and the opt-in compiled-effect routes of EasyGL,
  SDL_GPU, Vulkan, WebGPU, DirectX9/11/12 and Software. `CNA_RLGL_COMPILED_EFFECTS` goes; the target stays.
- `PlatformGlRendererState.hpp` — still used by OPENGL4.
- `PresentationRect.hpp`, `VertexColourPbrSupport.hpp`, `IPlatformSurfacePresenter`,
  `IPlatformVulkanSurface`, `NativeWindowHandle` — used by surviving renderers and platforms.
- `cmake/ThirdPartyFNA3D.cmake` (MojoShader), `cmake/ThirdPartyPortableGL.cmake`,
  `cmake/ThirdPartyWebGPU.cmake`, `cmake/ThirdPartySDLShaderCross.cmake` and every `mojoshader-*`
  / `sdl-shadercross-*` patch.
- The TERMINAL platform keeps its CPU renderers (SOFTWARE, PORTABLEGL, HEADLESS, STUB); its CI leg and
  integration test move from BLEND2D to SOFTWARE rather than being dropped.

Shared code whose only consumer is a retired family, and therefore goes with it:

- `modules/graphics/include/CNA/Internal/Renderers/Common/FixedFunctionArrayLayoutSupport.hpp` and its
  test (only `opengles1` included it).
- `cmake/ThirdParty{Blend2D,Diligent,IGL,LLGL,Magnum,NanoVG,OpenVG,PixiJS,Rlgl,Sokol,TinyGL,Wicked}.cmake`,
  `cmake/patches/{bgfx-*,apply-bgfx-*,shivavg-*,apply-shivavg-*,wicked-*}`,
  `cmake/Tests/WickedTests.cmake`, `cmake/RendererRuntime.cmake`'s Wicked payload, and
  `cmake/toolchains/mingw-w64-i686.cmake` if nothing but GLIDE uses it (verify in RRC-003).
- `scripts/check-directx{1,2,3,5,6,7,8,10}-*.sh`, `scripts/run-wine-directx{1,2,3,5,6,7,8,10}.sh`,
  `scripts/run-wine-glide.sh`, `scripts/opengles1-test-env.sh`,
  `scripts/run-oracle-corpus-diff-opengles1.sh`, `scripts/run_pixijs_browser_tests.mjs`.
- `.github/workflows/{nanovg-ci,rlgl-ci,tinygl-cross-platform-ci}.yml`.

### Decisions

1. **C ABI numbers are the stable identity contract; they do not move.** The 25 constants are removed
   from `CNA/C/graphics.h` exactly as `SKIA`'s was, their values join 19 as permanently retired, and
   `CNA_GRAPHICS_RENDERER_MAXIMUM` names the highest *live* identity, `PORTABLEGL` (46). Moving the
   ceiling and removing constants is incompatible, so the ABI goes to **`0.28.0`** with release notes
   and a regenerated baseline, as `0.20.0` did. The next never-used value is **52**.
2. **The C++ enumerator ordinals are not an ABI** and already differ from the C values (since `SKIA`).
   `GraphicsRendererType` stays contiguous because `CanonicalRendererCount()` and
   `tryParseGraphicsRendererName()` walk it by ordinal; the retired enumerators are removed.
3. **A retired selector is refused by name, early, with a reason.** Removing an `option()` would make
   `-DCNA_RENDERER_BGFX=ON` a silently ignored cache entry — a fallback to the default renderer. The
   retired identity list lives in one CMake file read before SDL availability is decided, and every
   route (`CNA_GRAPHICS_RENDERER`, `CNA_GRAPHICS_RENDERERS`, `CNA_RENDERER_<X>=ON`) fails the configure
   naming the identity, its retired C ABI value and `docs/removed-renderers.md`.
4. **Documentation.** Live pages describing a retired renderer (`docs/<x>-renderer.md`, parity reports)
   are deleted and replaced by tombstone entries; plans, ledgers, audits and spikes are history and
   stay, with a retired banner on the per-renderer ones so they cannot be read as current support.
5. **Commits.** One commit per task RRC-002 … RRC-008 (RRC-001 is the plan). Each leaves the
   configure working; the identity-count tripwires move in RRC-002 with the registry.

---

## RRC-002 — Registry, CMake selection, C ABI

- [ ] `GraphicsRendererType` loses the 25 enumerators; `getCurrentGraphicsRendererType`,
      `getGraphicsRendererName`, `tryParseGraphicsRendererName` (last enumerator) follow.
- [ ] `GraphicsBackendCategory.hpp`, `GraphicsBackendMaturity.hpp`, `GraphicsRendererSelection*.cpp`.
- [ ] `cmake/RendererSelection.cmake`: STRINGS, help text, the 25 `option()`s, the explicit-selection
      chain, every per-identity gate and dispatch arm, `CNA_SOKOL_API`, `CNA_RLGL_COMPILED_EFFECTS`,
      the TERMINAL CPU-renderer list (also drops the stale `SKIA`).
- [ ] New `cmake/RendererRetiredIdentities.cmake`: the retired identity → C ABI value table and the
      early refusal described in decision 3, included before SDL availability is decided.
- [ ] `cmake/RendererRegistry.cmake` map, `cmake/RendererCombinations.cmake` lists and the GLIDE rule,
      `cmake/RendererDescriptorGate.cmake`, `modules/renderers/CMakeLists.txt` (unconditional `glide`
      subdirectory, PixiJS host tests), `modules/CMakeLists.txt` partition list and Wicked tests,
      `CMakeLists.txt`, `cmake/RendererRuntime.cmake`, `cmake/UnitTests.cmake`,
      `cmake/ApplePlatform.cmake`, `cmake/SdlAvailability.cmake`, `cmake/Tests/ModuleProbes.cmake`.
- [ ] C ABI: `graphics.h` constants and `MAXIMUM`, `CnaCApiCoreExt.cpp` and `CnaCApiGraphics.cpp`
      identity tables, `abi.h` → `0.28.0`, `ABI_VERSIONING.md` release notes,
      `tools/c-api/abi_baseline.json`, `tools/c-api/release_gate.json`, ABI header tests.
- [ ] Every compile-affecting use of a retired enumerator (`CNA_RENDERER_IS(...)` lists and friends),
      the identity-count tripwires, `scripts/check_renderer_identities.py`'s table and the counted
      documents it reads.

**Acceptance.** `check_renderer_identities.py` reports 25 identities over 21 families; a HEADLESS
configure succeeds; `-DCNA_GRAPHICS_RENDERER=BGFX` fails before SDL is configured, naming `BGFX` and 7.

## RRC-003 — Implementation removal

- [ ] Delete `modules/renderers/<family>` for the 25 families.
- [ ] Delete the ThirdParty modules, patches, runtime payload, scripts listed above; verify each has no
      surviving consumer first.
- [ ] Remove `FixedFunctionArrayLayoutSupport.hpp` and its test.
- [ ] `.gitignore`, `.gitattributes`, `.bitbackupignore`, `THIRD_PARTY_NOTICES.md` entries that exist
      only for retired dependencies.

**Acceptance.** No directory under `modules/renderers/` without a live identity; configure and a
representative build succeed.

## RRC-004 — Tests and CI

- [ ] Shared tests keep running for the surviving renderers: retired parameters removed, arms whose
      only renderer was retired collapsed (no `CNA_RENDERER_IS()` left empty), tables recounted.
- [ ] glTF renderer policy/stride inventories, cube/volume storage gates, capability expectations.
- [ ] Renderer-specific examples and tests in shared modules removed; renderer smoke/oracle scripts.
- [ ] CI: delete the three retired workflows; `input-ci` loses its bgfx leg; `platform-ci`'s TERMINAL
      leg moves to SOFTWARE; `gltf-renderer-stride-ci` comment; nothing builds a retired renderer.

**Acceptance.** The CnaTests corpus builds; no test is deleted merely to make the build green.

## RRC-005 — Documentation

- [ ] Live docs state **25 public renderer identities** / **21 implementation families** wherever a
      count is stated: `README.md`, `CLAUDE.md`, `AGENTS.md`, `docs/renderer-registry.md`,
      `docs/runtime-renderer-selection.md`, `docs/physical-modules.md`,
      `docs/renderer-expansion-candidates.md`, `docs/graphics-renderer-feature-matrix.md`,
      `docs/cnaext-engine-layer.md`, `docs/c-api/*`, platform docs, `misc/FUTURE.md`, `plans/plan_platform.md`.
- [ ] The curated-set policy replaces "more renderers" framing; the expansion candidates are
      research, separated from roadmap commitments.
- [ ] `docs/removed-renderers.md`: one tombstone per retired identity (value, family, commit, size,
      pinned dependency, what it proved, why retired); the retired-ID table.
- [ ] Renderer-specific live pages deleted; links into them repaired.
- [ ] Retired banner on the per-renderer plans, ledgers, handoffs and spikes; `plans/README.md`.
- [ ] Generated documents regenerated with their tools (`docs/platform-renderer-sdl-audit.md`,
      `plans/plan_platform.md` inventory), not hand-edited.

## RRC-006 — Regression guard

- [x] `scripts/check_renderer_identities.py` checks the exact whitelist (names, enum names and C ABI
      values), the retired table (26 values including `SKIA`), that no live identity uses a retired
      name or value, that `CNA/C/graphics.h` defines exactly the live constants with those values and
      `MAXIMUM` = the highest, and that the CMake retired list matches the script's.
- [x] A `cmake -P` negative-configure test per retired identity, registered in CTest.
- [x] `GraphicsRendererDescriptorTests`' identity tripwire at 25.

**Evidence.** `IDENTITIES` now pins each identity's C ABI value per row rather than by position, a
new `RETIRED_IDENTITIES` table pins all 26 reserved values, and `NEXT_FREE_ABI_VALUE` is 52.
`check_abi_contract()` and `check_no_retired_selectors()` hold those against
`modules/c-api/include/CNA/C/graphics.h`, `modules/c-api/src/CnaCApiCoreExt.cpp`'s
`RendererIdentities[]`, `cmake/RendererIdentities.cmake` and the C++ enum.

The guard was verified by breaking the tree four ways and confirming each is caught, then restoring:

| Injected fault | Caught as |
|---|---|
| `VULKAN` renumbered 8 → 7 (reusing bgfx's reserved value) | "a surviving renderer's C ABI value must never be renumbered" |
| `CNA_GRAPHICS_RENDERER_BGFX` republished in `graphics.h` | "the value stays reserved; the constant does not stay published" |
| `BGFX` added back to `CNA_RENDERER_PUBLIC_IDENTITIES` | STRINGS divergence **and** "is retired but appears in CNA_RENDERER_PUBLIC_IDENTITIES" |
| `RLGL=51` dropped from the CMake retired list | the two retired tables "disagree ... one of the two is refusing the wrong set" |

`cmake/Tests/RendererRetiredIdentityCase.cmake` runs `cmake/RendererIdentities.cmake` in `cmake -P`
script mode and asserts the outcome per selection, on three routes (`CNA_GRAPHICS_RENDERER`, a
member of `CNA_GRAPHICS_RENDERERS`, and `CNA_RENDERER_<X>=ON`). `cmake/Tests/ModuleProbes.cmake`
generates one REFUSE case **per entry of `CNA_RENDERER_RETIRED_IDENTITIES`**, so retiring a
renderer cannot add a name to the refusal list without also adding its test, plus eight route and
control cases. The expected text is `removed-renderers.md`, the one phrase the retired message has
and the unknown-name message does not — so the test proves a retired selector is refused *as
retired*, not merely as unknown.

`ctest -R CnaRendererRetired` → **34/34 passed in 1.03 s** (26 identity sweeps + 8 route/control
cases). Direct sweeps outside ctest: all **26** retired identities refused with the retired-specific
message, all **25** live identities accepted. Two deliberate inversions (`VULKAN` expected REFUSE,
`BGFX` expected ACCEPT) both fail, so the harness discriminates rather than passing everything.

**Regression this caused and fixed.** Widening `IDENTITIES` from 2-tuples to 3-tuples broke two
sibling gates that scrape the table with a regex: `check_renderer_combinations.py` (reported
`SVG_DOM` and `METAL` as "not a public renderer identity") and `check_cnaext_matrix.py` (its
anchored pattern matched nothing, and its own no-match tripwire then reported the parse as broken).
Both patterns now match the first two fields without anchoring on the closing paren. Found by
running all five renderer gates rather than only the one being edited.

## RRC-007 — Second pass

- [x] Multi-spelling stale-reference audit rerun; every remaining hit classified as active (fixed),
      historical, or retired-ID documentation.
- [x] Empty directories, dead includes and forward declarations, unused helpers, orphaned shaders and
      patches, stale TODOs, CMake variables without readers, dead links.

No empty directories remain under `modules/`, `cmake/`, `scripts/`, `tools/`, `docs/`, `spikes/` or
`integration/`. No orphaned patch remains: every file under `cmake/patches/` is referenced, and all
of them now belong to FNA3D/mojoshader and SDL_shadercross — the eight retired-renderer patches went
with their renderers in `fc1b6a537`. A repository-wide relative-link check found no live document
still linking a deleted page; the pre-existing broken links it did find are listed under *Findings
outside scope* and are all unrelated to renderers.

### The stale-reference audit, and what "explain every remaining reference" means

Spellings swept, for each of the 26 retired identities: the CMake selector (`BGFX`), the enum
spelling (`Bgfx`), the upstream/lowercase form (`bgfx`) and the compile definition
(`CNA_RENDERER_BGFX`). `scripts/` + `cmake/` + `modules/` + every `.md`.

**Production code and build files carry no reference to a retired renderer as a live thing.** What
remains is classified below; `scratchpad/classify.py` reproduces the counts. The rule that decides
a bucket is a single question: *read today, does this sentence make a claim about what CNA
currently supports?*

| Class | Where | Disposition |
|---|---|---|
| **(a) Active claim** | A renderer list, a build/run instruction, a capability sentence in the present tense | **Fixed.** Includes the two glTF PBR refusal *runtime messages*, which named six retired renderers as alternatives for a user to switch to — the worst kind, because a user would follow the advice and hit a configure refusal. The real lists were derived from the guards' actual call sites, not guessed. Also the C ABI and XNA header docs naming `SKIA` as a device-reset renderer, `GraphicsCapability`'s 2D-only and volume-storage prose, `SpriteBatch::DrawMeshEXT`'s "implemented only by the Skia renderer", and `needsSurfacePresenter`'s "(SKIA, BLEND2D)" |
| **(b) Legitimate historical** | 376 comment sites in `modules/*/tests/` and `modules/*/examples/` | **Kept, tagged.** These are dated task records (`REMED-GFX-###`, `Task ###`, `SKIA-###`) and defect analyses that explain *why a shared test exists* — `rendertarget_first_use_test.cpp` carries a 40-line reading of a bgfx-local defect, quoting bgfx's own source. Deleting them would destroy the reason the test is there while changing nothing about the tree. The first mention in a block gets `(retired 2026-09-17)` |
| **(c) Retired-ID documentation** | `CNA/C/graphics.h`'s reserved-value comment, `cmake/RendererIdentities.cmake`, `scripts/check_renderer_identities.py`, `docs/removed-renderers.md`, `docs/renderer-registry.md`, `docs/c-api/ABI_VERSIONING.md`, `CLAUDE.md`/`AGENTS.md` | **Required to exist.** These *are* the record that a value is reserved. Removing them is how a value gets reused |
| **(d) Frozen archives** | `audit/` (321 files), `remediation/`, `modularization/`, `integration/lanes/`, `spikes/`, per-renderer `plans/plan_*.md`, `NEXT*.md`, `handoff_*.md` | **Kept verbatim, bannered.** One banner per archive index rather than edits to ~300 files: they record audits that really ran against the tree of their own date, and rewriting them would destroy that. The banner says the archive predates the curation and names what no longer exists |

Two judgement calls worth stating plainly. `docs/runtime-renderer-selection.md` keeps an `LLGL`
fallback transcript and an `LLGL` binary-size row: both are *measurements that really happened*,
the transcript is the only genuine (not simulated) environmental failure on record, and the size
row is the only one that includes a large third-party renderer. Both are labelled historical rather
than deleted or — worse — re-attributed to a surviving renderer, which would have been fabrication.
The `docs/graphics-renderer-feature-matrix.md` Skia companion matrix went the other way and was
deleted: every one of its 23 rows cited a `docs/skia-*.md` page or `plans/plan_skia.md` that was
removed with the renderer in 2026-08, so nothing in it could be checked against anything.

## RRC-008 — Verification

- [x] Identity/descriptor/combination/cnaext gates; platform boundary gates.
- [x] Representative builds (matrix below).
- [x] Negative configure of all 25 retired selectors — done for all **26**, including `SKIA`.
- [x] Full CTest corpus against the RRC-001 baseline.

### CTest, measured in the same tree as the baseline

`cmake-build-multi` — the tree RRC-001 measured — rebuilt and re-run, so this is like-for-like
rather than a comparison across configurations.

| | Registered | Failed |
|---|---:|---:|
| Baseline (`c0225e9d5`, pre-work) | 9 510 | 100 |
| After (`750ed854d`) | 9 456 | **46** |

54 fewer tests registered — the retired renderers' own suites — and **54 fewer failures**.
Comparing the failing *names*, not just the counts: **58 baseline failures are gone** and four
names appear that the baseline list does not have. None is renderer-related, and **all four pass when re-run individually** — they are flaky under
`-j8`, not regressions:

| Newly-listed failure | Verdict |
|---|---|
| `NetworkSessionTest.FindReturnsEmptyCollection` | **Flaky under `-j8`** — passes on rerun |
| `TwoProcessLoopbackTest.HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses` | **Flaky** — passes on rerun (spawns two real processes) |
| `ContentPipelineCliTest.WorkerCountsProduceIdenticalColdNoOpAndDependencyRebuilds` | **Flaky** — passes on rerun (asserts equality across worker counts under load) |
| `CnaInputTests` | **Flaky** — passes on rerun. `modules/input/` is also untouched by this branch (`git diff --name-only` against the baseline is empty) |

A second full run in `cmake-build-debug` (single-renderer `HEADLESS`, Debug, SDL3 platform) gives
**9 130 / 9 145**. Its 15 failures are a different set because the configuration differs, and each
was classified rather than counted: six are in the baseline list verbatim; the `Cnb*`/`Cnj*` texture
ones fail with *"this graphics renderer did not store the complete requested cube face region"*,
which is HEADLESS's documented capability boundary and precisely why they do not fail in a tree
that defaults to EasyGL; the rest are in modules this branch never touched.

**No test was deleted to make a build green.** The only tests removed are those of the retired
renderers themselves, and shared tests kept every parameter except the retired renderer.

### Gates

All nine green: `check_renderer_identities`, `check_renderer_combinations`,
`check_runtime_renderer_discipline`, `check_renderer_descriptors`, `check_cnaext_matrix`, and the
four platform boundary gates (`sdl_inventory`, `sdl_classify`, `renderer_sdl_audit`, `sdl_ratchet`)
plus `hot_path_lint`. `tools/c-api/generate_abi_baseline.py --check` reports the baseline current.

### Negative configure — all 26 retired selectors

Run as a **real project configure** (`cmake -S . -B ...`), not only in `cmake -P` script mode, so the
refusal is proven on the path a user actually takes. All 26 refused, each naming its permanently
reserved C ABI value and pointing at `docs/removed-renderers.md`. Spot-checked in the brief's own
list: `BGFX` (7), `OPENGLES1` (32), `DIRECTX7` (28), `OPENGL2` (35), `IGL` (48), `RLGL` (51),
`PIXIJS` (49), and `SKIA` (19).

**No silent fallback, proven rather than asserted:** after a refused configure the tree contains
**no generated build system** (no `build.ninja`, no `Makefile`), and `CMakeCache.txt` holds the
refused name itself — never a substituted renderer. There is no way to end up with a build that
quietly uses a different renderer than the one that was asked for.

A retired name is also refused through the other two routes that can name a renderer — a member of
`CNA_GRAPHICS_RENDERERS` and `CNA_RENDERER_<X>=ON` — and the refusal says *retired*, not merely
*unknown* (`ctest -R CnaRendererRetired`, 34/34).

### Build matrix, as actually run on this machine

Native Linux (Debian, GCC 14, Ninja, ccache), Emscripten 5.0.7 from `~/Downloads/emsdk`.

| Renderer | Configure | Renderer library built | Note |
|---|---|---|---|
| `OPENGLES3`, `OPENGL33` | ✅ | ✅ `cna_renderer_easygl` | |
| `OPENGL4` | ✅ | ✅ | |
| `VULKAN` | ✅ | ✅ | |
| `SDL_GPU` | ✅ | ✅ | |
| `SOFTWARE`, `HEADLESS`, `STUB`, `PORTABLEGL`, `SDL_RENDERER` | ✅ | ✅ | `HEADLESS` additionally has a **complete** build of the whole configuration (`cmake-build-debug`, every target, 0 errors) |
| `WEBGL1`, `WEBGL2`, `CANVAS`, `HTML_DOM`, `SVG_DOM` | ✅ (Emscripten) | ✅ | Real `emcmake` configure and build of each family's library |
| `FREEDIRECT` | ⛔ refused at its **documented dependency gate** | — | Needs the sibling checkout `../free-direct`, which is not on this machine. The refusal names the missing repository and the exact `git clone` that fixes it. Not cloned: this workstream does not touch sibling repositories |
| `DIRECTX9/11/12`, `DIRECT2D`, `GDI` | not attempted here | — | Windows-only. A MinGW cross-compile exists (`cmake-build-d3d11`) but **no Windows runtime validation is claimed from Linux** |
| `METAL` | not attempted | — | macOS-only; no Darwin toolchain on this machine. No macOS claim is made |
| `FNA3D` | not attempted | — | `~/deps/FNA3D` is present; deferred as not required for the identity contract |

Two builds are complete configurations rather than a single library: `cmake-build-debug`
(`HEADLESS`, every target) and `cmake-build-multi` (`OPENGL33` default with `VULKAN`, `SOFTWARE`
and `HEADLESS` compiled in, X11 platform) — the latter is the tree the RRC-001 baseline was
measured in, rebuilt so the before/after test comparison is like-for-like.

**Result: 14 of 14 attempted renderer libraries built, 0 errors** — ten native
(`cna_renderer_easygl` for both GL identities, `opengl4`, `vulkan`, `sdl_gpu`, `software`,
`headless`, `stub`, `portablegl`, `sdl_renderer`) and four under Emscripten (`easygl` for WEBGL2,
`canvas`, `html_dom`, `svg_dom`). The Emscripten half became possible because the project owner
pointed at `~/Downloads/emsdk`; without it these five identities would have been reported as
unverifiable on this machine rather than built.

---

## Findings outside scope

Recorded, not fixed here. Every one of these is independent of the renderer retirement: each
reproduces on `next` before this branch, and fixing them would be unrelated refactoring.

| Item | Class |
|---|---|
| `plans/plan_gdi.md` links 9 pre-modularization paths (`../src/CNA/Internal/Backends/...`, `../cmake/BackendSelection.cmake`, `../cmake/CnaLibrary.cmake`). Broken by the Phase-3 physical move, not by this work. | Stale link, pre-existing |
| `plans/plan_graphics.md` links `plan_graphics_20260708.md` and `plan_graphics_20260709.md`, which are not in the tree. | Stale link, pre-existing |
| `misc/cnj.md` links `xnb.md` and `plans/plan_xnb.md` as if from the repo root, but the file is in `misc/`, so both resolve one level too high. The targets exist. | Stale link, pre-existing |
| `misc/CNAEXT.md` links `docs/cnaext-nova3d.md`, which does not exist (same root-relative mistake, or a document never written). | Stale link, pre-existing |
| 25 apparent "dead links" in `docs/input-public-api-frozen.md`, `docs/model-content-pipeline-support.md`, `docs/viewport-displaymode-adapter-support.md`, `docs/xna-content-pipeline-parity-report.md` and `plans/plan_graphics.md` are false positives: C++ generic arguments (`std::vector<int>`, `Keys`, `intcs`) that a markdown link checker reads as `[text](target)`. Nothing to fix. | Not a defect |
| `tools/c-api/check_release_gate.py --check` reports the **limitations-matrix** criterion unmet: `generate_limitations.py` raises `RuntimeError: Explicit coverage rules matched no symbols: sprite-batch-begin-explicit-state`. Verified pre-existing by running the same tool in a worktree at the baseline commit `c0225e9d5`, where it fails with **four** unused rules rather than one — so this workstream did not cause it and in fact reduced it. The rule targets `SpriteBatch::Begin`, which this branch does not change in any way the parser reads (tested by restoring the committed header and re-running: identical failure). | Pre-existing tool defect |
| `docs/c-api/RELEASE_GATE.md` is generated and its published header is stale at ABI `0.21.0` while the tree is at `0.28.0`. **Deliberately not regenerated**: `--write` bakes the traceback above into the published document as a criterion's "measurement", which is worse than a stale version line. Regenerating is correct once the limitations generator is fixed, and belongs to whoever owns that tool. | Pre-existing, blocked on the above |
| **7 headless example targets fail to compile in a multi-renderer build**, each with `fatal error: CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp: No such file or directory`. Cause: `modules/graphics/CMakeLists.txt:44` links renderer targets into `cna_graphics_core` as **PRIVATE**, so a non-default renderer's public include directory never reaches an example target; the family loop in `modules/renderers/CMakeLists.txt` still enters `headless/examples` because it temporarily sets `CNA_GRAPHICS_RENDERER` per family. **Proven pre-existing**: building `cna_test_headless_smoke` in a git worktree at the baseline commit `c0225e9d5`, in the same multi configuration, fails with the byte-identical error. That `PRIVATE` link dates from `43053b185` (2026-08-14, RTR-P6) and this branch touches neither it, nor `modules/renderers/headless/` beyond one comment, nor the registration loop. `CnaTests` — the actual corpus — links and runs normally, so nothing is blocked. | Pre-existing build defect |
| No surviving renderer sets `needsSurfacePresenter`, so the `TERMINAL` platform has no renderer that presents CPU frames into a terminal. `BLEND2D` was the only one, and it was retired. This is a **consequence** of the curation, recorded rather than hidden; closing it needs an owner decision (teach `SOFTWARE` to use a surface presenter, or accept that `TERMINAL` is validation-only). | Consequence, needs owner decision |
| `SpriteBatch::DrawMeshEXT` — the 2D triangle-mesh entry point — now has **no implementer**. It was added for Skia's bounded `SkVertices`/SkSL mesh ABI (`SKIA-144`–`157`) and Skia was retired in 2026-08; `IGraphicsRenderer::DrawMeshEXT`'s base implementation throws, and no renderer overrides it. The public CNAEXT method, its C ABI route (`CnaCApiGraphics.cpp`) and its shared sort-mode test all still exist and behave correctly — every renderer refuses, which is exactly what they did when Skia existed. **Not removed here:** deleting it would break the published C ABI, and it is not one of the 25 renderers this workstream retires. Comments corrected to stop implying Skia is present. Whether the surface stays is an owner decision. | Consequence, needs owner decision |
