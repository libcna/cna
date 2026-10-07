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
| RRC-009 | Final pass: remove `DrawMeshEXT`, the dead API the curation left with no implementer | ✅ |
| RRC-010 | Final pass: audit `needsSurfacePresenter` and `TERMINAL` — decide, do not assume | ✅ |
| RRC-011 | Remove retired renderer spikes and the rejected Three.js probe; repair current-tree references and retention policy | ✅ |
| RRC-012 | Retire `DIRECT2D`, `FREEDIRECT` and `PORTABLEGL`: remove their families and every integration that existed only for them | ✅ `0f96ea703` |
| RRC-013 | Repair the runtime-discipline gate's identity parser; remove stale current-state references left by RRC-012 | ✅ |
| RRC-014 | Move the Software contracts only the GDI suite held into the Software suite | ✅ |
| RRC-015 | Retire `GDI`, `HTML_DOM`, `SVG_DOM` and `OPENGL4`: remove their families and every integration that existed only for them | ✅ |
| RRC-016 | Remove `CNA_SOFTWARE_2D_ONLY`, the reduced Software build that existed only for `GDI` | ✅ |
| RRC-017 | Remove the four renderers' documents, plans and handoffs; one retirement record | ✅ |
| RRC-018 | Retire `DIRECTX12`, `CANVAS`, `OPENGLES2` and `WEBGL1`: remove their families, EasyGL's ES 2.0 profiles and every integration that existed only for them | ✅ |
| RRC-019 | Remove the four renderers' documents and plans; one retirement record; correct every current-state document | ✅ |
| RRC-020 | `SDL_GPU` in a test-enabled multi-renderer build: the link group names the whole archive cycle | ✅ `c1959b961` |
| RRC-021 | FNA3D's suites compile wherever FNA3D is compiled in and run where it is active | ✅ `8bbba7b4f` |
| RRC-022 | A renderer family's example suite exists only where that family is the default | ✅ `f71f064b9` |
| RRC-023 | DirectX 11 executables carry the cross lane's Wine+DXVK launcher | ✅ `f5b120dd5` |
| RRC-024 | C API coverage generator: re-pin the approvals a re-declaration left stale | ✅ `bf410474b` |
| RRC-025 | `WEBGPU` sizes its own Asyncify stack under Emscripten | ✅ `bfd4b9e73` |
| RRC-026 | Vulkan/WebGPU blocks in the indexed-draw suites run only where their renderer is active | ✅ |

**2026-09-19 owner decision (`RRC-011`).** Retired renderer probes and the rejected Three.js
candidate probe no longer belong in the current `spikes/` tree. The earlier archive-retention
decision recorded below describes what happened in `RRC-001`–`RRC-008`; `RRC-011` supersedes it.
The historical plans keep the measurements, and Git history retains the deleted probe sources.

`RRC-009` and `RRC-010` are the **final cleanup pass** before integration, opened to close the two
rows this plan left under *"Findings outside scope"* as needing an owner decision. They are
deliberately narrow: `RRC-009` deletes one dead entry point, `RRC-010` decides the fate of one flag
and records what `TERMINAL` actually is. Neither adds, restores or redesigns anything.

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
| `audit/` (321), `modularization/` (102), `integration/` (33), `remediation/` (8), `spikes/` | dated evidence snapshots of earlier trees | originally kept as history and bannered (RRC-005); retired renderer probes later removed in RRC-011 |

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
4. **Documentation (RRC-001 decision, superseded for probes by RRC-011).** Live pages describing a
   retired renderer (`docs/<x>-renderer.md`, parity reports) are deleted and replaced by tombstone
   entries; plans, ledgers, audits and spikes were initially kept as history, with a retired banner
   on the per-renderer ones so they could not be read as current support.
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
| **(d) Frozen archives** | `audit/` (321 files), `remediation/`, `modularization/`, `integration/lanes/`, `spikes/`, per-renderer `plans/plan_*.md`, `NEXT*.md`, `handoff_*.md` | **Originally kept verbatim, bannered.** One banner per archive index rather than edits to ~300 files: they record audits that really ran against the tree of their own date. RRC-011 later removed the retired and rejected renderer probes from `spikes/`; Git history retains those sources. |

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
| No surviving renderer sets `needsSurfacePresenter`, so the `TERMINAL` platform has no renderer that presents CPU frames into a terminal. `BLEND2D` was the only one, and it was retired. This is a **consequence** of the curation, recorded rather than hidden. **Decided in `RRC-010`: the flag and the abstraction are KEPT**, and `TERMINAL` is documented as validation-only until a renderer feeds it. | Consequence, decided in RRC-010 |
| `SpriteBatch::DrawMeshEXT` — the 2D triangle-mesh entry point — had **no implementer** after the curation. It was added for Skia's bounded `SkVertices`/SkSL mesh ABI (`SKIA-144`–`157`) and Skia was retired in 2026-08. **Decided in `RRC-009`: removed completely**, C++ and C ABI alike, taking the ABI to `0.29.0`. | Consequence, decided in RRC-009 |

---

## RRC-009 — Remove `DrawMeshEXT`

`RRC-008` left this as *"needs owner decision"* because deleting it breaks the published C ABI. The
decision is **remove**, and the reasoning is that the alternative was worse: a route every supported
renderer refuses is a permanently dead branch of the ABI, and CNA's C ABI is still `0.x` and
explicitly experimental, so this is the cheapest moment it will ever be removed at. No compatibility
stub was left — a stub preserving the old symbol would only preserve the refusal.

### Audit before deletion — mechanical, not inherited from `RRC-008`

Every occurrence in the tree was enumerated and classified rather than trusted from the earlier
report. `modules/renderers/` contained **zero** occurrences across all 21 retained families: the
method was declared once, defaulted once, and never overridden.

| Surface | Site | Disposition |
|---|---|---|
| Renderer contract | `IGraphicsRenderer.hpp` — `ISpriteBatchRenderer::DrawMeshEXT`, virtual with a throwing default | deleted |
| XNA-layer public API | `SpriteBatch.hpp` declaration, `SpriteBatch.cpp` definition (`CNAEXT`, never XNA 4.0) | deleted |
| C ABI header | `graphics.h` — `cna_sprite_batch_draw_mesh_ext`, `CNA_SpriteMeshEXT` | deleted |
| C ABI implementation | `CnaCApiGraphics.cpp` — 97 lines of validation and conversion | deleted |
| C ABI layout tests | `AbiHeaderCpp.cpp` (5 asserts), `AbiHeaderC.c` (1 grouped assert) | deleted |
| Strict-C route test | `GraphicsDeviceSmoke.c` — mesh submission, malformed-mesh and optional-array legs | deleted |
| Shared behaviour test | `spritebatch_sort_mode_semantics_test.cpp` leg B2 used it as an *instrument* | **re-instrumented**, see below |
| Coverage source of truth | `tools/c-api/coverage_mappings.json` rule `spritebatch-text-and-mesh` | renamed `spritebatch-text`; regex alternative, mapping prose and test prose pruned |
| Contract-audit generator | `tools/check_sdlgpu_renderer_contract_audit.py` — `OUT_SPRITE_METHODS`, its `scope_for` branch, and the now-unreachable `evidence_for` branch | deleted (all three; the set would otherwise be empty and the branch dead) |
| Contract-audit manifest | `plans/sdlgpu_renderer_contract_audit.csv` row 133 | row deleted, 266 → 265 |
| ABI baseline | `tools/c-api/abi_baseline.json` | regenerated from the built library, not hand-edited |
| Docs | `docs/fna3d-renderer.md` refusal row; `docs/xna-4-api-coverage.md` sort-mode claim | row deleted; claim rewritten onto a live observable |
| `plans/`, `misc/`, dated audits | 8 files | kept as history — these record that the API existed, which remains true |

No enum value, capability bit, dispatch-table member or function-pointer table was involved: mesh
submission never had a capability flag. That absence is itself part of why it was removable — there
was no `SupportsCapability` answer for a caller to gate on, only a call that always threw.

### The one place deletion cost real coverage, and what replaced it

`spritebatch_sort_mode_semantics_test.cpp` leg B2 (`VULKAN-050`) asserted that `Begin()` carries the
sort mode down to the renderer seam, and it used `DrawMeshEXT`'s Immediate-only refusal as the
discriminator. Deleting the instrument would have deleted the assertion, so the leg was
**re-instrumented rather than dropped**, onto the device-level Immediate mutual exclusion
(`SpriteBatch.cpp` `Begin()`): an `Immediate` batch refuses any second batch, an active `Immediate`
batch refuses a `Deferred` one, and two `Deferred` batches coexist. The leg now asserts all three.

The replacement is strictly better than what it replaced, which is why it was preferred to simply
deleting the leg:

- it is **XNA-specified behaviour** (Microsoft coordinates every `SpriteBatch` on one device), not a
  CNAEXT extension;
- it needs **no renderer capability at all**, so it means the same thing on every renderer — the
  mesh probe had to tolerate a capability refusal and could not distinguish "accepted" from
  "refused for the other reason" without string-matching the exception message;
