# plan_vulkan_parity.md — Vulkan renderer parity on real Radeon/RADV hardware

A hardware-validation workstream for the **existing classic CNA/XNA Graphics contract** on the
Vulkan renderer. Not a modern-graphics/CNAEXT project, not a performance project, and not a second
copy of `plans/plan_vulkan.md` — that campaign's 243 rows reached their verdict on **2026-09-11**
against **llvmpipe under Xvfb**. This one re-measures the parts that verdict could not see, on the
machine's real **AMD Radeon 780M / RADV**, across the native **X11** and **Wayland** backends, and
closes what the newer `plans/plan_graphics_shared_cleanup.md` contracts left open for this renderer.

Task IDs: `VKPAR-0001`, `VKPAR-0002`, … . Rows carry their evidence; this file is the ledger.

**Evidence classes are kept apart and never blended.** A line that does not name one is not
evidence:

| Class | Meaning |
|---|---|
| **RADV-HW** | AMD Radeon 780M, `DRIVER_ID_MESA_RADV`, real GPU. The evidence this workstream exists to produce. |
| **LVP** | `lavapipe`, `DRIVER_ID_MESA_LLVMPIPE`, software. Cross-check only — never the primary evidence for a hardware claim. |
| **XNA-MEASURED** | Microsoft XNA 4.0 through `tools/xna-oracle`, references checked in. Outranks every renderer. |

---

## Status

