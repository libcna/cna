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
| RRC-002 | Retire the 25 identities from the registry, CMake selection and C ABI; reject retired selectors explicitly | ⬜ |
| RRC-003 | Remove the 25 implementation families and everything that existed only for them | ⬜ |
| RRC-004 | Tests, test infrastructure and CI | ⬜ |
| RRC-005 | Live documentation, historical banners, tombstones, curated-set policy | ⬜ |
| RRC-006 | Regression guard: exact 25-identity whitelist and permanently retired IDs | ⬜ |
| RRC-007 | Second pass: stale-reference audit and code-quality cleanup | ⬜ |
| RRC-008 | Build matrix, negative configure matrix, full test corpus, closing report | ⬜ |

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

- [ ] `scripts/check_renderer_identities.py` checks the exact whitelist (names, enum names and C ABI
      values), the retired table (26 values including `SKIA`), that no live identity uses a retired
      name or value, that `CNA/C/graphics.h` defines exactly the live constants with those values and
      `MAXIMUM` = the highest, and that the CMake retired list matches the script's.
- [ ] A `cmake -P` negative-configure test per retired identity, registered in CTest.
- [ ] `GraphicsRendererDescriptorTests`' identity tripwire at 25.

## RRC-007 — Second pass

- [ ] Multi-spelling stale-reference audit rerun; every remaining hit classified as active (fixed),
      historical, or retired-ID documentation.
- [ ] Empty directories, dead includes and forward declarations, unused helpers, orphaned shaders and
      patches, stale TODOs, CMake variables without readers, dead links.

## RRC-008 — Verification

- [ ] Identity/descriptor/combination/cnaext gates; platform boundary gates.
- [ ] Representative builds (see matrix, filled in when run).
- [ ] Negative configure of all 25 retired selectors.
- [ ] Full CTest corpus against the RRC-001 baseline.

---

## Findings outside scope

Recorded, not fixed here.

| Item | Class |
|---|---|