- it is **mutation-equivalent for the mutation `VULKAN-050` recorded**: forcing `sortMode_ =
  Deferred` in `Begin()` stops `spriteImmediateBeginCount_` from ever incrementing, so both refusals
  disappear and the leg fails — exactly as the old leg did.

`BasicEffect.hpp` and `Vector2.hpp` became dead includes in that file and were removed with it.

### ABI consequence

`0.28.0` → **`0.29.0`**, following the policy in `docs/c-api/ABI_VERSIONING.md`: under `0.x` an
incompatible change takes a **minor** increment plus release notes plus a regenerated baseline. The
"requires a new ABI major" sentence in that document governs `1.x` and later, and the precedent is
consistent — `0.20.0` and `0.28.0` both removed public identities on a minor bump, and `0.3.0`
changed 66 routes' argument handling on one.

Measured, not assumed: exported symbols **4,056 → 4,055**, recorded struct layouts **222 → 221**.
The four prose repetitions of the export count (`ABI_VERSIONING.md`, `CONSUMING.md`,
`LIMITATIONS.md`, `tools/c-api/limitations.json`) were updated with it, because
`check_doc_export_counts.py` exists precisely to fail when they drift.

## RRC-010 — `needsSurfacePresenter` and `TERMINAL`

### Decision: `needsSurfacePresenter` is **kept**

`RRC-008` correctly observed that no retained renderer sets it. That is a necessary condition for
removal, not a sufficient one, and the audit found the sufficient condition absent. The decisive
difference from `DrawMeshEXT` is **which side of the seam is missing**:

| | `DrawMeshEXT` | `needsSurfacePresenter` |
|---|---|---|
| Producer (caller / renderer that sets it) | present (any C or C++ caller) | **absent** — no retained renderer sets it |
| Consumer (code that does the work) | **absent** — no renderer implemented a mesh ABI | present — `TerminalSurfacePresenter`, 304 lines |
| In the published C ABI | yes | **no** — zero hits in `modules/c-api/include/` and `abi_baseline.json` |
| Deleting it would | remove a call that always threw | remove the only mechanism a working consumer is waiting for |

Concrete evidence for the consumer, which is what the retain rule requires — code, not plans:

- `needsSurfacePresenter` is read in production at `modules/graphics/src/Xna/GraphicsDevice.cpp`
  (`createRenderer`), which creates the platform presenter and passes it to the renderer through
  `GraphicsRendererCreateArgs::surfacePresenter`. The `GraphicsDevice` member is deliberately
  declared before `renderer_` so reverse destruction keeps presentation alive through the raster
  renderer's final destructor calls — lifetime ordering that exists for this path specifically.
- `IPlatformSurfacePresenter` is **pure virtual on `IPlatform`**, so it is not optional
  infrastructure: it is implemented by five retained backends (X11, Win32, Wayland, SDL3, Terminal)
  and explicitly refused by two (SDL2, Headless) with `PlatformNotSupportedException`. It has its own
  capability bit, `PlatformCapability::SurfacePresentation`, independent of this flag.
- `TerminalSurfacePresenter` is a complete, working RGBA8 → ANSI implementation (letterboxing,
  glyph-ramp quantisation, truecolor/256/16 SGR with remembered state, dirty-cell diffing), covered
  by 36 pseudo-TTY tests that construct it directly and pass today.
- `cmake/RendererSelection.cmake` still reserves `TERMINAL` for `SOFTWARE PORTABLEGL HEADLESS STUB`
  — all four retained — so the supported combination this flag serves has not gone anywhere.
- `RRC-001`'s own must-keep list already classified `IPlatformSurfacePresenter` as used by surviving
  renderers and platforms.

Deleting the flag would therefore not be a clean removal. It would delete the declared switch that a
live, tested, retained consumer is waiting for, and a later session restoring terminal output would
have to reintroduce the identical field — which is the "forces a later reintroduction" case that the
retain rule names. It is one `bool` with an accurate doc comment describing its own dormancy.

### `TERMINAL` is a live platform with a disconnected graphics output path

`RRC-008`'s sentence "`TERMINAL` currently has no renderer feeding it" is literally true and is the
whole of the problem — it must not be read as "`TERMINAL` is dead". Measured:

| Question | Answer |
|---|---|
| Is it implemented? | `modules/platform/src/Terminal/` — 22 files, 4 754 lines |
| Is it compiled? | **On every POSIX build, regardless of `CNA_PLATFORM`** (`modules/platform/CMakeLists.txt`) |
| Is it tested? | 134 `TEST` cases over 9 files, 3 402 lines, plus two pseudo-TTY harnesses; a member of the parameterized platform conformance suite |
| Is it selectable? | Yes, first-class on every non-Windows host (`cmake/PlatformSelection.cmake`); reserved with a `FATAL_ERROR` on Windows only, because it is built on `termios` |
| Is it in CI? | Yes — `platform-ci.yml` matrix cell *Terminal + Software + Null audio* |
| Can it present? | Yes — the presenter works when driven, proven by tests that construct it directly |
| Is anything driving it? | **No.** Nothing sets `needsSurfacePresenter`, so `GraphicsDevice` never creates the presenter |

`SOFTWARE` cannot substitute as-is, and the gap is wider than the one flag: `SoftwareRenderer::Present()`
is an empty body, and the read site also requires `platformWindow_ != nullptr` while
`SoftwareRendererDescriptor` sets `needsWindow = false`, which makes `GraphicsDevice` reset the
window. Retired `BLEND2D` set `needsWindow = true`, `windowKind = Plain` **and**
`needsSurfacePresenter = true` together; that triple is the working shape. Connecting it is feature
work and is deliberately **not** done here.

### One defect this branch introduced, found by the audit and fixed here

