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