| ID | Task | Status |
|---|---|---|
| VKPAR-0001 | Baseline: repository, hardware, platform paths, build | ⬜ |
| VKPAR-0002 | Renderer architecture audit and parity inventory | ⬜ |
| VKPAR-0003 | The Vulkan multi-renderer configuration does not build | ✅ |
| VKPAR-0011 | GSC-F1 — EasyGL per-pixel SkinnedEffect ambient-only white | ⬜ |
| VKPAR-0004 | GSC-F2 — Vulkan classic null-texture semantics (white → XNA's opaque black) | ✅ |
| VKPAR-0005 | Vulkan facedness: the two-sided stencil "driver quirk" claim, measured on RADV | ⬜ |
| VKPAR-0006 | Validation layers + synchronization validation across the renderer suite | ⬜ |
| VKPAR-0007 | X11 + Vulkan surface path on RADV | ⬜ |
| VKPAR-0008 | Wayland + Vulkan surface path on RADV | ⬜ |
| VKPAR-0009 | SDL-free native configurations | ⬜ |
| VKPAR-0010 | Parity corpus and CnaTests baselines, classified | ⬜ |

---

## VKPAR-0001 — Baseline

### Repository

| Fact | Value |
|---|---|
| Baseline commit | `391279c502026c3b8276881b6b35aef73670fbba` (= `origin/next`, fetched 2026-09-21) |
| Branch | `vulkan-parity`, branched from `origin/next` |
| Identity | `Robert Vokac <robertvokac@robertvokac.com>` (repository-local `user.name`/`user.email`) |
| Working tree at branch time | clean except untracked `startup-metrics.log` (pre-existing, not this workstream's) |
| Build directory | `cmake-build-vulkan/` (in-repo, shared, incremental — `CLAUDE.md` build-location rule) |

The `graphics-shared-cleanup` work **is** present: all eight task commits and the integration merge
`c0225e9d5` are ancestors of the baseline (`git merge-base --is-ancestor`, each verified
individually). Nothing was recreated or cherry-picked.

| Precondition fix | Commit | Ancestor of baseline |
|---|---|---|
| Two-sided stencil correction | `66186d6dd` | yes |
| SkinnedEffect non-uniform-scale normals | `9113ab4e7` | yes |
| Measured XNA missing-texture semantics + glTF white | `ea0656ca1` | yes |
| PresentationParameters depth-format contract | `c731c1a96` | yes |
| D3D validation enforcement | `12b6e09dd` | yes |
| `CaseInsensitivePathTest` cleanup | `9133cb279` | yes |
| AudioEngine per-process temp isolation | `e5b911a85` | yes |

### Hardware — measured, not assumed

| Fact | Value |
|---|---|
| OS | Debian GNU/Linux 13 (trixie), `DEBIAN_VERSION_FULL=13.6` |
| Kernel | `6.12.107+deb13-amd64` (`thinkpadt14`) |
| CPU / RAM | 16 threads, 30 GiB, **no swap** |
| GPU (PCI) | `c3:00.0 AMD/ATI Phoenix1 [1002:15bf] (rev dd)` |
| DRM | `/dev/dri/card0`, `/dev/dri/renderD128` |
| Vulkan instance | **1.4.309** |
| Physical device | **AMD Radeon 780M (RADV PHOENIX)** |
| driverID | `DRIVER_ID_MESA_RADV` (`radv`) |
| Device API version | 1.4.305 |
| driverVersion / Mesa | 25.0.7 |
| vendorID / deviceID | `0x1002` / `0x15bf` |
| Device type | `PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU` |
| Second ICD | `lvp_icd.json` — **lavapipe present**, so the LVP cross-check class is available |

Validation and diagnostics available on this host:

| Capability | State |
|---|---|
| `VK_LAYER_KHRONOS_validation` | present, **1.4.309** |
| `VK_EXT_debug_utils` | present (instance extension) |
| `VK_KHR_xlib_surface` / `VK_KHR_xcb_surface` | present |
| `VK_KHR_wayland_surface` | present |
| `VK_EXT_headless_surface` | present |
| Other layers | `VK_LAYER_MESA_device_select`, `VK_LAYER_MESA_overlay`, `VK_LAYER_INTEL_nullhw`, Steam's two |

### Platform paths — and one correction the brief's assumption needed

The session is GNOME/Wayland (`XDG_SESSION_TYPE=wayland`, `XDG_CURRENT_DESKTOP=GNOME`,
`gnome-shell` live, socket `wayland-0`), with `Xwayland :0` rootless beside it. But the **agent
shell does not inherit that session**: it starts with `WAYLAND_DISPLAY` empty and `DISPLAY=:99`.

`:99` is `Xvfb :99 -screen 0 1920x1080x24`, and it is **unusable for Vulkan presentation**:

```
vulkan: No DRI3 support detected - required for presentation
```

`vulkaninfo` on `:99` emits that for every ICD and enumerates RADV without a usable surface. On
`:0` there is no such message, `xdpyinfo` lists `DRI3`, and both RADV and lavapipe enumerate.

| Display | Server | DRI3 | Usable for Vulkan presentation |
|---|---|---|---|
| `:99` | `Xvfb` | no | **no** |
| `:0` | `Xwayland` (rootless, under Mutter) | yes | yes |
| `wayland-0` | Mutter/GNOME | n/a | yes (native `VK_KHR_wayland_surface`) |

`CNA_TEST_DISPLAY` in `cmake-build-vulkan/` is already `:0`, so the registered Vulkan ctests target
the DRI3-capable server rather than Xvfb. **Any Vulkan run on `:99` is an environment artefact, not
a renderer result**, and is classified as such wherever it appears below.

`:0` reports `1536x960` where the panel is 1920x1200 — consistent with the **125% fractional
scaling** Phase 56 asks about; confirmed against the compositor before that row is closed.

---

## VKPAR-0002 — Renderer architecture audit

The renderer is **not** a thin or early implementation. Measured on the baseline:

| Module | Lines | Note |
|---|---|---|
| `modules/renderers/vulkan/src` | 38 125 | of which `VulkanRenderer.cpp` alone is **23 183** |
| `modules/renderers/vulkan/include` | 6 955 | |
| `modules/renderers/vulkan/examples` | 42 009 | **155** example/test translation units |
| `modules/renderers/vulkan/tests` | 512 | `VulkanCompiledEffectTests` |
| **Total** | **87 601** | second-largest renderer family after `common` (96 509) |

For scale: `directx11` is 9 999 lines, `directx12` 16 747, `software` 36 249, `easygl` 72 104.

Renderer classes present (from `VulkanRenderer.hpp`): `VulkanTextureRenderer`,
`VulkanTexture3DRenderer`, `VulkanTextureCubeRenderer`, `VulkanTexture2DArrayRenderer`,
`VulkanStorageTexture2DRenderer`, `VulkanRenderTargetRenderer`, `VulkanRenderTargetCubeRenderer`,
`VulkanMRTProxy`, `VulkanEffectRenderer`, `VulkanSpriteBatchRenderer`, `VulkanVertexBufferRenderer`,
`VulkanIndexBufferRenderer`, `VulkanStorageBufferRenderer`, `VulkanComputeShaderRenderer`,
`VulkanOcclusionQueryRenderer`, `VulkanGpuTimerRenderer`, plus a `PipelineKey`/`PipelineKeyHash`
pipeline cache and the `IVulkanSamplable`/`Cube`/`Volume`/`Array` sampling contracts.

The prior campaign's own verdict and its scope are in `plans/plan_vulkan.md` §28. **This plan does
not restate it and does not inherit it**: its evidence is llvmpipe-under-Xvfb, and the rows below
say which of its conclusions survive contact with RADV.

---

## VKPAR-0003 — The Vulkan configuration does not build

Found by trying to produce the baseline, not by reading. On the baseline commit, with the build
directory's existing settings, `cmake --build cmake-build-vulkan` **fails**:

```
CNA_GRAPHICS_RENDERER   = VULKAN
CNA_GRAPHICS_RENDERERS  = VULKAN;OPENGL33
CNA_PLATFORM            = SDL3     CNA_CNAEXT = OFF     CMAKE_BUILD_TYPE = Debug
→ 19 errors in vulkan_shader_effect_test.cpp, then 2 in shader_effect_reflection_contract_test.cpp
```

Two independent defects, neither of them Vulkan-renderer defects — both are **test-registration**
defects, and both are invisible in the configurations these suites are usually built in.

### (a) Two Vulkan example TUs need `CNA_CNAEXT` and are registered unconditionally

`vulkan_shader_effect_test.cpp` and `vulkan_shader_dialect_contract_test.cpp` include
`CNA/Graphics/ShaderPackageEXT.hpp` and build a `ShaderPackageEXT` (`MOD-2216`/`MOD-2217`).
`ShaderCodeEXT` and `ShaderPackageEXT` live behind `ShaderEffect.hpp`'s own `#ifdef CNA_CNAEXT`, so
with the project default `CNA_CNAEXT=OFF` neither TU compiles:

```
vulkan_shader_effect_test.cpp:42:12: error: 'CNA::Graphics' has not been declared
vulkan_shader_effect_test.cpp:44:31: error: 'CNA::Examples' has not been declared
```

The same file already guards its `MOD-2243` block with `if(CNA_CNAEXT)`; these two registrations
were simply left outside it. Fixed by moving both inside that guard.

**Why no test caught it:** `CNAEXT_GuardDiscipline` checks that every file in `graphics-ext` is
wrapped in `#ifdef CNA_CNAEXT`. It says nothing about a *consumer* outside that module, and nothing
about CMake registration. The Vulkan example suite had therefore only ever been built with
`CNA_CNAEXT=ON` — which is not the configuration the classic XNA contract is tested in.

### (b) An EasyGL example is registered by family identity but compiles against the default renderer

`modules/renderers/CMakeLists.txt:162` re-points `CNA_GRAPHICS_RENDERER` to the identity of the
family currently being entered, and restores it afterwards (`:192`). EasyGL's example block guards
on `CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33"` (et al.), so in a multi-renderer build it is entered
**whenever OPENGL33 is in `CNA_GRAPHICS_RENDERERS`**, regardless of the default.

`shader_effect_reflection_contract_test.cpp` names itself from the *project-wide* `CNA_RENDERER_<X>`
macro, and only the **default** renderer's macro is defined project-wide (that is the whole design of
runtime renderer selection). With the default VULKAN it reaches its own `#else`:

```
shader_effect_reflection_contract_test.cpp:53:2: error:
    "ShaderEffect reflection contract requires EasyGL, DirectX 11, or DirectX 12"
```

This is the rule stated at the top of `modules/graphics/examples/CMakeLists.txt`
(`plans/plan_runtimerenderer.md` RTR-P9-13) — *a gate deciding whether a target exists, where that
target runs against whatever renderer the build defaults to, must test the default* — broken in a
way that file's own wording did not anticipate: the per-family re-point turns an equality test into a
membership test. Fixed by registering only when the family being entered **is** the default
(`CNA_GRAPHICS_RENDERER STREQUAL _cna_default_renderer_identity`), which is what the TU reads.

The narrow fix is deliberate. Other EasyGL example TUs in the same block compile in this
configuration; only the ones that switch on the default-renderer macro cannot, and this is the only
one that does. Widening the block's own guard would drop EasyGL tests that currently build and pass,
which is a different decision and not this workstream's to make.

**Classification:** test/fixture defect (registration), not a renderer defect. Neither changes any
rendering behaviour.

---

## VKPAR-0004 — GSC-F2: Vulkan's classic null-texture semantics

**The contract, XNA-MEASURED.** `GSC-0004` rendered seven scenes on the real XNA 4.0 runtime
(`tools/xna-oracle/scenes/null-texture/`) and checked the references in. Centre pixels, decoded
from `tools/xna-oracle/reference/null-texture/` on this baseline:

| Reference | Centre pixel |
|---|---|
| `basic_textureenabled_null.png` | `(0, 0, 0, 255)` |
| `skinned_null.png` | `(0, 0, 0, 255)` |
| `alphatest_null.png` | `(0, 0, 0, 255)` |
| `dualtexture_texture_null.png` | `(0, 0, 0, 255)` |
| `dualtexture_texture2_null.png` | `(0, 0, 0, 255)` |
| `envmap_texture_null.png` | `(100, 50, 25, 255)` |
| `envmap_cube_null.png` | `(128, 128, 128, 255)` |

Every classic stock effect reads an unbound texture as **opaque black**. Only the CNAEXT PBR base
colour keeps opaque white (glTF's "no `baseColorTexture`" identity), and the glTF importer binds
its own white through `ContentManager` rather than relying on renderer default behaviour.

**What Vulkan does instead.** `VulkanRenderer::FillStockFamilyRecordEXT` binds `defaultWhiteView_`
/ `defaultWhiteCubeView_` for *every* unbound slot, in every classic family:

| Family (`VulkanRenderer.cpp`) | Unbound slot binds | Should bind |
|---|---|---|
| `needsSkinned` | `defaultWhiteView_` | opaque black |
| `needsEnvMap` — `texture0` | `defaultWhiteView_` | opaque black |
| `needsEnvMap` — `envMap` cube | `defaultWhiteCubeView_` | opaque black cube |
| `needsDualTex` — both slots | `defaultWhiteView_` | opaque black |
| `needsLitTextured`/`LitUntextured`/`LitColored` | `defaultWhiteView_` | opaque black **when the effect samples** |
| shared fallback (alpha-test, `colored3d`/`textured3d`) | `defaultWhiteView_` | opaque black |
| `needsPbr`, `needsPbr && needsSkinned` — base colour | `defaultWhiteView_` | **white — correct, keep** |

The renderer has **no opaque-black fallback image at all**; the only 1×1 fillers it creates are
`defaultWhite{,Cube,Volume,Array}` and `defaultFlatNormal`.

**Why no test caught it.** Both renderer-neutral tests gate Vulkan out:

| Test | Gate | Vulkan |
|---|---|---|
| `StockEffectNullTextureTest` | `CNA_RENDERER_IS(Software, OpenGL33, OpenGLES3, DirectX11, DirectX12)` | skipped |
| `DualTextureEffectNullSamplerTest` | `CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, OpenGL4, Software, DirectX11, DirectX12)` | skipped |

and Vulkan's own examples **pin the white**, e.g. `vulkan_alphatest_null_texture_test.cpp`:
`kExpectedNullTexture(153, 102, 204, 255)` — which is `diffuse(0.6, 0.4, 0.8) × white`, where the
XNA reference is `(0, 0, 0, 255)`. Its header even records "Vulkan already had the correct
white-texture fallback … no bug found here", written before `SOFTWARE-303`/`GSC-0004` measured what
XNA actually does. This is exactly the shape `GSC-F2` predicted.

**BasicEffect's one exception, carried over from EasyGL.** `TextureEnabled=false` does not sample in
XNA, but Vulkan's lit programs — like EasyGL's — multiply unit 0 in unconditionally. That case keeps
the white identity; the black applies where the effect genuinely samples. `GSC-0004`'s EasyGL hunk
(`else if (!params.pbr && params.textureEnabled)`) is the precedent.

---

### Reproduction — RADV-HW, before any change

Both renderer-neutral suites were pointed at Vulkan (the gates above extended) on the unmodified
renderer, `cmake-build-vulkan`, `DISPLAY=:0`, device `AMD Radeon 780M (RADV PHOENIX)`:

```
9 tests from 2 test suites ran.
[  PASSED  ] 1 test.
[  FAILED  ] 8 tests
```

The readbacks name the defect exactly — `FF-FF FF-FF` is opaque white where the XNA reference is
`00-00 00-FF`:

| Test | Got | Expected |
|---|---|---|
| `StockEffectNullTextureTest.BasicEffectWithTextureEnabledSamplesOpaqueBlack` | `(255,255,255,255)` | `(0,0,0,255)` |
| `StockEffectNullTextureTest.SkinnedEffectSamplesOpaqueBlack` (both lighting modes) | `(255,255,255,255)` | `(0,0,0,255)` |
| `StockEffectNullTextureTest.AlphaTestEffectSamplesOpaqueBlack` | `(255,255,255,255)` | `(0,0,0,255)` |
| `StockEffectNullTextureTest.EnvironmentMapEffectSamplesOpaqueBlackForEitherSlot` | 127 off per channel (the white cube reflecting) | reference value |
| `DualTextureEffectNullSamplerTest.NullTexture*` (4 legs) | `(120,200,40,255)` / `(160,80,240,255)` | `(0,0,0,255)` |

The single pass is `BasicEffectWithoutTextureEnabledNeverReadsTheSlot`, and it passes for the right
reason: Vulkan's lit and textured fragment shaders read
`(pc.textureEnabled > 0.5) ? texture(...) : vec4(1.0)`, so `TextureEnabled=false` never samples.

### Fix

`VulkanRenderer` gains an opaque-black 1×1 2D image and an opaque-black 1×1 cube, built exactly like
the existing `EnsureDefaultFlatNormalTexture()` / `EnsureEnvMapResources()` fillers, created lazily
and released with the other defaults. Six binding sites in `FillStockFamilyRecordEXT` and
`DrawInstancedPrimitivesCoreEXT` move from `defaultWhiteView_`/`defaultWhiteCubeView_` to them:
skinned, env-map (2D **and** cube), dual-texture (both slots), the lit BasicEffect families, the
shared alpha-test / `textured3d` arm, and the instanced classic path.

**What deliberately did not change.** The CNAEXT PBR base colour keeps opaque white — glTF's "no
`baseColorTexture`" identity, the same split `GSC-0004` made on DirectX12 — as do the PBR maps, the
flat-normal fallback, the IBL cube fallbacks, the shadow-map fillers, and `ShaderEffect`'s own bound
resources (a CNAEXT custom effect sampling a unit the game never bound gets white, not undefined
memory; that is a different contract and `VULKAN-390` owns it). The instanced site makes the split
explicit with `d.usePbr || d.usePbrSkinned` rather than relying on the PBR pipeline binding a
different set.

**Unlike EasyGL, no `textureEnabled` special case was needed.** `GSC-0004` had to keep a white
identity for EasyGL because its lit programs multiply unit 0 in unconditionally. Vulkan's shaders
already branch on the flag, so the white identity lives in the shader where it belongs and the
binding can be black unconditionally.

### Result — RADV-HW

```
9 tests from 2 test suites ran.
[  PASSED  ] 9 tests.
```

### A second defect found on the way: three Vulkan examples had never asserted anything

`Vulkan_AlphaTest_NullTexture`, `Vulkan_DualTextureEffect_NullTexture0` and
`..._NullTexture2` abort before their first check:

```
CNA: fatal exception escaped Game::Run(): GetBackBufferData is not supported by the Reach graphics profile.
terminate called after throwing an instance of 'System::NotSupportedException'
```

Confirmed **on the baseline sources too** (the three files stashed, targets rebuilt, re-run): this is
not a consequence of this task's edits. `GetBackBufferData` is HiDef-only and these three never
called `setGraphicsProfileProperty`; their EasyGL siblings always have. Three registered tests were
therefore reporting a renderer defect they could not have detected either way. Fixed with the
missing `GraphicsProfile::HiDef`, and they now pass against the corrected values.

Their headers also had to be rewritten rather than just renumbered. All three asserted the white
fallback as *correct* — `vulkan_alphatest_null_texture_test.cpp` recorded "Vulkan already had the
correct white-texture fallback … no bug found here, confirmed by pixel readback" — which predates
`SOFTWARE-303` measuring XNA. And because the expected pixel is now black, each test clears to a
witness colour `(7,199,53,255)` before the draw under test: otherwise "sampled opaque black" and
"drew nothing at all" are the same readback. The AlphaTest test's retry loop, which used to wait for
a *non-black* pixel, now waits for a pixel that is not the witness.

**Classification:** renderer defect (the white fallback) plus test debt (three dead example tests,
and two renderer-neutral suites that skipped this renderer).

---

## VKPAR-0005 — Vulkan facedness, and a recorded rationale that does not hold

`FillDepthStencilState` sends XNA's `CounterClockwise*` operations to `VkStencilOpState front` and
the ordinary ones to `back`. That **arrangement** agrees with what `GSC-0002` later established for
DirectX11/12 (ordinary → `BackFace`, CCW → `FrontFace`). The arrangement is not in question.

**The recorded reason is.** The comment attributes the swap to an unisolated driver quirk:

> Root cause not fully isolated (plausibly an llvmpipe/Mesa software-rasterizer quirk in its own
> front/back `VkStencilOpState` assignment specifically, since culling's front/back classification
> is provably correct on this same driver) — swapped here pragmatically …

There is a mechanical explanation that needs no quirk. Every stock vertex shader negates clip Y
(`pos.y = -pos.y;`, `colored3d.vert.glsl:52` and siblings) because Vulkan NDC Y is inverted, and the
viewport is deliberately left **top-left/positive-height** for that reason
(`VulkanRenderer.cpp:13788`). Negating Y mirrors every triangle, so **framebuffer winding is the
reverse of XNA's displayed winding**, and with `rs.frontFace = VK_FRONT_FACE_CLOCKWISE` an
XNA-displayed-clockwise triangle is `back` to Vulkan. The stencil swap is then exactly right, and
principled — not a workaround.

That same reasoning predicts something the comment explicitly denies, which is what makes this
testable rather than cosmetic: culling maps `CullClockwiseFace → VK_CULL_MODE_FRONT_BIT`
(`VulkanRenderer.cpp:8654`), i.e. it culls the faces Vulkan calls front, which under the Y-flip are
XNA's *counter-clockwise* ones. `frontface_winding_test.cpp`'s oracle — unconditional, and
registered for Vulkan as `Vulkan_FrontFaceWinding` — states the contract that decides it:

> clockwise-as-displayed is the FRONT face, and each enum names the face it CULLS

So either the comment is wrong about the quirk, or the cull mapping is wrong, or the Y-flip's effect
on facedness is cancelled somewhere this audit has not found. The MojoShader compiled-effect path
uses a negative-height viewport instead of the shader flip (`dvp.height = -dvp.height`), which
reverses winding the same way — so whatever the answer is, it has to hold for both routes.

**This row is resolved by measurement on RADV-HW, not by argument**, and the comment is corrected to
say what the measurement shows. `plans/plan_vulkan.md` Task 870 derived it on llvmpipe; this is the
first time the question has had real hardware to answer it.

---

## Scope boundaries

Carried from the brief and not re-litigated per row:

* **No modern CNAEXT graphics API.** What Vulkan infrastructure that future workstream can reuse is
  recorded at the end of this file; none of it is implemented here.
* **No performance work.** Pathological behaviour is reported, not optimized. No benchmarking
  against DirectX11/12 while parity is still moving.
* **No other renderer project** (WebGPU, OpenGL4, SDL_GPU, Metal). EasyGL and Software are touched
  only as reference renderers and for regression.
* **Evidence outranks convenience.** A RADV or Mesa behaviour, a stale fixture, a harness bug and
  undefined Vulkan behaviour are each named as such rather than absorbed into CNA.