`RRC-004` retargeted the terminal integration test from `BLEND2D` to `SOFTWARE` mechanically —
`TerminalBlend2DDemoIntegration` → `TerminalSoftwareDemoIntegration` — and in doing so deleted the
comment recording the invariant that made it work ("the one tuple that has a CPU presenter capable
of reaching a terminal"). The assertions were left untouched, and three of them
(`\x1b[?1049h`, `dropped_frames=`, `kitty_keyboard=`) have exactly one producer in the tree:
`TerminalSurfacePresenter`, which under `SOFTWARE` is never constructed. The test could not pass.
`RRC-008` recorded the consequence in prose while the CI leg asserting the opposite stayed
registered.

That is this branch's own regression, not a pre-existing one, so it is repaired here rather than
deferred — see the `RRC-010` verification notes for what the repair was and the evidence that the
test behaves as claimed.

### RRC-009 / RRC-010 verification

Measured on 2026-09-17, Linux, `-j8`. Starting HEAD `33928af3d`, clean tree.

| Check | Result |
|---|---|
| `cmake-build-debug` (HEADLESS/SDL3) full build | clean, 0 errors |
| `TERMINAL`+`SOFTWARE` configure and `cna_demo_2d` build (`build-probe`) | clean — the removal does not disturb the platform this pass investigated |
| `cmake-build-vulkan` sort-mode semantics target | built and run, see below |
| `scripts/check_renderer_identities.py` | **25 identities, 21 families, 26 retired values reserved, next free 52** — byte-identical to `RRC-008` |
| `scripts/check_removed_renderer_api.py` (new, `RendererCurationApiDecisions`) | 4/4 groups pass |
| `tools/c-api/generate_abi_baseline.py --check` | current: 221 structs, 4 055 exports recorded |
| `tools/c-api/check_doc_export_counts.py` | 6 prose counts agree with the measured 4 055 |
| 5 platform boundary gates (`sdl_inventory`, `sdl_classify`, `renderer_sdl_audit`, `sdl_ratchet`, `hot_path_lint`) | all pass |
| 4 renderer gates (`combinations`, `descriptors`, `runtime_discipline`, `target_discipline`) | all pass |

**The new guard was mutation-tested**, one mutation at a time, each caught by exactly the intended
arm and by no other, and the tree restored after each:

| Injected fault | Caught as |
|---|---|
| `CNAEXT void DrawMeshEXT(Effect&);` re-added to `SpriteBatch.hpp` | *"names the removed `DrawMeshEXT`"* — RRC-009 arm |
| `descriptor.needsSurfacePresenter` replaced by `false` in `GraphicsDevice.cpp` | *"no longer reads `needsSurfacePresenter`"* — RRC-010 arm |
| `TerminalSurfacePresenter.cpp` deleted | *"missing — the terminal presenter that consumes it"* — RRC-010 arm |

**The ABI gate was verified to catch this change before the baseline was re-recorded**, which is
what proves the diff is exactly the intended one and nothing more: it reported precisely five
breaks — `CNA_ABI_VERSION` 7168→7424, `CNA_ABI_VERSION_MINOR` 28→29, `struct removed:
CNA_SpriteMeshEXT`, and the two encoded/minor version rows — with no unintended layout or constant
movement anywhere in the 221 remaining structs, 350 scalar types or 1 558 constants.

### `TerminalSoftwareDemoIntegration` — verified failing, then repaired

The failure was **measured, not inferred**: configured `TERMINAL`+`SOFTWARE`, built `cna_demo_2d`,
ran the test. It failed in 12.12 s with
`AssertionError: demo never entered the terminal alternate screen` — its first gate, exactly as the
presenter analysis predicted.

It is **this branch's regression**, and that too is measured rather than argued: at the `RRC-001`
baseline the CI cell was *Terminal + Blend2D + Null audio* running `TerminalBlend2DDemoIntegration`,
and BLEND2D's descriptor set `needsSurfacePresenter = true`. `RRC-004` renamed the cell and the test
to `SOFTWARE` without giving SOFTWARE a presenter.

Repair, chosen to fit this pass's scope: the test keeps its registration and gains
`DISABLED TRUE`, with the reason and the one-line condition for re-enabling it stated at the
registration site. Implementing a SOFTWARE presenter is feature work and is explicitly out of scope
here. Verified after the change: `ctest -R '^TerminalSoftwareDemoIntegration$'` reports
`Not Run (Disabled)` and **exits 0**, so the CI step passes instead of hanging for 12 s and failing,
while the leg re-arms automatically when the property is removed. The runner's docstring, which
still named Blend2D, was corrected; the runner's logic was deliberately left untouched so it is
ready to use as-is.

Deleting the test instead would have hidden the gap; leaving it red would have normalised a failing
CI leg. `TERMINAL`'s real coverage — 134 unit tests including 36 pseudo-TTY presenter tests — runs in
`CnaPlatformTests` and is unaffected either way.

### Generated files: what was regenerated, and what could not be

`tools/c-api/abi_baseline.json` **was** re-recorded, and not by hand: the generator's own
`measure()` produced the header half and the recorded export list was carried forward minus the one
name whose definition no longer exists in the source (4 056 → 4 055). The reason the ordinary
`--write` path was not used is a **pre-existing build failure**, proven rather than assumed:
`-DCNA_BUILD_C_API=ON` does not compile, failing in `modules/c-api/src/CnaCApiEffects.cpp` with 8
errors about `EffectAnnotation` references and lvalue `&` operands. Compiling that single
translation unit in a `git worktree` at `33928af3d` — which contains none of this pass's changes —
fails with the **byte-identical 8 errors**, and this pass touches neither that file nor
`EffectAnnotation`. `--write` refuses to record a baseline without the library, by design.

One consequence, recorded so it is not read later as drift: the re-recorded `abi_version` object no
longer carries the `runtime` field, which only a real library can supply. The checker classifies a
field present in the measurement but absent from the baseline as an **addition** (permitted), not a
break, so the first library-backed run will ask to re-record rather than report an ABI break.

`docs/c-api/RELEASE_GATE.md` is generated and was **not** regenerated — `--write` still bakes the
pre-existing `generate_limitations.py` traceback into the published document, which `RRC-008` refused
for good reason and this pass refuses too. Its one measurement this change invalidated was corrected
in place to the value the generator's own formula yields from the re-recorded baseline
(`len(structs)` and `len(exports)`: *"221 struct layouts and 4055 exported symbols recorded"*),
because `check_doc_export_counts.py` **passed at the start of this pass** and leaving it red would
have been a new regression. The document's stale `0.21.0` header and its *"1 criteria are unmet"*
verdict are untouched and remain the pre-existing defect `RRC-008` describes.

`plans/sdlgpu_renderer_contract_audit.csv` lost exactly the one row whose method no longer exists
(266 → 265). The generator could not be re-run here: it needs two configured trees, and
`SDLGPU-135` already records that the manifest is a function of a build option it does not itself
record, with the standing instruction to leave it at its committed value. The other 265 rows are
untouched, and the generator's own now-dead classification rules were removed so it can never
re-emit the deleted row.

`tools/c-api/generate_limitations.py` still fails with the single unused rule
`sprite-batch-begin-explicit-state` — **unchanged** by this pass, neither worsened nor fixed. The
renamed `spritebatch-text` rule still matches symbols (it covers `DrawString`, the constructors and
`GetTypeName`), so narrowing its regex added no unused rule.

### Full CTest corpus, against this pass's own baseline

`cmake-build-debug` (HEADLESS/SDL3, Debug), `-j8`, run **before** any edit and again after, in the
same tree — so this is like-for-like rather than compared with `RRC-008`'s different configuration.

| | Tests | Failed |
|---|---:|---:|
| Before (`33928af3d`) | 9 145 | 16 |
| After | 9 146 | 18 |

The `+1` test is `RendererCurationApiDecisions`, which passes. A name-level diff of the two failure
lists is the honest comparison, and it comes out clean:

- **Nothing that failed before was fixed or vanished** — all 16 are still present, so nothing was
  masked.
- **Two names are new**, and both are timing-sensitive audio tests that **pass on individual rerun**:
  `Sdl3AudioRecordingDeviceTests.CaptureReadIsNonBlockingBoundedAndPreservesSuffix` and
  `CueTest.PlayingCueNaturallyTransitionsToStoppedAfterPlaybackFinishes`. Three of the 16 pre-existing
  failures are from the same family (`CueTest.PauseAfterNaturalCompletionIsANoOp`,
  `SoundBankTest.IsInUseFalseSoonAfter…`, `WaveBankTest.IsInUseFalseSoonAfter…`) and **also pass on
  individual rerun**. All five were re-run one at a time before being classified; the corpus is
  flaky here under `-j8`, in both directions, and that flakiness predates this pass.

So the genuine failure set is **unchanged at 13**, and the three C API ones among them
(`CApiCoverageMatrix`, `CApiLimitations`, `CApiReleaseGate`) are the pre-existing
`generate_limitations.py` defect `RRC-008` already recorded — measured again here as the *same single*
unused rule, not a longer list.

**26/26 retired-selector negative configure tests pass** (`CnaRendererRetired_Selector_*`), so the
retirement decisions this pass must not disturb are still enforced by real project configures.

**`Vulkan_SpriteBatch_SortModeSemantics` was verified separately**, because it is registered only for
Vulkan and EasyGL and therefore never runs in the HEADLESS corpus above — which is exactly the kind
of gap that lets a rewritten test go unchecked. Built and run in `cmake-build-vulkan` on an AMD
Radeon 780M (RADV):

| Legs | Before (baseline sources restored into the same tree) | After |
|---|---|---|
| 6 | **5/6** — B2 passed as *"Deferred refused the mesh for its sort mode, Immediate did not (refused on capability)"*; C2 failed | **5/6** — B2 passes as *"Immediate+Deferred refused (yes), Deferred+Immediate refused (yes), Deferred+Deferred allowed (yes)"*; C2 fails |

The rewritten leg B2 passes, now asserting three answers where the mesh probe asserted one. **C2's
failure is pre-existing and untouched by this pass**, proven by restoring the four affected files to
`33928af3d`, rebuilding the target in the same tree and re-running: byte-identical failure message
(`got=(255,0,0) (want blue, the one issued second)`), and it reproduces on three consecutive runs, so
it is not flake. It belongs to `SpriteSortMode::Texture` grouping, which this pass does not change;
recording it here rather than fixing it keeps this pass narrow.

### Not verified here, stated plainly

- **Windows and macOS runtime**: nothing claimed. `DIRECTX9/11/12`, `DIRECT2D`, `GDI` and `METAL`
  were not run; only their sources were compiled where this Linux host can.
- **`FREEDIRECT`**: still needs the absent sibling checkout, unchanged from `RRC-008`.
- **The C API library itself**: cannot be built on this branch at all (pre-existing, proven above),
  so the *export half* of the ABI gate and the strict-C route tests were not executed. The header
  half runs and is green. This is worth an owner decision on its own: the C ABI cannot currently be
  built or shipped from this branch, independently of anything the curation did.

### One thing the new gate caught on its author

Worth recording because it is the gate doing its job on the change that introduced it: the
`RRC-010` justification comment added to `GraphicsRendererDescriptor.hpp` originally contrasted the
decision with `DrawMeshEXT` **by name**, which is precisely what the `RRC-009` arm forbids in active
code. The gate failed, and the comment was reworded to name the task rather than the symbol rather
than adding an exemption for the file — an exemption would have opened the whole descriptor header to
the name it exists to keep out. The only allowances are the gate's own source, the files whose
subject *is* the removal (`CHANGELOG.md`, the ABI release notes, the dated `0.9.0` handoff note), the
separately-checked ABI baseline, and one explanatory comment in the re-instrumented sort-mode test.

---

## RRC-011 — Remove obsolete renderer probes

**Owner decision, 2026-09-19.** A retired renderer's standalone existence-gate probe no longer
belongs in the current tree. Delete its probe directory when the renderer is retired; keep the
conclusion in the historical plan or tombstone and rely on Git history for the original source.
The same rule applies to a rejected candidate such as Three.js.

This pass removed 41 tracked files from eleven directories: `dx2`, `dx3`, `dx5`, `dx6`, `dx7`,
`dx8`, `dx10`, `nanovg`, `openvg`, `tinygl` and `threejs` under `spikes/`, each with the `-spike`
suffix. The first ten belong to the 25 retired identities; Three.js never became an identity and
its feasibility analysis recommends against building it. The other 26 spike directories remain.
Obsolete ignore rules were removed from `.gitignore`. `CLAUDE.md` and the candidate guidance now
require probe removal when a renderer is retired or rejected. Current documentation points to the
recorded findings rather than missing directories; historical plans have a notice explaining their
old probe references. The Three.js reproduction recipe was removed because the PixiJS browser
runner it invoked had already been deleted with that renderer.

**Verification.** A directory inventory confirms all eleven named directories are absent and the
other 26 remain. No current-tree reference to their paths remains outside historical plans and the
dated integration record. `scripts/check_renderer_identities.py` passes, preserving 25 live and 26
retired identities; all 34 `CnaRendererRetired` CTests pass. Incremental builds of
`cna_renderer_headless` and `cna_c_api` pass with `CCACHE_DISABLE=1`. The whole
`cmake-build-debug` configuration is still blocked by the unrelated
`EasyGLRedundantStateTests.cpp` missing `metagl/metagl.hpp`; its first attempt also encountered a
read-only `/rv/cnaccache` and was rerun with ccache disabled. No CMake or runtime source was
changed in RRC-011.

## RRC-012 — Retire `DIRECT2D`, `FREEDIRECT` and `PORTABLEGL`

**Owner instruction, 2026-09-27.** An intentional, permanent scope reduction: remove the three
renderers completely -- implementation, selection, build, tests, CI, scripts and live documentation
-- without disturbing `GDI`, `SOFTWARE`, shared Windows/CPU presentation code or any other retained
renderer. Not a deprecation: no dormant implementation, stub or compatibility wrapper remains.

It follows `RRC-002`/`RRC-003` exactly. The three names move from `CNA_RENDERER_PUBLIC_IDENTITIES`
to `CNA_RENDERER_RETIRED_IDENTITIES` (`DIRECT2D=16`, `FREEDIRECT=21`, `PORTABLEGL=46`), so all three
selection routes refuse them by name; the enumerators, registry rows, C ABI constants and their
mappings are removed; `CNA_GRAPHICS_RENDERER_MAXIMUM` moves from 46 to 44 (`SVG_DOM`) and the C ABI
goes to `0.31.0`. 22 public identities remain over 18 implementation families.

**Audit, before deleting.** Every match of `FreeDirect`, `Direct2D`, `D2D`, `PortableGL` and their
spellings was classified. Shared code was checked by its includers, not its name, and none of it
was renderer-specific: `PlatformRendererSurfaceState.hpp` (Direct2D) is still included by `GDI`,
`DIRECTX9`/`11`/`12`, `METAL` and `WEBGPU`; `NoOp3DResources.hpp` and `Sdl3RendererInterop.hpp`
(FreeDirect) by `SDL_RENDERER`, `CANVAS`, `SDL_GPU` and `FNA3D`; `VertexDeclarationFidelity.hpp`
(PortableGL) throughout. Nothing under `modules/renderers/gdi`, `modules/renderers/software` or
`modules/platform` referred to any of the three. Two things became dead with the removals and were
deleted: `CNA_RENDERER_REAL_GL_FAMILIES`, which only the `PORTABLEGL` combination rule read, and the
PortableGL FetchContent pin. `RequirePbrShadingSupportEXT` stays: `FNA3D` still calls it. No
`spikes/` probe existed for any of the three.

**What moved rather than went.** `cross_renderer_2d_corpus.cpp`, written for the Direct2D/EasyGL
differential, is renderer-agnostic and still built by EasyGL and Vulkan; its comments now say so
instead of pointing at the deleted `docs/direct2d-easygl-differential.md`. The Windows CI workflow
keeps its `DIRECTX11`/`DIRECTX12` legs and their `--parallel 2` build unchanged.

**Generated artefacts** were regenerated with their own tools: `docs/platform-renderer-sdl-audit.md`
(18 families, allowlist `fna3d`, `sdl-gpu`, `sdl-renderer`), `plans/plan_platform.md` §2, and
`docs/c-api/COVERAGE.md`. `tools/platform/nonproduction_sdl_budget.json` lost exactly the seven
entries of deleted files; `--update` would also have tightened two unrelated entries, so it was not
used. Two generated C ABI documents, `COMPATIBILITY.md` and `LIMITATIONS.md`, were already stale
before this change (from `MOD-RETIRE-1`) and are left for their own refresh.

**Verification.**

| Check | Result |
|---|---|
| `scripts/check_renderer_identities.py` | 22 identities / 18 families; 29 retired, values reserved, next free 52 |
| `check_renderer_combinations.py`, `check_runtime_renderer_discipline.py` | pass (2 rules; 18 families) -- but see `RRC-013`: the discipline gate's identity-dependent checks were checking zero identities |
| `renderer_sdl_audit.py`, `sdl_inventory.py`, `sdl_ratchet.py`, `hot_path_lint.py`, `nonproduction_sdl_audit.py` | pass; `sdl_classify.py` fails on the pre-existing unclassified `SDL_TOUCH_MOUSEID` |
| `cmake -P cmake/RendererIdentities.cmake` | refuses `DIRECT2D`, `freedirect`, `PortableGL`, `-DCNA_RENDERER_PORTABLEGL=ON` and `CNA_GRAPHICS_RENDERERS=HEADLESS;DIRECT2D` by name; accepts `GDI` |
| `cmake-build-multi`, clean configure, `SDL_RENDERER;OPENGLES3;VULKAN;SOFTWARE;HEADLESS;STUB` | configures; `CnaTests` builds; 331 renderer-related cases on the private display: 317 pass, 14 skipped, 0 fail; 44 configuration/registry/ABI ctests pass |
| `cmake-build-debug` (`OPENGLES3`, SDL3), full ctest on the private display | before 134/10,140 failed, after 120/10,144; the only after-only failure, `CnaInputTests` (X11 `BadWindow` at `-j8`), passes twice alone |
| C ABI | `abi_baseline.json` regenerated with the library: three constants, `MAXIMUM` and version only; 3,213 exports unchanged; `CApi_CoreExtSmoke`, `CApi_AbiSmoke`, `CApi_RenderTargetLifetimeSmoke` pass |
| `cmake-build-gdi`, clean MinGW-w64 configure, `GDI` | configures; every GDI renderer unit compiles; linking blocked by the pre-existing `SoftwareRenderer2D.cpp` 2D-only break (`8465377b1`/`0f213ebaf`) |

Failures present both before and after, none caused here: `EasyGLRedundantStateTest` (3, abort),
`ENet*` (3), the C ABI generated-document gates stale since `MOD-RETIRE-1`
(`CApiCompatibilityMatrix`, `CApiLimitations`, `CApiDocExportCounts`, `CApiBoolContractCurrent`,
`CApiReleaseGate`), `CApi_InstalledConsumer` (static library not built in that tree), and 107
tests whose executables that tree does not build. Not validated: any Windows, macOS or browser
run, and the `DIRECTX9/11/12`, `METAL`, `WEBGPU`, `SDL_GPU`, `OPENGL4`, `FNA3D` and Emscripten
builds, none of whose sources changed.

## RRC-013 — Repair the runtime-discipline gate; stale references

**Found by review of `RRC-012`.** `scripts/check_runtime_renderer_discipline.py` reads the public
identities from `check_renderer_identities.py`'s `IDENTITIES` table with a pattern that required
each row to close right after the enum name. `RRC-006` (`d1a579e00`, 2026-09-17) added the C ABI
value as a third field, and from then on the pattern matched nothing. `check_registry_map()` and
`check_identity_define_scope()` iterated over an empty list and passed, and the gate printed
"all 0 public identities reach the generated registry" -- in `RRC-006`..`RRC-011` and again in
`RRC-012`'s own verification, which reported the gate as passing without noticing the zero.

The parser now reads the first two fields of a row whatever follows them, the same way
`check_renderer_combinations.py` reads that table, and an empty result is a hard error rather than
a vacuous pass. Its `registry_map()` had a second, smaller defect: it read on past the map's closing
parenthesis to the next `list(FIND`, so a comment after the map ("a CNA_GRAPHICS_RENDERER identity")
became a 23rd entry. It now stops at the map's own `)` and strips comments, like the identity gate's
copy of the same parser.

**Verification.** The gate reports 22 public identities and the map has exactly 22 entries, none
missing and none extra. Three defects injected in memory -- `VULKAN` removed from the map, `GDI`
mapped to an accessor its descriptor unit does not define, and an `add_compile_definitions(CNA_RENDERER_SOFTWARE)`
appended to `RendererSelection.cmake` -- are each reported; the pre-repair script reports nothing for
the first and parses zero identities. The real tree passes.

**Stale references.** `NEXT.md`'s platform rule named `freedirect` among the SDL renderer exceptions,
and `NEXT_platform.md` described `FREEDIRECT` and PortableGL as unavailable in the environment rather
than retired; both now say so. `scripts/check_renderer_configure_sweep.sh` used `free-direct` as its
example of a missing sibling checkout, which is now `easy-gl` (whose check prints the same message).
`NEXT.md` gained the ledger entry `RRC-012` should have added. Historical and tombstone mentions
are unchanged.

**Out of scope, recorded.** `GDI` does not link on this tree: `SoftwareRenderer2D.cpp`, compiled into
`GDI` with `CNA_SOFTWARE_2D_ONLY`, calls helpers defined only outside that mode (since
`8465377b1`/`0f213ebaf`). It predates `RRC-012` and is a separate task.

## RRC-014 — Software contracts held only by the GDI suite

**Owner decision, 2026-09-28:** `GDI`, `HTML_DOM`, `SVG_DOM` and `OPENGL4` are retired (`RRC-015`).
`GDI` compiled eight of the Software module's CPU-2D translation units, so its suite partly tested
Software. Each of its 18 executables was classified before deletion:

- **Win32 presentation, DC, DPI, DWM, damage and GDI-only contracts -- removed with `GDI`:** smoke,
  dirty damage, repaint invalidation, presentation oracle/configuration/mode transaction, DC
  release transaction, window metrics, applied state, unsupported features, the 2D benchmark.
- **Already covered for Software:** 2D regression and public API (the Software example suite and
  the shared graphics tests run against `SOFTWARE`), public stencil (`Software_DepthStencilState_*`),
  and the MSAA contract (`Software_MsaaFragmentContract`, `Software_MsaaStorage`).
- **Held only by the GDI suite, migrated:** the host-width framebuffer and texture allocation
  layouts, the live Software allocation refusals (render target, resize, texture upload pitch,
  source-buffer size, budget) and `ColorMatrixEffect` end to end, now
  `SoftwareAllocationTests.cpp` and `SoftwareColorMatrixEffectTests.cpp`. One expectation changed
  with the owner: a Software render target without a depth format is colour only, so the
  byte-budget refusal is asserted at 11586², not GDI's 11000² (which only exceeded the budget with
  GDI's mandatory stencil plane). The GDI-only "ShaderEffect is refused" check was not migrated;
  Software accepts `ShaderEffect`.

Verified in `cmake-build-software`: `CnaRendererTests --gtest_filter='Software*'` 36/36 pass.


## RRC-015 — Retire `GDI`, `HTML_DOM`, `SVG_DOM` and `OPENGL4`

An owner decision to move CNA from breadth to long-term human maintenance. None of the four is
replaced: `OPENGL33` stays CNA's desktop OpenGL identity with its 3.3 contract (no EasyGL OpenGL 4
profile), `CANVAS`, `WEBGL1`/`WEBGL2` and `WEBGPU` cover the browser, and `SOFTWARE` stays the full
CPU renderer. Result: **18 public renderer identities over 14 implementation families**.

- **Removed:** `modules/renderers/{gdi,html-dom,svg-dom,opengl4}` (118 files, 46,933 lines); the
  GDI Windows and HTML DOM browser workflows; six DOM browser scripts; `tools/htmldom`,
  `tools/opengl4`; the DOM host-test options; the OpenGL4 GL-error ctest output gate; the GDI branch
  of the Software CMake (`CNA_GDI_SOFTWARE_SOURCES`, `cna_renderer_software_headers`); the
  `GDI`+`SOFTWARE` combination rule; every per-renderer arm in the shared tests and examples.
- **Identity:** the four names move to `CNA_RENDERER_RETIRED_IDENTITIES` (`HTML_DOM=18`,
  `OPENGL4=33`, `GDI=40`, `SVG_DOM=44`) and are refused by name on every route; their C ABI
  constants are gone and the values permanently reserved; `CNA_GRAPHICS_RENDERER_MAXIMUM` is `43`
  (`FNA3D`); the next new value stays `52`. The dense C++ enum loses four enumerators. C ABI `0.33.0`.
- **Ownership moved, not deleted:** `GlStockShaderSources.hpp` and `GlPresentationSurfaceState.hpp`
  had been lifted into `Common/` only so `OPENGL4` could share them; they are EasyGL's again.
- **Kept on purpose:** the Win32 platform's GDI surface presenter (`StretchDIBits`), which is the
  Win32 CPU-frame path `SOFTWARE` presents through; GDI+/`gdi32`/GDI-object mentions that are
  Windows API facts; the Mesa EGL leak suppression, which is not OpenGL4-specific.
- **Emscripten multi-renderer CI** now builds `WEBGL2`+`WEBGL1`+`CANVAS`: a GPU and a GPU-free
  renderer, and still a multi-identity family in one bundle.

**Verified** (Linux x86-64, 2026-09-28). All five identity/registry gates pass: 18 identities over
14 families, 33 retired values reserved, next free value 52. A real top-level configure refuses each
of the four names by name and value in about 0.5 s, before any SDL sub-build, on every route
(`CNA_GRAPHICS_RENDERER`, a `CNA_GRAPHICS_RENDERERS` member, `CNA_RENDERER_<X>=ON`, any letter
case). The C ABI baseline changes only by the four constants, the maximum and the version; its
3,215 exports are unchanged. In `cmake-build-software` (`SOFTWARE`, C API on) the full ctest corpus
ran on the private display: 10,604 of 10,625 passed, including all 159 Software-labelled tests. Every
failure was classified; none is a defect this change introduced:

- Measured failing identically at the pre-retirement commit: `CApi_TextureVolumeSmoke`, the three
  `CApi_Audio*Smoke` tests and `GuideTest.TheClickThatAnswersAMessageBoxDoesNotAlsoReachTheGame`.
- Older stale checks: the WebGPU PBR binding evidence (the code changed in `85c3baa09`, also
  counted by `CnaGltfConformanceL0`), the XNA pipeline parity/component reports (hand-edited in
  `5cc244f23`), and two C API artefacts left by `MOD-RETIRE-1` (regenerated in `b6309c0a8`).
- Configuration-dependent: `CnaXnbModelCorpusSweep` (vendored Draco builds fixtures the manifest
  records as refused).
- Load-sensitive: four ENet cases and two `DynamicSoundEffectInstance` cases pass when rerun alone,
  and the audio pair passes 20/20 repeats both before and after the change.
- Generated reports the ABI change had to refresh, regenerated here with their own tools and
  passing afterwards: `CApiCoverageMatrix`, `CApiLimitations` and `CApiReleaseGate` (whose record
  also still carried the unregenerated `0.32.0` and planned-symbol count).


The Emscripten bundle the CI job now builds (`WEBGL2;WEBGL1;CANVAS`, emsdk 6.0.9) configures and
links 54 of its 82 executables -- every Canvas/WebGL example, demo and the benchmark -- and every
graphics test object compiles. The other 28 (the 21 gtest suite binaries, the strict-API leak check
and six tools/harnesses) are blocked by six failures unrelated to renderers (`posix_spawnp` in the
content-pipeline host process, host headers in two net harnesses, a missing `<algorithm>` in
`PathUtf8Tests` under this newer emsdk). The bundle passes the job's own assertions: both JS selection exports, the `easygl` and `canvas` archives, three
registered renderers. In headless Chrome the renderer benchmark ran to completion under each of the
three, each selected at runtime.

## RRC-016 — Remove `CNA_SOFTWARE_2D_ONLY`

`GDI` was the only thing that ever compiled Software with `CNA_SOFTWARE_2D_ONLY`: its CMake defined
the macro for the eight shared units, and `SoftwareRenderer2D.cpp` was a one-line wrapper that
defined it and `#include`d `SoftwareRenderer.cpp`. With `GDI` gone the mode has no user (the only
remaining textual mentions are this plan, the removal record and the historical
`modularization/tools/gen_module_cmake.py`), so it is removed rather than left dormant:

- `unifdef -UCNA_SOFTWARE_2D_ONLY` over `SoftwareRenderer.cpp` and `SoftwareRenderer2DState.cpp`
  deletes only the 2D-only alternatives -- the `NotSupportedException` stubs for Texture3D,
  TextureCube, RenderTargetCube, effects, vertex/index buffers and the 3D draw entry points, and
  the no-environment-map fallbacks -- and keeps every line the full build compiled. One mixed
  condition (`COMPILED_EFFECTS && !2D_ONLY`) is reduced by hand.
- `SoftwareRenderer2D.cpp` and the glob exclusion that kept it out of the SOFTWARE archive are gone.
- The hooks only `GDI` overrode are gone: the virtual `OnSpriteRasterBounds` with the per-sprite
  damage-bounds computation that fed it, and the protected `BackbufferFramebuffer()` accessors.
  Neither changes any Software output.
- Comments that described `GDI` as a current consumer of Software are rewritten; dangling `GDI-0xx`
  task tags in Software comments are dropped (their plan is deleted; Git has it).

The full Software behaviour is unchanged: 23 lines added, 241 removed, all in the Software module.

**Verified** in `cmake-build-software` on the private display: the full ctest corpus 10,616/10,624
(the Guide test that hangs at the parent commit excluded), every Software test passing, the 8
failures being the pre-existing ones classified in `RRC-015`; then, reconfigured with
`CNA_SOFTWARE_COMPILED_EFFECTS=ON` (MojoShader from the shared `~/deps/FNA3D` pin), all 208
Software-labelled, compiled-effect and Software gtest cases pass, including
`Software_CompiledEffectRuntime`.

## RRC-017 — Documentation: one retirement record, no archive

The four renderers' own documents, plans and handoff were deleted, not marked retired: Git history
is the archive (`docs/{gdi,html-dom,svg-dom,opengl4}-renderer.md`, `NEXT_gdi.md`,
`plans/plan_{gdi,html_dom,svg_dom,opengl4,opengl4_modern_graphics,street_opengl4}.md`, 6,017
lines). `docs/removed-renderers.md` carries the one concise record: identity, retired value, date,
reason and the coverage that remains, plus the shared-code outcome. Active documents that listed
the four as current were corrected (README, CLAUDE/AGENTS, indexes, feature matrices, platform
notes, glTF limits, CHANGELOG, NEXT); open plan rows that depended on them are withdrawn with the
reason in the row (`plan_modern.md`, `plan_csl.md`). Dated history -- task rows, ledgers,
retrospectives, `integration/`, `modularization/`, `remediation/` -- is left as written.

**Verification of the whole retirement** (after `RRC-016`):

- `cmake-build-multi` (`OPENGL33` default, `VULKAN`, `SOFTWARE`, `HEADLESS`, `STUB`) configures and
  builds; its `CnaTests` corpus on the private display's real GPU passes 10,210 of 10,232 (the hung
  Guide case excluded). The 22 failures: four `EasyGLRedundantStateTest` aborts measured identically
  at the parent commit; three `IndexedDrawDeferredTest` strip cases whose Vulkan-only block is gated
  on Vulkan being compiled in rather than active (a multi-tree test defect this change does not
  touch); the WebGPU policy evidence and glTF L0 from `RRC-015`; shared-`/tmp` and host collisions
  (four Unicode-path cases, a storage sentinel, the content-CLI staging scavenger, a keyboard
  orientation case, the Wine differential timeout); and load-sensitive ENet and audio cases.
- A MinGW-w64 cross-build (`cmake-build-d3d11`: `DIRECTX11`, `DIRECTX12`, `SOFTWARE`, `HEADLESS`)
  compiles 535 units and links; under Wine its renderer benchmark runs with `SOFTWARE` and with
  `DIRECTX11`, each selected at runtime. That tree now has `CNA_ENABLE_NET=OFF`: networking needs a
  MinGW CURL this host does not have, a requirement added after the tree was last configured.
- A case-insensitive scan for every spelling of the four names leaves only the retired-identity
  machinery (tables, reserved-value comments, refusal tests), `docs/removed-renderers.md` and this
  plan, one test note marked historical, Windows API facts (the Win32 platform's GDI surface
  presenter, GDI+, `gdi32`, GDI-object accounting, the D3D/GDI line rule, the `gdi` abbreviation of
  `GraphicsDeviceInformation`), substrings of unrelated words, and dated history (task rows,
  ledgers, retrospectives, `integration/`, `modularization/`, `remediation/`, `audit/`).
- Found, not fixed (outside scope): the full ctest run rewrites
  `docs/xna-content-pipeline-parity-report.md` and deletes
  `tests/assets/media/video/video_xnb_object_fixture.xnb`; the renderer benchmark's banner names
  the compile-time default rather than the runtime renderer; `check_renderer_configure_sweep.sh`
  parses identities from a STRINGS line that no longer lists them literally.

## RRC-018 — Retire `DIRECTX12`, `CANVAS`, `OPENGLES2` and `WEBGL1`

An owner decision (2026-10-06) to reduce renderer scope and maintenance burden. None of the four is
replaced or aliased. Result: **14 public renderer identities over 12 implementation families**;
EasyGL serves `OPENGLES3`, `OPENGL33` and `WEBGL2`. The record is `docs/removed-renderers.md`.

- **Identity/ABI:** the names move to `CNA_RENDERER_RETIRED_IDENTITIES` (`OPENGLES2=2`, `WEBGL1=5`,
  `DIRECTX12=15`, `CANVAS=17`) and are refused by name on every configure route and, at run time,
  by `SetPreferred()` in any spelling. The four C constants are gone, the values reserved,
  `CNA_GRAPHICS_RENDERER_MAXIMUM` stays `43`, the next value stays `52`, C ABI `0.45.0`.
- **Removed families:** `modules/renderers/{directx12,canvas}`, the Canvas host-test target, the
  D3D12 arms of `cmake/DirectXParityTests.cmake` (now a DirectX11-only inventory, same 264 fixtures
  in the same order), `scripts/run-proton-vkd3d.sh`, `spikes/d3d12-warp-spike`, the D3D12 MSVC CI
  leg. `D3DCommon` lost its D3D12 input-layout tables and the second debug-layer API.
- **EasyGL:** `GlProfile` has three values; `UsesGlslEs100`/`UsesEs2ApiGeneration` and every
  branch they guarded are gone (shader lowering to GLSL ES 1.00, texture-object sampler emulation
  and level registry, no-VAO paths, unsized RGBA, combined-framebuffer readback, single-sample
  pins, ES 2.0 context request, capability refusals). The stock shader text each remaining profile
  compiles is unchanged.
- **Kept:** `D3DCommon`; `scripts/run-wine-vkd3d*.sh` (SDL_GPU's Direct3D 12 lane); the easy-gl
  sibling, which still contains WebGL 1 support CNA no longer selects.

**Verified** (Linux x86-64, 2026-10-06). The identity, combination, descriptor, discipline,
target, removed-API and platform boundary gates pass; each of the four names is refused by name and
value on the selector, set and option routes (`CnaRendererRetired_*`) and by `SetPreferred()` at run
time. `cmake-build-multi` (`OPENGLES3` default with `OPENGL33`, `VULKAN`, `WEBGPU`, `SDL_RENDERER`,
`SOFTWARE`, `HEADLESS`, `STUB`) was run on the private display beside the parent commit built in the
identical configuration: the full corpus (12,077 tests) fails exactly the parent's tests apart from
load-sensitive cases that pass alone, and every family's own suite, run with that family selected
at run time, has the same failure set before and after. A MinGW-w64 build (`DIRECTX9`,
`DIRECTX11`, `SOFTWARE`, `HEADLESS`) passes the 58 DirectX11 tests whose shared sources this change
edited under Wine+DXVK, and its benchmark runs under `DIRECTX11`, `DIRECTX9` and `SOFTWARE`
selected at run time. The `WEBGL2`+`WEBGPU` wasm bundle builds and runs the benchmark in headless
Chrome under each (`WEBGPU` then needed `-sASYNCIFY_STACK_SIZE=1048576`; since `RRC-025` a build
containing `WEBGPU` sizes it automatically). **Not run:** native Windows or MSVC, macOS (`METAL`),
the complete DirectX11 parity corpus, a real OpenGL ES 3.0 device.

Found, not fixed (pre-existing, outside scope): in a multi-renderer tree the per-family example
suites register for every compiled-in family but assert the default renderer's compile-time
contract (two Vulkan example sources even `#error`); `Fna3dSurfaceFormatTests` does not compile
when FNA3D is compiled in but not the default; `CApi_InstalledConsumer` cannot link an installed
consumer against an external `wgpu_native`; the C API coverage and limitations generators stop on
the `texture-cube-value-copy` rule; `scripts/check_cnaext_matrix.py` misses a heading in
`docs/cnaext-engine-layer.md`; the shared parity-fixture registration gives cross-compiled DirectX11
executables no Wine emulator.

**Status 2026-10-07:** fixed by `RRC-020`–`RRC-025` below -- the example-suite registration and
its two `#error`s (`RRC-022`), `Fna3dSurfaceFormatTests` (`RRC-021`), the coverage and limitations
generators (`RRC-024`) and the DirectX11 parity-fixture emulator (`RRC-023`); `RRC-020` and
`RRC-025` close the SDL_GPU link-group cycle and the hand-set Asyncify stack. Still open:
`CApi_InstalledConsumer` and `check_cnaext_matrix.py`.

## RRC-020 — `SDL_GPU` in a test-enabled multi-renderer build: the link group names the whole cycle

Found while repeating `RRC-018`'s multi-renderer verification with `SDL_GPU` added.

- **Symptom.** Configuring
  `OPENGLES3;OPENGL33;VULKAN;WEBGPU;SDL_GPU;SDL_RENDERER;SOFTWARE;HEADLESS;STUB` (default
  `OPENGLES3`) with `CNA_BUILD_TESTS=ON` stopped at generate time:
  "modules/renderers/sdl-gpu/examples/CMakeLists.txt:67: The inter-target dependency graph, for the
  target "cna_test_sdlgpu_constructor_exception_safety", contains the following strongly connected
  component (cycle): group "RESCAN:{cna_input,cna_graphics_core,cna_renderer_sdl_gpu}" depends on
  "cna_renderer_easygl" ... "cna_renderer_easygl" depends on group ...", once per other renderer.
- **Root cause.** `SDLGPU-114`'s constructor-rollback fixture links
  `$<LINK_GROUP:RESCAN,cna_input,cna_graphics_core,${RENDERER_TARGET}>` so a static link rescans the
  graphics-core archive cycle. That cycle is `cna_graphics_core` ↔ `cna_input` ↔ every selected
  renderer (each links back through `cna_renderer_common_setup`), plus the helper archives that link
  back (`cna_renderer_d3dcommon`, `cna_renderer_mojoshader_effect`). In a single-renderer build the
  group named all of it; in a multi-renderer build it named one renderer of several. CMake
  substitutes the group for every use of its members, so the members left out depend on the group
  and it on them, and a cycle through a link group is refused. Nothing new was cyclic; the group was
  too small. The suite was reachable at all in an `OPENGLES3`-default tree only because of
  `RRC-022`.
- **Fix.** `cna_graphics_archive_cycle()` in `modules/renderers/CMakeLists.txt` names the cycle once
  -- `cna_input`, `cna_graphics_core`, `CNA_RENDERER_TARGETS` and the helper archives that exist --
  and the fixture groups all of it. A single-renderer build gets the same group as before.
- **Verified.** `cmake-build-multi` with
  `OPENGLES3;OPENGL33;VULKAN;WEBGPU;SDL_GPU;FNA3D;SDL_RENDERER;SOFTWARE;HEADLESS;STUB` and tests on
  configures, generates and builds (1,903 steps, no failure). A second multi-renderer tree with
  `SDL_GPU` as the default (`SDL_GPU;OPENGLES3;VULKAN;FNA3D;SOFTWARE;HEADLESS`,
  `CNA_SHARED_LIBRARY=OFF`, so the group is a real static `--start-group ... --end-group`) registers
  SDL_GPU's 199 example tests; the fixture's group holds `cna_input`, `cna_graphics_core`, all six
  renderer archives and `cna_renderer_mojoshader_effect`. On the private display the fixture links
  and `SdlGpu_ConstructorExceptionSafety` passes, as do `SdlGpu_DrawLineTopology`, `SdlGpu_2D` and
  `SdlGpu_Parity_blend_states`; `SdlGpu_Smoke` fails one capability check (`Texture3D`), as it does
  in `plan_pre_sdlgpu_closeout.md`'s list of SDL_GPU's classic failures.

## RRC-021 — FNA3D's suites compile wherever FNA3D is compiled in and run where it is active

- **Symptom.** With `FNA3D` compiled in but not the default, `CnaTests` did not compile:
  `'SurfaceFormat' has not been declared`, `'Ordinal' was not declared in this scope`,
  `'FormatRowByteCount' was not declared in this scope` from line 127 (reproduced with the
  `cmake-build-multi` compile command: `CNA_RENDERER_EASYGL` plus `CNA_RENDERER_PRESENT_FNA3D`).
- **Root cause.** The file's own guards, not the build. `RTR-P9-9` widened its whole-file guard to
  `CNA_RENDERER_FNA3D || CNA_RENDERER_PRESENT_FNA3D`; the transfer-boundary tests (`855fd8c21`,
  `d5c3460db`) were written against the old one, and the merge left both: the new outer guard, a
  stray inner `#if defined(CNA_RENDERER_FNA3D)` around the header and the first seven tests, and an
  `#endif` after them. With only `PRESENT_` defined the last three tests compiled without the
  header. The test registration, include roots and the `PRESENT_` macro were already right, and a
  build with FNA3D as the default compiles both halves, which is why nothing saw it.
- **Fix.** One guard around the whole file, as its four sibling suites have.
- **Verified.** Compiles in `cmake-build-multi` (FNA3D present, `OPENGLES3` default), and running it
  exposed the other half: 14 device tests in `Fna3dCompiledEffectTests` constructed an `Effect` on
  whatever renderer was active and failed under `OPENGLES3` ("The active graphics renderer does not
  support compiled XNA/FNA Effect Framework bytecode"). They now carry the capability gate the
  file's other device tests already use -- compiled where FNA3D is, run where compiled effects
  execute (`RTR-P9-9`). All 103 `Fna3d*` tests then pass both ways: under `OPENGLES3` 48 run and 55
  skip; with `CNA_GRAPHICS_RENDERER=FNA3D` 102 run and 1 skips by its own check
  (`SharedCubeAndVolumeSamplerContract`), the 52 compiled-effect tests among them.

## RRC-022 — A renderer family's example suite exists only where that family is the default

- **Symptom.** In `cmake-build-multi` (`OPENGLES3` default) `ninja` stopped on
  `vulkan_cube_face_readback_dependency_test.cpp:45` and `vulkan_mrt_mip_finalization_test.cpp:50`:
  `#error "... is Vulkan-only."`. Underneath, that tree registered 373 `Vulkan_*` tests and the
  `SDL_Renderer_*`, `WebGPU_*`, `Software_*` and `Stub_*` suites, every one of which ran `OPENGLES3`
  under another family's name; 169 of the tree's 212 failures at `9fbe34468` were those.
- **Root cause.** The per-family loop in `modules/renderers/CMakeLists.txt` re-pointed
  `CNA_GRAPHICS_RENDERER` to the identity of the family it entered (`RTR-P6`), and each family's
  `examples/CMakeLists.txt` is entered from there. Those gates are equality with
  `CNA_GRAPHICS_RENDERER`, meaning "this family is the default" (`RTR-P9-13`): the sources compile
  against the default's project-wide `CNA_RENDERER_<X>` and run the default renderer. The re-point
  turned every such gate into list membership. `VKPAR-0003`/`VKPAR-0016` had worked around it in two
  families (EasyGL, Headless); the other nine still registered. The `#error` was right: it caught a
  target that could not mean what its name said.
- **Fix.** The loop no longer re-points `CNA_GRAPHICS_RENDERER`; inside a family it names the
  default, which is what every example gate assumed. The two family libraries that read it as "am I
  selected" (`software`, `sdl-renderer`) ask list membership instead, and the EasyGL/Headless
  workarounds collapse into the ordinary gate. A single-renderer build is unchanged: the loop ran
  once, with the default. `docs/runtime-renderer-selection.md` states the rule.
- **Verified.** At `bfd4b9e73` the full suite of `cmake-build-multi` (ten renderers, see `RRC-020`)
  on the private display runs 11,424 tests and fails 40, against 212 of 12,078 at `9fbe34468` in the
  eight-renderer configuration: 39 are among the parent's own failures, and the 40th, a timing-based
  scaling test, passes alone. Of the parent's other 173, 169 are the de-registered example tests,
  `CApiReleaseGate` is `RRC-024`, and three networking and offline cases pass in both runs of the
  fixed tree. Only the default's example suite registers (400 `EasyGL_*`), and the WebGPU browser
  pages no longer join a `WEBGL2`-default wasm bundle. All ten renderers run the renderer benchmark
  selected at runtime from that one binary, and the seven `CrossRendererContractTest` cases walk all
  ten. A multi-renderer tree whose default is `VULKAN`
  (`VULKAN;OPENGLES3;SDL_GPU;WEBGPU;SOFTWARE;HEADLESS;STUB`) registers exactly the Vulkan suite --
  373 tests, the two former `#error` sources among them -- and no other family's; there both of
  those, `Vulkan_SpriteBatchPresentation`, `Vulkan_Demo2D_SmokeTest` and
  `Vulkan_BlendState_AlphaBlend` build and pass on the private display, on Vulkan (563 `[PASS]`
  checks, no `[FAIL]`, in the two matrix fixtures). Reconfigured as a single-renderer `VULKAN`
  build, the same tree registers the same 373 Vulkan tests and the same five pass (575 `[PASS]`, no
  `[FAIL]`).

## RRC-023 — DirectX 11 executables carry the cross lane's Wine+DXVK launcher

- **Symptom.** In a MinGW cross build, `DirectX11_DrawLineTopology` was registered as the bare
  Windows `.exe`; so were `DirectX11_InstancedTexturedDraw`, `DirectX11_Win32HardwareSmoke` and the
  32 shared parity fixtures (`DirectX11_Parity_*`). CTest cannot run a PE on Linux.
- **Root cause.** The family has two registration routes. `cna_directx11_ctest_command()` wraps a
  command in `scripts/run-wine-dxvk.sh` when cross-compiling, and the 264 DirectX parity fixtures go
  through it. The three tests added for native Windows in `WIN11-0011-0019` registered
  `$<TARGET_FILE:...>` directly, and `cna_register_parity_fixtures()` registers every renderer's
  fixtures by bare target name, which CTest launches through the target's `CROSSCOMPILING_EMULATOR`
  -- a property `cna_directx11_test()` never set (`cna_sdlgpu_test()` does).
- **Fix.** `cna_directx11_test()` sets `CROSSCOMPILING_EMULATOR` to `scripts/run-wine-dxvk.sh` when
  cross-compiling, so any registration by target name runs through Wine+DXVK, and the three direct
  registrations name their targets. Native builds set no emulator and run the same executables.
- **Verified** in the MinGW-w64 tree `cmake-build-d3d11` (`DIRECTX11` default with `DIRECTX9`,
  `SOFTWARE`, `HEADLESS`), tests on. At `9fbe34468` its generated `CTestTestfile` registered
  `DirectX11_DrawLineTopology` as `.../cna_test_directx11_draw_line_topology.exe`, likewise the
  others above; now each is `scripts/run-wine-dxvk.sh <exe>`. Under Wine 10 and DXVK 2.6.0 on the
  private display, `DrawLineTopology` (DXVK engaged, every `[PASS]` held), `InstancedTexturedDraw`,
  `Win32HardwareSmoke`, `DirectX11_Smoke`, `DirectX11_DxvkGate`, `BlendState_SeparateFunctions` and
  all 32 `DirectX11_Parity_*` pass. A shell that exports `SDL_VIDEODRIVER=x11` fails every Wine run,
  since the Windows SDL has no such driver; unset it. **Not run:** native Windows or MSVC, where
  `CMAKE_CROSSCOMPILING` is false, no emulator is set and the executables run as before.

## RRC-024 — C API coverage generator: re-pin the approvals a re-declaration left stale

- **Symptom.** `generate_coverage_inventory.py --check` (the `CApiCoverageMatrix` gate) and
  `generate_limitations.py --check` stopped: "texture-cube-value-copy: its pattern matches only
  declarations already approved by graphics-resource-move-semantics,
  texture3d-and-texturecube-complete-contract -- move them there rather than re-running approval".
- **Root cause.** Approval pins are content-derived IDs of the reviewed declarations. `MSR-027`
  (`af3c5037c`) moved `TextureCube`'s and `Texture2D`'s copy constructor and copy assignment from
  `= default` to out-of-line definitions with a named parameter: the same operations with the same
  sharing semantics, but new IDs. `texture-cube-value-copy` then approved nothing that exists and
  the dead-rule gate stopped; `texture-and-texture2d-complete-contract` still approved other
  symbols, so its two copy members fell silently to `planned`. The rule was not stale and the parser
  was right; the diagnostic was misleading, because it listed only declarations owned by other rules
  and never the unowned re-declarations. Behind that gate, `CBIND-156`'s two `Effect::*Internal`
  hooks were `planned` under the finished `CBIND-080`.
- **Fix.** The four copy members are re-pinned to the rules that reviewed them; the two `Effect`
  hooks join `buffer-internal-set-data-helpers`, the rule for the same kind of helper
  (`ThrowIfDisposedForCloneInternal`). The dead-rule diagnostic now reports a rule whose pins no
  longer exist while its pattern reaches unapproved declarations, with a fixture test.
  `COVERAGE.md`, `LIMITATIONS.md` and `RELEASE_GATE.md` regenerated with their own tools.
- **Verified.** `generate_coverage_inventory.py --check` (469 headers, 8,142 symbols: 7,026
  implemented, 15 partial, 658 planned, 443 not applicable), `generate_limitations.py --check`,
  `check_release_gate.py --check` (verdict unchanged: not ready, the same one criterion) and
  `test_coverage_scope.py` (32 of 32) pass; run against the parent's mappings, the new diagnostic
  names the two stale pins and the two re-declarations. In `cmake-build-multi` the coverage, scope,
  limitations, release, ABI header, ABI export, declared-export, export-count and compatibility
  gates pass: ABI baseline current (197 structs, 3,210 exports), declared and exported agree on
  3,210 routes, C ABI `0.45.0` unchanged.

## RRC-025 — `WEBGPU` sizes its own Asyncify stack under Emscripten

- **Symptom.** In the `WEBGL2`+`WEBGPU` bundle `WEBGL2` ran, and `WEBGPU` aborted with
  `RuntimeError: unreachable` on its first Asyncify unwind unless linked with
  `-sASYNCIFY_STACK_SIZE=1048576` by hand (`RRC-018`).
- **Root cause.** Measured rather than assumed: the bundle's `Asyncify.StackSize` was raised from
  the page and every unwind's size recorded. Under `WEBGPU` the deepest unwinds are the adapter and
  device requests inside `GraphicsDevice` construction, 4,324 and 4,484 bytes (renderer benchmark;
  `cna_house3d_demo`: 4,544 and 4,704), against Emscripten's default 4,096. Every later wait, frame
  waits and the depth probe's readback inside `Draw` included, stays under 2.2 KB. `WEBGL2` suspends
  only at the frame boundary, 896 bytes. So the requirement is real but small -- 1 MiB was never
  measured -- and there is no Asyncify misuse to remove: blocking device construction over an
  asynchronous API is what Asyncify exists for.
- **Fix.** `modules/renderers/webgpu/CMakeLists.txt` adds `-sASYNCIFY_STACK_SIZE=65536` to
  `CNA::EmscriptenAsyncify` under Emscripten, so every Asyncify executable of a build that compiles
  `WEBGPU` in -- alone or beside `WEBGL2` -- gets it, an order of magnitude above the measured depth
  for game code above a suspension. A `WEBGL2`-only build keeps the default; the C API, which links
  `ASYNCIFY=0`, is untouched. The documents describe the automatic behaviour.
- **Verified** with `cmake-build-wasm-multi` (`WEBGL2` default with `WEBGPU`, Debug, emsdk 6.0.9) in
  headless Chrome 152 on the real GPU through the private display runner, each renderer selected
  through `Module.cnaPreferredRenderer` with no extra flag: the benchmark exits 0 under `WEBGL2`
  (deepest unwind 896 bytes) and under `WEBGPU` (4,484, clean device teardown), and
  `cna_house3d_demo --depth-probe` reports "the nearer plane occludes" under both. Configure-only: a
  `WEBGL2`-only tree links its 23 Asyncify executables without the option, a `WEBGPU`-only tree all
  26 with it, the bundle all 66. **Not run:** a `WEBGPU`-only bundle in a browser.

Found, not fixed (pre-existing, outside these six): `IndexedDrawDeferredTests` (9 cases) and
`IndexBufferEmptyDataTest` (1) compile their Vulkan and WebGPU device blocks under
`CNA_RENDERER_PRESENT_*` but run them on whatever renderer is active, so they fail in a
multi-renderer tree whose default is neither -- the shape `RRC-021` closed for FNA3D; `common/d3d`
is entered only when `DIRECTX11` is the default and `metal` only for a `METAL` default, so a
multi-renderer build holding either as a non-default family should not link (read from the code, not
built); with `SDL_GPU` selected, its two `SdlGpuIndexedDrawRangeTest` buffer-rewrite cases fail as
`plan_street_perf.md` already records; the benchmark banner still names the compile-time default
(`RRC-017`).

## RRC-026 — Vulkan and WebGPU device checks run only where their renderer is active

Second stabilization pass, opened from the debt `RRC-020`–`RRC-025` left (owner, 2026-10-07).

- **Symptom.** In `cmake-build-multi` (`OPENGLES3` default) ten tests failed at every run since
  `RRC-018`: `IndexedDrawDeferredTest` and `IndexBufferEmptyDataTest` cases asserting
  `nullptr != vulkanRenderer` or `nullptr != renderer` after a `dynamic_cast` of the device's
  renderer to `VulkanRenderer` or `WebGPURenderer`.
- **Root cause.** The class `RRC-021` closed for FNA3D. These blocks are compiled under
  `CNA_TEST_VULKAN_AVAILABLE`/`CNA_TEST_WEBGPU_AVAILABLE`, i.e. whenever the renderer is compiled
  in, but assumed it was also the active one. Two shapes: seven WebGPU-only tests lacked the
  `CNA_SKIP_IF_RENDERER_IS_NOT(WebGPU)` gate their sibling `WebGpuNativeErrorScopesStayClean`
  already has (`RTR-P9-9`); and five renderer-neutral tests added Vulkan validation or WebGPU
  error-scope diagnostics that asserted the cast unconditionally. A context-aware scan of every test
  source (a renderer-family cast under a compiled-in guard, asserted non-null, with no runtime gate
  naming only that family) finds exactly these 12 tests, 13 sites; two of them
  (`IndexedTopologiesRenderExactDistinctGeometry`,
  `PublicThirtyTwoBitTopologiesRenderExactDistinctGeometry`) only escaped the OPENGLES3 run because
  they skip there, and failed under `SOFTWARE`.
- **Fix.** The WebGPU-only tests skip unless WebGPU is active. The neutral tests keep every
  assertion they make for all renderers and apply the renderer-specific diagnostics where that
  renderer is active; where it is, the cast must still succeed
  (`if (CNA_RENDERER_IS(Vulkan)) ASSERT_NE(nullptr, vulkanRenderer)`). The scan now reports no site.
- **Verified.** Both suites (40 tests) with each of the ten renderers of `cmake-build-multi`
  selected at runtime, on the private display: no failure under `OPENGLES3`, `OPENGL33`, `VULKAN`,
  `SDL_GPU`, `FNA3D`, `SDL_RENDERER`, `SOFTWARE`, `HEADLESS` or `STUB`. Under `WEBGPU`, where the
  WebGPU tests now actually run, two fail with "The index buffer resource is in use" and "The vertex
  buffer resource is in use": they rewrite a still-bound buffer, which the device refuses as XNA
  does -- the buffer-rewrite class `plan_street_perf.md` records for SDL_GPU, left with it.
